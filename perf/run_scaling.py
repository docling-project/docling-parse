#!/usr/bin/env python3
"""
Thread-scaling benchmark for docling-parse.

Parses all PDFs in a directory first with the sequential DoclingPdfParser,
then with DoclingThreadedPdfParser at 1, 2, 4, 8, 12 and 16 threads.
Prints a table of total wall time vs thread count.

Usage:
    python perf/perf_scaling.py ./path/to/pdfs [-r] [--limit N] [--max-concurrent-results 64] [--threads 1,2,4,8,12,16]
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import List

from tabulate import tabulate


def find_pdfs(path: Path, recursive: bool = False) -> List[Path]:
    if path.is_file():
        return [path] if path.suffix.lower() == ".pdf" else []
    pattern = "**/*.pdf" if recursive else "*.pdf"
    return sorted([p for p in path.glob(pattern) if p.is_file()])


def run_sequential(pdf_paths: List[Path]) -> float:
    """Run sequential DoclingPdfParser over all PDFs. Returns wall time in seconds."""
    from docling_parse.pdf_parser import DoclingPdfParser
    from docling_parse.pdf_parsers import DecodePageConfig  # type: ignore[import]

    config = DecodePageConfig()
    config.keep_char_cells = False
    config.keep_shapes = False
    config.keep_bitmaps = False
    config.create_word_cells = False
    config.create_line_cells = True
    config.do_thread_safe = False  # no need for isolated QPDF per page

    parser = DoclingPdfParser(loglevel="fatal")

    t0 = time.perf_counter()

    for pdf_path in pdf_paths:
        try:
            doc = parser.load(str(pdf_path), lazy=True)
            for page_no, _ in doc.iterate_pages(config=config):
                pass
            doc.unload()
        except Exception as e:
            print(f"  sequential error on {pdf_path}: {e}")

    t1 = time.perf_counter()
    return t1 - t0


def run_threaded(
    pdf_paths: List[Path],
    num_threads: int,
    max_concurrent_results: int,
) -> float:
    """Run DoclingThreadedPdfParser over all PDFs. Returns wall time in seconds."""
    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )
    from docling_parse.pdf_parsers import DecodePageConfig  # type: ignore[import]

    decode_config = DecodePageConfig()
    decode_config.keep_char_cells = False
    decode_config.keep_shapes = False
    decode_config.keep_bitmaps = False
    decode_config.create_word_cells = False
    decode_config.create_line_cells = True

    parser_config = ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=num_threads,
        max_concurrent_results=max_concurrent_results,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=parser_config,
        decode_config=decode_config,
    )

    for pdf_path in pdf_paths:
        try:
            parser.load(str(pdf_path))
        except Exception as e:
            print(f"  threaded load error on {pdf_path}: {e}")

    t0 = time.perf_counter()

    from docling_parse.pdf_parser import PdfDocument
    from docling_core.types.doc.page import PdfPageBoundaryType

    # Reuse PdfDocument's conversion methods via a lightweight instance
    dummy_doc = PdfDocument.__new__(PdfDocument)
    dummy_doc._boundary_type = PdfPageBoundaryType.CROP_BOX

    count = 0
    errors = 0
    while parser.has_tasks():
        task = parser.get_task()
        if task.success:
            page_decoder, timings = task.get()
            # Convert to SegmentedPdfPage (same work as sequential path)
            _ = dummy_doc._to_segmented_page_from_decoder(
                page_decoder=page_decoder, config=decode_config,
            )
            count += 1
        else:
            errors += 1

    t1 = time.perf_counter()

    if errors > 0:
        print(f"  threads={num_threads}: {errors} page errors")

    return t1 - t0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Thread-scaling benchmark: sequential vs threaded at various thread counts"
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
        help="Max buffered results for threaded parser (default: 64)",
    )
    ap.add_argument(
        "--threads", type=str, default="1,2,4,8,12,16",
        help="Comma-separated list of thread counts to test (default: 1,2,4,8,12,16)",
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

    total_pages = 0
    for pdf_path in pdfs:
        try:
            from docling_parse.pdf_parser import DoclingPdfParser

            p = DoclingPdfParser(loglevel="fatal")
            d = p.load(str(pdf_path), lazy=True)
            total_pages += d.number_of_pages()
            d.unload()
        except Exception:
            pass

    print(f"Benchmark: {len(pdfs)} documents, {total_pages} total pages")
    print(f"Thread counts to test: {thread_counts}")
    print(f"Max concurrent results: {args.max_concurrent_results}")
    print()

    # Sequential run
    print("Running sequential (DoclingPdfParser) ...")
    seq_time = run_sequential(pdfs)
    print(f"  sequential: {seq_time:.3f}s")
    print()

    # Threaded runs
    threaded_results = []
    baseline_threaded_time = None
    for n in thread_counts:
        print(f"Running threaded with {n} threads ...")
        t = run_threaded(pdfs, n, args.max_concurrent_results)
        if baseline_threaded_time is None:
            baseline_threaded_time = t
        speedup_vs_seq = seq_time / t if t > 0 else float("inf")
        speedup_vs_t1 = baseline_threaded_time / t if t > 0 else float("inf")
        threaded_results.append((n, t, speedup_vs_seq, speedup_vs_t1))
        print(f"  threads={n}: {t:.3f}s (vs seq: {speedup_vs_seq:.2f}x, vs t=1: {speedup_vs_t1:.2f}x)")

    # Summary table
    print()
    seq_vs_t1 = baseline_threaded_time / seq_time if baseline_threaded_time > 0 else float("inf")
    headers = ["mode", "threads", "wall_time (s)", "vs sequential", "vs threaded(1)"]
    table = [["sequential", "-", f"{seq_time:.3f}", "1.00x", f"{seq_vs_t1:.2f}x"]]
    for n, t, s_seq, s_t1 in threaded_results:
        table.append(["threaded", str(n), f"{t:.3f}", f"{s_seq:.2f}x", f"{s_t1:.2f}x"])

    print(tabulate(table, headers=headers))

    # Pages per second
    print()
    headers2 = ["mode", "threads", "pages/sec"]
    table2 = [["sequential", "-", f"{total_pages / seq_time:.1f}" if seq_time > 0 else "n/a"]]
    for n, t, _, _ in threaded_results:
        pps = total_pages / t if t > 0 else 0
        table2.append(["threaded", str(n), f"{pps:.1f}"])

    print(tabulate(table2, headers=headers2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
