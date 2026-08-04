#!/usr/bin/env python3
"""Shared helpers for the perf scripts.

The important thing here is `PageRow`: one timed page, tagged with the backend,
task and thread count that produced it.  It is the single interchange format
between the three perf entry points --- `run_scaling.py` writes it,
`run_eval.py` plots it, `run_analysis.py` drills into it --- so that adding a
column benefits all three instead of forking another CSV dialect.
"""

from __future__ import annotations

import csv
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

TASK_PARSE = "parse"
TASK_RENDER = "parse+render"

# Per-page timing breakdown reported by the C++ layer. Absent (zero) for
# third-party backends, which expose no internal timings.
STAGE_TIMING_FIELDS = [
    "make_page_decoder_s",
    "decode_page_s",
    "create_word_cells_s",
    "create_line_cells_s",
    "render_page_s",
]

PAGE_CSV_FIELDS = [
    "backend",
    "task",
    "threads",
    "doc_key",
    "page_number",
    "success",
    "elapsed_s",
    "wall_gap_s",
    "image_width",
    "image_height",
    *STAGE_TIMING_FIELDS,
    "error_message",
]


@dataclass
class PageRow:
    """One timed page from one backend."""

    backend: str
    task: str
    threads: int
    doc_key: str
    page_number: int
    success: bool
    elapsed_s: float  # the per-page cost used for statistics
    wall_gap_s: float = 0.0  # observed arrival gap; == elapsed_s when unthreaded
    # Rasterised size in pixels, so a render run can be checked for producing
    # the same canvas across backends. Zero outside the render task.
    image_width: int = 0
    image_height: int = 0
    stage_timings: Dict[str, float] = field(default_factory=dict)
    error_message: str = ""

    @property
    def series(self) -> str:
        """Label identifying the (backend, task, threads) this row belongs to."""
        return series_label(self.backend, self.task, self.threads)


def series_label(backend: str, task: str, threads: int) -> str:
    suffix = f" x{threads}" if threads > 1 else ""
    return f"{backend} [{task}]{suffix}"


# -------- Filesystem helpers --------


def find_pdfs(path: Path, recursive: bool = False) -> List[Path]:
    if path.is_file():
        return [path] if path.suffix.lower() == ".pdf" else []
    pattern = "**/*.pdf" if recursive else "*.pdf"
    return sorted([p for p in path.glob(pattern) if p.is_file()])


def ensure_parent_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def safe_name(value: str) -> str:
    """Filesystem-safe version of a series label."""
    return re.sub(r"[^A-Za-z0-9_.=-]+", "-", value)


# -------- Statistics --------


def percentile(values: List[float], p: float) -> float:
    """Linearly interpolated percentile."""
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
    return vs[f] * (c - k) + vs[c] * (k - f)


def fmt_seconds(seconds: float) -> str:
    return f"{seconds:.6f}"


# -------- Per-page CSV --------


def write_page_rows(path: Path, rows: List[PageRow]) -> None:
    ensure_parent_dir(path)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=PAGE_CSV_FIELDS)
        writer.writeheader()
        for row in rows:
            record = {
                "backend": row.backend,
                "task": row.task,
                "threads": row.threads,
                "doc_key": row.doc_key,
                "page_number": row.page_number,
                "success": int(row.success),
                "elapsed_s": f"{row.elapsed_s:.9f}",
                "wall_gap_s": f"{row.wall_gap_s:.9f}",
                "image_width": row.image_width,
                "image_height": row.image_height,
                "error_message": row.error_message,
            }
            for key in STAGE_TIMING_FIELDS:
                record[key] = f"{row.stage_timings.get(key, 0.0):.9f}"
            writer.writerow(record)


def _as_bool(value: str) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes"}


def _as_float(value: str, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _as_int(value: str, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def _row_from_canonical(record: Dict[str, str]) -> PageRow:
    return PageRow(
        backend=record.get("backend", "unknown"),
        task=record.get("task", TASK_PARSE),
        threads=_as_int(record.get("threads", "1"), 1),
        doc_key=record.get("doc_key", ""),
        page_number=_as_int(record.get("page_number", "0")),
        success=_as_bool(record.get("success", "")),
        elapsed_s=_as_float(record.get("elapsed_s", "0")),
        wall_gap_s=_as_float(record.get("wall_gap_s", "0")),
        image_width=_as_int(record.get("image_width", "0")),
        image_height=_as_int(record.get("image_height", "0")),
        stage_timings={
            key: _as_float(record.get(key, "0")) for key in STAGE_TIMING_FIELDS
        },
        error_message=record.get("error_message", ""),
    )


def read_page_rows(path: Path) -> List[PageRow]:
    """Read a per-page CSV written by `run_scaling.py --pages-csv`."""
    rows: List[PageRow] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if "backend" not in set(reader.fieldnames or []):
            raise ValueError(f"Not a per-page CSV (no `backend` column): {path}")
        for record in reader:
            try:
                rows.append(_row_from_canonical(record))
            except Exception:
                continue  # skip malformed rows rather than abort a long file
    return rows


def group_by_series(rows: List[PageRow]) -> Dict[str, List[PageRow]]:
    """Group rows into one series per (backend, task, threads)."""
    grouped: Dict[str, List[PageRow]] = {}
    for row in rows:
        grouped.setdefault(row.series, []).append(row)
    return grouped


def successful_page_times(rows: List[PageRow]) -> List[float]:
    return [r.elapsed_s for r in rows if r.success and r.page_number > 0]
