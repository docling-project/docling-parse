#!/usr/bin/env python3
"""
Visualize per-page total timings from perf/run_scaling.py CSV output.

This script reads the timing CSV written by `perf/run_scaling.py
--enable-timing` and creates one histogram per thread count, using the
top-level `timing_total_s` column.

Usage:
    python perf/run_scaling_visualization.py timing-2026-05-28-53-19.csv
    python perf/run_scaling_visualization.py timing.csv --mode render --bins 80
"""

from __future__ import annotations

import argparse
import csv
import math
import time
from collections import defaultdict
from pathlib import Path
from typing import Dict, List

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def timestamped_out_dir() -> Path:
    ts = time.strftime("%Y%m%d-%H%M%S")
    return Path("perf") / "results" / f"scaling_viz_{ts}"


def read_timings(path: Path, mode: str) -> Dict[int, List[float]]:
    per_threads: Dict[int, List[float]] = defaultdict(list)
    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if mode != "both" and row.get("mode") != mode:
                continue

            success = str(row.get("success", "")).strip().lower() in {"1", "true"}
            if not success:
                continue

            try:
                threads = int(row["threads"])
                total_s = float(row["timing_total_s"])
            except Exception:
                continue

            if math.isfinite(total_s) and total_s >= 0.0:
                per_threads[threads].append(total_s)

    return dict(sorted(per_threads.items()))


def plot_histograms(
    per_threads: Dict[int, List[float]], out_dir: Path, bins: int, mode: str
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    filtered: List[tuple[int, List[float]]] = []
    for threads, values in per_threads.items():
        positive_values = [value for value in values if value > 0.0]
        if not positive_values:
            continue
        filtered.append((threads, positive_values))

    if not filtered:
        return

    global_min = min(min(values) for _, values in filtered)
    global_max = max(max(values) for _, values in filtered)
    if global_min == global_max:
        global_min *= 0.5
        global_max *= 2.0

    bin_edges = np.logspace(np.log10(global_min), np.log10(global_max), bins + 1)

    fig, axes = plt.subplots(
        nrows=len(filtered),
        ncols=1,
        figsize=(9, 3.2 * len(filtered)),
        sharex=True,
        squeeze=False,
    )

    for ax, (threads, values) in zip(axes.flat, filtered):
        ax.hist(values, bins=bin_edges, color="#1f77b4", alpha=0.85)
        ax.set_xscale("log")
        ax.set_ylabel("count")
        ax.set_title(f"threads={threads} (n={len(values)})")

    axes[-1, 0].set_xlabel("total time / page (s)")
    fig.suptitle(f"Per-page total timing histograms — mode={mode}", y=0.995)
    fig.tight_layout()
    fig.savefig(out_dir / "hist_stacked.png", dpi=160)
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Visualize per-page total timings from perf/run_scaling.py CSV output"
    )
    ap.add_argument("timing_csv", type=Path, help="Timing CSV from perf/run_scaling.py")
    ap.add_argument("--mode", choices=["parse", "render", "both"], default="both")
    ap.add_argument("--bins", type=int, default=50)
    ap.add_argument("--out-dir", type=Path, default=timestamped_out_dir())
    args = ap.parse_args()

    per_threads = read_timings(args.timing_csv, args.mode)
    if not per_threads:
        raise SystemExit("No timing rows matched the requested filters")

    plot_histograms(per_threads, args.out_dir, args.bins, args.mode)
    print(f"Wrote histograms to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
