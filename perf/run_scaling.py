#!/usr/bin/env python3
"""
Thread-scaling benchmark for docling-parse.

Runs DoclingThreadedPdfParser at increasing thread counts and prints a
scaling table.  Three modes are supported:

  parse   — decode-only (render_config=None); always includes a
            single-threaded DoclingPdfParser baseline.
  render  — decode + rasterise (RenderConfig.scale=...).
  both    — runs both of the above and prints two tables.

Third-party single-threaded backends (selected via --other) are run as
additional baselines, in both parse and render modes.  Supported names:
  - pypdfium2  (default)
  - pymupdf

Inputs may be either a local PDF file/directory, or a Hugging Face dataset
repo-id whose `pdf/` subfolder contains the PDFs.  When omitted, defaults to
the HF repo `docling-project/performance-dataset-bo767`.

Usage:
    python perf/run_scaling.py                                   # HF default, render mode, pypdfium2
    python perf/run_scaling.py ./pdfs --mode parse
    python perf/run_scaling.py --mode both --other "pypdfium2;pymupdf"
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import List, Tuple

from tabulate import tabulate
from tqdm import tqdm


DEFAULT_HF_REPO_ID = "docling-project/performance-dataset-bo767"
HF_PDF_SUBDIR = "pdf"


# -------- Input resolution --------


def find_pdfs(path: Path, recursive: bool = False) -> List[Path]:
    if path.is_file():
        return [path] if path.suffix.lower() == ".pdf" else []
    pattern = "**/*.pdf" if recursive else "*.pdf"
    return sorted([p for p in path.glob(pattern) if p.is_file()])


def resolve_pdf_inputs(input_str: str, recursive: bool = False) -> List[Path]:
    """Resolve `input_str` to a list of PDFs.

    If it matches an existing local file or directory, search it for PDFs.
    Otherwise treat it as a Hugging Face dataset repo-id, download via
    snapshot_download (restricted to the `pdf/` subfolder), and iterate
    the downloaded `pdf/` directory recursively.
    """
    p = Path(input_str)
    if p.exists():
        return find_pdfs(p, recursive=recursive)

    from huggingface_hub import snapshot_download

    print(f"Downloading HF dataset {input_str!r} (pattern {HF_PDF_SUBDIR}/**) ...")
    local_dir = snapshot_download(
        repo_id=input_str,
        repo_type="dataset",
        allow_patterns=[f"{HF_PDF_SUBDIR}/**"],
    )
    pdf_dir = Path(local_dir) / HF_PDF_SUBDIR
    if not pdf_dir.is_dir():
        raise RuntimeError(
            f"HF dataset {input_str!r} has no {HF_PDF_SUBDIR}/ subfolder at {pdf_dir}"
        )
    return find_pdfs(pdf_dir, recursive=True)


def count_pages(pdf_paths: List[Path]) -> int:
    """Count total pages across all PDFs using DoclingPdfParser."""
    from docling_parse.pdf_parser import DoclingPdfParser

    parser = DoclingPdfParser(loglevel="fatal")
    total = 0
    for pdf_path in tqdm(pdf_paths, desc="counting pages", unit="doc"):
        try:
            d = parser.load(str(pdf_path), lazy=True)
            total += d.number_of_pages()
            d.unload()
        except Exception:
            pass
    return total


# -------- Decode config helper --------


def _decode_config():
    from docling_parse.pdf_parsers import DecodePageConfig  # type: ignore[import]

    c = DecodePageConfig()
    c.keep_char_cells = False
    c.keep_shapes = False
    c.keep_bitmaps = True
    c.materialize_bitmap_bytes = False
    c.create_word_cells = False
    c.create_line_cells = True
    return c


# -------- Baselines --------


def run_sequential_parse(pdf_paths: List[Path]) -> float:
    """Sequential DoclingPdfParser decode (no render). Returns wall time in seconds."""
    from docling_parse.pdf_parser import DoclingPdfParser

    config = _decode_config()
    config.do_thread_safe = False  # no need for isolated QPDF per page

    parser = DoclingPdfParser(loglevel="fatal")

    t0 = time.perf_counter()
    for pdf_path in tqdm(pdf_paths, desc="  sequential parse", unit="doc", leave=False):
        try:
            doc = parser.load(str(pdf_path), lazy=True)
            for _, _ in doc.iterate_pages(config=config):
                pass
            doc.unload()
        except Exception as e:
            print(f"  sequential error on {pdf_path}: {e}")
    return time.perf_counter() - t0


def run_pypdfium_parse(pdf_paths: List[Path], total_pages: int) -> float:
    """Single-threaded pypdfium2 text extraction."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-parse", unit="page") as pbar:
        for pdf_path in pdf_paths:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                for i in range(len(doc)):
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for l in range(text_page.count_rects()):
                            rect = text_page.get_rect(l)
                            _ = text_page.get_text_bounded(*rect)
                        text_page.close()
                        page.close()
                    except Exception as e:
                        print(f"  pypdfium2 page error on {pdf_path} page {i}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pypdfium2: {errors} errors")
    return time.perf_counter() - t0


def run_pypdfium_render(pdf_paths: List[Path], total_pages: int) -> float:
    """Single-threaded pypdfium2: text extract + scale=2 render to PIL."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-render", unit="page") as pbar:
        for pdf_path in pdf_paths:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                for i in range(len(doc)):
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for l in range(text_page.count_rects()):
                            rect = text_page.get_rect(l)
                            _ = text_page.get_text_bounded(*rect)
                        text_page.close()
                        bitmap = page.render(scale=2)
                        _ = bitmap.to_pil()
                        bitmap.close()
                        page.close()
                    except Exception as e:
                        print(f"  pypdfium2 page error on {pdf_path} page {i}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pypdfium2: {errors} errors")
    return time.perf_counter() - t0


def run_pymupdf_parse(pdf_paths: List[Path], total_pages: int) -> float:
    """Single-threaded pymupdf text extraction."""
    try:
        import fitz  # PyMuPDF
    except ImportError as e:
        print(f"  pymupdf not available: {e}", file=sys.stderr)
        return float("nan")

    # MuPDF writes "MuPDF error: ..." lines to stderr from the C layer;
    # silence them so perf output stays clean.
    try:
        fitz.TOOLS.mupdf_display_errors(False)
    except Exception:
        pass

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pymupdf-parse", unit="page") as pbar:
        for pdf_path in pdf_paths:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                for page in doc:
                    try:
                        _ = page.get_text("text")
                    except Exception as e:
                        print(f"  pymupdf page error on {pdf_path}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pymupdf: {errors} errors")
    return time.perf_counter() - t0


def run_pymupdf_render(pdf_paths: List[Path], total_pages: int) -> float:
    """Single-threaded pymupdf: text extract + scale=2 render to PIL."""
    try:
        import fitz  # PyMuPDF
    except ImportError as e:
        print(f"  pymupdf not available: {e}", file=sys.stderr)
        return float("nan")

    try:
        fitz.TOOLS.mupdf_display_errors(False)
    except Exception:
        pass

    matrix = fitz.Matrix(2, 2)
    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pymupdf-render", unit="page") as pbar:
        for pdf_path in pdf_paths:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                for page in doc:
                    try:
                        _ = page.get_text("text")
                        pix = page.get_pixmap(matrix=matrix)
                        _ = pix.pil_image()
                    except Exception as e:
                        print(f"  pymupdf page error on {pdf_path}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pymupdf: {errors} errors")
    return time.perf_counter() - t0


# Registry: 3rd-party single-threaded backends.
# Each entry maps a name to {"parse": fn, "render": fn} where each fn has
# signature (pdf_paths, total_pages) -> wall_time_seconds.
OTHER_BACKENDS = {
    "pypdfium2": {
        "parse": run_pypdfium_parse,
        "render": run_pypdfium_render,
    },
    "pymupdf": {
        "parse": run_pymupdf_parse,
        "render": run_pymupdf_render,
    },
}


def parse_other_arg(arg: str) -> List[str]:
    names = [n.strip() for n in arg.split(";") if n.strip()]
    unknown = [n for n in names if n not in OTHER_BACKENDS]
    if unknown:
        raise SystemExit(
            f"Unknown --other backend(s): {unknown}. "
            f"Choose from: {sorted(OTHER_BACKENDS)}"
        )
    return names


# -------- Threaded run --------


def run_threaded(
    pdf_paths: List[Path],
    num_threads: int,
    max_concurrent_results: int,
    total_pages: int,
    *,
    render: bool,
    scale: float,
) -> float:
    """Run DoclingThreadedPdfParser; render=True enables rasterisation."""
    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )
    from docling_parse.pdf_parsers import RenderConfig  # type: ignore[import]

    decode_config = _decode_config()

    render_config = None
    if render:
        render_config = RenderConfig()
        render_config.scale = scale

    parser_config = ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=num_threads,
        max_concurrent_results=max_concurrent_results,
        render_config=render_config,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=parser_config,
        decode_config=decode_config,
    )

    for pdf_path in tqdm(pdf_paths, desc="  loading", unit="doc", leave=False):
        try:
            parser.load(str(pdf_path))
        except Exception as e:
            print(f"  threaded load error on {pdf_path}: {e}")

    desc = "  rendering" if render else "  parsing"
    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc=desc, unit="page") as pbar:
        for result in parser.iterate_results():
            if result.success:
                if render:
                    _ = result.get_image()
                    #_ = result.get_page()
                else:
                    _ = result.get_page()
            else:
                errors += 1
            pbar.update(1)
    t1 = time.perf_counter()
    if errors:
        print(f"  threads={num_threads}: {errors} page errors")
    return t1 - t0


# -------- Reporting --------


def _isnan(x: float) -> bool:
    return x != x


def _fmt_speedup(s: float) -> str:
    return "n/a" if _isnan(s) else f"{s:.2f}x"


def _print_table(
    title: str,
    baselines: List[Tuple[str, float]],
    threaded_results: List[Tuple[int, float]],
    total_pages: int,
) -> None:
    """Print one unified table.

    `baselines` is a list of (label, wall_time) for non-threaded reference
    runs (sequential docling, plus selected 3rd-party backends).
    `threaded_results` is a list of (num_threads, wall_time) for the docling
    threaded scaling sweep.

    Columns: backend, threads, wall_time, vs threaded(1), one `vs <baseline>`
    column per baseline (sequential docling and each selected `--other`),
    then pages/sec and ms/page.  All `vs X` values are `X_time / row_time`,
    so higher means the row is faster than X.
    """
    threaded_1 = threaded_results[0][1] if threaded_results else float("nan")

    headers = ["backend", "threads", "wall_time (s)", "vs threaded(1)"]
    for label, _ in baselines:
        headers.append(f"vs {label}")
    headers.extend(["pages/sec", "ms/page"])

    n_vs_baseline = len(baselines)

    def _row(name: str, threads, t: float) -> List[str]:
        if _isnan(t):
            return [name, str(threads), "n/a", "n/a"] + ["n/a"] * n_vs_baseline + ["n/a", "n/a"]
        cells: List[str] = [name, str(threads), f"{t:.3f}"]
        vs_t1 = threaded_1 / t if t > 0 and not _isnan(threaded_1) else float("nan")
        cells.append(_fmt_speedup(vs_t1))
        for _, bt in baselines:
            vs_b = bt / t if t > 0 and not _isnan(bt) else float("nan")
            cells.append(_fmt_speedup(vs_b))
        cells.append(f"{total_pages / t:.1f}" if t > 0 else "n/a")
        cells.append(
            f"{1000.0 * t / total_pages:.2f}" if total_pages > 0 and t > 0 else "n/a"
        )
        return cells

    rows: List[List[str]] = []
    for label, t in baselines:
        rows.append(_row(label, "-", t))
    for n, t in threaded_results:
        rows.append(_row("docling threaded", n, t))

    print()
    print(f"=== {title} ===")
    print(tabulate(rows, headers=headers))


# -------- Mode runner --------


def _run_one_mode(
    pdf_paths: List[Path],
    thread_counts: List[int],
    max_concurrent_results: int,
    total_pages: int,
    other_backends: List[str],
    *,
    render: bool,
    scale: float,
) -> Tuple[List[Tuple[str, float]], List[Tuple[int, float]]]:
    baselines: List[Tuple[str, float]] = []

    # Sequential docling baseline is only meaningful for parse mode
    # (DoclingPdfParser has no rendering path).
    if not render:
        print("Running sequential (DoclingPdfParser) ...")
        t = run_sequential_parse(pdf_paths)
        print(f"  sequential: {t:.3f}s")
        baselines.append(("sequential docling (1t)", t))
        print()

    stage = "render" if render else "parse"
    for name in other_backends:
        fn = OTHER_BACKENDS[name][stage]
        print(f"Running {name} {stage} reference (1 thread) ...")
        t = fn(pdf_paths, total_pages)
        print(f"  {name}: {t:.3f}s")
        baselines.append((f"{name} (1t)", t))
        print()

    threaded_results: List[Tuple[int, float]] = []
    stage_label = "renderer" if render else "parser"
    for n in thread_counts:
        print(f"Running threaded {stage_label} with {n} threads ...")
        t = run_threaded(
            pdf_paths,
            num_threads=n,
            max_concurrent_results=max_concurrent_results,
            total_pages=total_pages,
            render=render,
            scale=scale,
        )
        threaded_results.append((n, t))
        print(f"  threads={n}: {t:.3f}s")

    return baselines, threaded_results


# -------- Main --------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Thread-scaling benchmark for docling-parse (parse and/or render)"
    )
    ap.add_argument(
        "input",
        nargs="?",
        default=DEFAULT_HF_REPO_ID,
        help=(
            "Local PDF file/directory, or a Hugging Face dataset repo-id whose "
            f"`{HF_PDF_SUBDIR}/` subfolder contains the PDFs. "
            f"Default: {DEFAULT_HF_REPO_ID}"
        ),
    )
    ap.add_argument(
        "--mode",
        choices=["parse", "render", "both"],
        default="render",
        help="Benchmark stage: parse (decode-only), render (decode+raster), or both (default: render)",
    )
    ap.add_argument(
        "--recursive", "-r", action="store_true",
        help="Recurse into subdirectories (local paths only; HF downloads always recurse)",
    )
    ap.add_argument(
        "--limit", "-l", type=int, default=None,
        help="Maximum number of documents to process",
    )
    ap.add_argument(
        "--max-concurrent-results", type=int, default=64,
        help="Max buffered results for the threaded parser/renderer (default: 64)",
    )
    ap.add_argument(
        "--threads", type=str, default="1,2,4,8,12,16",
        help="Comma-separated list of thread counts to test (default: 1,2,4,8,12,16)",
    )
    ap.add_argument(
        "--scale", type=float, default=1.0,
        help="Render scale for rendering (default: 1.0; render/both modes only)",
    )
    ap.add_argument(
        "--other",
        type=str,
        default="pypdfium2",
        help=(
            "Semicolon-separated 3rd-party single-threaded backends to run as "
            f"reference baselines. Available: {';'.join(sorted(OTHER_BACKENDS))}. "
            'Default: "pypdfium2". Use "" to skip.'
        ),
    )

    args = ap.parse_args(argv)

    # Validate CLI args before doing any I/O (HF download, page counting).
    thread_counts = [int(x.strip()) for x in args.threads.split(",")]
    other_backends = parse_other_arg(args.other)

    pdfs = resolve_pdf_inputs(args.input, recursive=args.recursive)
    if args.limit is not None:
        pdfs = pdfs[: args.limit]
    if not pdfs:
        print(f"No PDFs found for input: {args.input}", file=sys.stderr)
        return 2

    total_pages = count_pages(pdfs)

    print(f"Benchmark: {len(pdfs)} documents, {total_pages} total pages")
    print(f"Mode: {args.mode}")
    print(f"Thread counts to test: {thread_counts}")
    print(f"Max concurrent results: {args.max_concurrent_results}")
    print(f"Other backends: {other_backends if other_backends else '(none)'}")
    if args.mode in ("render", "both"):
        print(f"Render scale: {args.scale}")
    print()

    modes_to_run = ["parse", "render"] if args.mode == "both" else [args.mode]
    for m in modes_to_run:
        render = m == "render"
        title = "RENDER (decode + rasterise)" if render else "PARSE (decode only)"
        print(f"\n##### {title} #####")
        baselines, threaded_results = _run_one_mode(
            pdfs,
            thread_counts,
            args.max_concurrent_results,
            total_pages,
            other_backends,
            render=render,
            scale=args.scale,
        )
        _print_table(title, baselines, threaded_results, total_pages)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
