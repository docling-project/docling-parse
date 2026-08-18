#!/usr/bin/env python3
"""
Plot and cross-compare per-page timing CSVs.

Input is one or more per-page CSVs written by `scripts/benchmarking/run_performance_benchmarking.py --pages-csv`,
or directories to scan.  Each row carries its own `backend`, `task` and
`threads`, so a single file holding several backends is split into one series
per (backend, task, threads) --- there is no need for one file per backend, and
the series are never inferred from the filename.

Outputs go to `--viz-dir`, defaulting to the input CSV path with `.csv` dropped
so the plots sit beside the report they belong to:
  1) `hist_stacked.png` --- per-page time histograms, one panel per series on a
     shared log-log axis
  2) `hist_pages_per_document.png` --- corpus shape
  3) `scaling_<task>.png` --- docling-parse throughput against thread count
  4) `hex_loglog_*.png` --- per-page time of docling-parse at one thread
     against each other package, log-log, one plot per package and task
  5) a per-document statistics table and CSV

Usage examples:
  python scripts/benchmarking/run_performance_eval.py scripts/benchmarking/results/pages.csv
  python scripts/benchmarking/run_performance_eval.py scripts/benchmarking/results
  python scripts/benchmarking/run_performance_eval.py pages.csv --task parse --threads 1
  python scripts/benchmarking/run_performance_eval.py  # defaults to scanning scripts/benchmarking/results
"""

from __future__ import annotations

import argparse
import math
import os
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib

matplotlib.use("Agg")  # non-interactive backend for headless environments
import csv

import matplotlib.pyplot as plt
import numpy as np
from _common import (
    PageRow,
    ensure_parent_dir,
    fmt_seconds,
    group_by_series,
    percentile,
    read_page_rows,
    safe_name,
)
from matplotlib.colors import LogNorm
from tabulate import tabulate

# -------------- Input --------------


def find_csvs(inputs: List[str]) -> List[Path]:
    if not inputs:
        base = Path("scripts") / "benchmarking" / "results"
        return sorted(base.rglob("*.csv")) if base.is_dir() else []

    paths: List[Path] = []
    for arg in inputs:
        p = Path(arg)
        if p.is_file() and p.suffix.lower() == ".csv":
            paths.append(p)
        elif p.is_dir():
            paths.extend(sorted(p.rglob("*.csv")))

    seen = set()
    unique: List[Path] = []
    for p in paths:
        if p not in seen:
            seen.add(p)
            unique.append(p)
    return unique


def default_viz_dir(csv_paths: List[Path]) -> Path:
    """Where plots go when `--viz-dir` is not given.

    For a single CSV this is the CSV path with `.csv` dropped, so the plots sit
    beside the report they belong to and inherit its
    `<cpu>_<dataset>_<mode>` name.  For several inputs there is no single run
    to name after, so they land in a `viz/` directory next to them.
    """
    if len(csv_paths) == 1:
        return csv_paths[0].with_suffix("")
    parents = {p.parent for p in csv_paths}
    if len(parents) == 1:
        return next(iter(parents)) / "viz"
    common = Path(os.path.commonpath([str(p.parent) for p in csv_paths]))
    return common / "viz"


def load_rows(paths: List[Path]) -> List[PageRow]:
    rows: List[PageRow] = []
    for path in paths:
        try:
            loaded = read_page_rows(path)
        except ValueError as e:
            # Directory scans pick up unrelated CSVs; skip rather than abort.
            print(f"Skipping {path}: {e}")
            continue
        if not loaded:
            continue
        rows.extend(loaded)
        series = sorted({r.series for r in loaded})
        print(f"Read {len(loaded):>7} rows from {path}  ({', '.join(series)})")
    return rows


def filter_rows(
    rows: List[PageRow],
    *,
    backend: str | None,
    task: str | None,
    threads: int | None,
) -> List[PageRow]:
    def keep(row: PageRow) -> bool:
        if backend is not None and row.backend != backend:
            return False
        if task is not None and row.task != task:
            return False
        if threads is not None and row.threads != threads:
            return False
        return True

    return [row for row in rows if keep(row)]


# -------------- Aggregation --------------


def series_page_times(rows: List[PageRow]) -> np.ndarray:
    return np.array(
        [
            r.elapsed_s
            for r in rows
            if r.page_number > 0 and r.success and math.isfinite(r.elapsed_s)
        ],
        dtype=float,
    )


def pairwise_common_page_times(
    rows_a: List[PageRow], rows_b: List[PageRow]
) -> Tuple[np.ndarray, np.ndarray]:
    def as_map(rows: List[PageRow]) -> Dict[Tuple[str, int], float]:
        return {
            (r.doc_key, r.page_number): r.elapsed_s
            for r in rows
            if r.page_number > 0 and r.success and math.isfinite(r.elapsed_s)
        }

    map_a, map_b = as_map(rows_a), as_map(rows_b)
    common = sorted(set(map_a) & set(map_b))
    return (
        np.array([map_a[k] for k in common], dtype=float),
        np.array([map_b[k] for k in common], dtype=float),
    )


# -------------- Per-document table --------------


PER_DOC_FIELDS = ["pages", "total", "mean", "median", "min", "max", "p90", "p95", "p99"]


def compute_per_document_stats(rows: List[PageRow]) -> List[dict]:
    times_by_doc: Dict[str, List[float]] = defaultdict(list)
    pages_by_doc: Dict[str, int] = defaultdict(int)
    for r in rows:
        if r.page_number > 0:
            pages_by_doc[r.doc_key] += 1
        if r.page_number > 0 and r.success:
            times_by_doc[r.doc_key].append(r.elapsed_s)

    stats: List[dict] = []
    for doc in sorted(set(times_by_doc) | set(pages_by_doc)):
        times = times_by_doc.get(doc, [])
        stats.append(
            {
                "document": doc,
                "pages": pages_by_doc.get(doc, 0),
                "total": sum(times),
                "mean": sum(times) / len(times) if times else 0.0,
                "median": percentile(times, 50),
                "min": min(times) if times else 0.0,
                "max": max(times) if times else 0.0,
                "p90": percentile(times, 90),
                "p95": percentile(times, 95),
                "p99": percentile(times, 99),
            }
        )
    return stats


def print_per_document_table(series: str, rows: List[PageRow], top: int) -> None:
    stats = compute_per_document_stats(rows)
    if not stats:
        return
    stats.sort(key=lambda s: s["total"], reverse=True)
    shown = stats[:top]
    table = [
        [Path(s["document"]).name, s["pages"]]
        + [fmt_seconds(s[k]) for k in PER_DOC_FIELDS[1:]]
        for s in shown
    ]
    print(f"\nPer-document statistics (sec/page) — {series}, slowest {len(shown)}:")
    print(tabulate(table, headers=["document", *PER_DOC_FIELDS]))


def write_per_document_csv(
    path: Path, rows_by_series: Dict[str, List[PageRow]]
) -> None:
    ensure_parent_dir(path)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["series", "basename", "document", *PER_DOC_FIELDS])
        for series, rows in rows_by_series.items():
            for s in compute_per_document_stats(rows):
                writer.writerow(
                    [
                        series,
                        Path(s["document"]).name,
                        s["document"],
                        s["pages"],
                        *[fmt_seconds(s[k]) for k in PER_DOC_FIELDS[1:]],
                    ]
                )
    print(f"Wrote per-document statistics to {path}")


# -------------- Plotting --------------


def _log_bins(values: np.ndarray, count: int) -> np.ndarray | None:
    positive = values[values > 0]
    if positive.size == 0:
        return None
    low, high = float(np.min(positive)), float(np.max(positive))
    if not (low > 0 and np.isfinite(low) and np.isfinite(high)):
        return None
    if low == high:
        low, high = low * 0.5, high * 2.0
    return np.logspace(np.log10(low), np.log10(high), count)


def plot_pages_per_document(rows: List[PageRow], viz_dir: Path) -> None:
    """Corpus shape: how many pages the documents have."""
    pages_by_doc: Dict[str, int] = defaultdict(int)
    seen: set = set()
    for r in rows:
        key = (r.doc_key, r.page_number)
        if r.page_number > 0 and key not in seen:
            seen.add(key)
            pages_by_doc[r.doc_key] += 1
    counts = np.array(list(pages_by_doc.values()), dtype=float)
    if counts.size == 0:
        return

    plt.figure(figsize=(8, 5))
    edges = _log_bins(counts, 40)
    if edges is None:
        plt.hist(counts, bins=40, color="#2ca02c", alpha=0.85)
    else:
        plt.hist(counts, bins=edges, color="#2ca02c", alpha=0.85)
        plt.xscale("log")
    plt.title(
        f"Pages per document — {counts.size} documents, {int(counts.sum())} pages"
    )
    plt.xlabel("Pages per document")
    plt.ylabel("Number of documents")
    plt.grid(True, alpha=0.3, which="both")
    plt.tight_layout()
    plt.savefig(viz_dir / "hist_pages_per_document.png", dpi=150)
    plt.close()


def plot_histograms_stacked(
    per_series: Dict[str, np.ndarray], viz_dir: Path, bins: int
) -> None:
    """One panel per series on a shared x-axis.

    This subsumes the old `run_scaling_visualization.py`: pass a CSV whose
    series differ only by thread count and the panels are the thread sweep.
    """
    items = [(s, t[t > 0]) for s, t in per_series.items() if t[t > 0].size > 0]
    if not items:
        return
    edges = _log_bins(np.concatenate([t for _, t in items]), bins)
    if edges is None:
        return

    fig, axes = plt.subplots(
        nrows=len(items),
        ncols=1,
        figsize=(9, max(2.8 * len(items), 4.0)),
        sharex=True,
        squeeze=False,
    )
    for ax, (series, times) in zip(axes.flat, items):
        ax.hist(times, bins=edges, color="#1f77b4", alpha=0.85, log=True)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.grid(True, alpha=0.3, which="both")
        ax.set_ylabel("Count (log)")
        ax.set_title(f"{series} (n={times.size})", loc="left", fontsize=10)

    axes[-1, 0].set_xlabel("Seconds per page (log)")
    fig.suptitle("Page time histograms — stacked (common x-axis, log-log)", y=0.99)
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    fig.savefig(viz_dir / "hist_stacked.png", dpi=150)
    plt.close(fig)


REFERENCE_BACKEND = "docling-parse"


def _hex_pairs_to_plot(
    per_series_rows: Dict[str, List[PageRow]],
) -> List[Tuple[str, str]]:
    """docling-parse against every other package, per task.

    Pairs never cross tasks --- a `parse` time against a `parse+render` time is
    not a like-for-like page.  Prefer docling-parse at one thread when present;
    otherwise fall back to the lowest available docling-parse thread count for
    that task.
    """
    by_task: Dict[str, List[str]] = defaultdict(list)
    for series, rows in per_series_rows.items():
        if rows:
            by_task[rows[0].task].append(series)

    pairs: List[Tuple[str, str]] = []
    for task, names in sorted(by_task.items()):
        references = sorted(
            (s for s in names if per_series_rows[s][0].backend == REFERENCE_BACKEND),
            key=lambda s: (
                per_series_rows[s][0].threads != 1,
                per_series_rows[s][0].threads,
                s,
            ),
        )
        reference = references[0] if references else None
        if reference is None:
            print(f"  no {REFERENCE_BACKEND} series for task {task}; no hexbins")
            continue
        if per_series_rows[reference][0].threads != 1:
            print(
                f"  no {REFERENCE_BACKEND} (1t) series for task {task}; "
                f"using {reference} for hexbins"
            )
        pairs.extend(
            (reference, other)
            for other in names
            if per_series_rows[other][0].backend != REFERENCE_BACKEND
        )
    return pairs


def plot_thread_scaling(
    per_series_rows: Dict[str, List[PageRow]], viz_dir: Path
) -> None:
    """Throughput of docling-parse against its thread count, one plot per task.

    Total time is reconstructed as the sum of `wall_gap_s`, which tiles the
    interval from the end of loading to the last result and so is the wall
    clock of the processing phase.  Summing `elapsed_s` would be wrong here:
    that is per-page cost, which by design stays flat as threads increase.
    """
    by_task: Dict[str, Dict[int, List[PageRow]]] = defaultdict(dict)
    for rows in per_series_rows.values():
        if rows and rows[0].backend == REFERENCE_BACKEND:
            by_task[rows[0].task][rows[0].threads] = rows

    for task, by_threads in sorted(by_task.items()):
        if len(by_threads) < 2:
            continue

        threads: List[int] = []
        sec_per_page: List[float] = []
        pages_per_sec: List[float] = []
        for count in sorted(by_threads):
            ok = [r for r in by_threads[count] if r.success and r.page_number > 0]
            total = sum(r.wall_gap_s for r in ok)
            if not ok or total <= 0:
                continue
            threads.append(count)
            sec_per_page.append(total / len(ok))
            pages_per_sec.append(len(ok) / total)
        if len(threads) < 2:
            continue

        fig, ax = plt.subplots(figsize=(8, 5))
        (line_time,) = ax.plot(
            threads,
            sec_per_page,
            "-o",
            color="black",
            label="total time / total pages",
        )
        ax.set_yscale("log")
        ax.set_xlabel("docling-parse threads")
        ax.set_ylabel("Seconds per page (log)", color="black")
        ax.tick_params(axis="y", labelcolor="black")
        ax.set_xticks(threads)
        ax.set_xticklabels([str(t) for t in threads])
        ax.grid(True, alpha=0.3, which="both")

        ax_rate = ax.twinx()
        (line_rate,) = ax_rate.plot(
            threads,
            pages_per_sec,
            "s-",
            color="red",
            label="total pages / total time",
        )
        ax_rate.set_yscale("log")
        ax_rate.set_ylabel("Pages per second (log)", color="red")
        ax_rate.tick_params(axis="y", labelcolor="red")

        ax.set_title(f"Thread scaling — docling-parse, {task}")
        ax.legend(handles=[line_time, line_rate], loc="center right")
        fig.tight_layout()
        fig.savefig(viz_dir / f"scaling_{safe_name(task)}.png", dpi=150)
        plt.close(fig)


def plot_hex_pairs(per_series_rows: Dict[str, List[PageRow]], viz_dir: Path) -> None:
    """Log-log hexbin of per-page times, docling-parse (1t) vs each package."""
    for pa, pb in _hex_pairs_to_plot(per_series_rows):
        xa, yb = pairwise_common_page_times(per_series_rows[pa], per_series_rows[pb])
        mask = (xa > 0) & (yb > 0)
        xa, yb = xa[mask], yb[mask]
        if xa.size == 0:
            continue

        plt.figure(figsize=(6.5, 6))
        plt.hexbin(
            xa,
            yb,
            gridsize=50,
            norm=LogNorm(),
            cmap="viridis",
            xscale="log",
            yscale="log",
        )
        plt.colorbar(label="count (log)")
        low, high = min(xa.min(), yb.min()), max(xa.max(), yb.max())
        plt.plot([low, high], [low, high], "r-", linewidth=1.5, label="x=y")
        plt.legend(loc="upper left")
        plt.xlabel(f"Seconds/page (log) — {pa}")
        plt.ylabel(f"Seconds/page (log) — {pb}")
        plt.title(f"{pa} vs {pb} (n={xa.size})")
        plt.grid(True, alpha=0.2, which="both")
        plt.tight_layout()
        plt.savefig(
            viz_dir / f"hex_loglog_{safe_name(pa)}_vs_{safe_name(pb)}.png", dpi=150
        )
        plt.close()


# -------------- Main --------------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Plot per-page timing CSVs from scripts/benchmarking/run_performance_benchmarking.py"
    )
    ap.add_argument(
        "inputs",
        nargs="*",
        help=(
            "CSV files and/or directories to scan. If omitted, scans "
            "scripts/benchmarking/results"
        ),
    )
    ap.add_argument(
        "--viz-dir",
        type=Path,
        default=None,
        help=(
            "Output directory for generated visualizations. Defaults to the "
            "input CSV path with `.csv` dropped, so plots sit beside the report "
            "they belong to; several inputs land in a `viz/` directory next to "
            "them"
        ),
    )
    ap.add_argument("--backend", default=None, help="Keep only this backend")
    ap.add_argument("--task", default=None, help="Keep only this task")
    ap.add_argument(
        "--threads", type=int, default=None, help="Keep only this thread count"
    )
    ap.add_argument("--bins", type=int, default=50, help="Histogram bins (default: 50)")
    ap.add_argument(
        "--top-documents",
        type=int,
        default=20,
        help="Slowest documents to print per series (default: 20)",
    )
    args = ap.parse_args(argv)

    csv_paths = find_csvs(args.inputs)
    if not csv_paths:
        print(
            "No CSV files found. Provide paths or ensure "
            "scripts/benchmarking/results has CSVs."
        )
        return 2

    rows = load_rows(csv_paths)
    rows = filter_rows(rows, backend=args.backend, task=args.task, threads=args.threads)
    if not rows:
        print("No page rows matched the requested filters.")
        return 2

    viz_dir = args.viz_dir or default_viz_dir(csv_paths)
    viz_dir.mkdir(parents=True, exist_ok=True)
    print(f"\nWriting visualizations to: {viz_dir}")

    per_series_rows = group_by_series(rows)
    per_series_times = {s: series_page_times(r) for s, r in per_series_rows.items()}

    print(f"\nSeries found: {len(per_series_rows)}")
    for series, times in per_series_times.items():
        print(f"  {series}: {times.size} timed pages")

    plot_histograms_stacked(per_series_times, viz_dir, args.bins)
    plot_pages_per_document(rows, viz_dir)
    plot_thread_scaling(per_series_rows, viz_dir)
    plot_hex_pairs(per_series_rows, viz_dir)

    for series, series_rows in per_series_rows.items():
        print_per_document_table(series, series_rows, args.top_documents)
    write_per_document_csv(viz_dir / "per_document.csv", per_series_rows)

    print(f"\nWrote visualizations to: {viz_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
