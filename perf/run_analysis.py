#!/usr/bin/env python3
"""
Analyze slowest pages from a perf CSV and extract detailed timings
from docling-parse to help identify bottlenecks.

Input CSV format (as produced by perf/run_perf.py):
  filename,page_number,elapsed_sec,success,error

What this script does:
  1) Reads a CSV and finds the top N slowest successful pages.
  2) Loads those documents with docling-parse (typed or json pipeline selection).
  3) Retrieves detailed stage timings from the underlying parser (via JSON call).
  4) Writes a consolidated CSV of per-page stage timings and prints a short summary.

Notes:
  - Detailed timings are extracted from the JSON decoder entry point, which reports
    C++ stage timings common across the core decoding pipeline. This is useful for
    both 'typed' and 'json' analyses to understand low-level bottlenecks.

Usage examples:
  python perf/run_analysis.py perf/results/perf_docling_*.csv --top 25 --mode typed --loglevel fatal
  python perf/run_analysis.py perf/results/perf_docling_20250915-151237.csv --mode json --nth 7
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

from docling_core.types.doc.page import PdfPageBoundaryType
from docling_parse.pdf_parser import CONVERSION_MODE, DoclingPdfParser


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
    stages: Dict[str, float]


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


def top_slowest_pages(rows: List[PerfRow], top_n: int, min_sec: float | None = None) -> List[PerfRow]:
    cands = [r for r in rows if r.success and r.page_number > 0 and math.isfinite(r.elapsed_sec)]
    if min_sec is not None:
        cands = [r for r in cands if r.elapsed_sec >= min_sec]
    cands.sort(key=lambda r: r.elapsed_sec, reverse=True)
    return cands[:top_n]


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def timestamped_out_path(prefix: str = "analysis") -> Path:
    ts = time.strftime("%Y%m%d-%H%M%S")
    return Path("perf") / "results" / f"{prefix}_{ts}.csv"


# -------------- Timing extraction --------------

# Keys to skip from timing aggregation (e.g., per-page wrapper times)
SKIP_KEY_RE = re.compile(r"^\s*(?:decoding page|decode[_\s]?page)\s+\d+\s*$", re.IGNORECASE)

def should_skip_timing_key(key: str) -> bool:
    return bool(SKIP_KEY_RE.match(key.strip()))


def extract_timings_for_page(
    doc, page_number: int,
    *,
    mode: str = "typed",
    keep_chars: bool = True,
    keep_lines: bool = True,
    keep_bitmaps: bool = True,
    create_words: bool = True,
    create_textlines: bool = True,
) -> Dict[str, float]:
    """Run docling-parse on the given page and return a mapping of stage timings.

    Uses the public get_page_with_timings(...) API for either typed/json mode
    and returns the reported stage timings.
    """

    try:
        conv_mode = CONVERSION_MODE.JSON if mode.lower() == "json" else CONVERSION_MODE.TYPED
        _, timings = doc.get_page_with_timings(
            page_number,
            mode=conv_mode,
            keep_chars=keep_chars,
            keep_lines=keep_lines,
            keep_bitmaps=keep_bitmaps,
            create_words=create_words,
            create_textlines=create_textlines,
        )
    except Exception:
        return {}

    # Convert to plain dict and filter unwanted keys
    result: Dict[str, float] = {}
    try:
        for k, v in timings.items():
            key = str(k).strip()
            if should_skip_timing_key(key):
                continue
            try:
                result[key] = float(v)
            except Exception:
                continue
    except Exception:
        return {}

    return result


def analyze_pages(
    csv_path: Path,
    top_n: int,
    mode: str,
    min_sec: float | None = None,
    *,
    nth: int | None = None,
    loglevel: str = "fatal",
) -> List[PageTimings]:
    rows = read_perf_csv(csv_path)
    cands = [r for r in rows if r.success and r.page_number > 0 and math.isfinite(r.elapsed_sec)]
    if min_sec is not None:
        cands = [r for r in cands if r.elapsed_sec >= min_sec]
    cands.sort(key=lambda r: r.elapsed_sec, reverse=True)

    selected: List[PerfRow] = []
    # If nth is specified, analyze only that single page and ignore --top
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
            doc = parser.load(filename, lazy=True, boundary_type=PdfPageBoundaryType.CROP_BOX)
        except Exception as e:
            # Unable to load document; record empty timings for its pages
            for r in pages:
                results.append(PageTimings(filename=r.filename, page_number=r.page_number, elapsed_original=r.elapsed_sec, stages={}))
            continue

        for r in pages:
            stage_times = extract_timings_for_page(
                doc,
                r.page_number,
                mode=mode,
                keep_chars=True,
                keep_lines=True,
                keep_bitmaps=True,
                create_words=True,
                create_textlines=True,
            )
            results.append(PageTimings(filename=r.filename, page_number=r.page_number, elapsed_original=r.elapsed_sec, stages=stage_times))

        # Best-effort unload
        try:
            doc.unload()
        except Exception:
            pass

    return results


def write_results_csv(out_path: Path, pages: List[PageTimings]) -> None:
    ensure_parent(out_path)
    # Union of all stage keys
    all_keys: List[str] = []
    seen = set()
    for p in pages:
        for k in p.stages.keys():
            if should_skip_timing_key(k):
                continue
            if k not in seen:
                seen.add(k)
                all_keys.append(k)
    header = ["filename", "page_number", "elapsed_original_sec"] + all_keys
    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for p in pages:
            row = [p.filename, p.page_number, f"{p.elapsed_original:.9f}"]
            for k in all_keys:
                v = p.stages.get(k, "")
                row.append(f"{v:.9f}" if isinstance(v, (int, float)) else "")
            w.writerow(row)


def print_summary(pages: List[PageTimings], top_k: int = 10) -> None:
    if not pages:
        print("No pages analyzed.")
        return

    # Aggregate average contribution per stage across analyzed pages
    agg: Dict[str, List[float]] = defaultdict(list)
    for p in pages:
        for k, v in p.stages.items():
            if should_skip_timing_key(k):
                continue
            if isinstance(v, (int, float)) and math.isfinite(v):
                agg[k].append(float(v))

    means: List[Tuple[str, float]] = []
    for k, vals in agg.items():
        if vals:
            means.append((k, sum(vals) / len(vals)))
    means.sort(key=lambda x: x[1], reverse=True)

    print("\nTop stage averages across analyzed pages:")
    for name, val in means[:top_k]:
        print(f" - {name}: {val:.6f} sec (avg over {len(agg[name])} pages)")


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Analyze slowest pages and extract detailed parser timings")
    ap.add_argument("csv", help="Perf CSV file path (from perf/run_perf.py)")
    ap.add_argument("--top", type=int, default=20, help="Number of slowest pages to analyze")
    ap.add_argument("--nth", type=int, default=None, help="Additionally include the Nth slowest page (1-based)")
    ap.add_argument("--min-sec", type=float, default=None, help="Optional minimum elapsed_sec threshold")
    ap.add_argument("--mode", choices=["typed", "json"], default="typed", help="Pipeline to trigger before fetching timings")
    ap.add_argument(
        "--loglevel",
        choices=["fatal", "error", "warning", "info"],
        default="fatal",
        help="Docling parser log level",
    )
    ap.add_argument("--out", type=str, default=None, help="Output CSV path for stage timings (defaults under perf/results)")

    args = ap.parse_args(argv)

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

    out_path = Path(args.out) if args.out else timestamped_out_path(prefix="analysis")
    write_results_csv(out_path, pages)
    print_summary(pages)
    print(f"\nWrote detailed timings: {out_path}")
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
