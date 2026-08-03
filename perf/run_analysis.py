#!/usr/bin/env python3
"""
Analyze slowest pages from a per-page CSV and extract detailed timings
from docling-parse to help identify bottlenecks.

Input is a per-page CSV written by `perf/run_scaling.py --pages-csv`.  Rows
carry `backend`, `task` and `threads`, so `--backend`/`--task`/`--threads`
narrow a mixed file down to the series worth drilling into.

What this script does:
  1) Reads a CSV and finds the top N slowest successful pages.
  2) Loads those documents with docling-parse via the typed pipeline.
  3) Retrieves detailed stage timings from the underlying parser.
  4) Outputs results based on mode:
     --top: CSV with static timings per pdf-page, plus an aggregate breakdown
     --nth: Table with all timings (static + dynamic) showing sum, avg, std, count

Usage examples:
  python perf/run_analysis.py perf/results/pages.csv --top 25 --loglevel fatal
  python perf/run_analysis.py perf/results/pages.csv --nth 7
  python perf/run_analysis.py pages.csv --top 25 --task parse --threads 1
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

from _common import PageRow, ensure_parent_dir, read_page_rows
from docling_core.types.doc.page import PdfPageBoundaryType
from tabulate import tabulate

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    Timings,
    get_decode_page_timing_keys,
    get_static_timing_keys,
    is_static_timing_key,
)

# -------------- Data types --------------


@dataclass
class PageTimings:
    filename: str
    page_number: int
    elapsed_original: float
    timings: Timings = field(default_factory=lambda: Timings())


# -------------- IO helpers --------------


def get_sorted_candidates(
    rows: List[PageRow],
    min_sec: float | None = None,
    *,
    backend: str | None = None,
    task: str | None = None,
    threads: int | None = None,
) -> List[PageRow]:
    """Successful pages sorted by elapsed time descending."""
    cands = [
        r
        for r in rows
        if r.success and r.page_number > 0 and math.isfinite(r.elapsed_s)
    ]
    if backend is not None:
        cands = [r for r in cands if r.backend == backend]
    if task is not None:
        cands = [r for r in cands if r.task == task]
    if threads is not None:
        cands = [r for r in cands if r.threads == threads]
    if min_sec is not None:
        cands = [r for r in cands if r.elapsed_s >= min_sec]
    cands.sort(key=lambda r: r.elapsed_s, reverse=True)
    return cands


def timestamped_out_path(prefix: str = "analysis") -> Path:
    ts = time.strftime("%Y%m%d-%H%M%S")
    return Path("perf") / "results" / f"{prefix}_{ts}.csv"


# -------------- Timing extraction --------------


def extract_timings_for_page(
    doc,
    page_number: int,
) -> Timings:
    """Run docling-parse on the given page and return Timings object."""
    try:
        _, timings = doc.get_page_with_timings(page_number)
        return timings
    except Exception:
        return Timings()


def analyze_pages(
    csv_path: Path,
    top_n: int | None,
    min_sec: float | None = None,
    *,
    nth: int | None = None,
    loglevel: str = "fatal",
    backend: str | None = None,
    task: str | None = None,
    threads: int | None = None,
) -> List[PageTimings]:
    rows = read_page_rows(csv_path)
    cands = get_sorted_candidates(
        rows, min_sec, backend=backend, task=task, threads=threads
    )

    selected: List[PageRow] = []
    # If nth is specified, analyze only that single page
    if nth is not None:
        if nth <= 0:
            raise ValueError(f"--nth must be >= 1 (got {nth})")
        if nth > len(cands):
            raise ValueError(
                f"--nth {nth} exceeds number of candidate pages {len(cands)}"
            )
        selected = [cands[nth - 1]]
    elif top_n and top_n > 0:
        selected = cands[:top_n]

    if not selected:
        return []

    # Group target pages by document for efficient load
    pages_by_file: Dict[str, List[PageRow]] = defaultdict(list)
    for r in selected:
        pages_by_file[r.doc_key].append(r)

    parser = DoclingPdfParser(loglevel=loglevel)
    results: List[PageTimings] = []

    for filename, pages in pages_by_file.items():
        # Sort pages descending by original elapsed (for determinism)
        pages.sort(key=lambda r: r.elapsed_s, reverse=True)
        try:
            doc = parser.load(
                filename, lazy=True, boundary_type=PdfPageBoundaryType.CROP_BOX
            )
        except Exception:
            # Unable to load document; record empty timings for its pages
            for r in pages:
                results.append(
                    PageTimings(
                        filename=r.doc_key,
                        page_number=r.page_number,
                        elapsed_original=r.elapsed_s,
                    )
                )
            continue

        for r in pages:
            timings = extract_timings_for_page(
                doc,
                r.page_number,
            )
            results.append(
                PageTimings(
                    filename=r.doc_key,
                    page_number=r.page_number,
                    elapsed_original=r.elapsed_s,
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
    ensure_parent_dir(out_path)

    # Get decode_page keys in order (excludes the global decode_page timer)
    decode_page_keys = get_decode_page_timing_keys()

    header = ["filename", "page_number", "elapsed_original_sec", *decode_page_keys]

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


def print_aggregate_breakdown(pages: List[PageTimings]) -> None:
    """Average cost and share of each static timing key across the selection.

    This is the whole-selection view; `--nth` gives the same breakdown for one
    page.
    """
    analysed = [p for p in pages if p.timings.data]
    if not analysed:
        return

    total_elapsed = sum(p.elapsed_original for p in analysed)
    rows = []
    for key in sorted(get_static_timing_keys()):
        key_total = sum(p.timings.get(key, 0.0) for p in analysed)
        if key_total <= 0.0:
            continue
        share = (key_total / total_elapsed * 100.0) if total_elapsed > 0 else 0.0
        rows.append(
            [
                key,
                f"{key_total:.6f}",
                f"{key_total / len(analysed):.6f}",
                f"{share:.2f}%",
            ]
        )

    if not rows:
        return
    rows.sort(key=lambda r: float(r[1]), reverse=True)
    print(f"\nTiming breakdown across {len(analysed)} analysed pages:")
    print(tabulate(rows, headers=["timing_key", "total_sec", "avg_sec", "avg_%"]))


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
        table_data.append(
            [key, key_type, f"{total:.6f}", f"{avg:.6f}", f"{std:.6f}", count]
        )

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
    ap.add_argument("csv", help="Per-page CSV (from perf/run_scaling.py --pages-csv)")
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
    ap.add_argument("--backend", default=None, help="Keep only this backend")
    ap.add_argument("--task", default=None, help="Keep only this task")
    ap.add_argument(
        "--threads", type=int, default=None, help="Keep only this thread count"
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
            min_sec=args.min_sec,
            nth=args.nth,
            loglevel=args.loglevel,
            backend=args.backend,
            task=args.task,
            threads=args.threads,
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
        out_path = (
            Path(args.out) if args.out else timestamped_out_path(prefix="analysis")
        )
        write_static_timings_csv(out_path, pages)
        print_top_summary(pages)
        print_aggregate_breakdown(pages)
        print(f"\nWrote static timings CSV: {out_path}")

    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
