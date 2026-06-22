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
    python perf/run_scaling.py ./pdfs --mode render --keep-char-cells=true \
        --create-word-cells=true --create-line-cells=true \
        --keep-shapes=true --keep-bitmaps=true
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import List, Tuple

from tabulate import tabulate
from tqdm import tqdm

DEFAULT_HF_REPO_ID = "docling-project/performance-dataset-bo767"
HF_PDF_SUBDIR = "pdf"


def _default_timing_csv_path() -> Path:
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    return Path(f"timing-{timestamp}.csv")


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


def page_counts(pdf_paths: List[Path]) -> List[Tuple[Path, int]]:
    """Count pages per PDF using DoclingPdfParser."""
    from docling_parse.pdf_parser import DoclingPdfParser

    parser = DoclingPdfParser(loglevel="fatal")
    counts: List[Tuple[Path, int]] = []
    for pdf_path in tqdm(pdf_paths, desc="counting pages", unit="doc"):
        try:
            d = parser.load(str(pdf_path), lazy=True)
            counts.append((pdf_path, d.number_of_pages()))
            d.unload()
        except Exception:
            pass
    return counts


def apply_max_pages(
    pdf_paths: List[Path], max_pages: int | None
) -> Tuple[List[Tuple[Path, List[int] | None]], int]:
    """Apply an exact total-page cap across PDFs in input order.

    Returns a schedule of `(pdf_path, page_numbers)` where `page_numbers` is
    `None` for all pages in a document, or an explicit 1-indexed subset for the
    final truncated document. The second return value is the total scheduled
    page count.
    """
    counts = page_counts(pdf_paths)
    if max_pages is None:
        return [(pdf_path, None) for pdf_path, _ in counts], sum(
            count for _, count in counts
        )

    if max_pages <= 0:
        return [], 0

    schedule: List[Tuple[Path, List[int] | None]] = []
    remaining = max_pages
    total = 0

    for pdf_path, count in counts:
        if remaining <= 0:
            break
        if count <= remaining:
            schedule.append((pdf_path, None))
            remaining -= count
            total += count
        else:
            page_numbers = list(range(1, remaining + 1))
            schedule.append((pdf_path, page_numbers))
            total += remaining
            remaining = 0
            break

    return schedule, total


# -------- Decode config helper --------


def _str_to_bool(value: str | bool) -> bool:
    if isinstance(value, bool):
        return value
    normalized = value.strip().lower()
    if normalized in {"1", "true", "t", "yes", "y", "on"}:
        return True
    if normalized in {"0", "false", "f", "no", "n", "off"}:
        return False
    raise argparse.ArgumentTypeError(
        f"expected a boolean value, got {value!r}; use true or false"
    )


def _add_bool_value_arg(
    ap: argparse.ArgumentParser,
    name: str,
    *,
    default: bool,
    help: str,
) -> None:
    ap.add_argument(
        f"--{name}",
        type=_str_to_bool,
        default=default,
        metavar="{true,false}",
        help=f"{help} (default: {str(default).lower()})",
    )


def _decode_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "keep_char_cells": args.keep_char_cells,
        "keep_shapes": args.keep_shapes,
        "keep_bitmaps": args.keep_bitmaps,
        "create_word_cells": args.create_word_cells,
        "create_line_cells": args.create_line_cells,
    }


def _materialization_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "materialize_char_cells": args.materialize_char_cells,
        "materialize_word_cells": args.materialize_word_cells,
        "materialize_line_cells": args.materialize_line_cells,
        "materialize_shapes": args.materialize_shapes,
        "materialize_bitmaps": args.materialize_bitmaps,
        "materialize_bitmap_bytes": args.materialize_bitmap_bytes,
    }


def _materializes_page_data(materialization_options: dict[str, bool]) -> bool:
    return any(
        materialization_options[name]
        for name in (
            "materialize_char_cells",
            "materialize_word_cells",
            "materialize_line_cells",
            "materialize_shapes",
            "materialize_bitmaps",
        )
    )


def _decode_config():
    from docling_parse.pdf_parser import DecodeConfig

    return DecodeConfig()


def _content_config(
    decode_options: dict[str, bool], materialization_options: dict[str, bool]
):
    from docling_parse.pdf_parser import ContentConfig, ContentLevel

    def _level(keep: bool, materialize: bool) -> ContentLevel:
        if materialize:
            return ContentLevel.COMPUTE_AND_MATERIALIZE
        if keep:
            return ContentLevel.COMPUTE
        return ContentLevel.SKIP

    return ContentConfig(
        char_cells_content_level=_level(
            decode_options["keep_char_cells"],
            materialization_options["materialize_char_cells"],
        ),
        word_cells_content_level=_level(
            decode_options["create_word_cells"],
            materialization_options["materialize_word_cells"],
        ),
        line_cells_content_level=_level(
            decode_options["create_line_cells"],
            materialization_options["materialize_line_cells"],
        ),
        shapes_content_level=_level(
            decode_options["keep_shapes"],
            materialization_options["materialize_shapes"],
        ),
        bitmaps_content_level=_level(
            decode_options["keep_bitmaps"],
            materialization_options["materialize_bitmaps"],
        ),
        include_bitmap_bytes=materialization_options["materialize_bitmap_bytes"],
    )


def _config_rows(values: dict[str, object], fields: List[str]) -> List[List[str]]:
    return [[field, values[field]] for field in fields]


def _print_run_configs(
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
) -> None:
    from docling_parse.pdf_parsers import RenderConfig  # type: ignore[import]

    decode_config = _decode_config()
    decode_fields = [
        "do_sanitization",
        "enforce_same_font",
        "horizontal_cell_tolerance",
        "word_space_width_factor_for_merge",
        "line_space_width_factor_for_merge",
        "line_space_width_factor_for_merge_with_space",
        "max_num_lines",
        "max_num_bitmaps",
        "do_thread_safe",
        "release_native_memory_every_n_pages",
        "keep_glyphs",
        "keep_qpdf_warnings",
    ]
    print("Decode config:")
    print(
        tabulate(
            _config_rows(decode_config.model_dump(), decode_fields),
            headers=["parameter", "value"],
        )
    )
    print()

    content_config = _content_config(decode_options, materialization_options)
    content_fields = [
        "char_cells_content_level",
        "word_cells_content_level",
        "line_cells_content_level",
        "shapes_content_level",
        "bitmaps_content_level",
        "include_bitmap_bytes",
    ]
    print("Content config:")
    print(
        tabulate(
            _config_rows(content_config.model_dump(), content_fields),
            headers=["parameter", "value"],
        )
    )
    print()

    print("Render config:")
    if not render:
        print(tabulate([["enabled", False]], headers=["parameter", "value"]))
        return

    render_config = RenderConfig()
    render_config.scale = scale
    render_values = {
        "render_text": render_config.render_text,
        "draw_text_bbox": render_config.draw_text_bbox,
        "draw_text_basepoint": render_config.draw_text_basepoint,
        "fit_glyph_bbox_to_target": render_config.fit_glyph_bbox_to_target,
        "resolve_fonts": render_config.resolve_fonts,
        "font_similarity_cutoff": render_config.font_similarity_cutoff,
        "scale": render_config.scale,
        "canvas_width": render_config.canvas_width,
        "canvas_height": render_config.canvas_height,
    }
    render_fields = [
        "render_text",
        "draw_text_bbox",
        "draw_text_basepoint",
        "fit_glyph_bbox_to_target",
        "resolve_fonts",
        "font_similarity_cutoff",
        "scale",
        "canvas_width",
        "canvas_height",
    ]
    print(
        tabulate(
            _config_rows(render_values, render_fields),
            headers=["parameter", "value"],
        )
    )


def _timing_csv_fieldnames() -> List[str]:
    return [
        "mode",
        "threads",
        "render",
        "doc_key",
        "page_number",
        "success",
        "timing_total_s",
        "timing_make_page_decoder_s",
        "timing_decode_page_s",
        "timing_create_word_cells_s",
        "timing_create_line_cells_s",
        "timing_render_page_s",
        "error_message",
    ]


def _timing_csv_row(
    *, mode: str, num_threads: int, render: bool, result
) -> dict[str, object]:
    row: dict[str, object] = {
        "mode": mode,
        "threads": num_threads,
        "render": render,
        "doc_key": result.doc_key,
        "page_number": result.page_number,
        "success": result.success,
        "error_message": result.error_message,
    }
    timing_keys = _timing_csv_fieldnames()[7:-1]
    if result.success:
        from docling_parse.pdf_parser import PageRenderTimings

        timings = result.timings
        row["timing_total_s"] = timings.total_s
        row["timing_make_page_decoder_s"] = timings.make_page_decoder_s
        row["timing_decode_page_s"] = timings.decode_page_s
        row["timing_create_word_cells_s"] = timings.create_word_cells_s
        row["timing_create_line_cells_s"] = timings.create_line_cells_s
        row["timing_render_page_s"] = (
            timings.render_page_s if isinstance(timings, PageRenderTimings) else 0.0
        )
    else:
        row["timing_total_s"] = 0.0
        for key in timing_keys:
            row[key] = 0.0
    return row


# -------- Baselines --------


def run_sequential_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
) -> float:
    """Sequential DoclingPdfParser decode (no render). Returns wall time in seconds."""
    from docling_parse.pdf_parser import DoclingPdfParser

    config = _decode_config()
    config.do_thread_safe = False  # no need for isolated QPDF per page
    content_config = _content_config(decode_options, materialization_options)

    parser = DoclingPdfParser(loglevel="fatal")

    t0 = time.perf_counter()
    for pdf_path, page_numbers in tqdm(
        pdf_schedule, desc="  sequential parse", unit="doc", leave=False
    ):
        try:
            doc = parser.load(
                str(pdf_path),
                lazy=True,
                decode_config=config,
                content_config=content_config,
            )
            if page_numbers is None:
                for _, _ in doc.iterate_pages():
                    pass
            else:
                for page_number in page_numbers:
                    _ = doc.get_page(page_number)
            doc.unload()
        except Exception as e:
            print(f"  sequential error on {pdf_path}: {e}")
    return time.perf_counter() - t0


def run_pypdfium_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pypdfium2 text extraction."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-parse", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    range(len(doc))
                    if page_numbers is None
                    else (page_number - 1 for page_number in page_numbers)
                )
                for i in pages:
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for rect_idx in range(text_page.count_rects()):
                            rect = text_page.get_rect(rect_idx)
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


def run_pypdfium_render(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pypdfium2: text extract + scale=2 render to PIL."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-render", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    range(len(doc))
                    if page_numbers is None
                    else (page_number - 1 for page_number in page_numbers)
                )
                for i in pages:
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for rect_idx in range(text_page.count_rects()):
                            rect = text_page.get_rect(rect_idx)
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


def run_pymupdf_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
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
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    doc
                    if page_numbers is None
                    else (doc[page_number - 1] for page_number in page_numbers)
                )
                for page in pages:
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


def run_pymupdf_render(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
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
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    doc
                    if page_numbers is None
                    else (doc[page_number - 1] for page_number in page_numbers)
                )
                for page in pages:
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
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    num_threads: int,
    max_concurrent_results: int,
    total_pages: int,
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    enable_timing: bool,
    timing_csv: Path,
) -> float:
    """Run DoclingThreadedPdfParser; render=True enables rasterisation."""
    from docling_parse.pdf_parsers import RenderConfig  # type: ignore[import]

    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )

    decode_config = _decode_config()
    content_config = _content_config(decode_options, materialization_options)
    materialize_page = _materializes_page_data(materialization_options)

    render_config = None
    if render:
        render_config = RenderConfig()
        render_config.scale = scale

    parser_config = ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=num_threads,
        max_concurrent_results=max_concurrent_results,
        render_config=render_config,
        page_content_config=content_config,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=parser_config,
        decode_config=decode_config,
    )

    for pdf_path, page_numbers in tqdm(
        pdf_schedule, desc="  loading", unit="doc", leave=False
    ):
        try:
            parser.load(str(pdf_path), page_numbers=page_numbers)
        except Exception as e:
            print(f"  threaded load error on {pdf_path}: {e}")

    desc = "  rendering" if render else "  parsing"
    mode = "render" if render else "parse"
    t0 = time.perf_counter()
    errors = 0
    timing_handle = None
    timing_writer = None
    try:
        if enable_timing:
            timing_csv.parent.mkdir(parents=True, exist_ok=True)
            timing_handle = timing_csv.open("a", newline="", encoding="utf-8")
            timing_writer = csv.DictWriter(
                timing_handle,
                fieldnames=_timing_csv_fieldnames(),
            )
            if timing_handle.tell() == 0:
                timing_writer.writeheader()

        with tqdm(total=total_pages, desc=desc, unit="page") as pbar:
            for result in parser.iterate_results():
                if result.success:
                    if render:
                        result.get_image()
                        if materialize_page:
                            result.get_page()

                        """
                        assert len(page.shapes)==0, "len(page.shapes)==0"
                        assert len(page.char_cells)==0, "len(page.char_cells)==0"

                        for br in page.bitmap_resources:
                            assert br.image==None
                        """
                    else:
                        if materialize_page:
                            result.get_page()
                else:
                    errors += 1

                if timing_writer is not None:
                    timing_writer.writerow(
                        _timing_csv_row(
                            mode=mode,
                            num_threads=num_threads,
                            render=render,
                            result=result,
                        )
                    )
                pbar.update(1)
    finally:
        if timing_handle is not None:
            timing_handle.close()
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
            return (
                [name, str(threads), "n/a", "n/a"]
                + ["n/a"] * n_vs_baseline
                + ["n/a", "n/a"]
            )
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
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    thread_counts: List[int],
    max_concurrent_results: int,
    total_pages: int,
    other_backends: List[str],
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    enable_timing: bool,
    timing_csv: Path,
) -> Tuple[List[Tuple[str, float]], List[Tuple[int, float]]]:
    baselines: List[Tuple[str, float]] = []

    # Sequential docling baseline is only meaningful for parse mode
    # (DoclingPdfParser has no rendering path).
    if not render:
        print("Running sequential (DoclingPdfParser) ...")
        t = run_sequential_parse(
            pdf_schedule,
            decode_options,
            materialization_options,
        )
        print(f"  sequential: {t:.3f}s")
        baselines.append(("sequential docling (1t)", t))
        print()

    stage = "render" if render else "parse"
    for name in other_backends:
        fn = OTHER_BACKENDS[name][stage]
        print(f"Running {name} {stage} reference (1 thread) ...")
        t = fn(pdf_schedule, total_pages)
        print(f"  {name}: {t:.3f}s")
        baselines.append((f"{name} (1t)", t))
        print()

    threaded_results: List[Tuple[int, float]] = []
    stage_label = "renderer" if render else "parser"
    for n in thread_counts:
        print(f"Running threaded {stage_label} with {n} threads ...")
        t = run_threaded(
            pdf_schedule,
            num_threads=n,
            max_concurrent_results=max_concurrent_results,
            total_pages=total_pages,
            render=render,
            scale=scale,
            decode_options=decode_options,
            materialization_options=materialization_options,
            enable_timing=enable_timing,
            timing_csv=timing_csv,
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
        "--recursive",
        "-r",
        action="store_true",
        help="Recurse into subdirectories (local paths only; HF downloads always recurse)",
    )
    ap.add_argument(
        "--max-pages",
        "-l",
        type=int,
        default=None,
        help="Maximum number of pages to process across all input PDFs",
    )
    ap.add_argument(
        "--max-concurrent-results",
        type=int,
        default=64,
        help="Max buffered results for the threaded parser/renderer (default: 64)",
    )
    ap.add_argument(
        "--threads",
        type=str,
        default="1,2,4,8,12,16",
        help="Comma-separated list of thread counts to test (default: 1,2,4,8,12,16)",
    )
    ap.add_argument(
        "--scale",
        type=float,
        default=1.0,
        help="Render scale for rendering (default: 1.0; render/both modes only)",
    )
    _add_bool_value_arg(
        ap,
        "keep-char-cells",
        default=True,
        help="Populate character cells and emit text render instructions",
    )
    _add_bool_value_arg(
        ap,
        "create-word-cells",
        default=False,
        help="Create word cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "create-line-cells",
        default=False,
        help="Create line cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "keep-shapes",
        default=False,
        help="Keep vector shape cells",
    )
    _add_bool_value_arg(
        ap,
        "keep-bitmaps",
        default=False,
        help="Keep bitmap resources/cells",
    )
    _add_bool_value_arg(
        ap,
        "materialize-char-cells",
        default=False,
        help="Materialize character cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-word-cells",
        default=False,
        help="Materialize word cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-line-cells",
        default=False,
        help="Materialize line cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-shapes",
        default=False,
        help="Materialize vector shapes into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmaps",
        default=False,
        help="Materialize bitmap locations into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmap-bytes",
        default=False,
        help="Materialize bitmap image bytes when bitmap locations are materialized",
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
    ap.add_argument(
        "--enable-timing",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Write one CSV timing row per page result (default: disabled)",
    )
    ap.add_argument(
        "--timing-csv",
        type=Path,
        default=_default_timing_csv_path(),
        help="CSV path used when --enable-timing is set",
    )

    args = ap.parse_args(argv)

    # Validate CLI args before doing any I/O (HF download, page counting).
    thread_counts = [int(x.strip()) for x in args.threads.split(",")]
    other_backends = parse_other_arg(args.other)
    decode_options = _decode_options_from_args(args)
    materialization_options = _materialization_options_from_args(args)

    pdfs = resolve_pdf_inputs(args.input, recursive=args.recursive)
    if not pdfs:
        print(f"No PDFs found for input: {args.input}", file=sys.stderr)
        return 2

    pdf_schedule, total_pages = apply_max_pages(pdfs, args.max_pages)
    if not pdf_schedule or total_pages <= 0:
        print("No pages selected for benchmarking", file=sys.stderr)
        return 2

    print(f"Benchmark: {len(pdf_schedule)} documents, {total_pages} total pages")
    print(f"Mode: {args.mode}")
    print(f"Thread counts to test: {thread_counts}")
    print(f"Max concurrent results: {args.max_concurrent_results}")
    print(f"Other backends: {other_backends if other_backends else '(none)'}")
    if args.mode in ("render", "both"):
        print(f"Render scale: {args.scale}")
    print()
    _print_run_configs(
        render=args.mode in ("render", "both"),
        scale=args.scale,
        decode_options=decode_options,
        materialization_options=materialization_options,
    )
    print()

    modes_to_run = ["parse", "render"] if args.mode == "both" else [args.mode]
    for m in modes_to_run:
        render = m == "render"
        title = "RENDER (decode + rasterise)" if render else "PARSE (decode only)"
        print(f"\n##### {title} #####")
        baselines, threaded_results = _run_one_mode(
            pdf_schedule,
            thread_counts,
            args.max_concurrent_results,
            total_pages,
            other_backends,
            render=render,
            scale=args.scale,
            decode_options=decode_options,
            materialization_options=materialization_options,
            enable_timing=args.enable_timing,
            timing_csv=args.timing_csv,
        )
        _print_table(title, baselines, threaded_results, total_pages)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
