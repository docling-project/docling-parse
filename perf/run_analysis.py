#!/usr/bin/env python3
"""
Performance analysis for parser benchmark CSVs produced by perf/run_perf.py.

Inputs
- One or more CSVs via dynamic flags like --csv-docling path/to.csv, --csv-pypdfium path/to.csv, etc.
  Each CSV must have headers: ["filename", "page_number", "elapsed_sec", "success", "error"].

Outputs (saved under --outdir, default: perf/out)
- histograms.pdf: One page per parser with a histogram of elapsed_sec for successful rows.
- scatters.pdf: Pairwise scatter plots between parsers using only rows where both succeeded,
  including a least-squares linear fit (y = m x + b); slope m indicates relative speed.
- success_matrix.pdf: 2 x N heatmap of success/fail counts per parser.

Notes
- Scatter plots are based on joining by (filename, page_number) for successful rows in both parsers.
- Linear fit is computed without numpy using simple statistics to avoid hard deps.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from collections import defaultdict
from dataclasses import dataclass
from statistics import mean, median
from typing import Dict, Iterable, List, Tuple

try:
    import matplotlib
    matplotlib.use("Agg")  # headless
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
except Exception as e:  # pragma: no cover
    print("matplotlib is required to run this script: {}".format(e), file=sys.stderr)
    sys.exit(2)


CSV_REQUIRED_HEADERS = ["filename", "page_number", "elapsed_sec", "success", "error"]


@dataclass
class Record:
    filename: str
    page_number: int
    elapsed_sec: float
    success: bool
    error: str

    @property
    def key(self) -> Tuple[str, int]:
        return (self.filename, self.page_number)


def parse_bool(s: str) -> bool:
    if isinstance(s, bool):
        return s
    if s is None:
        return False
    sl = str(s).strip().lower()
    return sl in {"1", "true", "t", "yes", "y"}


def load_csv(path: str) -> List[Record]:
    records: List[Record] = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        headers = [h.strip() for h in reader.fieldnames or []]
        missing = [h for h in CSV_REQUIRED_HEADERS if h not in headers]
        if missing:
            raise ValueError(
                f"CSV at {path} missing required headers: {missing}. Found: {headers}"
            )
        for row in reader:
            try:
                filename = row["filename"].strip()
                page_number = int(row["page_number"]) if row["page_number"] != "" else 0
                elapsed_sec = float(row["elapsed_sec"]) if row["elapsed_sec"] not in ("", None) else math.nan
                success = parse_bool(row["success"]) if "success" in row else False
                error = (row.get("error") or "").strip()
                records.append(Record(filename, page_number, elapsed_sec, success, error))
            except Exception:
                # Skip malformed rows instead of failing entire run
                continue
    return records


def sanitize_label(name: str) -> str:
    return "".join(ch if (ch.isalnum() or ch in ("-", "_")) else "_" for ch in name).strip("_")


def linear_fit(x: List[float], y: List[float]) -> Tuple[float, float, float]:
    """Return slope m, intercept b, and R^2 for y ~ m*x + b.
    Uses simple stats to avoid numpy dependency.
    """
    n = len(x)
    if n == 0:
        return float("nan"), float("nan"), float("nan")
    mx = mean(x)
    my = mean(y)
    # Compute covariance and variance
    cov = 0.0
    varx = 0.0
    vary = 0.0
    for xi, yi in zip(x, y):
        dx = xi - mx
        dy = yi - my
        cov += dx * dy
        varx += dx * dx
        vary += dy * dy
    if varx == 0.0:
        m = float("nan")
        b = my
        r2 = 0.0
    else:
        m = cov / varx
        b = my - m * mx
        # Pearson r^2
        r2 = (cov * cov) / (varx * vary) if (varx > 0 and vary > 0) else 0.0
    return m, b, r2


def ensure_outdir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def build_histograms(pdata: Dict[str, List[Record]], outdir: str) -> str:
    outpath = os.path.join(outdir, "histograms.pdf")
    with PdfPages(outpath) as pdf:
        for parser, records in pdata.items():
            vals = [r.elapsed_sec for r in records if r.success and math.isfinite(r.elapsed_sec)]
            if not vals:
                # Create an empty page noting no data
                fig, ax = plt.subplots(figsize=(7, 5))
                ax.axis("off")
                ax.text(0.5, 0.5, f"{parser}: no successful rows", ha="center", va="center")
                pdf.savefig(fig)
                plt.close(fig)
                continue
            vmin, vmax = min(vals), max(vals)
            vmean = mean(vals)
            vmedian = median(vals)

            fig, ax = plt.subplots(figsize=(7, 5))
            ax.hist(vals, bins="auto", color="#4477aa", edgecolor="white")
            ax.set_xlabel("elapsed_sec (s)")
            ax.set_ylabel("count")
            ax.grid(True, alpha=0.3)
            title = (
                f"{parser} elapsed_sec (success only)\n"
                f"min={vmin:.3f}s  max={vmax:.3f}s  mean={vmean:.3f}s  median={vmedian:.3f}s  n={len(vals)}"
            )
            ax.set_title(title)
            fig.tight_layout()
            pdf.savefig(fig)
            plt.close(fig)
    return outpath


def build_pairwise_scatters(pdata: Dict[str, List[Record]], outdir: str) -> str:
    outpath = os.path.join(outdir, "scatters.pdf")
    # Build index by key for each parser for quick join
    per_parser_by_key: Dict[str, Dict[Tuple[str, int], Record]] = {}
    for parser, records in pdata.items():
        per_parser_by_key[parser] = {r.key: r for r in records}

    parsers = list(pdata.keys())
    if len(parsers) < 2:
        # Nothing to compare; still create a small pdf
        with PdfPages(outpath) as pdf:
            fig, ax = plt.subplots(figsize=(7, 5))
            ax.axis("off")
            ax.text(0.5, 0.5, "Need at least two parsers for scatter plots", ha="center", va="center")
            pdf.savefig(fig)
            plt.close(fig)
        return outpath

    with PdfPages(outpath) as pdf:
        for i in range(len(parsers)):
            for j in range(i + 1, len(parsers)):
                p1 = parsers[i]
                p2 = parsers[j]
                idx1 = per_parser_by_key[p1]
                idx2 = per_parser_by_key[p2]
                xs: List[float] = []
                ys: List[float] = []
                for key, r1 in idx1.items():
                    r2 = idx2.get(key)
                    if not r2:
                        continue
                    if r1.success and r2.success and math.isfinite(r1.elapsed_sec) and math.isfinite(r2.elapsed_sec):
                        xs.append(r1.elapsed_sec)
                        ys.append(r2.elapsed_sec)

                fig, ax = plt.subplots(figsize=(6.5, 6.0))
                if xs:
                    ax.scatter(xs, ys, s=10, alpha=0.6, color="#66aa55", edgecolor="none")
                    # Linear fit y ~ m x + b
                    m, b, r2 = linear_fit(xs, ys)
                    if math.isfinite(m) and math.isfinite(b):
                        # Plot fit line across the x-range
                        xmin, xmax = min(xs), max(xs)
                        xr = xmax - xmin if xmax > xmin else 1.0
                        x0 = xmin - 0.02 * xr
                        x1 = xmax + 0.02 * xr
                        ax.plot([x0, x1], [m * x0 + b, m * x1 + b], color="#cc3344", lw=2, label=f"fit: y={m:.3f}x+{b:.3f}")
                        ax.legend(loc="upper left")
                    ax.plot([0, max(xs + ys)], [0, max(xs + ys)], color="#999999", lw=1, ls="--", alpha=0.6)
                    info = f"n={len(xs)}  slope={m:.3f}  R^2={r2:.3f}"
                else:
                    info = "no overlapping successful rows"
                ax.set_title(f"{p1} (x) vs {p2} (y)\n{info}")
                ax.set_xlabel(f"{p1} elapsed_sec (s)")
                ax.set_ylabel(f"{p2} elapsed_sec (s)")
                ax.grid(True, alpha=0.3)
                fig.tight_layout()
                pdf.savefig(fig)
                plt.close(fig)
    return outpath


def build_success_matrix(pdata: Dict[str, List[Record]], outdir: str) -> str:
    outpath = os.path.join(outdir, "success_matrix.pdf")
    parsers = list(pdata.keys())
    # Compute counts per parser
    counts = []  # list of (success_count, fail_count)
    for parser in parsers:
        s = sum(1 for r in pdata[parser] if r.success)
        f = sum(1 for r in pdata[parser] if not r.success)
        counts.append((s, f))

    # Prepare a 2 x N matrix: rows = [success, fail], cols = parsers
    mat = [
        [c[0] for c in counts],  # success row
        [c[1] for c in counts],  # fail row
    ]

    with PdfPages(outpath) as pdf:
        fig, ax = plt.subplots(figsize=(max(6.0, 1.6 * len(parsers)), 3.8))
        im = ax.imshow(mat, cmap="YlGnBu")
        ax.set_xticks(range(len(parsers)))
        ax.set_xticklabels(parsers, rotation=45, ha="right")
        ax.set_yticks([0, 1])
        ax.set_yticklabels(["success", "fail"])
        ax.set_title("Success vs Fail counts per parser")
        # Annotate cells with counts
        for i in range(2):
            for j in range(len(parsers)):
                ax.text(j, i, f"{mat[i][j]}", ha="center", va="center", color="black")
        fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        fig.tight_layout()
        pdf.savefig(fig)
        plt.close(fig)
    return outpath


def parse_args(argv: List[str]) -> Tuple[argparse.Namespace, Dict[str, str]]:
    parser = argparse.ArgumentParser(description="Analyze perf CSVs and generate PDF plots")
    parser.add_argument("--outdir", default=os.path.join("perf", "out"), help="Output directory for PDFs")
    parser.add_argument("--title", default=None, help="Optional title suffix for plots")
    # We will parse dynamic flags of the form --csv-<name> <path> from unknowns
    args, unknown = parser.parse_known_args(argv)

    csv_map: Dict[str, str] = {}

    # Support patterns: --csv-foo path, --csv-foo=path
    i = 0
    while i < len(unknown):
        token = unknown[i]
        if token.startswith("--csv-"):
            if "=" in token:
                flag, path = token.split("=", 1)
                pname = sanitize_label(flag[len("--csv-"):]) or "parser"
                csv_map[pname] = path
                i += 1
            else:
                pname = sanitize_label(token[len("--csv-"):]) or "parser"
                if i + 1 >= len(unknown):
                    parser.error(f"Flag {token} requires a path argument")
                csv_map[pname] = unknown[i + 1]
                i += 2
        else:
            # Skip unrelated unknowns
            i += 1

    if not csv_map:
        parser.print_help(sys.stderr)
        print("\nProvide at least one CSV via --csv-<parser> PATH", file=sys.stderr)
        sys.exit(2)

    return args, csv_map


def main(argv: List[str]) -> int:
    args, csv_map = parse_args(argv)
    ensure_outdir(args.outdir)

    # Load all CSVs
    per_parser_records: Dict[str, List[Record]] = {}
    for name, path in csv_map.items():
        if not os.path.exists(path):
            print(f"CSV not found for parser '{name}': {path}", file=sys.stderr)
            return 2
        try:
            recs = load_csv(path)
        except Exception as e:
            print(f"Failed to load CSV '{path}' for parser '{name}': {e}", file=sys.stderr)
            return 2
        per_parser_records[name] = recs

    # Generate plots
    hist_pdf = build_histograms(per_parser_records, args.outdir)
    scat_pdf = build_pairwise_scatters(per_parser_records, args.outdir)
    succ_pdf = build_success_matrix(per_parser_records, args.outdir)

    print("Saved:")
    print(f"- histograms: {hist_pdf}")
    print(f"- scatters:   {scat_pdf}")
    print(f"- matrix:     {succ_pdf}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

