#!/usr/bin/env python3
"""
Evaluate perf CSVs and generate visualizations.

Inputs:
- One or more CSV files or directories containing CSVs (recursively scanned).
- If no inputs are provided, scans "perf/results" for CSVs.

CSV format expected (as produced by perf/run_perf.py):
  filename,page_number,elapsed_sec,success,error

Output visualizations are written to perf/viz:
  0) Reports detected parser name for each CSV
  1) Per-parser page-time histograms + a superposed histogram across parsers
  2) Per-parser scatter: document page-count vs total time, with linear fit
  3) Pairwise hexbin plots: per-page times (x=parserA, y=parserB)

If only one CSV is provided/found, only steps 0, 1, and 2 are produced.

Usage examples:
  python perf/run_eval.py perf/results/*.csv
  python perf/run_eval.py perf/results
  python perf/run_eval.py  # defaults to scanning perf/results
"""

from __future__ import annotations

import argparse
import csv
import itertools
import math
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import matplotlib
matplotlib.use("Agg")  # non-interactive backend for headless environments
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LogNorm


# -------------- Data types --------------


@dataclass
class PageRow:
    filename: str
    page_number: int
    elapsed_sec: float
    success: bool


# -------------- Utilities --------------


KNOWN_PARSERS = [
    # Known keys from perf/run_perf.py
    "docling-mode=json",
    "docling-mode=typed",
    "pdfplumber",
    "pypdfium2",
    "pypdfium",
    "pymupdf",
    # Legacy/short names sometimes used in filenames
    "docling",
]


def find_csvs(inputs: List[str]) -> List[Path]:
    paths: List[Path] = []
    if not inputs:
        base = Path("perf") / "results"
        if base.is_dir():
            paths.extend(sorted(base.rglob("*.csv")))
        return paths

    for arg in inputs:
        p = Path(arg)
        if p.is_file() and p.suffix.lower() == ".csv":
            paths.append(p)
        elif p.is_dir():
            paths.extend(sorted(p.rglob("*.csv")))
    # Remove duplicates while preserving order
    seen = set()
    uniq: List[Path] = []
    for p in paths:
        if p not in seen:
            seen.add(p)
            uniq.append(p)
    return uniq


def detect_parser_name(csv_path: Path) -> str:
    # Try to parse filenames like: perf_<parser>_<YYYYmmdd-HHMMSS>.csv
    m = re.search(r"(^|/)perf_([^_/]+(?:=[^_/]+)?)_\d{8}-\d{6}\.csv$", str(csv_path))
    if m:
        return m.group(2)
    # Else, best-effort: longest known parser token contained in name
    name_lower = csv_path.name.lower()
    candidates = [k for k in KNOWN_PARSERS if k.lower() in name_lower]
    if candidates:
        return max(candidates, key=len)
    # Fallback: stem
    return csv_path.stem


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def read_csv(csv_path: Path) -> List[PageRow]:
    rows: List[PageRow] = []
    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            try:
                filename = r["filename"]
                page_number = int(r["page_number"]) if r["page_number"] else -1
                elapsed_sec = float(r["elapsed_sec"]) if r["elapsed_sec"] else math.nan
                success = str(r["success"]).strip() in {"1", "true", "True"}
            except Exception:
                # Skip malformed row
                continue
            rows.append(PageRow(filename, page_number, elapsed_sec, success))
    return rows


def per_document_aggregates(rows: List[PageRow]) -> List[Tuple[str, int, float]]:
    # returns list of (document_path, page_count_total, total_time_success_pages)
    page_counts: Dict[str, int] = defaultdict(int)
    time_sums: Dict[str, float] = defaultdict(float)
    for r in rows:
        if r.page_number > 0:
            page_counts[r.filename] += 1
        if r.page_number > 0 and r.success and not math.isnan(r.elapsed_sec):
            time_sums[r.filename] += r.elapsed_sec
    docs = sorted(set(page_counts.keys()) | set(time_sums.keys()))
    return [(d, page_counts.get(d, 0), time_sums.get(d, 0.0)) for d in docs]


def pairwise_common_page_times(
    rows_a: List[PageRow], rows_b: List[PageRow]
) -> Tuple[np.ndarray, np.ndarray]:
    # Build maps of (filename, page_number) -> elapsed
    map_a: Dict[Tuple[str, int], float] = {}
    map_b: Dict[Tuple[str, int], float] = {}
    for r in rows_a:
        if r.page_number > 0 and r.success and not math.isnan(r.elapsed_sec):
            map_a[(r.filename, r.page_number)] = r.elapsed_sec
    for r in rows_b:
        if r.page_number > 0 and r.success and not math.isnan(r.elapsed_sec):
            map_b[(r.filename, r.page_number)] = r.elapsed_sec
    common_keys = sorted(set(map_a.keys()) & set(map_b.keys()))
    x = np.array([map_a[k] for k in common_keys], dtype=float)
    y = np.array([map_b[k] for k in common_keys], dtype=float)
    return x, y


# -------------- Plotting --------------


def plot_histograms(per_parser_times: Dict[str, np.ndarray], viz_dir: Path) -> None:
    # Individual histograms in log-log scale
    for parser, times in per_parser_times.items():
        if times.size == 0:
            continue
        # Keep only strictly positive times for log scale
        tpos = times[times > 0]
        if tpos.size == 0:
            continue
        tmin, tmax = float(np.min(tpos)), float(np.max(tpos))
        if tmin <= 0 or not np.isfinite(tmin) or not np.isfinite(tmax) or tmin == tmax:
            # Fallback to skip degenerate
            continue
        bins = np.logspace(np.log10(tmin), np.log10(tmax), 50)

        plt.figure(figsize=(8, 5))
        plt.hist(tpos, bins=bins, color="#1f77b4", alpha=0.8, log=True)
        plt.xscale("log")
        plt.title(f"Page time histogram (log-log) — {parser} (n={tpos.size})")
        plt.xlabel("Seconds per page (log)")
        plt.ylabel("Count (log)")
        plt.grid(True, alpha=0.3, which="both")
        out = viz_dir / f"hist_{safe_name(parser)}.png"
        plt.tight_layout()
        plt.savefig(out, dpi=150)
        plt.close()

    # Superposed histogram across parsers in log-log scale
    if len(per_parser_times) >= 2:
        all_times = np.concatenate([t[t > 0] for t in per_parser_times.values() if t.size > 0])
        if all_times.size:
            tmin, tmax = float(np.min(all_times)), float(np.max(all_times))
            if tmin > 0 and np.isfinite(tmin) and np.isfinite(tmax) and tmin < tmax:
                bins = np.logspace(np.log10(tmin), np.log10(tmax), 60)
                plt.figure(figsize=(9, 5))
                for parser, times in per_parser_times.items():
                    tpos = times[times > 0]
                    if tpos.size == 0:
                        continue
                    plt.hist(
                        tpos,
                        bins=bins,
                        density=True,
                        alpha=0.45,
                        label=f"{parser} (n={tpos.size})",
                        log=True,
                    )
                plt.xscale("log")
                plt.title("Page time histograms (log-log) — overlay")
                plt.xlabel("Seconds per page (log)")
                plt.ylabel("Density (log)")
                plt.legend()
                plt.grid(True, alpha=0.3, which="both")
                out = viz_dir / "hist_superposed.png"
                plt.tight_layout()
                plt.savefig(out, dpi=150)
                plt.close()


def plot_scatter_per_doc(per_parser_docs: Dict[str, List[Tuple[str, int, float]]], viz_dir: Path) -> None:
    for parser, docs in per_parser_docs.items():
        if not docs:
            continue
        xs = np.array([d[1] for d in docs], dtype=float)  # pages
        ys = np.array([d[2] for d in docs], dtype=float)  # total time (sec)
        if xs.size == 0:
            continue
        plt.figure(figsize=(8, 5))
        plt.scatter(xs, ys, s=18, alpha=0.7, label="documents")

        # Linear fit if we have 2+ points and non-NaN values
        if xs.size >= 2 and np.isfinite(xs).all() and np.isfinite(ys).all():
            try:
                coeffs = np.polyfit(xs, ys, deg=1)
                slope, intercept = coeffs[0], coeffs[1]
                x_line = np.linspace(xs.min(), xs.max(), 100)
                y_line = slope * x_line + intercept
                # R^2 for fit quality
                y_pred = slope * xs + intercept
                ss_res = np.sum((ys - y_pred) ** 2)
                ss_tot = np.sum((ys - np.mean(ys)) ** 2)
                r2 = 1 - ss_res / ss_tot if ss_tot > 0 else np.nan
                plt.plot(x_line, y_line, color="orange", label=f"fit: y={slope:.4f}x+{intercept:.3f} (R²={r2:.3f})")
            except Exception:
                pass

        plt.title(f"Total time vs pages — {parser} (n={xs.size})")
        plt.xlabel("Pages per document")
        plt.ylabel("Total seconds per document")
        plt.grid(True, alpha=0.3)
        plt.legend()
        out = viz_dir / f"scatter_pages_vs_time_{safe_name(parser)}.png"
        plt.tight_layout()
        plt.savefig(out, dpi=150)
        plt.close()


def plot_hex_pairs(per_parser_rows: Dict[str, List[PageRow]], viz_dir: Path) -> None:
    parsers = list(per_parser_rows.keys())
    if len(parsers) < 2:
        return
    for a_idx in range(len(parsers)):
        for b_idx in range(a_idx + 1, len(parsers)):
            pa, pb = parsers[a_idx], parsers[b_idx]
            xa, yb = pairwise_common_page_times(per_parser_rows[pa], per_parser_rows[pb])
            if xa.size == 0:
                continue
            plt.figure(figsize=(6.5, 6))
            plt.hexbin(xa, yb, gridsize=50, norm=LogNorm(), cmap="viridis")
            plt.colorbar(label="count (log)")
            # Add x=y diagonal line
            lim_min = min(xa.min(), yb.min())
            lim_max = max(xa.max(), yb.max())
            plt.plot([lim_min, lim_max], [lim_min, lim_max], 'r-', linewidth=1.5, label="x=y")
            plt.legend(loc="upper left")
            plt.xlabel(f"Seconds/page — {pa}")
            plt.ylabel(f"Seconds/page — {pb}")
            plt.title(f"Per-page time hexbin — {pa} vs {pb} (n={xa.size})")
            plt.grid(True, alpha=0.2)
            out = viz_dir / f"hex_{safe_name(pa)}_vs_{safe_name(pb)}.png"
            plt.tight_layout()
            plt.savefig(out, dpi=150)
            plt.close()


def plot_hex_pairs_loglog(per_parser_rows: Dict[str, List[PageRow]], viz_dir: Path) -> None:
    parsers = list(per_parser_rows.keys())
    if len(parsers) < 2:
        return
    for a_idx in range(len(parsers)):
        for b_idx in range(a_idx + 1, len(parsers)):
            pa, pb = parsers[a_idx], parsers[b_idx]
            xa, yb = pairwise_common_page_times(per_parser_rows[pa], per_parser_rows[pb])
            if xa.size == 0:
                continue
            # Filter to positive values for log scale
            mask = (xa > 0) & (yb > 0)
            xa_pos, yb_pos = xa[mask], yb[mask]
            if xa_pos.size == 0:
                continue
            plt.figure(figsize=(6.5, 6))
            plt.hexbin(xa_pos, yb_pos, gridsize=50, norm=LogNorm(), cmap="viridis", xscale="log", yscale="log")
            plt.colorbar(label="count (log)")
            # Add x=y diagonal line
            lim_min = min(xa_pos.min(), yb_pos.min())
            lim_max = max(xa_pos.max(), yb_pos.max())
            plt.plot([lim_min, lim_max], [lim_min, lim_max], 'r-', linewidth=1.5, label="x=y")
            plt.legend(loc="upper left")
            plt.xlabel(f"Seconds/page (log) — {pa}")
            plt.ylabel(f"Seconds/page (log) — {pb}")
            plt.title(f"Per-page time hexbin (log-log) — {pa} vs {pb} (n={xa_pos.size})")
            plt.grid(True, alpha=0.2, which="both")
            out = viz_dir / f"hex_loglog_{safe_name(pa)}_vs_{safe_name(pb)}.png"
            plt.tight_layout()
            plt.savefig(out, dpi=150)
            plt.close()


def safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.=-]+", "-", s)


def plot_histograms_stacked(per_parser_times: Dict[str, np.ndarray], viz_dir: Path) -> None:
    # Build list of (parser, positive_times)
    items = []
    for parser, times in per_parser_times.items():
        tpos = times[times > 0]
        if tpos.size > 0:
            items.append((parser, tpos))
    if not items:
        return

    # Shared log-spaced bins across all parsers
    all_pos = np.concatenate([tp for _, tp in items])
    tmin, tmax = float(np.min(all_pos)), float(np.max(all_pos))
    if not (tmin > 0 and np.isfinite(tmin) and np.isfinite(tmax) and tmin < tmax):
        return
    bins = np.logspace(np.log10(tmin), np.log10(tmax), 50)

    n = len(items)
    fig, axes = plt.subplots(nrows=n, ncols=1, figsize=(9, max(2.5 * n, 4.0)), sharex=True)
    if n == 1:
        axes = [axes]  # normalize to list

    for ax, (parser, tpos) in zip(axes, items):
        ax.hist(tpos, bins=bins, color="#1f77b4", alpha=0.85, log=True)
        ax.set_yscale("log")
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3, which="both")
        ax.set_ylabel("Count (log)")
        ax.set_title(f"{parser} (n={tpos.size})", loc="left", fontsize=10)

    axes[-1].set_xlabel("Seconds per page (log)")
    fig.suptitle("Page time histograms — stacked (common x-axis, log-log)", y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    out = viz_dir / "hist_stacked.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)


# -------------- Main --------------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Generate perf visualizations from CSVs")
    ap.add_argument(
        "inputs",
        nargs="*",
        help="CSV files and/or directories to scan. If omitted, scans perf/results",
    )
    ap.add_argument(
        "--viz-dir",
        default=str(Path("perf") / "viz"),
        help="Output directory for generated visualizations",
    )
    args = ap.parse_args(argv)

    csv_paths = find_csvs(args.inputs)
    if not csv_paths:
        print("No CSV files found. Provide paths or ensure perf/results has CSVs.")
        return 2

    viz_dir = Path(args.viz_dir)
    ensure_dir(viz_dir)

    # Step 0: determine parser names and load data
    per_parser_rows: Dict[str, List[PageRow]] = {}
    for p in csv_paths:
        parser = detect_parser_name(p)
        rows = read_csv(p)
        per_parser_rows[parser] = rows
        print(f"Detected parser: {parser}  from: {p}")

    # Prepare data arrays
    per_parser_times: Dict[str, np.ndarray] = {}
    per_parser_docs: Dict[str, List[Tuple[str, int, float]]] = {}
    for parser, rows in per_parser_rows.items():
        page_times = np.array(
            [r.elapsed_sec for r in rows if r.page_number > 0 and r.success and not math.isnan(r.elapsed_sec)],
            dtype=float,
        )
        per_parser_times[parser] = page_times
        per_parser_docs[parser] = per_document_aggregates(rows)

    # Step 1: histograms (and superposed)
    plot_histograms(per_parser_times, viz_dir)
    # Additional: stacked histograms with shared x-axis
    plot_histograms_stacked(per_parser_times, viz_dir)

    # Step 2: scatter pages vs total time per document with linear fit
    plot_scatter_per_doc(per_parser_docs, viz_dir)

    # Step 3: hexbin for every pair of parsers (only if 2+ parsers)
    if len(per_parser_rows) >= 2:
        plot_hex_pairs(per_parser_rows, viz_dir)
        plot_hex_pairs_loglog(per_parser_rows, viz_dir)

    print(f"Wrote visualizations to: {viz_dir}")
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
