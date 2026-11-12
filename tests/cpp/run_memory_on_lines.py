#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the native memory probe; never compile it or import Python PDF bindings.

Start small:
  python3 tests/cpp/run_memory_on_lines.py --output /tmp/memory-smoke \
    --sizes 1000 --repeats 1

Use a NEW output directory for each iteration. Counts and PNG comparisons are
diagnostics for these generated grid fixtures, not general PDF equivalence tests.
Use --baseline RESULT_DIRECTORY to compare memory, process wall time and images.
The default 5% slowdown threshold reflects the user's accepted runtime budget.
Appearance fixtures are opt-in with --layouts appearance,widget,appearance-repeat.
Procedural Type3 fixtures are opt-in with --layouts type3,type3-repeat.
Soft-mask fixtures are opt-in with --layouts soft-mask,soft-mask-repeat.
Pattern fixtures are opt-in with --layouts pattern,pattern-repeat,pattern-lattice.
Clip-history fixtures are opt-in with --layouts clip-chain,clip-scoped (max 4000).
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import platform
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

from tabulate import tabulate

ROOT = Path(__file__).resolve().parents[2]
MODES = ("qpdf-discard", "collect", "decode", "skip-shapes", "render")
DEFAULT_LAYOUTS = ("direct", "form", "form-repeat", "form-nested")
APPEARANCE_LAYOUTS = ("appearance", "widget", "appearance-repeat")
TYPE3_LAYOUTS = ("type3", "type3-repeat")
SOFT_MASK_LAYOUTS = ("soft-mask", "soft-mask-repeat")
PATTERN_LAYOUTS = ("pattern", "pattern-repeat", "pattern-lattice")
CLIP_LAYOUTS = ("clip-chain", "clip-scoped")
LAYOUTS = (
    DEFAULT_LAYOUTS
    + APPEARANCE_LAYOUTS
    + TYPE3_LAYOUTS
    + SOFT_MASK_LAYOUTS
    + PATTERN_LAYOUTS
    + CLIP_LAYOUTS
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def selection(value: str, choices: tuple[str, ...]) -> list[str]:
    selected = value.split(",")
    if len(set(selected)) != len(selected) or not all(x in choices for x in selected):
        raise argparse.ArgumentTypeError(
            "choose distinct values from " + ",".join(choices)
        )
    return selected


def command(argv: list[str], log: Path, timeout: int) -> tuple[list[dict], float]:
    started = time.monotonic()
    try:
        result = subprocess.run(
            argv, capture_output=True, text=True, timeout=timeout, check=False
        )
    except subprocess.TimeoutExpired as error:
        stdout = (
            error.stdout.decode(errors="replace")
            if isinstance(error.stdout, bytes)
            else error.stdout or ""
        )
        stderr = (
            error.stderr.decode(errors="replace")
            if isinstance(error.stderr, bytes)
            else error.stderr or ""
        )
        log.write_text(
            "COMMAND: "
            + json.dumps(argv)
            + "\nTIMEOUT\nSTDOUT:\n"
            + stdout
            + "\nSTDERR:\n"
            + stderr
        )
        raise RuntimeError(f"child timed out after {timeout}s; see {log}") from error
    elapsed = time.monotonic() - started
    log.write_text(
        "COMMAND: "
        + json.dumps(argv)
        + f"\nEXIT: {result.returncode}\nSTDOUT:\n"
        + result.stdout
        + "\nSTDERR:\n"
        + result.stderr
    )
    require(result.returncode == 0, f"child exited {result.returncode}; see {log}")
    try:
        records = [
            json.loads(line) for line in result.stdout.splitlines() if line.strip()
        ]
    except json.JSONDecodeError as error:
        raise RuntimeError(f"unexpected non-JSON probe output; see {log}") from error
    require(bool(records), f"probe emitted no JSON; see {log}")
    return records, elapsed


def validate_clip_snapshots(decoded: dict, info: dict, mode: str, layout: str) -> None:
    if layout not in CLIP_LAYOUTS:
        return
    count = info["unique_lines"] if mode != "skip-shapes" else 0
    total = count * (count + 1) // 2 if layout == "clip-chain" else count
    require(decoded["instruction_clip_paths"] == total, "wrong clip snapshot count")
    require(
        decoded["max_clip_paths"]
        == (count if layout == "clip-chain" else min(count, 1)),
        "wrong clip-history high-water mark",
    )
    require(decoded["instruction_clip_points"] == 5 * total, "wrong clip point count")
    if "clip_history_storage" in decoded:
        storage = decoded["clip_history_storage"]
        require(storage in ("shared", "copied"), "unknown clip history representation")
        unique = count if storage == "shared" else total
        require(
            decoded["instruction_clip_unique_paths"] == unique,
            "wrong unique clip path count",
        )
        require(
            decoded["instruction_clip_unique_points"] == 5 * unique,
            "wrong unique clip points",
        )
        require(
            decoded["instruction_clip_history_nodes"] == count,
            "wrong retained clip node count",
        )


def validate(stages: list[dict], info: dict, mode: str, layout: str) -> dict:
    by_stage = {stage["stage"]: stage for stage in stages}
    require("baseline" in by_stage and "loaded" in by_stage, "missing memory snapshots")
    if mode in ("qpdf-discard", "collect"):
        counts = by_stage["tokenized"]
        unique = info["unique_lines"]
        type3 = layout in TYPE3_LAYOUTS
        clipping = layout in CLIP_LAYOUTS
        expected_tokens = (
            (16 if layout == "clip-scoped" else 14) * unique
            if clipping
            else 7 * unique + (5 if type3 else 2)
        )
        expected_operators = (
            (8 if layout == "clip-scoped" else 6) * unique
            if clipping
            else 3 * unique + (2 if type3 else 1)
        )
        require(
            counts["tokens"] == expected_tokens,
            "unexpected source token count",
        )
        require(
            counts["operators"] == expected_operators,
            "unexpected source operator count",
        )
        require(
            counts["decompressed_bytes"] == info["content_bytes"], "wrong decoded size"
        )
        require(counts["eof_reached"], "QPDF did not report EOF")
        require(counts["content_size_calls"] == 1, "expected one selected stream")
        if layout in PATTERN_LAYOUTS:
            expected_stream = "pattern_cell"
        elif layout in SOFT_MASK_LAYOUTS:
            expected_stream = "soft_mask_group"
        elif layout in TYPE3_LAYOUTS:
            expected_stream = "type3_charproc"
        elif layout in APPEARANCE_LAYOUTS:
            expected_stream = "appearance"
        else:
            expected_stream = "page" if layout == "direct" or clipping else "form_leaf"
        require(counts["measured_stream"] == expected_stream, "wrong stream selected")
        require(
            counts["stream_executions"] == 1, "raw parsing must visit leaf only once"
        )
        if mode == "qpdf-discard":
            require(counts["retained_tokens"] == 0, "discard retained token objects")
            require(
                counts["token_inline_bytes"] == 0, "discard allocated a token vector"
            )
        else:
            require(
                counts["retained_tokens"] == counts["tokens"], "collector lost tokens"
            )
        return counts

    decoded = by_stage["decoded"]
    soft_mask = layout in SOFT_MASK_LAYOUTS
    pattern = layout in PATTERN_LAYOUTS
    clipping = layout in CLIP_LAYOUTS
    paints = info.get("soft_mask_paints", 0)
    fills = info.get("pattern_fills", 0)
    lattice_cells = info.get("pattern_lattice_cells_per_fill", 0)
    # A mask's grid lives in its private instruction group. Page output is
    # one filled rectangle per paint, with the decoded mask shared by all.
    rectangles = soft_mask or (pattern and not lattice_cells)
    expected = paints if soft_mask else fills if rectangles else info["lines"]
    if lattice_cells:
        expected *= lattice_cells
    if mode == "skip-shapes":
        expected = 0
    expected_page_shapes = 0 if layout in TYPE3_LAYOUTS else expected
    if lattice_cells and mode != "skip-shapes":
        # The unresolved outline is retained as a page shape but does not
        # become a render instruction; the lattice already supplied the ink.
        expected_page_shapes += fills
    require(
        decoded["page_shapes"] == expected_page_shapes, "unexpected page-shape count"
    )
    for field in ("shape_instructions", "subpaths"):
        require(
            decoded[field] == expected,
            f"{field}: expected {expected}, got {decoded[field]}",
        )
    require(
        decoded["segments"] == (3 if rectangles else 1) * expected,
        "unexpected path segment count",
    )
    require(
        decoded["points"] == (4 if rectangles else 2) * expected,
        "unexpected path point count",
    )
    require(decoded["stored_dash_bytes"] == 0, "solid grid retained dash data")
    validate_clip_snapshots(decoded, info, mode, layout)
    if "page_content_delivery" in decoded:
        if clipping:
            expected_objects = (16 if layout == "clip-scoped" else 14) * info[
                "unique_lines"
            ]
            expected_bytes, expected_pending = info["page_content_bytes"], 4
        elif pattern:
            expected_objects = 4 + 6 * fills
            expected_bytes, expected_pending = info["page_content_bytes"], 4
        elif soft_mask:
            expected_objects = 8 * paints
            expected_bytes, expected_pending = info["page_content_bytes"], 4
        elif layout in TYPE3_LAYOUTS:
            expected_objects = 5 + 9 * info["type3_repeats"]
            expected_bytes, expected_pending = info["page_content_bytes"], 6
        elif layout in APPEARANCE_LAYOUTS:
            expected_objects, expected_bytes, expected_pending = 0, 0, 0
        elif layout == "direct":
            expected_objects = 7 * info["unique_lines"] + 2
            expected_bytes, expected_pending = info["content_bytes"], 2
        else:
            expected_objects = 4 * info["form_repeats"]
            expected_bytes, expected_pending = 12 * info["form_repeats"], 1
        require(
            decoded["page_source_objects"] == expected_objects,
            "wrong page /Contents object count",
        )
        require(
            decoded["page_source_bytes_observed"] == expected_bytes,
            "wrong page /Contents byte count",
        )
        if decoded["page_content_delivery"] == "callback":
            require(
                decoded["page_buffered_tokens"] == 0,
                "callback page retained source tokens",
            )
            require(
                decoded["page_buffered_token_capacity"] == 0,
                "callback page retained token capacity",
            )
        else:
            require(
                decoded["page_buffered_tokens"] == expected_objects,
                "buffered page lost source tokens",
            )
        if decoded["page_operand_diagnostics"]:
            require(
                decoded["page_max_pending_operands"] == expected_pending,
                "unexpected operand high-water mark",
            )
    if decoded.get("form_diagnostics"):
        depth, repeats = info["form_depth"], info["form_repeats"]
        expected_form_objects = (
            repeats * (7 * info["unique_lines"] + 2 + 4 * (depth - 1)) if depth else 0
        )
        require(
            decoded["form_executions"] == depth * repeats, "wrong Form execution count"
        )
        require(
            decoded["form_source_objects"] == expected_form_objects,
            "wrong Form source-object count",
        )
        require(decoded["form_max_depth"] == depth, "wrong Form nesting depth")
        require(
            decoded["form_max_pending_operands"] == (2 if depth else 0),
            "unexpected Form operand high-water mark",
        )
        require(
            decoded["form_content_delivery"] in ("callback", "buffered"),
            "unknown Form delivery mode",
        )
        expected_materialized = (
            2 * expected_form_objects
            if decoded["form_content_delivery"] == "buffered"
            else 0
        )
        require(
            decoded["form_materialized_tokens"] == expected_materialized,
            "unexpected Form token materialization",
        )
    if decoded.get("appearance_diagnostics"):
        count = info.get("appearance_count", 0)
        expected_objects = count * (7 * info["unique_lines"] + 2)
        require(
            decoded["appearance_executions"] == count,
            "wrong appearance execution count",
        )
        require(
            decoded["appearance_widget_executions"] == info.get("widget_count", 0),
            "wrong widget appearance count",
        )
        require(
            decoded["appearance_source_objects"] == expected_objects,
            "wrong appearance source-object count",
        )
        require(
            decoded["appearance_source_bytes_observed"]
            == count * info["content_bytes"],
            "wrong appearance source bytes",
        )
        require(
            decoded["appearance_max_pending_operands"] == (2 if count else 0),
            "unexpected appearance operand high-water mark",
        )
        delivery = decoded["appearance_content_delivery"]
        require(
            delivery in ("callback", "buffered"), "unknown appearance delivery mode"
        )
        expected_materialized = expected_objects if delivery == "buffered" else 0
        require(
            decoded["appearance_materialized_tokens"] == expected_materialized,
            "unexpected appearance token materialization",
        )
        capacity = decoded["appearance_max_buffered_token_capacity"]
        if delivery == "callback" or count == 0:
            require(capacity == 0, "unused appearance vector retained capacity")
        else:
            require(
                capacity >= 7 * info["unique_lines"] + 2,
                "buffered appearance capacity too small",
            )
    if decoded.get("type3_diagnostics"):
        repeats = info.get("type3_repeats", 0)
        expected_type3_objects = repeats * (7 * info["unique_lines"] + 5)
        require(
            decoded["type3_executions"] == repeats,
            "wrong procedural Type3 execution count",
        )
        require(
            decoded["type3_source_objects"] == expected_type3_objects,
            "wrong Type3 source-object count",
        )
        require(
            decoded["type3_max_depth"] == (1 if repeats else 0),
            "unexpected Type3 depth",
        )
        require(
            decoded["type3_recursion_skips"] == 0, "unexpected Type3 recursion skip"
        )
        require(
            decoded["type3_max_pending_operands"] == (2 if repeats else 0),
            "unexpected Type3 operand high-water mark",
        )
        delivery = decoded["type3_content_delivery"]
        require(delivery in ("callback", "buffered"), "unknown Type3 delivery mode")
        materialized = 2 * expected_type3_objects if delivery == "buffered" else 0
        require(
            decoded["type3_materialized_tokens"] == materialized,
            "unexpected Type3 token materialization",
        )
        if repeats:
            require(
                decoded["text_instructions"] == repeats,
                "unexpected Type3 text-instruction count",
            )
    if decoded.get("soft_mask_diagnostics"):
        require(
            decoded["soft_mask_executions"] == (1 if paints else 0),
            "soft-mask group must decode once per cache entry/CTM",
        )
        require(
            decoded["soft_mask_cache_hits"] == max(0, paints - 1),
            "unexpected soft-mask cache reuse",
        )
        objects = 7 * info["unique_lines"] + 2 if paints else 0
        require(
            decoded["soft_mask_source_objects"] == objects,
            "wrong soft-mask source-object count",
        )
        require(
            decoded["soft_mask_source_bytes_observed"]
            == (info["content_bytes"] if paints else 0),
            "wrong soft-mask source bytes",
        )
        require(
            decoded["soft_mask_shape_instructions"]
            == (info["unique_lines"] if paints and mode != "skip-shapes" else 0),
            "unexpected private mask geometry count",
        )
        require(
            decoded["soft_mask_max_pending_operands"] == (2 if paints else 0),
            "unexpected soft-mask operand high-water mark",
        )
        delivery = decoded["soft_mask_content_delivery"]
        require(delivery in ("callback", "buffered"), "unknown soft-mask delivery mode")
        require(
            decoded["soft_mask_materialized_tokens"]
            == (objects if delivery == "buffered" else 0),
            "unexpected soft-mask token materialization",
        )
        capacity = decoded["soft_mask_max_buffered_token_capacity"]
        if delivery == "callback" or not paints:
            require(capacity == 0, "unused soft-mask vector retained capacity")
        else:
            require(capacity >= objects, "buffered soft-mask capacity too small")
    if decoded.get("pattern_diagnostics"):
        active = pattern and mode != "skip-shapes"
        source_objects = 7 * info["unique_lines"] + 2 if active else 0
        executions = (lattice_cells * fills if lattice_cells else 1) if active else 0
        checks = {
            "source_parses": 1 if active else 0,
            "source_cache_hits": fills - 1 if active else 0,
            "cached_tokens": source_objects,
            "decoded_cells": 1 if active and not lattice_cells else 0,
            "decoded_cache_hits": fills - 1 if active and not lattice_cells else 0,
            "decoded_shape_instructions": info["unique_lines"]
            if active and not lattice_cells
            else 0,
            "lattice_executions": executions if lattice_cells else 0,
            "replay_source_objects": executions * source_objects,
            "max_pending_operands": 2 if active else 0,
        }
        for field, value in checks.items():
            require(decoded["pattern_" + field] == value, f"wrong pattern {field}")
        delivery = decoded["pattern_content_delivery"]
        require(delivery in ("borrowed", "copied"), "unknown pattern delivery mode")
        require(
            decoded["pattern_replay_materialized_tokens"]
            == (2 * executions * source_objects if delivery == "copied" else 0),
            "unexpected pattern replay vector materialization",
        )
        cached_capacity = decoded["pattern_cached_token_capacity"]
        require(
            cached_capacity >= source_objects if active else cached_capacity == 0,
            "unexpected pattern source-cache capacity",
        )
        replay_capacity = decoded["pattern_max_replay_token_capacity"]
        require(
            replay_capacity >= source_objects
            if active and delivery == "copied"
            else replay_capacity == 0,
            "unexpected pattern replay vector capacity",
        )
    if mode == "render":
        require("rendered" in by_stage, "missing render snapshot")
    return decoded


def stage_median(group: list[dict], stage: str, field: str) -> float | None:
    values = [
        s[field]
        for r in group
        for s in r["stages"]
        if s["stage"] == stage and field in s
    ]
    return statistics.median(values) if values else None


def summarize(records: list[dict], output: Path) -> None:
    grouped = defaultdict(list)
    for record in records:
        grouped[record["layout"], record["requested_lines"], record["mode"]].append(
            record
        )
    rows = []
    for (layout, requested, mode), group in grouped.items():
        last = [r["stages"][-1] for r in group]
        decoded: dict = next(
            (s for s in group[0]["stages"] if s["stage"] == "decoded"), {}
        )
        tokenized: dict = next(
            (s for s in group[0]["stages"] if s["stage"] == "tokenized"), {}
        )
        rows.append(
            {
                "layout": layout,
                "requested_lines": requested,
                "unique_lines": group[0]["generation"]["unique_lines"],
                "painted_lines": group[0]["generation"]["lines"],
                "mode": mode,
                "repeats": len(group),
                "median_peak_mib": statistics.median(x["peak_rss_bytes"] for x in last)
                / 2**20,
                "min_peak_mib": min(x["peak_rss_bytes"] for x in last) / 2**20,
                "max_peak_mib": max(x["peak_rss_bytes"] for x in last) / 2**20,
                "median_snapshot_elapsed_s": statistics.median(
                    x["elapsed_s"] for x in last
                ),
                "median_process_wall_s": statistics.median(
                    r["process_wall_s"] for r in group
                ),
                "tokens": tokenized.get("tokens"),
                "decompressed_bytes": tokenized.get("decompressed_bytes"),
                "retained_tokens": tokenized.get("retained_tokens"),
                "token_inline_bytes": tokenized.get("token_inline_bytes"),
                "shape_instructions": decoded.get("shape_instructions"),
                "instruction_clip_paths": decoded.get("instruction_clip_paths"),
                "instruction_clip_bytes": decoded.get("instruction_clip_bytes"),
                "instruction_clip_logical_bytes": decoded.get(
                    "instruction_clip_logical_bytes"
                ),
                "instruction_clip_unique_paths": decoded.get(
                    "instruction_clip_unique_paths"
                ),
                "instruction_clip_unique_points": decoded.get(
                    "instruction_clip_unique_points"
                ),
                "instruction_clip_history_nodes": decoded.get(
                    "instruction_clip_history_nodes"
                ),
                "clip_history_storage": decoded.get("clip_history_storage"),
                "median_decode_work_s": stage_median(group, "decoded", "decode_work_s"),
                "median_render_work_s": stage_median(
                    group, "rendered", "render_work_s"
                ),
                "page_content_delivery": decoded.get("page_content_delivery"),
                "page_source_objects": decoded.get("page_source_objects"),
                "page_buffered_tokens": decoded.get("page_buffered_tokens"),
                "page_max_pending_operands": decoded.get("page_max_pending_operands"),
                "form_content_delivery": decoded.get("form_content_delivery"),
                "form_executions": decoded.get("form_executions"),
                "form_source_objects": decoded.get("form_source_objects"),
                "form_materialized_tokens": decoded.get("form_materialized_tokens"),
                "form_max_pending_operands": decoded.get("form_max_pending_operands"),
                "form_max_depth": decoded.get("form_max_depth"),
                "type3_content_delivery": decoded.get("type3_content_delivery"),
                "type3_executions": decoded.get("type3_executions"),
                "type3_source_objects": decoded.get("type3_source_objects"),
                "type3_materialized_tokens": decoded.get("type3_materialized_tokens"),
                "type3_max_pending_operands": decoded.get("type3_max_pending_operands"),
                "type3_max_depth": decoded.get("type3_max_depth"),
                "type3_recursion_skips": decoded.get("type3_recursion_skips"),
                "appearance_content_delivery": decoded.get(
                    "appearance_content_delivery"
                ),
                "appearance_executions": decoded.get("appearance_executions"),
                "appearance_widget_executions": decoded.get(
                    "appearance_widget_executions"
                ),
                "appearance_source_objects": decoded.get("appearance_source_objects"),
                "appearance_source_bytes_observed": decoded.get(
                    "appearance_source_bytes_observed"
                ),
                "appearance_materialized_tokens": decoded.get(
                    "appearance_materialized_tokens"
                ),
                "appearance_max_buffered_token_capacity": decoded.get(
                    "appearance_max_buffered_token_capacity"
                ),
                "appearance_max_pending_operands": decoded.get(
                    "appearance_max_pending_operands"
                ),
                **{
                    "soft_mask_" + field: decoded.get("soft_mask_" + field)
                    for field in (
                        "content_delivery",
                        "executions",
                        "cache_hits",
                        "source_objects",
                        "source_bytes_observed",
                        "materialized_tokens",
                        "max_buffered_token_capacity",
                        "max_pending_operands",
                        "shape_instructions",
                    )
                },
                **{
                    "pattern_" + field: decoded.get("pattern_" + field)
                    for field in (
                        "content_delivery",
                        "source_parses",
                        "source_cache_hits",
                        "cached_tokens",
                        "cached_token_capacity",
                        "decoded_cells",
                        "decoded_cache_hits",
                        "decoded_shape_instructions",
                        "lattice_executions",
                        "replay_source_objects",
                        "replay_materialized_tokens",
                        "max_replay_token_capacity",
                        "max_pending_operands",
                    )
                },
            }
        )
    with (output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    table = [
        [
            row["layout"],
            row["requested_lines"],
            row["mode"],
            row["median_peak_mib"],
            f"{row['min_peak_mib']:.2f}..{row['max_peak_mib']:.2f}",
        ]
        for row in rows
    ]
    print(
        "\n"
        + tabulate(
            table,
            headers=["Layout", "Target", "Mode", "Peak MiB", "Range MiB"],
            tablefmt="simple",
            floatfmt=".2f",
            intfmt=",",
        ),
        flush=True,
    )


def baseline_data(path: Path, args: argparse.Namespace) -> dict:
    """Read existing observations only; do not launch or rebuild the baseline."""
    require(path.resolve() != args.output.resolve(), "baseline and output must differ")
    status = json.loads((path / "status.json").read_text())
    require(status["status"] == "passed", "baseline run did not pass its checks")
    with (path / "summary.csv").open(newline="") as stream:
        rows = {
            (row["layout"], int(row["requested_lines"]), row["mode"]): row
            for row in csv.DictReader(stream)
        }
    for size in args.sizes:
        for layout in args.layouts:
            for mode in args.modes:
                require((layout, size, mode) in rows, "baseline lacks a requested case")
    grouped = defaultdict(list)
    for line in (path / "measurements.jsonl").read_text().splitlines():
        record = json.loads(line)
        grouped[record["layout"], record["requested_lines"], record["mode"]].append(
            record
        )
    return {
        "directory": path,
        "rows": rows,
        "records": grouped,
        "metadata": json.loads((path / "metadata.json").read_text()),
    }


def compare_baseline(
    records: list[dict], output: Path, baseline: dict, slowdown_limit: float
) -> dict:
    """Compare matched input/output and repeated process timings, not estimates."""
    current_metadata = json.loads((output / "metadata.json").read_text())
    previous_metadata = baseline["metadata"]
    for setting in ("probe", "dependency_sha256"):
        require(
            previous_metadata[setting] == current_metadata[setting],
            f"baseline/candidate {setting} differs; use a controlled comparison",
        )
    grouped = defaultdict(list)
    for record in records:
        grouped[record["layout"], record["requested_lines"], record["mode"]].append(
            record
        )
    semantic_fields = (
        "page_shapes",
        "shape_instructions",
        "subpaths",
        "segments",
        "points",
        "instruction_dash_entries",
        "max_dash_entries",
        "page_shape_dash_entries",
        "instruction_clip_paths",
        "max_clip_paths",
        "instruction_clip_points",
    )
    rows, image_checks = [], []
    for key, group in grouped.items():
        layout, requested, mode = key
        before = baseline["records"][key]
        first_before, first_after = before[0], group[0]
        require(
            first_before["generation"]["pdf_sha256"]
            == first_after["generation"]["pdf_sha256"],
            f"baseline/candidate PDF differs for {layout}-{requested}",
        )
        require(
            first_before["stages"][0]["qpdf_version"]
            == first_after["stages"][0]["qpdf_version"],
            "baseline/candidate QPDF version differs",
        )
        if mode in ("decode", "skip-shapes", "render"):
            old_output = next(
                s for s in first_before["stages"] if s["stage"] == "decoded"
            )
            new_output = next(
                s for s in first_after["stages"] if s["stage"] == "decoded"
            )
            require(
                all(
                    old_output[field] == new_output[field] for field in semantic_fields
                ),
                f"baseline/candidate output counters differ for {layout}-{requested}-{mode}",
            )
            if layout in SOFT_MASK_LAYOUTS:
                require(
                    all(
                        old_output[field] == new_output[field]
                        for field in (
                            "soft_mask_executions",
                            "soft_mask_cache_hits",
                            "soft_mask_source_objects",
                            "soft_mask_shape_instructions",
                        )
                    ),
                    "baseline/candidate soft-mask source, geometry or reuse differs",
                )
            if layout in PATTERN_LAYOUTS:
                require(
                    all(
                        old_output["pattern_" + field] == new_output["pattern_" + field]
                        for field in (
                            "source_parses",
                            "source_cache_hits",
                            "cached_tokens",
                            "cached_token_capacity",
                            "decoded_cells",
                            "decoded_cache_hits",
                            "decoded_shape_instructions",
                            "lattice_executions",
                            "replay_source_objects",
                        )
                    ),
                    "baseline/candidate pattern source, geometry or reuse differs",
                )
        if mode == "render":
            filename = f"{layout}-{requested}.png"
            same_image = sha256(baseline["directory"] / filename) == sha256(
                output / filename
            )
            image_checks.append(
                {
                    "layout": layout,
                    "target": requested,
                    "identical_png_bytes": same_image,
                }
            )
            require(same_image, f"baseline/candidate PNG bytes differ: {filename}")

        old_peak = (
            statistics.median(r["stages"][-1]["peak_rss_bytes"] for r in before) / 2**20
        )
        new_peak = (
            statistics.median(r["stages"][-1]["peak_rss_bytes"] for r in group) / 2**20
        )
        old_times = [r["process_wall_s"] for r in before]
        new_times = [r["process_wall_s"] for r in group]
        old_wall, new_wall = statistics.median(old_times), statistics.median(new_times)
        delta_time = 100 * (new_wall / old_wall - 1)
        overlap = max(min(old_times), min(new_times)) <= min(
            max(old_times), max(new_times)
        )
        if min(len(before), len(group)) < 3:
            timing = "diagnostic: <3 reps"
        elif delta_time > slowdown_limit:
            timing = "recheck: overlap" if overlap else "review slowdown"
        else:
            timing = "within budget"
        rows.append(
            {
                "layout": layout,
                "requested_lines": requested,
                "mode": mode,
                "baseline_peak_mib": old_peak,
                "candidate_peak_mib": new_peak,
                "memory_change_pct": 100 * (new_peak / old_peak - 1),
                "baseline_process_wall_s": old_wall,
                "candidate_process_wall_s": new_wall,
                "time_change_pct": delta_time,
                "baseline_min_wall_s": min(old_times),
                "baseline_max_wall_s": max(old_times),
                "candidate_min_wall_s": min(new_times),
                "candidate_max_wall_s": max(new_times),
                "timing_ranges_overlap": overlap,
                "timing_review": timing,
            }
        )
    with (output / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    report = {
        "baseline": str(baseline["directory"]),
        "same_binary": previous_metadata["binary_sha256"]
        == current_metadata["binary_sha256"],
        "slowdown_review_limit_pct": slowdown_limit,
        "compared_cases": len(rows),
        "image_checks": image_checks,
        "timing_review_cases": sum(r["timing_review"] != "within budget" for r in rows),
        "note": "Timing flags request review, not a significance test or automatic failure.",
    }
    (output / "baseline-comparison.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    if report["same_binary"]:
        print(
            "\nIdentical binary hashes: this is a repeat/control comparison.",
            flush=True,
        )
    print(
        "\nBaseline comparison (negative changes are improvements):\n"
        + tabulate(
            [
                [
                    row["layout"],
                    row["requested_lines"],
                    row["mode"],
                    row["baseline_peak_mib"],
                    row["candidate_peak_mib"],
                    row["memory_change_pct"],
                    row["baseline_process_wall_s"],
                    row["candidate_process_wall_s"],
                    row["time_change_pct"],
                    row["timing_review"],
                ]
                for row in rows
            ],
            headers=[
                "Layout",
                "Target",
                "Mode",
                "Before MiB",
                "After MiB",
                "Mem %",
                "Before s",
                "After s",
                "Time %",
                "Timing",
            ],
            tablefmt="simple",
            floatfmt=("", "", "", ".2f", ".2f", ".2f", ".3f", ".3f", ".2f", ""),
            intfmt=",",
        ),
        flush=True,
    )
    return report


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/test_memory_on_lines")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--sizes", default="1000,8000", help="comma-separated line targets"
    )
    parser.add_argument("--repeats", type=positive_int, default=3)
    parser.add_argument(
        "--layouts", type=lambda s: selection(s, LAYOUTS), default=list(DEFAULT_LAYOUTS)
    )
    parser.add_argument(
        "--modes", type=lambda s: selection(s, MODES), default=list(MODES)
    )
    parser.add_argument("--form-repeats", type=positive_int, default=4)
    parser.add_argument("--form-depth", type=positive_int, default=3)
    parser.add_argument("--appearance-repeats", type=positive_int, default=4)
    parser.add_argument("--type3-repeats", type=positive_int, default=4)
    parser.add_argument("--soft-mask-repeats", type=positive_int, default=4)
    parser.add_argument("--pattern-repeats", type=positive_int, default=4)
    parser.add_argument("--timeout", type=positive_int, default=180)
    parser.add_argument(
        "--baseline", type=Path, help="compare with a previous result directory"
    )
    parser.add_argument(
        "--slowdown-limit-pct",
        type=float,
        default=5.0,
        help="timing review threshold, not an automatic failure (default: 5)",
    )
    args = parser.parse_args()
    if not math.isfinite(args.slowdown_limit_pct) or args.slowdown_limit_pct < 0:
        parser.error("--slowdown-limit-pct must be finite and nonnegative")
    try:
        sizes = [positive_int(value) for value in args.sizes.split(",")]
    except (ValueError, argparse.ArgumentTypeError) as error:
        parser.error(str(error))
    if not sizes or min(sizes) < 10 or max(sizes) > 2**31 - 1:
        parser.error("line targets must be 10..INT_MAX")
    if len(set(sizes)) != len(sizes):
        parser.error("line targets must be distinct")
    if args.form_depth > 32:
        parser.error("--form-depth must be 1..32")
    if "form-repeat" in args.layouts and args.form_repeats < 2:
        parser.error("--form-repeats must be at least 2 for form-repeat")
    if "form-nested" in args.layouts and args.form_depth < 2:
        parser.error("--form-depth must be at least 2 for form-nested")
    if "appearance-repeat" in args.layouts and args.appearance_repeats < 2:
        parser.error("--appearance-repeats must be at least 2 for appearance-repeat")
    if "type3-repeat" in args.layouts and args.type3_repeats < 2:
        parser.error("--type3-repeats must be at least 2 for type3-repeat")
    if "soft-mask-repeat" in args.layouts and args.soft_mask_repeats < 2:
        parser.error("--soft-mask-repeats must be at least 2 for soft-mask-repeat")
    if "pattern-repeat" in args.layouts and args.pattern_repeats < 2:
        parser.error("--pattern-repeats must be at least 2 for pattern-repeat")
    if any(layout in CLIP_LAYOUTS for layout in args.layouts) and max(sizes) > 4000:
        parser.error("clip-history fixtures require line targets of at most 4000")
    args.sizes = sizes
    return args


def compare_layout_images(
    image_hashes: dict[tuple[str, int], str], sizes: list[int]
) -> list[dict]:
    pairs = [
        ("direct", name)
        for name in ("form", "form-nested", "appearance", "widget", "type3")
    ]
    pairs.append(("clip-chain", "clip-scoped"))
    comparisons = []
    for size in sizes:
        for reference, layout in pairs:
            first = image_hashes.get((reference, size))
            other = image_hashes.get((layout, size))
            if first and other:
                comparisons.append(
                    {
                        "requested_lines": size,
                        "reference_layout": reference,
                        "layout": layout,
                        "identical_png_bytes": first == other,
                    }
                )
    return comparisons


def main() -> None:
    args = arguments()
    sizes = args.sizes
    baseline = baseline_data(args.baseline.resolve(), args) if args.baseline else None
    exe = args.exe.resolve()
    require(exe.is_file(), f"probe not found: {exe}; compile it first")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = output / "source"
    sources.mkdir()
    source_hashes = {}
    for relative in (
        "tests/cpp/test_memory_on_lines.cpp",
        "tests/cpp/run_memory_on_lines.py",
        "CMakeLists.txt",
        "src/parse/pdf_decoders/page.h",
        "src/parse/pdf_decoders/stream.h",
        "src/parse/pdf_resources/page_xobject_form.h",
        "src/parse/pdf_resources/page_font.h",
        "src/parse/pdf_resources/page_pattern.h",
        "src/parse/pdf_states/shape.h",
        "src/parse/page_items/render_instructions.h",
        "src/render/blend2d_renderer.h",
        "src/parse/qpdf/stream_decoder.h",
        "src/parse/utils/pdf_timings.h",
        "tests/test_unit_appearance_streaming.py",
        "tests/test_unit_type3_streaming.py",
        "tests/test_unit_soft_mask_streaming.py",
        "tests/test_unit_pattern_replay.py",
        "tests/cpp/test_stream_state_lifetimes.cpp",
        "tests/cpp/test_clip_history.cpp",
        "tests/test_unit_clipping.py",
    ):
        path = ROOT / relative
        source_hashes[relative] = sha256(path)
        (sources / path.name).write_bytes(path.read_bytes())
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    diff = subprocess.run(
        ["git", "--no-pager", "diff", "--no-ext-diff", "--no-textconv"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    (output / "working-tree.diff").write_text(diff.stdout + diff.stderr)
    for filename in ("flags.make", "link.txt"):
        path = exe.parent / "CMakeFiles" / f"{exe.name}.dir" / filename
        if path.is_file():
            (sources / filename).write_bytes(path.read_bytes())
    archives = list((ROOT / "externals/lib").glob("*.a"))
    archives += [
        ROOT / "build/_deps/logurugitrepo-build/libloguru.a",
        ROOT / "build/_deps/blend2d-build/libblend2d.a",
        ROOT / "build/libpdfium_jbig2.a",
    ]
    metadata = {
        "created": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "head": head.stdout.strip(),
        "platform": platform.platform(),
        "executable": str(exe),
        "binary_sha256": sha256(exe),
        "source_sha256": source_hashes,
        "dependency_sha256": {
            str(path): sha256(path) for path in archives if path.is_file()
        },
        "sizes": sizes,
        "layouts": args.layouts,
        "modes": args.modes,
        "repeats": args.repeats,
        "form_repeats": args.form_repeats,
        "form_depth": args.form_depth,
        "appearance_repeats": args.appearance_repeats,
        "type3_repeats": args.type3_repeats,
        "soft_mask_repeats": args.soft_mask_repeats,
        "pattern_repeats": args.pattern_repeats,
        "baseline": str(args.baseline.resolve()) if args.baseline else None,
        "slowdown_review_limit_pct": args.slowdown_limit_pct,
        "probe": {"do_thread_safe": False, "render_scale": 1.0, "pause_seconds": 0},
        "raw_token_scope": "one page, Form leaf, appearance, CharProc, mask or pattern source; not execution",
        "note": "No compilation or PDF Python bindings; each child is a fresh process.",
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    records: list[dict] = []
    image_hashes: dict[tuple[str, int], str] = {}
    raw_signatures: dict[tuple[int, str], tuple[int, int, int]] = {}
    comparisons = []
    try:
        with (output / "measurements.jsonl").open("w") as stream:
            for size in sizes:
                for layout in args.layouts:
                    stem = f"{layout}-{size}"
                    pdf = output / f"{stem}.pdf"
                    if layout in CLIP_LAYOUTS:
                        variant = layout
                    elif layout in PATTERN_LAYOUTS:
                        variant = (
                            "pathstorm-pattern-lattice"
                            if layout == "pattern-lattice"
                            else "pathstorm-pattern"
                        )
                    elif layout in SOFT_MASK_LAYOUTS:
                        variant = "pathstorm-soft-mask"
                    elif layout in TYPE3_LAYOUTS:
                        variant = "pathstorm-type3"
                    elif layout in APPEARANCE_LAYOUTS:
                        variant = (
                            "pathstorm-widget"
                            if layout == "widget"
                            else "pathstorm-appearance"
                        )
                    else:
                        variant = (
                            "pathstorm" if layout == "direct" else "pathstorm-form"
                        )
                    argv = [str(exe), "generate", variant, str(size), str(pdf)]
                    if layout in PATTERN_LAYOUTS:
                        argv += [
                            str(
                                args.pattern_repeats
                                if layout == "pattern-repeat"
                                else 1
                            )
                        ]
                    elif layout in SOFT_MASK_LAYOUTS:
                        repeat = (
                            args.soft_mask_repeats
                            if layout == "soft-mask-repeat"
                            else 1
                        )
                        argv += [str(repeat)]
                    elif layout in TYPE3_LAYOUTS:
                        argv += [
                            str(args.type3_repeats if layout == "type3-repeat" else 1)
                        ]
                    elif layout in APPEARANCE_LAYOUTS:
                        repeat = (
                            args.appearance_repeats
                            if layout == "appearance-repeat"
                            else 1
                        )
                        argv += [str(repeat)]
                    elif layout != "direct" and layout not in CLIP_LAYOUTS:
                        repeat = args.form_repeats if layout == "form-repeat" else 1
                        depth = args.form_depth if layout == "form-nested" else 1
                        argv += [str(repeat), str(depth)]
                    generation, _ = command(
                        argv, output / f"{stem}-generate.log", args.timeout
                    )
                    require(len(generation) == 1, "expected one generator record")
                    info = generation[0]
                    info["pdf_sha256"] = sha256(pdf)
                    (output / f"{stem}-generation.json").write_text(
                        json.dumps(info, indent=2) + "\n"
                    )
                    for mode in args.modes:
                        for repetition in range(1, args.repeats + 1):
                            case = f"{stem}-{mode}-{repetition}"
                            argv = [str(exe), "measure", mode, str(pdf)]
                            png = output / f"{stem}.png"
                            if mode == "render" and repetition == 1:
                                argv += ["0", str(png)]
                            stages, wall = command(
                                argv, output / f"{case}.log", args.timeout
                            )
                            counts = validate(stages, info, mode, layout)
                            if mode in ("qpdf-discard", "collect"):
                                signature = (
                                    counts["tokens"],
                                    counts["operators"],
                                    counts["decompressed_bytes"],
                                )
                                signature_key = (
                                    size,
                                    "type3"
                                    if layout in TYPE3_LAYOUTS
                                    else layout
                                    if layout in CLIP_LAYOUTS
                                    else "grid",
                                )
                                require(
                                    raw_signatures.setdefault(signature_key, signature)
                                    == signature,
                                    "discard/collect or selected-source counts differ",
                                )
                            record = {
                                "layout": layout,
                                "requested_lines": size,
                                "mode": mode,
                                "repetition": repetition,
                                "generation": info,
                                "command": argv,
                                "process_wall_s": wall,
                                "stages": stages,
                            }
                            records.append(record)
                            stream.write(json.dumps(record) + "\n")
                            stream.flush()
                            if mode == "render" and repetition == 1:
                                image_hashes[layout, size] = sha256(png)
                            print(
                                f"{case}: {stages[-1]['peak_rss_bytes'] / 2**20:.2f} MiB; "
                                f"process {wall:.3f}s",
                                flush=True,
                            )
        summarize(records, output)
        comparisons = compare_layout_images(image_hashes, sizes)
        (output / "image-comparisons.json").write_text(
            json.dumps(comparisons, indent=2) + "\n"
        )
        require(
            all(row["identical_png_bytes"] for row in comparisons),
            "equivalent-layout PNG bytes differ; inspect pixels/encoding before accepting",
        )
        comparison_report = None
        if baseline:
            comparison_report = compare_baseline(
                records, output, baseline, args.slowdown_limit_pct
            )
        (output / "status.json").write_text(
            json.dumps(
                {
                    "status": "passed",
                    "measurements": len(records),
                    "baseline_comparison": comparison_report,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"\nSaved {len(records)} measurements to {output}", flush=True)
    except Exception as error:
        (output / "status.json").write_text(
            json.dumps(
                {
                    "status": "failed",
                    "completed_measurements": len(records),
                    "error": str(error),
                },
                indent=2,
            )
            + "\n"
        )
        raise


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Memory experiment stopped: {error}", file=sys.stderr)
        raise SystemExit(1)
