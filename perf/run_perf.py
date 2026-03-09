#!/usr/bin/env python3
"""
Performance harness for page-by-page PDF parsing.

Outputs a CSV with rows:
filename,page_number,elapsed_sec,success,error

Parsers supported:
- docling (default) — uses docling-parse
- pdfplumber
- pypdfium2 (alias: pypdfium)
- pymupdf (fitz)

Install extras for non-docling parsers only when needed, e.g.:
  pip install .[perf-tools]
or with uv:
  uv sync --group perf-test
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, median
from typing import Callable, Iterable, List, Tuple
from tqdm import tqdm
from tabulate import tabulate


# -------- Utilities --------


def find_pdfs(path: Path, recursive: bool = False) -> List[Path]:
    if path.is_file():
        return [path] if path.suffix.lower() == ".pdf" else []
    pattern = "**/*.pdf" if recursive else "*.pdf"
    return sorted([p for p in path.glob(pattern) if p.is_file()])


def ensure_parent_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    if p <= 0:
        return min(values)
    if p >= 100:
        return max(values)
    vs = sorted(values)
    k = (len(vs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(vs) - 1)
    if f == c:
        return vs[f]
    d0 = vs[f] * (c - k)
    d1 = vs[c] * (k - f)
    return d0 + d1


def fmt_seconds(s: float) -> str:
    return f"{s:.6f}"


@dataclass
class Row:
    filename: str
    page_number: int
    elapsed_sec: float
    success: bool
    error: str
    timings_detail: dict = None  # optional per-key timing breakdown (docling only)

    def __post_init__(self):
        if self.timings_detail is None:
            self.timings_detail = {}


# -------- Parser adapters --------


def _get_docling_static_timing_keys() -> List[str]:
    """Return all static timing keys from the C++ pybind module."""
    from docling_parse.pdf_parsers import get_static_timing_keys  # type: ignore[import]
    return sorted(get_static_timing_keys())


def parse_with_docling(use_bytesio: bool = False) -> Callable[[Path], Iterable[Row]]:
    def _runner(pdf_path: Path) -> Iterable[Row]:
        from io import BytesIO
        from docling_parse.pdf_parser import DoclingPdfParser
        from docling_parse.pdf_parsers import DecodePageConfig  # type: ignore[import]
        from docling_core.types.doc.page import PdfPageBoundaryType

        timing_keys = _get_docling_static_timing_keys()

        rows: List[Row] = []
        try:
            parser = DoclingPdfParser(loglevel="fatal")
            if use_bytesio:
                source = BytesIO(pdf_path.read_bytes())
            else:
                source = str(pdf_path)
            doc = parser.load(
                source,
                lazy=True,
                boundary_type=PdfPageBoundaryType.CROP_BOX,
            )
            try:
                n = doc.number_of_pages()
            except Exception as e:  # pragma: no cover
                rows.append(Row(str(pdf_path), -1, 0.0, False, f"num_pages: {e}"))
                return rows

            for page_idx in range(1, n + 1):
                t0 = time.perf_counter()
                err = ""
                ok = True
                detail: dict = {}
                try:
                    perf_config = DecodePageConfig()
                    perf_config.keep_char_cells = False
                    perf_config.keep_shapes = False
                    perf_config.keep_bitmaps = False
                    perf_config.create_word_cells = False
                    perf_config.create_line_cells = True
                    _, timings_obj = doc.get_page_with_timings(
                        page_idx,
                        config=perf_config,
                    )
                    static_t = timings_obj.get_static_timings()
                    for key in timing_keys:
                        detail[key] = static_t.get(key, 0.0)
                except Exception as e:  # pragma: no cover
                    ok = False
                    err = str(e)
                    print(f"error: {err}")
                t1 = time.perf_counter()
                rows.append(Row(str(pdf_path), page_idx, t1 - t0, ok, err, detail))

            # best-effort cleanup
            try:
                doc.unload()
            except Exception:
                pass

        except Exception as e:  # pragma: no cover
            rows.append(Row(str(pdf_path), -1, 0.0, False, f"load: {e}"))

        return rows

    return _runner


def parse_with_pdfplumber(pdf_path: Path) -> Iterable[Row]:
    try:
        import pdfplumber  # type: ignore
    except Exception as e:  # pragma: no cover
        return [Row(str(pdf_path), -1, 0.0, False, f"import pdfplumber: {e}")]

    rows: List[Row] = []
    try:
        with pdfplumber.open(str(pdf_path)) as pdf:
            n = len(pdf.pages)
            for idx in range(n):
                t0 = time.perf_counter()
                ok = True
                err = ""
                try:
                    _ = pdf.pages[idx].extract_text()  # parse text via pdfminer
                except Exception as e:  # pragma: no cover
                    ok = False
                    err = str(e)
                    print(f"error: {err}")
                    
                t1 = time.perf_counter()
                rows.append(Row(str(pdf_path), idx + 1, t1 - t0, ok, err))
    except Exception as e:  # pragma: no cover
        rows.append(Row(str(pdf_path), -1, 0.0, False, f"open: {e}"))
    return rows


def parse_with_pypdfium2(pdf_path: Path) -> Iterable[Row]:
    try:
        import pypdfium2 as pdfium  # type: ignore
    except Exception as e:  # pragma: no cover
        return [Row(str(pdf_path), -1, 0.0, False, f"import pypdfium2: {e}")]

    rows: List[Row] = []
    try:
        doc = pdfium.PdfDocument(str(pdf_path))
    except Exception as e:  # pragma: no cover
        return [Row(str(pdf_path), -1, 0.0, False, f"open: {e}")]

    try:
        n = len(doc)
        for i in range(n):
            t0 = time.perf_counter()
            ok = True
            err = ""
            try:
                page = doc[i]
                text_page = page.get_textpage()

                # _ = textpage.get_text_range()  # extract all page text
                for l in range(text_page.count_rects()):
                    rect = text_page.get_rect(l)
                    text_piece = text_page.get_text_bounded(*rect)
                    # x0, y0, x1, y1 = rect
                    # print(f"{rect}: {text_piece}")

                text_page.close()
                page.close()
            except Exception as e:  # pragma: no cover
                ok = False
                err = str(e)
                print(f"error: {err}")
                
            t1 = time.perf_counter()
            rows.append(Row(str(pdf_path), i + 1, t1 - t0, ok, err))
    finally:
        try:
            doc.close()
        except Exception:
            pass

    return rows


def parse_with_pymupdf(pdf_path: Path) -> Iterable[Row]:
    try:
        import fitz  # PyMuPDF
    except Exception as e:  # pragma: no cover
        return [Row(str(pdf_path), -1, 0.0, False, f"import pymupdf: {e}")]

    rows: List[Row] = []
    try:
        doc = fitz.open(str(pdf_path))
    except Exception as e:  # pragma: no cover
        return [Row(str(pdf_path), -1, 0.0, False, f"open: {e}")]

    try:
        for i, page in enumerate(doc):
            t0 = time.perf_counter()
            ok = True
            err = ""
            try:
                _ = page.get_text("text")  # plain text extraction
            except Exception as e:  # pragma: no cover
                ok = False
                err = str(e)
            t1 = time.perf_counter()
            rows.append(Row(str(pdf_path), i + 1, t1 - t0, ok, err))
    finally:
        try:
            doc.close()
        except Exception:
            pass

    return rows


def parse_with_docling_threaded(
    num_threads: int = 4,
    max_concurrent_results: int = 64,
) -> Callable[[List[Path]], Tuple[List[Row], float]]:
    """Return a runner that loads *all* PDFs, then decodes pages in parallel.

    Unlike the other adapters this one consumes the full list of files at once
    so that the thread pool can work across documents.  It returns
    (rows, wall_time) so the caller can report the true parallel wall time.
    """

    def _runner(pdf_paths: List[Path]) -> Tuple[List[Row], float]:
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
                pass  # will surface as missing results below

        rows: List[Row] = []
        wall_start = time.perf_counter()

        while parser.has_tasks():
            t0 = time.perf_counter()
            task = parser.get_task()
            t1 = time.perf_counter()

            if task.success:
                page_decoder, timings_dict = task.get()
                detail: dict = {}
                for key, val in timings_dict.items():
                    detail[key] = val
                rows.append(
                    Row(
                        filename=task.doc_key,
                        page_number=task.page_number + 1,
                        elapsed_sec=t1 - t0,
                        success=True,
                        error="",
                        timings_detail=detail,
                    )
                )
            else:
                rows.append(
                    Row(
                        filename=task.doc_key,
                        page_number=task.page_number + 1,
                        elapsed_sec=t1 - t0,
                        success=False,
                        error=task.error(),
                    )
                )

        wall_end = time.perf_counter()
        return rows, wall_end - wall_start

    return _runner


NON_DOCLING_PARSERS: dict[str, Callable[[Path], Iterable[Row]]] = {
    "pdfplumber": parse_with_pdfplumber,
    "pypdfium2": parse_with_pypdfium2,
    "pypdfium": parse_with_pypdfium2,  # alias
    "pymupdf": parse_with_pymupdf,
}

ALL_PARSER_NAMES = sorted({"docling", "docling-threaded"} | set(NON_DOCLING_PARSERS.keys()))


# -------- Main program --------


def compute_stats(rows: List[Row]) -> dict:
    times = [r.elapsed_sec for r in rows if r.page_number > 0 and r.success]
    total_pages = sum(1 for r in rows if r.page_number > 0)
    ok_pages = len(times)
    failed_pages = total_pages - ok_pages
    total_time = sum(times)
    stats = {
        "files": len(set(r.filename for r in rows)),
        "pages_total": total_pages,
        "pages_ok": ok_pages,
        "pages_failed": failed_pages,
        "time_total_sec": total_time,
        "time_avg_sec": mean(times) if times else 0.0,
        "p50_sec": percentile(times, 50),
        "p90_sec": percentile(times, 90),
        "p95_sec": percentile(times, 95),
        "p99_sec": percentile(times, 99),
        "min_sec": min(times) if times else 0.0,
        "max_sec": max(times) if times else 0.0,
    }
    return stats


def print_stats(stats: dict, parser_name: str) -> None:
    print("")
    print(f"Summary for parser={parser_name}")
    print(f" - files:        {stats['files']}")
    print(f" - pages total:  {stats['pages_total']}")
    print(f" - pages ok:     {stats['pages_ok']}")
    print(f" - pages failed: {stats['pages_failed']}")
    print(f" - total sec:    {fmt_seconds(stats['time_total_sec'])}")
    print(f" - avg sec/page: {fmt_seconds(stats['time_avg_sec'])}")
    print(f" - p50: {fmt_seconds(stats['p50_sec'])}  p90: {fmt_seconds(stats['p90_sec'])}  p95: {fmt_seconds(stats['p95_sec'])}  p99: {fmt_seconds(stats['p99_sec'])}")
    print(f" - min: {fmt_seconds(stats['min_sec'])}  max: {fmt_seconds(stats['max_sec'])}")


def compute_per_document_stats(rows: List[Row]) -> List[dict]:
    # Collect per-file successful page times and total page counts
    times_by_file: dict[str, List[float]] = {}
    total_pages_by_file: dict[str, int] = {}

    for r in rows:
        if r.page_number > 0:
            total_pages_by_file[r.filename] = total_pages_by_file.get(r.filename, 0) + 1
        if r.page_number > 0 and r.success:
            times_by_file.setdefault(r.filename, []).append(r.elapsed_sec)

    per_doc: List[dict] = []
    for fname in sorted(set(times_by_file.keys()) | set(total_pages_by_file.keys())):
        times = times_by_file.get(fname, [])
        pages_total = total_pages_by_file.get(fname, 0)
        per_doc.append(
            {
                "document": fname,
                "pages": pages_total,
                "total": sum(times) if times else 0.0,
                "mean": mean(times) if times else 0.0,
                "median": median(times) if times else 0.0,
                "min": min(times) if times else 0.0,
                "max": max(times) if times else 0.0,
                "p90": percentile(times, 90),
                "p95": percentile(times, 95),
                "p99": percentile(times, 99),
            }
        )
    return per_doc


def print_per_document_table(rows: List[Row]) -> None:
    per_doc = compute_per_document_stats(rows)
    if not per_doc:
        print("\nNo per-document stats to display (no successful pages).")
        return

    headers = ["document", "pages", "total", "mean", "median", "min", "max", "p90", "p95", "p99"]
    table_rows = []
    for s in per_doc:
        table_rows.append(
            [
                s["document"],
                s["pages"],
                fmt_seconds(s["total"]),
                fmt_seconds(s["mean"]),
                fmt_seconds(s["median"]),
                fmt_seconds(s["min"]),
                fmt_seconds(s["max"]),
                fmt_seconds(s["p90"]),
                fmt_seconds(s["p95"]),
                fmt_seconds(s["p99"]),
            ]
        )

    print("\nPer-document statistics (sec/page):")
    print(tabulate(table_rows, headers=headers))


def write_per_document_csv(rows: List[Row], out_path: Path) -> Path:
    per_doc = compute_per_document_stats(rows)
    per_doc_path = out_path.with_name(out_path.stem + "_per_doc" + out_path.suffix)
    with per_doc_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["basename", "document", "pages", "total", "mean", "median", "min", "max", "p90", "p95", "p99"])
        for s in per_doc:
            w.writerow([
                Path(s["document"]).name,
                s["document"],
                s["pages"],
                fmt_seconds(s["total"]),
                fmt_seconds(s["mean"]),
                fmt_seconds(s["median"]),
                fmt_seconds(s["min"]),
                fmt_seconds(s["max"]),
                fmt_seconds(s["p90"]),
                fmt_seconds(s["p95"]),
                fmt_seconds(s["p99"]),
            ])
    return per_doc_path


def _get_timing_keys_from_rows(rows: List[Row]) -> List[str]:
    """Extract the sorted set of timing detail keys present across all rows."""
    keys: set = set()
    for r in rows:
        keys.update(r.timings_detail.keys())
    return sorted(keys)


def print_timing_breakdown(rows: List[Row], timing_keys: List[str]) -> None:
    """Print a table showing average absolute time and % for each static timing key."""
    ok_rows = [r for r in rows if r.page_number > 0 and r.success and r.timings_detail]
    if not ok_rows:
        return

    n = len(ok_rows)
    total_elapsed = sum(r.elapsed_sec for r in ok_rows)

    headers = ["timing_key", "total_sec", "avg_sec", "avg_%"]
    table_rows = []
    for key in timing_keys:
        key_total = sum(r.timings_detail.get(key, 0.0) for r in ok_rows)
        key_avg = key_total / n
        key_pct = (key_total / total_elapsed * 100.0) if total_elapsed > 0 else 0.0
        table_rows.append([key, fmt_seconds(key_total), fmt_seconds(key_avg), f"{key_pct:.2f}%"])

    print("\nTiming breakdown (static keys, across all successful pages):")
    print(tabulate(table_rows, headers=headers))


def default_output_path(parser_name: str) -> Path:
    ts = time.strftime("%Y%m%d-%H%M%S")
    return Path("perf") / "results" / f"perf_{parser_name}_{ts}.csv"


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Page-level PDF parsing perf harness")
    ap.add_argument("input", help="Path to a PDF file or directory of PDFs")
    ap.add_argument(
        "--parser",
        "-p",
        default="docling",
        choices=ALL_PARSER_NAMES,
        help="Parser backend to benchmark (docling, pdfplumber, pypdfium2, pypdfium, pymupdf)",
    )
    ap.add_argument(
        "--recursive",
        "-r",
        action="store_true",
        help="Recurse into subdirectories when input is a directory",
    )
    ap.add_argument(
        "--output",
        "-o",
        type=str,
        default=None,
        help="Output CSV path. Defaults to perf/results/perf_<parser>_<timestamp>.csv",
    )
    ap.add_argument(
        "--limit",
        "-l",
        type=int,
        default=None,
        help="Maximum number of documents to process",
    )
    ap.add_argument(
        "--bytesio",
        action="store_true",
        help="(docling only) Read PDFs into memory and pass as BytesIO instead of file path",
    )
    ap.add_argument(
        "--threads",
        "-t",
        type=int,
        default=4,
        help="(docling-threaded only) Number of worker threads (default: 4)",
    )
    ap.add_argument(
        "--max-concurrent-results",
        type=int,
        default=64,
        help="(docling-threaded only) Max buffered results before workers pause (default: 64)",
    )

    args = ap.parse_args(argv)

    parser_key = args.parser
    if parser_key == "docling":
        parser_fn = parse_with_docling(use_bytesio=args.bytesio)
    elif parser_key == "docling-threaded":
        if args.bytesio:
            ap.error("--bytesio is not supported with --parser docling-threaded")
        # handled separately below
        parser_fn = None
    else:
        if args.bytesio:
            ap.error("--bytesio is only supported with --parser docling")
        parser_fn = NON_DOCLING_PARSERS[parser_key]
    input_path = Path(args.input)
    pdfs = find_pdfs(input_path, recursive=args.recursive)

    if args.limit is not None:
        pdfs = pdfs[:args.limit]

    if not pdfs:
        print(f"No PDFs found at {input_path}", file=sys.stderr)
        return 2

    out_path = Path(args.output) if args.output else default_output_path(parser_key)
    ensure_parent_dir(out_path)

    rows: List[Row] = []
    started = time.perf_counter()

    if parser_key == "docling-threaded":
        threaded_fn = parse_with_docling_threaded(
            num_threads=args.threads,
            max_concurrent_results=args.max_concurrent_results,
        )
        print(f"Loading {len(pdfs)} PDFs and parsing with {args.threads} threads ...")
        rows, wall_time = threaded_fn(pdfs)
        ended = started + wall_time
    else:
        for pdf in tqdm(pdfs, desc=f"Parsing PDFs with {parser_key}"):
            # print(pdf)
            rows.extend(list(parser_fn(pdf)))
        ended = time.perf_counter()

    # Collect timing detail keys from the rows (docling only)
    timing_keys = _get_timing_keys_from_rows(rows)

    # Write CSV
    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        header = ["filename", "page_number", "elapsed_sec", "success", "error"]
        for key in timing_keys:
            header.append(key)
            header.append(f"{key}_%")
        w.writerow(header)
        for r in rows:
            row_data = [r.filename, r.page_number, f"{r.elapsed_sec:.9f}", int(r.success), r.error]
            for key in timing_keys:
                val = r.timings_detail.get(key, 0.0)
                pct = (val / r.elapsed_sec * 100.0) if r.elapsed_sec > 0 else 0.0
                row_data.append(f"{val:.9f}")
                row_data.append(f"{pct:.2f}")
            w.writerow(row_data)

    # Print summary
    stats = compute_stats(rows)
    print_stats(stats, parser_key)
    if timing_keys:
        print_timing_breakdown(rows, timing_keys)
    print_per_document_table(rows)
    per_doc_path = write_per_document_csv(rows, out_path)
    print(f"\nWrote: {out_path}")
    print(f"Wrote: {per_doc_path}")
    print(f"Total wall time: {fmt_seconds(ended - started)} sec")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
