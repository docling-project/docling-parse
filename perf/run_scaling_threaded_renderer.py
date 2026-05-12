#!/usr/bin/env python3
"""
Thread-scaling benchmark for docling-parse threaded parse-and-render mode.

Renders all PDFs in a directory with DoclingThreadedPdfParser at
1, 2, 4, 8, 12 and 16 threads and prints a table of total wall time
vs thread count.  A single-threaded pypdfium2 run (text + image at
scale=2) is included as a reference baseline.

Usage:
    python perf/run_scaling_threaded_renderer.py ./path/to/pdfs [-r] [--limit N] [--max-concurrent-results 64] [--threads 1,2,4,8,12,16] [--canvas-width 1024]
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import List, Tuple

from tabulate import tabulate
from tqdm import tqdm


def find_pdfs(path: Path, recursive: bool = False) -> List[Path]:
    if path.is_file():
        return [path] if path.suffix.lower() == ".pdf" else []
    pattern = "**/*.pdf" if recursive else "*.pdf"
    return sorted([p for p in path.glob(pattern) if p.is_file()])


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


def run_pypdfium(pdf_paths: List[Path], total_pages: int) -> float:
    """Render all PDFs with pypdfium2 (single-threaded, text + image at scale=2).

    Returns wall time in seconds.
    """
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()

    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2", unit="page") as pbar:
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

                        # Extract text (same as run_perf.py)
                        text_page = page.get_textpage()
                        for l in range(text_page.count_rects()):
                            rect = text_page.get_rect(l)
                            _ = text_page.get_text_bounded(*rect)
                        text_page.close()

                        # Render image at scale=2
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

    t1 = time.perf_counter()

    if errors > 0:
        print(f"  pypdfium2: {errors} errors")

    return t1 - t0


def run_threaded(
    pdf_paths: List[Path],
    num_threads: int,
    max_concurrent_results: int,
    canvas_width: int,
    total_pages: int,
) -> float:
    """Run DoclingThreadedPdfParser with rendering enabled over all PDFs."""
    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )
    from docling_parse.pdf_parsers import DecodePageConfig, RenderConfig  # type: ignore[import]

    decode_config = DecodePageConfig()
    decode_config.keep_char_cells = False
    decode_config.keep_shapes = False
    decode_config.keep_bitmaps = False
    decode_config.create_word_cells = False
    decode_config.create_line_cells = True

    render_config = RenderConfig()
    render_config.canvas_width = canvas_width

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

    t0 = time.perf_counter()

    errors = 0
    with tqdm(total=total_pages, desc="  rendering", unit="page") as pbar:
        for result in parser.iterate_results():
            if result.success:
                _ = result.get_image()
            else:
                errors += 1
            pbar.update(1)

    t1 = time.perf_counter()

    if errors > 0:
        print(f"  threads={num_threads}: {errors} page errors")

    return t1 - t0


def _fmt_row(
    label: str,
    t: float,
    total_pages: int,
    speedup_vs_pypdfium: float,
    speedup_vs_t1: float,
) -> List[str]:
    if t != t:  # nan
        return [label, "n/a", "n/a", "n/a", "n/a", "n/a"]
    def _spd(s: float) -> str:
        return f"{s:.2f}x" if s == s else "n/a"
    return [
        label,
        f"{t:.3f}",
        _spd(speedup_vs_pypdfium),
        _spd(speedup_vs_t1),
        f"{total_pages / t:.1f}" if t > 0 else "n/a",
        f"{1000.0 * t / total_pages:.2f}" if total_pages > 0 and t > 0 else "n/a",
    ]


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Thread-scaling benchmark for the threaded renderer at various thread counts"
    )
    ap.add_argument("input", help="Path to a PDF file or directory of PDFs")
    ap.add_argument(
        "--recursive", "-r", action="store_true",
        help="Recurse into subdirectories",
    )
    ap.add_argument(
        "--limit", "-l", type=int, default=None,
        help="Maximum number of documents to process",
    )
    ap.add_argument(
        "--max-concurrent-results", type=int, default=64,
        help="Max buffered results for the threaded renderer (default: 64)",
    )
    ap.add_argument(
        "--threads", type=str, default="1,2,4,8,12,16",
        help="Comma-separated list of thread counts to test (default: 1,2,4,8,12,16)",
    )
    ap.add_argument(
        "--canvas-width", type=int, default=1024,
        help="Canvas width in pixels for rendering (default: 1024)",
    )

    args = ap.parse_args(argv)

    input_path = Path(args.input)
    pdfs = find_pdfs(input_path, recursive=args.recursive)

    if args.limit is not None:
        pdfs = pdfs[: args.limit]

    if not pdfs:
        print(f"No PDFs found at {input_path}", file=sys.stderr)
        return 2

    thread_counts = [int(x.strip()) for x in args.threads.split(",")]

    total_pages = count_pages(pdfs)

    print(f"Benchmark: {len(pdfs)} documents, {total_pages} total pages")
    print(f"Thread counts to test: {thread_counts}")
    print(f"Max concurrent results: {args.max_concurrent_results}")
    print(f"Canvas width: {args.canvas_width}px")
    print()

    # pypdfium2 reference (single-threaded)
    print("Running pypdfium2 reference (1 thread, text + image at scale=2) ...")
    pypdfium_time = run_pypdfium(pdfs, total_pages)
    print(f"  pypdfium2: {pypdfium_time:.3f}s")
    print()

    # docling threaded renderer
    threaded_results: List[Tuple[int, float]] = []
    baseline_time = None
    for n in thread_counts:
        print(f"Running threaded renderer with {n} threads ...")
        t = run_threaded(pdfs, n, args.max_concurrent_results, args.canvas_width, total_pages)
        if baseline_time is None:
            baseline_time = t
        threaded_results.append((n, t))
        speedup_vs_t1 = baseline_time / t if t > 0 else float("inf")
        print(f"  threads={n}: {t:.3f}s (vs t=1: {speedup_vs_t1:.2f}x)")

    baseline_threaded_time = threaded_results[0][1] if threaded_results else float("nan")

    print()
    headers = ["backend", "wall_time (s)", "vs pypdfium2", "vs docling(1t)", "pages/sec", "ms/page"]
    table = [_fmt_row("pypdfium2 (1t)", pypdfium_time, total_pages, 1.0, pypdfium_time / baseline_threaded_time if baseline_threaded_time > 0 else float("nan"))]
    for n, t in threaded_results:
        speedup_vs_pypdfium = pypdfium_time / t if t > 0 and pypdfium_time == pypdfium_time else float("nan")
        speedup_vs_t1 = baseline_threaded_time / t if t > 0 and baseline_threaded_time == baseline_threaded_time else float("nan")
        table.append(_fmt_row(f"docling ({n}t)", t, total_pages, speedup_vs_pypdfium, speedup_vs_t1))

    print(tabulate(table, headers=headers))

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
