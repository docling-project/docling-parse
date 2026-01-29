#!/usr/bin/env python3
"""
Analyze slowest pages from a perf CSV and extract detailed timings
from docling-parse to help identify bottlenecks.

Input CSV format (as produced by perf/run_perf.py):
  filename,page_number,elapsed_sec,success,error

What this script does:
  1) Reads a CSV and finds the top N slowest successful pages.
  2) Loads those documents with docling-parse (typed or json pipeline selection).
  3) Retrieves detailed stage timings from the underlying parser.
  4) Outputs results based on mode:
     --top: CSV with static timings per pdf-page
     --nth: Table with all timings (static + dynamic) showing sum, avg, std, count

Usage examples:
  python perf/run_analysis.py perf/results/perf_docling_*.csv --top 25 --mode typed --loglevel fatal
  python perf/run_analysis.py perf/results/perf_docling_20250915-151237.csv --mode json --nth 7
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

from docling_core.types.doc.page import PdfPageBoundaryType
from tabulate import tabulate

from docling_parse.pdf_parser import (
    CONVERSION_MODE,
    DoclingPdfParser,
    Timings,
    get_decode_page_timing_keys,
    get_static_timing_keys,
    is_static_timing_key,
)


# -------------- Data types --------------


@dataclass
class PerfRow:
    filename: str
    page_number: int
    elapsed_sec: float
    success: bool


@dataclass
class PageTimings:
    filename: str
    page_number: int
    elapsed_original: float
    timings: Timings = field(default_factory=lambda: Timings())


# -------------- IO helpers --------------


def read_perf_csv(path: Path) -> List[PerfRow]:
    rows: List[PerfRow] = []
    with path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            try:
                filename = r.get("filename", "").strip()
                page_number = int(r.get("page_number", "0") or 0)
                elapsed_sec = float(r.get("elapsed_sec", "nan") or "nan")
                success_str = str(r.get("success", "")).strip()
                success = success_str in {"1", "true", "True"}
            except Exception:
                continue
            rows.append(PerfRow(filename, page_number, elapsed_sec, success))
    return rows


def get_sorted_candidates(
    rows: List[PerfRow], min_sec: float | None = None
) -> List[PerfRow]:
    """Get successful pages sorted by elapsed time descending."""
    cands = [
        r
        for r in rows
        if r.success and r.page_number > 0 and math.isfinite(r.elapsed_sec)
    ]
    if min_sec is not None:
        cands = [r for r in cands if r.elapsed_sec >= min_sec]
    cands.sort(key=lambda r: r.elapsed_sec, reverse=True)
    return cands


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def timestamped_out_path(prefix: str = "analysis") -> Path:
    ts = time.strftime("%Y%m%d-%H%M%S")
    return Path("perf") / "results" / f"{prefix}_{ts}.csv"


# -------------- Timing extraction --------------


def extract_timings_for_page(
    doc,
    page_number: int,
    *,
    mode: str = "typed",
    keep_chars: bool = True,
    keep_lines: bool = True,
    keep_bitmaps: bool = True,
    create_words: bool = True,
    create_textlines: bool = True,
) -> Timings:
    """Run docling-parse on the given page and return Timings object."""
    try:
        conv_mode = (
            CONVERSION_MODE.JSON if mode.lower() == "json" else CONVERSION_MODE.TYPED
        )
        _, timings = doc.get_page_with_timings(
            page_number,
            mode=conv_mode,
            keep_chars=keep_chars,
            keep_lines=keep_lines,
            keep_bitmaps=keep_bitmaps,
            create_words=create_words,
            create_textlines=create_textlines,
        )
        return timings
    except Exception:
        return Timings()


def analyze_pages(
    csv_path: Path,
    top_n: int | None,
    mode: str,
    min_sec: float | None = None,
    *,
    nth: int | None = None,
    loglevel: str = "fatal",
) -> List[PageTimings]:
    rows = read_perf_csv(csv_path)
    cands = get_sorted_candidates(rows, min_sec)

    selected: List[PerfRow] = []
    # If nth is specified, analyze only that single page
    if nth is not None:
        if nth <= 0:
            raise ValueError(f"--nth must be >= 1 (got {nth})")
        if nth > len(cands):
            raise ValueError(f"--nth {nth} exceeds number of candidate pages {len(cands)}")
        selected = [cands[nth - 1]]
    elif top_n and top_n > 0:
        selected = cands[:top_n]

    if not selected:
        return []

    # Group target pages by filename for efficient load
    pages_by_file: Dict[str, List[PerfRow]] = defaultdict(list)
    for r in selected:
        pages_by_file[r.filename].append(r)

    parser = DoclingPdfParser(loglevel=loglevel)
    results: List[PageTimings] = []

    for filename, pages in pages_by_file.items():
        # Sort pages descending by original elapsed (for determinism)
        pages.sort(key=lambda r: r.elapsed_sec, reverse=True)
        try:
            doc = parser.load(
                filename, lazy=True, boundary_type=PdfPageBoundaryType.CROP_BOX
            )
        except Exception:
            # Unable to load document; record empty timings for its pages
            for r in pages:
                results.append(
                    PageTimings(
                        filename=r.filename,
                        page_number=r.page_number,
                        elapsed_original=r.elapsed_sec,
                    )
                )
            continue

        for r in pages:
            timings = extract_timings_for_page(
                doc,
                r.page_number,
                mode=mode,
                keep_chars=True,
                keep_lines=True,
                keep_bitmaps=True,
                create_words=True,
                create_textlines=True,
            )
            results.append(
                PageTimings(
                    filename=r.filename,
                    page_number=r.page_number,
                    elapsed_original=r.elapsed_sec,
                    timings=timings,
                )
            )

        # Best-effort unload
        try:
            doc.unload()
        except Exception:
            pass

    return results


# -------------- Output: --top mode (CSV with static timings) --------------


def write_static_timings_csv(out_path: Path, pages: List[PageTimings]) -> None:
    """Write CSV with decode_page timing keys only, one row per page."""
    ensure_parent(out_path)

    # Get decode_page keys in order (excludes the global decode_page timer)
    decode_page_keys = get_decode_page_timing_keys()

    header = ["filename", "page_number", "elapsed_original_sec"] + decode_page_keys

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for p in pages:
            row = [p.filename, p.page_number, f"{p.elapsed_original:.9f}"]
            for k in decode_page_keys:
                v = p.timings.get(k, 0.0)
                row.append(f"{v:.9f}" if v else "")
            w.writerow(row)


def print_top_summary(pages: List[PageTimings]) -> None:
    """Print summary for --top mode."""
    if not pages:
        print("No pages analyzed.")
        return

    print(f"\nAnalyzed {len(pages)} pages.")
    print(f"decode_page timing keys: {get_decode_page_timing_keys()}")


# -------------- Output: --nth mode (table with all timings) --------------


def print_nth_table(page: PageTimings) -> None:
    """Print detailed table for a single page with all timings."""
    print(f"\n{'=' * 80}")
    print(f"File: {page.filename}")
    print(f"Page: {page.page_number}")
    print(f"Original elapsed: {page.elapsed_original:.6f} sec")
    print(f"{'=' * 80}\n")

    timings = page.timings

    if not timings.data:
        print("No timing data available.")
        return

    # Collect all timing data with statistics
    table_data = []

    # Get all keys, separating static and dynamic
    all_keys = list(timings.data.keys())
    static_keys = [k for k in all_keys if is_static_timing_key(k)]
    dynamic_keys = [k for k in all_keys if not is_static_timing_key(k)]

    # Sort each group
    static_keys.sort()
    dynamic_keys.sort()

    def add_timing_row(key: str, is_static: bool):
        """Add a row for the given timing key."""
        values = timings.get_all(key)
        if not values:
            values = [timings.get(key, 0.0)]

        total = sum(values)
        count = len(values)
        avg = total / count if count > 0 else 0.0
        std = statistics.stdev(values) if count > 1 else 0.0

        key_type = "static" if is_static else "dynamic"
        table_data.append([key, key_type, f"{total:.6f}", f"{avg:.6f}", f"{std:.6f}", count])

    # Add static timings first
    for key in static_keys:
        add_timing_row(key, is_static=True)

    # Add separator row if we have both static and dynamic
    if static_keys and dynamic_keys:
        table_data.append(["---", "---", "---", "---", "---", "---"])

    # Add dynamic timings
    for key in dynamic_keys:
        add_timing_row(key, is_static=False)

    # Print table
    headers = ["Timing Key", "Type", "Total (sec)", "Average (sec)", "Std Dev", "Count"]
    print(tabulate(table_data, headers=headers, tablefmt="grid"))

    # Print totals
    print(f"\nTotal static time: {sum(timings.get_static_timings().values()):.6f} sec")
    print(f"Total dynamic time: {sum(timings.get_dynamic_timings().values()):.6f} sec")
    print(f"Total all timings: {timings.total():.6f} sec")


# -------------- Main --------------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Analyze slowest pages and extract detailed parser timings"
    )
    ap.add_argument("csv", help="Perf CSV file path (from perf/run_perf.py)")
    ap.add_argument(
        "--top",
        type=int,
        default=None,
        help="Analyze top N slowest pages, output CSV with static timings",
    )
    ap.add_argument(
        "--nth",
        type=int,
        default=None,
        help="Analyze the Nth slowest page (1-based), show detailed table",
    )
    ap.add_argument(
        "--min-sec",
        type=float,
        default=None,
        help="Optional minimum elapsed_sec threshold",
    )
    ap.add_argument(
        "--mode",
        choices=["typed", "json"],
        default="typed",
        help="Pipeline to trigger before fetching timings",
    )
    ap.add_argument(
        "--loglevel",
        choices=["fatal", "error", "warning", "info"],
        default="fatal",
        help="Docling parser log level",
    )
    ap.add_argument(
        "--out",
        type=str,
        default=None,
        help="Output CSV path for --top mode (defaults under perf/results)",
    )

    args = ap.parse_args(argv)

    # Validate arguments
    if args.top is None and args.nth is None:
        print("Error: Must specify either --top or --nth")
        return 2

    if args.top is not None and args.nth is not None:
        print("Error: Cannot specify both --top and --nth")
        return 2

    csv_path = Path(args.csv)
    if not csv_path.exists():
        print(f"CSV not found: {csv_path}")
        return 2

    try:
        pages = analyze_pages(
            csv_path,
            top_n=args.top,
            mode=args.mode,
            min_sec=args.min_sec,
            nth=args.nth,
            loglevel=args.loglevel,
        )
    except ValueError as e:
        print(f"Error: {e}")
        return 2

    if not pages:
        print("No pages met the criteria or failed to parse timings.")
        return 1

    # Output based on mode
    if args.nth is not None:
        # --nth mode: print detailed table
        print_nth_table(pages[0])
    else:
        # --top mode: write CSV with static timings
        out_path = Path(args.out) if args.out else timestamped_out_path(prefix="analysis")
        write_static_timings_csv(out_path, pages)
        print_top_summary(pages)
        print(f"\nWrote static timings CSV: {out_path}")

    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
