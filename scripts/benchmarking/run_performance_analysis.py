#!/usr/bin/env python3
"""
Analyze slowest pages from a per-page CSV and extract detailed timings
from docling-parse to help identify bottlenecks.

Input is a per-page CSV written by `scripts/benchmarking/run_performance_benchmarking.py --pages-csv`.  Rows
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
  python scripts/benchmarking/run_performance_analysis.py scripts/benchmarking/results/pages.csv --top 25 --loglevel fatal
  python scripts/benchmarking/run_performance_analysis.py scripts/benchmarking/results/pages.csv --nth 7
  python scripts/benchmarking/run_performance_analysis.py pages.csv --top 25 --task parse --threads 1
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
    ContentConfig,
    ContentLevel,
    DecodeConfig,
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


DECODE_PAGE_CHILDREN = [
    "to_json_page",
    "extract_annots_json",
    "decode_dimensions",
    "decode_resources",
    "decode_contents",
    "decode_annots",
    "rotate_contents",
    "sanitize_orientation",
    "sanitize_cells",
    "sanitise_contents",
]

TIMING_CHILDREN = {
    "pipeline": [
        "decode_page",
        "create_word_cells",
        "create_line_cells",
    ],
    "decode_page": DECODE_PAGE_CHILDREN,
    "sanitize_cells": [
        "sanitize_cells.remove_duplicate_cells",
        "sanitize_cells.sanitize_text",
    ],
    "sanitise_contents": [
        "sanitise_contents.copy_cells",
        "sanitise_contents.sanitize_bbox",
    ],
    "create_line_cells": [
        "create_line_cells.copy_cells",
        "create_line_cells.sanitize_bbox",
        "create_line_cells.remove_duplicate_cells",
    ],
    "decode_contents": [
        "content_decode_total",
        "interprete_ops_total",
        "decode_xobjects_total",
        "decode_grphs_total",
        "decode_fonts_total",
        "parse_stream_total",
        "do_form_machinery_total",
        "do_image_total",
    ],
    "decode_fonts_total": [
        "font: init-copy",
        "font: init-metrics",
        "font: font-cmap",
        "font: font-cmap-stream-decode",
        "font: font-cmap-resources",
        "font: font-chars",
    ],
    "font: font-cmap": ["cmap-parse-total"],
    "cmap-parse-total": [
        "cmap-parse-endbfchar",
        "cmap-parse-endbfrange",
        "cmap-parse-endcodespacerange",
    ],
}

TOP_LEVEL_TIMING_KEYS = set(TIMING_CHILDREN["pipeline"])
NESTED_TIMING_KEYS = {
    child
    for parent, children in TIMING_CHILDREN.items()
    if parent != "pipeline"
    for child in children
}


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
    return Path("scripts") / "benchmarking" / "results" / f"{prefix}_{ts}.csv"


# -------------- Config helpers --------------


def _add_bool_value_arg(
    parser: argparse.ArgumentParser,
    name: str,
    *,
    default: bool,
    help: str,
) -> None:
    parser.add_argument(
        f"--{name}",
        choices=["true", "false"],
        default="true" if default else "false",
        help=f"{help} (default: {str(default).lower()})",
    )


def _arg_was_passed(argv: List[str], name: str) -> bool:
    option = f"--{name}"
    return any(arg == option or arg.startswith(f"{option}=") for arg in argv)


def _parse_bool_arg(value: str) -> bool:
    return value.lower() == "true"


def _decode_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "keep_char_cells": _parse_bool_arg(args.keep_char_cells),
        "keep_shapes": _parse_bool_arg(args.keep_shapes),
        "keep_bitmaps": _parse_bool_arg(args.keep_bitmaps),
        "create_word_cells": _parse_bool_arg(args.create_word_cells),
        "create_line_cells": _parse_bool_arg(args.create_line_cells),
    }


def _materialization_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "materialize_char_cells": _parse_bool_arg(args.materialize_char_cells),
        "materialize_word_cells": _parse_bool_arg(args.materialize_word_cells),
        "materialize_line_cells": _parse_bool_arg(args.materialize_line_cells),
        "materialize_shapes": _parse_bool_arg(args.materialize_shapes),
        "materialize_bitmaps": _parse_bool_arg(args.materialize_bitmaps),
        "materialize_bitmap_bytes": _parse_bool_arg(args.materialize_bitmap_bytes),
    }


def _content_config(
    decode_options: dict[str, bool], materialization_options: dict[str, bool]
) -> ContentConfig:
    def _level(keep: bool, materialize: bool) -> ContentLevel:
        if materialize:
            return ContentLevel.COMPUTE_AND_MATERIALIZE
        if keep:
            return ContentLevel.COMPUTE
        return ContentLevel.SKIP

    return ContentConfig(
        char_cells_content_level=_level(
            decode_options["keep_char_cells"],
            materialization_options["materialize_char_cells"],
        ),
        word_cells_content_level=_level(
            decode_options["create_word_cells"],
            materialization_options["materialize_word_cells"],
        ),
        line_cells_content_level=_level(
            decode_options["create_line_cells"],
            materialization_options["materialize_line_cells"],
        ),
        shapes_content_level=_level(
            decode_options["keep_shapes"],
            materialization_options["materialize_shapes"],
        ),
        bitmaps_content_level=_level(
            decode_options["keep_bitmaps"],
            materialization_options["materialize_bitmaps"],
        ),
        include_bitmap_bytes=materialization_options["materialize_bitmap_bytes"],
    )


def _content_level_name(value: object) -> object:
    if isinstance(value, ContentLevel):
        return value.name
    if isinstance(value, bool):
        return str(value).lower()
    return value


def print_effective_config(
    decode_config: DecodeConfig,
    content_config: ContentConfig,
) -> None:
    decode_rows = [
        [key, _content_level_name(value)]
        for key, value in sorted(decode_config.model_dump().items())
    ]
    content_rows = [
        [key, _content_level_name(value)]
        for key, value in content_config.model_dump().items()
    ]
    print("\nEffective decode config:")
    print(tabulate(decode_rows, headers=["field", "value"]))
    print("\nEffective content config:")
    print(tabulate(content_rows, headers=["field", "value"]))


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
    decode_config: DecodeConfig,
    content_config: ContentConfig,
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
                filename,
                lazy=True,
                boundary_type=PdfPageBoundaryType.CROP_BOX,
                decode_config=decode_config,
                content_config=content_config,
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


def ordered_static_timing_keys() -> List[str]:
    ordered: List[str] = []

    def add_children(parent: str) -> None:
        for key in TIMING_CHILDREN.get(parent, []):
            if key not in ordered:
                ordered.append(key)
            add_children(key)

    add_children("pipeline")
    for key in sorted(set(get_static_timing_keys()) - set(ordered)):
        ordered.append(key)
    return ordered


def write_static_timings_csv(out_path: Path, pages: List[PageTimings]) -> None:
    """Write CSV with static timing keys, one row per page."""
    ensure_parent_dir(out_path)

    timing_keys = ordered_static_timing_keys()

    header = ["filename", "page_number", "elapsed_original_sec", *timing_keys]

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for p in pages:
            row = [p.filename, p.page_number, f"{p.elapsed_original:.9f}"]
            for k in timing_keys:
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
    """Average cost and share of each timing bucket across the selection.

    Percentages are branch-local to avoid double-counting nested timers. The
    `% original` column keeps the link to the per-page cost from
    run_performance_benchmarking.py.
    """
    analysed = [p for p in pages if p.timings.data]
    if not analysed:
        return

    total_elapsed = sum(p.elapsed_original for p in analysed)
    totals = {
        key: sum(p.timings.get(key, 0.0) for p in analysed)
        for key in get_static_timing_keys()
    }

    root_total = sum(totals.get(key, 0.0) for key in TIMING_CHILDREN["pipeline"])
    rows = []
    seen: set[str] = set()

    def add_scope(parent: str) -> None:
        children = TIMING_CHILDREN.get(parent, [])
        sibling_total = sum(totals.get(key, 0.0) for key in children)
        for key in children:
            key_total = totals.get(key, 0.0)
            if key_total <= 0.0:
                continue
            seen.add(key)
            parent_pct = (
                key_total / sibling_total * 100.0 if sibling_total > 0.0 else 0.0
            )
            original_pct = (
                key_total / total_elapsed * 100.0 if total_elapsed > 0.0 else 0.0
            )
            pipeline_pct = key_total / root_total * 100.0 if root_total > 0.0 else 0.0
            rows.append(
                [
                    parent,
                    key,
                    f"{key_total:.6f}",
                    f"{key_total / len(analysed):.6f}",
                    f"{parent_pct:.2f}%",
                    f"{pipeline_pct:.2f}%",
                    f"{original_pct:.2f}%",
                ]
            )

    for scope in TIMING_CHILDREN:
        add_scope(scope)

    for key in sorted(set(get_static_timing_keys()) - seen - NESTED_TIMING_KEYS):
        key_total = totals.get(key, 0.0)
        if key_total <= 0.0:
            continue
        original_pct = (key_total / total_elapsed * 100.0) if total_elapsed > 0 else 0.0
        rows.append(
            [
                "(unmapped)",
                key,
                f"{key_total:.6f}",
                f"{key_total / len(analysed):.6f}",
                "",
                "",
                f"{original_pct:.2f}%",
            ]
        )

    if not rows:
        return
    print(f"\nTiming breakdown across {len(analysed)} analysed pages:")
    print(
        tabulate(
            rows,
            headers=[
                "scope",
                "timing_key",
                "total_sec",
                "avg_sec",
                "% parent",
                "% pipeline",
                "% original",
            ],
        )
    )
    print(
        "\nPercentages are not meant to sum down the whole table: nested rows are "
        "shown as a share of their parent branch."
    )


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
    print(
        "\nSum of reported static timings "
        f"(nested; not wall time): {sum(timings.get_static_timings().values()):.6f} sec"
    )
    print(
        "Sum of reported dynamic timings "
        f"(nested; not wall time): {sum(timings.get_dynamic_timings().values()):.6f} sec"
    )
    print(f"decode_page timer: {timings.get('decode_page', 0.0):.6f} sec")


# -------------- Main --------------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Analyze slowest pages and extract detailed parser timings"
    )
    ap.add_argument(
        "csv",
        help="Per-page CSV (from scripts/benchmarking/run_performance_benchmarking.py --pages-csv)",
    )
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
        help=(
            "Output CSV path for --top mode (defaults under "
            "scripts/benchmarking/results)"
        ),
    )
    ap.add_argument("--backend", default=None, help="Keep only this backend")
    ap.add_argument("--task", default=None, help="Keep only this task")
    ap.add_argument(
        "--threads", type=int, default=None, help="Keep only this thread count"
    )
    _add_bool_value_arg(
        ap,
        "keep-char-cells",
        default=True,
        help="Populate character cells and emit text render instructions",
    )
    _add_bool_value_arg(
        ap,
        "create-word-cells",
        default=False,
        help="Create word cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "create-line-cells",
        default=False,
        help="Create line cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "keep-shapes",
        default=False,
        help="Keep vector shape cells",
    )
    _add_bool_value_arg(
        ap,
        "keep-bitmaps",
        default=False,
        help="Keep bitmap resources/cells",
    )
    _add_bool_value_arg(
        ap,
        "materialize-char-cells",
        default=False,
        help="Materialize character cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-word-cells",
        default=False,
        help="Materialize word cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-line-cells",
        default=True,
        help="Materialize line cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-shapes",
        default=False,
        help="Materialize vector shapes into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmaps",
        default=True,
        help="Materialize bitmap locations into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmap-bytes",
        default=False,
        help="Materialize bitmap image bytes when bitmap locations are materialized",
    )

    args = ap.parse_args(argv)
    if args.task == "parse" and not _arg_was_passed(argv, "materialize-bitmaps"):
        args.materialize_bitmaps = "false"

    decode_config = DecodeConfig()
    content_config = _content_config(
        _decode_options_from_args(args),
        _materialization_options_from_args(args),
    )

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
            decode_config=decode_config,
            content_config=content_config,
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
        print_effective_config(decode_config, content_config)
        print_nth_table(pages[0])
    else:
        # --top mode: write CSV with static timings
        out_path = (
            Path(args.out) if args.out else timestamped_out_path(prefix="analysis")
        )
        write_static_timings_csv(out_path, pages)
        print_top_summary(pages)
        print_effective_config(decode_config, content_config)
        print_aggregate_breakdown(pages)
        print(f"\nWrote static timings CSV: {out_path}")

    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
