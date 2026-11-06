#!/usr/bin/env python3
"""Validate and plot a refresh_performance_benchmarks.sh output directory.

Scaling uses native console/summary-file wall_s, replay document total_s, or comparison
console wall time. Worker elapsed seconds and arrival gaps are never substituted
for wall time. Different workloads remain in separate tables and figures.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import shlex
import statistics
import textwrap
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

WORKLOAD_LABELS = {
    "native": "Native C++ scaling",
    "replay": "Python scaling with Docling settings",
    "comparison": "Python cross-package comparison",
}
NATIVE_PROFILES = {
    "render-only": {"create_word_cells": False, "create_line_cells": False},
    "render-with-text": {"create_word_cells": True, "create_line_cells": True},
}
BACKEND_LABELS = {"pdfium": "pypdfium2", "threaded": "docling-parse"}


def task_title(workload: str, task: str) -> str:
    if workload == "native":
        return {
            "render-only": "Render images",
            "render-with-text": "Render images and construct word/line text geometry",
            "render": "Render images and construct word/line text geometry (legacy profile)",
        }[task]
    if workload == "replay":
        return "Images, word/line text geometry and bitmap bounding boxes"
    return (
        "Character text and bounding boxes"
        if task == "parse"
        else "Images plus character text and bounding boxes"
    )


def plot_title(workload: str, task: str) -> str:
    return f"{WORKLOAD_LABELS[workload]}\n{textwrap.fill(task_title(workload, task), width=70)}"


def figure_prefix(workload: str, task: str) -> str:
    if workload == "native" and task != "render":
        return "native_" + task.replace("-", "_")
    if workload == "comparison":
        return "comparison_" + task.replace("+", "_")
    return workload


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def seconds(value: Any) -> float:
    result = float(value)
    require(math.isfinite(result) and result >= 0, f"Invalid duration: {value}")
    return result


def match(pattern: str, text: str, source: Path) -> re.Match[str]:
    result = re.search(pattern, text, re.M)
    require(result is not None, f"Missing {pattern!r} in {source}")
    assert result is not None
    return result


@dataclass
class Measurement:
    workload: str
    backend: str
    task: str
    threads: int
    repetition: int
    wall_s: float
    pages: int
    documents: int
    source: str
    timing_basis: str
    phases: dict[str, float] = field(default_factory=dict)
    durations: list[float] = field(default_factory=list)

    @property
    def group(self) -> tuple[str, str, str, int]:
        return self.workload, self.backend, self.task, self.threads


class ReportInputs:
    def __init__(self, root: Path):
        self.root = root
        self.metadata = json.loads((root / "metadata.json").read_text())
        require(self.metadata["schema_version"] == 1, "Unsupported metadata schema")
        profiles = self.metadata.get("native_profiles")
        require(
            profiles is None or profiles == NATIVE_PROFILES,
            "Unknown native profile configuration",
        )
        self.native_profiles = list(profiles) if profiles is not None else ["render"]
        self.aliases: dict[str, str] = {}
        self.expected: set[tuple[str, int]] = set()
        self.files = self.metadata["dataset"]["files"]
        self.page_counts = {item["name"]: item["pages"] for item in self.files}
        for item in self.files:
            for alias in (item["name"], item["path"], item["resolved_path"]):
                require(
                    alias not in self.aliases or self.aliases[alias] == item["name"],
                    f"Ambiguous document identity: {alias}",
                )
                self.aliases[alias] = item["name"]
            self.expected.update((item["name"], p) for p in range(1, item["pages"] + 1))
        require(bool(self.expected), "Empty dataset manifest")
        self.canvas_checks: list[dict[str, Any]] = []

    def identity(self, document: str, page: Any) -> tuple[str, int]:
        document = document.removeprefix("key=")
        require(
            document in self.aliases, f"Document is absent from manifest: {document}"
        )
        result = self.aliases[document], int(page)
        require(result in self.expected, f"Page is absent from manifest: {result}")
        return result

    def repetition(self, path: Path) -> int:
        parts = [
            p
            for p in path.relative_to(self.root).parts
            if re.fullmatch(r"r[1-9]\d*", p)
        ]
        require(len(parts) == 1, f"Missing repetition directory for {path}")
        return int(parts[0][1:])

    def native(self, path: Path) -> list[Measurement]:
        parts = path.relative_to(self.root / "native").parts
        task = parts[0] if len(parts) == 3 else "render"
        require(
            len(parts) in (2, 3) and task in self.native_profiles,
            f"Native path does not match the declared profiles: {path}",
        )
        log_path = path.with_suffix(".log")
        log = log_path.read_text()
        require(
            "--glyph-cache-diagnostics" not in log, f"Glyph diagnostics enabled: {path}"
        )
        counts = match(r"Benchmark: (\d+) documents, (\d+) total pages", log, log_path)
        require(
            (int(counts[1]), int(counts[2])) == (len(self.files), len(self.expected)),
            f"Native input counts differ from manifest: {path}",
        )
        summary_path = Path(str(path) + ".summary.log")
        timing_path = summary_path if summary_path.exists() else log_path
        timing_log = summary_path.read_text() if summary_path.exists() else log
        blocks = re.split(
            r"Timing diagnostics \(threads=(\d+), CSV schema 2\)", timing_log
        )
        summaries = {int(blocks[i]): blocks[i + 1] for i in range(1, len(blocks), 2)}
        seen: dict[int, set[tuple[str, int]]] = defaultdict(set)
        totals: dict[int, dict[str, float]] = defaultdict(lambda: defaultdict(float))
        durations: dict[int, list[float]] = defaultdict(list)
        with path.open(newline="") as stream:
            for row in csv.DictReader(stream):
                require(
                    row["schema_version"] == "2"
                    and row["mode"] == "render"
                    and row["render"] == "true"
                    and row["success"] == "true"
                    and not row["error_message"],
                    f"Invalid or unsuccessful native row: {path}",
                )
                t = int(row["threads"])
                key = self.identity(row["doc_key"], row["page_number"])
                require(
                    key not in seen[t], f"Duplicate native page: {path}: {t}, {key}"
                )
                seen[t].add(key)
                for name, value in row.items():
                    if name.startswith("timing_"):
                        totals[t][name.removeprefix("timing_")] += seconds(value)
                durations[t].append(seconds(row["timing_total_s"]))
                if task == "render-only":
                    require(
                        seconds(row["timing_create_word_cells_s"]) == 0
                        and seconds(row["timing_create_line_cells_s"]) == 0,
                        f"Word/line cell creation found in render-only profile: {path}",
                    )
        require(set(seen) == set(summaries), f"CSV/log thread mismatch: {path}")
        result = []
        for t, identities in seen.items():
            require(
                identities == self.expected, f"Incomplete native page set: {path}, {t}"
            )
            summary = summaries[t]
            wall = seconds(match(r"wall_s: ([\d.]+)", summary, timing_path)[1])
            for name, value in totals[t].items():
                if name == "consumer_wait_s":
                    continue
                printed = seconds(
                    match(
                        r"^  " + re.escape(name) + r"\s+([\d.]+)$", summary, timing_path
                    )[1]
                )
                require(
                    math.isclose(value, printed, abs_tol=2e-6, rel_tol=1e-8),
                    f"Native stage sum disagrees with log: {path}, {name}",
                )
            stages = {
                name: totals[t][name]
                for name in (
                    "make_page_decoder_s",
                    "decode_page_s",
                    "create_word_cells_s",
                    "create_line_cells_s",
                    "render_page_s",
                    "get_canvas_s",
                    "collect_detail_s",
                )
            }
            other = totals[t]["total_s"] - sum(stages.values())
            require(other >= -2e-6, f"Overlapping native stage totals: {path}")
            stages["other_s"] = max(0.0, other)
            result.append(
                Measurement(
                    "native",
                    "docling-parse",
                    task,
                    t,
                    self.repetition(path),
                    wall,
                    len(identities),
                    len(self.files),
                    str(timing_path.relative_to(self.root)),
                    "native wall_s; includes collection/CSV; excludes document preload",
                    stages,
                    durations[t],
                )
            )
        return result

    def replay(self, path: Path) -> list[Measurement]:
        report = json.loads(path.read_text())
        require(
            report["provenance"]["parse_git"]["revision"]
            == self.metadata["source_revision"],
            f"Replay source revision differs from build metadata: {path}",
        )
        require(
            report["provenance"]["parse_git"]["diff_sha256"]
            == self.metadata["source_diff_sha256"],
            f"Replay tracked source diff changed during the run: {path}",
        )
        result = []
        for run in report["runs"]:
            settings = run["settings"]
            require(
                settings["requested_scale"] == 1.0, f"Unexpected replay scale: {path}"
            )
            seen_documents: set[str] = set()
            phases: dict[str, float] = defaultdict(float)
            durations = []
            for document in run["documents"]:
                name = self.identity(document["document"], 1)[0]
                require(
                    name not in seen_documents,
                    f"Duplicate replay document: {path}: {name}",
                )
                seen_documents.add(name)
                expected_count = self.page_counts[name]
                require(
                    document["pages"] == expected_count,
                    f"Incomplete replay document: {name}",
                )
                parts = {
                    k: seconds(document[k])
                    for k in (
                        "construction_load_s",
                        "page_processing_s",
                        "unload_s",
                    )
                }
                total = seconds(document["total_s"])
                require(
                    math.isclose(total, sum(parts.values()), abs_tol=1e-7),
                    f"Replay phase mismatch: {name}",
                )
                for k, value in parts.items():
                    phases[k] += value
                durations.append(total)
            require(
                len(seen_documents) == len(self.files),
                f"Incomplete replay corpus: {path}",
            )
            # Prefer the adjacent artifact when a bundle has been copied;
            # an old absolute path may still exist but refer to another copy.
            pages_path = path.parent / Path(run["pages_file"]).name
            if not pages_path.is_file():
                pages_path = Path(run["pages_file"])
            identities = set()
            with pages_path.open() as stream:
                for line in stream:
                    row = json.loads(line)
                    key = self.identity(row["document"], row["page"])
                    require(
                        key not in identities,
                        f"Duplicate replay page: {pages_path}: {key}",
                    )
                    identities.add(key)
                    require(
                        row["image_mode"] == "RGB"
                        and len(row["image_size"]) == 2
                        and all(v > 0 for v in row["image_size"]),
                        f"Invalid replay image metadata: {pages_path}: {key}",
                    )
            require(
                identities == self.expected,
                f"Incomplete replay page metadata: {pages_path}",
            )
            result.append(
                Measurement(
                    "replay",
                    settings["backend"],
                    "render",
                    settings["parser"]["threads"],
                    self.repetition(path),
                    sum(durations),
                    len(identities),
                    len(self.files),
                    str(path.relative_to(self.root)),
                    "sum of document total_s; includes construction/load, processing and unload",
                    dict(phases),
                    durations,
                )
            )
        return result

    def comparison(self, path: Path) -> list[Measurement]:
        log_path = path.parent / "run.log"
        log = log_path.read_text()
        task = "parse" if path.parent.name == "parse-without-bitmap" else "parse+render"
        table = log.rsplit("=== ", 1)[-1]
        walls = {}
        for line in table.splitlines():
            native = re.match(r"^docling threaded\s+(\d+)\s+([\d.]+)\s", line)
            reference = re.match(
                r"^(pymupdf|pypdfium2|pdfplumber|pdfminer\.six|pypdf) \(1t\)\s+-\s+([\d.]+)\s",
                line,
            )
            if native:
                walls["docling-parse", int(native[1])] = seconds(native[2])
            if reference:
                walls[reference[1], 1] = seconds(reference[2])
        seen: dict[tuple[str, int], set[tuple[str, int]]] = defaultdict(set)
        durations: dict[tuple[str, int], list[float]] = defaultdict(list)
        sizes: dict[tuple[str, int], dict[tuple[str, int], tuple[int, int]]] = (
            defaultdict(dict)
        )
        with path.open(newline="") as stream:
            for row in csv.DictReader(stream):
                require(
                    row["success"] == "1"
                    and not row["error_message"]
                    and row["task"] == task,
                    f"Invalid or unsuccessful comparison page: {path}",
                )
                group = row["backend"], int(row["threads"])
                identity = self.identity(row["doc_key"], row["page_number"])
                require(
                    identity not in seen[group],
                    f"Duplicate comparison page: {path}: {identity}",
                )
                seen[group].add(identity)
                durations[group].append(seconds(row["elapsed_s"]))
                size = int(row["image_width"]), int(row["image_height"])
                if task == "parse+render":
                    require(min(size) > 0, f"Missing comparison image size: {path}")
                    sizes[group][identity] = size
        require(set(seen) == set(walls), f"Comparison CSV/log series mismatch: {path}")
        result = []
        for (backend, t), identities in seen.items():
            require(
                identities == self.expected,
                f"Incomplete comparison page set: {path}: {backend}",
            )
            result.append(
                Measurement(
                    "comparison",
                    backend,
                    task,
                    t,
                    self.repetition(path),
                    walls[backend, t],
                    len(identities),
                    len(self.files),
                    str(log_path.relative_to(self.root)),
                    "whole-corpus console wall time, rounded to 0.001 s; includes document loading",
                    durations=durations[backend, t],
                )
            )
            if sizes:
                reference_sizes = sizes["pypdfium2", 1]
                require(
                    set(reference_sizes) == self.expected,
                    f"Missing PDFium image sizes: {path}",
                )
                deltas = [
                    max(abs(a - b) for a, b in zip(size, reference_sizes[key]))
                    for key, size in sizes[backend, t].items()
                ]
                self.canvas_checks.append(
                    {
                        "repetition": self.repetition(path),
                        "backend": backend,
                        "threads": t,
                        "pages": len(deltas),
                        "mismatches_gt_2px": sum(x > 2 for x in deltas),
                        "worst_delta_px": max(deltas),
                    }
                )
        return result

    def load(self) -> list[Measurement]:
        measurements = []
        for path in sorted((self.root / "native").rglob("*.csv")):
            measurements.extend(self.native(path))
        for path in sorted((self.root / "replay").glob("r*/*/report.json")):
            measurements.extend(self.replay(path))
        for path in sorted((self.root / "comparison").glob("r*/*/*.csv")):
            measurements.extend(self.comparison(path))
        actual = [(m.group, m.repetition) for m in measurements]
        require(len(actual) == len(set(actual)), "Duplicate measurement/repetition")
        expected = set()
        for r in range(1, self.metadata["repeats"] + 1):
            for t in self.metadata["threads"]:
                for profile in self.native_profiles:
                    expected.add((("native", "docling-parse", profile, t), r))
                expected.add((("replay", "threaded", "render", t), r))
                if self.metadata["comparison"]:
                    for task in ("parse", "parse+render"):
                        expected.add((("comparison", "docling-parse", task, t), r))
            expected.add((("replay", "pdfium", "render", 1), r))
            if self.metadata["comparison"]:
                for backend in (
                    "pymupdf",
                    "pypdfium2",
                    "pdfplumber",
                    "pdfminer.six",
                    "pypdf",
                ):
                    for task in (
                        ("parse",)
                        if backend in ("pdfminer.six", "pypdf")
                        else ("parse", "parse+render")
                    ):
                        expected.add((("comparison", backend, task, 1), r))
        require(
            set(actual) == expected,
            f"Missing/unexpected measurements: missing={expected - set(actual)}, extra={set(actual) - expected}",
        )
        require(all(m.wall_s > 0 for m in measurements), "Nonpositive corpus wall time")
        return measurements


def summarize(measurements: list[Measurement]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str, int], list[Measurement]] = defaultdict(list)
    for measurement in measurements:
        groups[measurement.group].append(measurement)
    medians = {
        key: statistics.median(m.wall_s for m in values)
        for key, values in groups.items()
    }
    result = []
    for key, values in sorted(groups.items()):
        workload, backend, task, threads = key
        wall = medians[key]
        docling_backend = "threaded" if workload == "replay" else "docling-parse"
        baseline = medians.get((workload, docling_backend, task, 1))
        speedup = baseline / wall if baseline is not None else None
        pdfium_backend = "pdfium" if workload == "replay" else "pypdfium2"
        pdfium_baseline = medians.get((workload, pdfium_backend, task, 1))
        result.append(
            {
                "workload": workload,
                "backend": backend,
                "task": task,
                "threads": threads,
                "repetitions": len(values),
                "documents": values[0].documents,
                "pages": values[0].pages,
                "median_wall_s": wall,
                "min_wall_s": min(m.wall_s for m in values),
                "max_wall_s": max(m.wall_s for m in values),
                "pages_per_s": values[0].pages / wall,
                "amortized_ms_per_page": 1000 * wall / values[0].pages,
                "speedup_vs_docling_1t": speedup,
                "speedup_vs_pypdfium2": pdfium_baseline / wall
                if pdfium_baseline is not None
                else None,
                "efficiency_pct": 100 * speedup / threads
                if speedup is not None and backend in ("docling-parse", "threaded")
                else None,
                "errors": 0,
                "timing_basis": values[0].timing_basis,
            }
        )
    return result


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        return
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def write_report(
    inputs: ReportInputs, measurements: list[Measurement], output: Path
) -> None:
    output.mkdir(parents=True, exist_ok=True)
    summary = summarize(measurements)
    write_csv(output / "summary.csv", summary)
    write_csv(
        output / "runs.csv",
        [
            {
                "workload": m.workload,
                "backend": m.backend,
                "task": m.task,
                "threads": m.threads,
                "repetition": m.repetition,
                "wall_s": m.wall_s,
                "pages": m.pages,
                "documents": m.documents,
                "source": m.source,
                "timing_basis": m.timing_basis,
            }
            for m in measurements
        ],
    )
    for workload in ("native", "replay"):
        write_csv(
            output / f"{workload}_phases.csv",
            [
                {
                    "backend": m.backend,
                    "task": m.task,
                    "threads": m.threads,
                    "repetition": m.repetition,
                    **m.phases,
                }
                for m in measurements
                if m.workload == workload
            ],
        )
    write_csv(output / "canvas_checks.csv", inputs.canvas_checks)
    meta = inputs.metadata
    lines = [
        "# Performance measurement report",
        "",
        f"Source: `{meta['source_revision']}`; QPDF `{meta['qpdf']}`.",
        "",
        f"Machine: {meta.get('cpu', meta['machine'])}; {meta['platform']}.",
        "",
        f"Dataset: `{meta['dataset']['repository']}` at `{meta['dataset']['revision']}`; "
        f"{len(inputs.files)} PDFs, {len(inputs.expected)} pages.",
        "",
        "See `../metadata.json`, `../commands.log` and `runs.csv` for provenance and raw inputs.",
        "",
        "Values are medians across repetitions, with observed minimum-maximum ranges. "
        "One repetition is a single observation, not a variability estimate. "
        "Speedup vs pypdfium2 uses its one-worker median wall time divided by the row's wall time. "
        "Speedup vs docling-parse uses its one-thread median divided by the row's wall time. "
        "References never cross workloads or tasks; efficiency uses the docling-parse baseline.",
        "",
    ]
    for workload in ("native", "replay", "comparison"):
        rows = [row for row in summary if row["workload"] == workload]
        if not rows:
            continue
        lines += [
            f"## {WORKLOAD_LABELS[workload]}",
            "",
            rows[0]["timing_basis"] + ".",
            "",
            "| Backend | Task | Threads | Runs | Wall median (s) | Min-max (s) | Pages/s | "
            + ("Speedup vs pypdfium2 (1t) | " if workload != "native" else "")
            + "Speedup vs docling-parse (1t) | Efficiency |",
            "|---|---|---:|---:|---:|---:|---:|"
            + ("---:|" if workload != "native" else "")
            + "---:|---:|",
        ]
        for row in rows:
            speedup = (
                f"{row['speedup_vs_docling_1t']:.2f}x"
                if row["speedup_vs_docling_1t"] is not None
                else "—"
            )
            pdfium_speedup = (
                f"{row['speedup_vs_pypdfium2']:.2f}x"
                if row["speedup_vs_pypdfium2"] is not None
                else "—"
            )
            efficiency = (
                f"{row['efficiency_pct']:.1f}%"
                if row["efficiency_pct"] is not None
                else "—"
            )
            lines.append(
                f"| {BACKEND_LABELS.get(row['backend'], row['backend'])} "
                f"| {task_title(workload, row['task'])} | {row['threads']} | {row['repetitions']} "
                f"| {row['median_wall_s']:.3f} | {row['min_wall_s']:.3f}-{row['max_wall_s']:.3f} "
                f"| {row['pages_per_s']:.1f} | "
                + (f"{pdfium_speedup} | " if workload != "native" else "")
                + f"{speedup} | {efficiency} |"
            )
        lines += [""]
    if inputs.canvas_checks:
        lines += [
            "## Comparison canvas sizes",
            "",
            "Against PDFium with a two-pixel tolerance. Matching dimensions do not establish pixel equivalence.",
            "",
            "| Repetition | Backend | Threads | Pages | Mismatches >2 px | Worst delta (px) |",
            "|---:|---|---:|---:|---:|---:|",
        ]
        lines += [
            f"| {r['repetition']} | {r['backend']} | {r['threads']} | {r['pages']} "
            f"| {r['mismatches_gt_2px']} | {r['worst_delta_px']} |"
            for r in inputs.canvas_checks
        ]
        lines += [""]
    figures = plot(measurements, summary, output)
    lines += figure_sections(inputs.metadata, measurements, figures)
    (output / "summary.md").write_text("\n".join(lines) + "\n")


def reproduction_commands(meta: dict[str, Any], workload: str, task: str) -> list[str]:
    """Recreate one profile's measurements in fresh processes and output paths."""
    root = meta["dataset"].get("root") or str(
        Path(meta["dataset"]["files"][0]["path"]).parent
    )
    threads = " ".join(str(int(t)) for t in meta["threads"])
    revision = shlex.quote(meta["dataset"]["revision"])
    scale = str(float(meta["scale"]))
    queue = str(int(meta["max_concurrent_results"]))
    lines = [
        "set -euo pipefail",
        f"PDF_ROOT={shlex.quote(root)}",
        'REPRO_OUT=$(mktemp -d "$PWD/scratch-performance-reproduce-XXXXXX")',
        f"for ((r=1; r<={int(meta['repeats'])}; r++)); do",
    ]
    if workload == "native":
        flags = NATIVE_PROFILES.get(task, NATIVE_PROFILES["render-with-text"])
        words = str(flags["create_word_cells"]).lower()
        line_cells = str(flags["create_line_cells"]).lower()
        lines += [
            f"  for t in {threads}; do",
            '    ./build/run_scaling.exe "$PDF_ROOT" -v WARNING --recursive --mode render \\',
            f'      --threads "$t" --max-concurrent-results {queue} --scale {scale} \\',
            f"      --create-word-cells={words} --create-line-cells={line_cells} \\",
            '      --enable-timing --timing-csv "$REPRO_OUT/r${r}-t${t}.csv" \\',
            '      2>&1 | tee "$REPRO_OUT/r${r}-t${t}.log"',
            "  done",
        ]
    elif workload == "replay":
        command = "uv run --no-sync python scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py"
        lines += [
            f'  {command} "$PDF_ROOT" \\',
            f"    --backend pdfium --dataset-revision {revision} \\",
            '    --output-dir "$REPRO_OUT/r${r}-pdfium" 2>&1 | tee "$REPRO_OUT/r${r}-pdfium.log"',
            f"  for t in {threads}; do",
            f'    {command} "$PDF_ROOT" \\',
            f'      --backend threaded --threads "$t" --dataset-revision {revision} \\',
            '      --output-dir "$REPRO_OUT/r${r}-t${t}" 2>&1 | tee "$REPRO_OUT/r${r}-t${t}.log"',
            "  done",
        ]
    else:
        render = task == "parse+render"
        mode = "render" if render else "parse-without-bitmap"
        scene = str(render).lower()
        thread_csv = ",".join(str(int(t)) for t in meta["threads"])
        lines += [
            '  uv run --no-sync python scripts/benchmarking/run_performance_benchmarking.py "$PDF_ROOT" \\',
            f"    --recursive --mode {mode} --threads {thread_csv} --scale {scale} --max-concurrent-results {queue} \\",
            "    --3rd-party-backends all --only-threaded \\",
            "    --keep-char-cells=true --create-word-cells=false --create-line-cells=false \\",
            f"    --keep-shapes={scene} --keep-bitmaps={scene} \\",
            "    --materialize-char-cells=true --materialize-word-cells=false \\",
            "    --materialize-line-cells=false --materialize-shapes=false --materialize-bitmaps=false \\",
            '    --output-dir "$REPRO_OUT/r${r}" 2>&1 | tee "$REPRO_OUT/r${r}.log"',
        ]
    return [*lines, "done"]


def figure_sections(
    meta: dict[str, Any], measurements: list[Measurement], figures: list[Path]
) -> list[str]:
    lines = [
        "## Figures",
        "",
        "These plots separate native rendering from Python output construction. "
        "Each profile has its own one-thread baseline; speedup and efficiency never mix profiles.",
        "",
        "Run the commands from the repository root with the source revision, native build and "
        "Python packages recorded above. Each reproduction block below covers all plots in its "
        "profile, uses the recorded thread counts and repetitions, and creates fresh output paths. "
        "The native and replay sweeps use a fresh process per thread count, as in the launcher.",
        "",
        "To collect a complete new bundle and regenerate all figures:",
        "",
        "```bash",
        f"DATASET={shlex.quote(meta['dataset']['repository'].rsplit('-', 1)[-1])} "
        f"DATASET_REVISION={shlex.quote(meta['dataset']['revision'])} \\",
        f"  REPEATS={int(meta['repeats'])} THREADS={','.join(str(int(t)) for t in meta['threads'])} "
        f"RUN_COMPARISON={int(meta['comparison'])} \\",
        "  bash scripts/benchmarking/refresh_performance_benchmarks.sh",
        "```",
        "",
    ]
    names = {figure.name for figure in figures}
    for workload in ("native", "replay", "comparison"):
        tasks = sorted({m.task for m in measurements if m.workload == workload})
        if not tasks:
            continue
        lines += [f"### {WORKLOAD_LABELS[workload]}", ""]
        for task in tasks:
            title = task_title(workload, task)
            if workload == "native":
                extra = (
                    "Word/line cell construction is disabled."
                    if task == "render-only"
                    else "Word/line text cells and their geometry are also constructed in native memory."
                )
                description = (
                    "Each page is decoded, rasterized and copied into a native RGBA canvas. "
                    + extra
                    + " "
                    "Characters and bitmap geometry needed for drawing are decoded in both profiles. "
                    "Python text models and separate bitmap bounding-box outputs are not materialized. "
                    "Document loading is outside the native wall-time boundary."
                )
            elif workload == "replay":
                description = (
                    "This profile follows Docling settings: it creates RGB images, consumes word/line "
                    "text geometry, materializes bitmap bounding boxes and queries reading-order "
                    "separators. Text segmentation differs between PDFium and docling-parse. "
                    "The timer includes per-document parser construction/loading, processing and unloading."
                )
            else:
                description = (
                    "This API comparison extracts text with positions using each package's supported "
                    "geometry API; Docling materializes character models with word/line aggregation disabled. "
                    + (
                        "Supported renderers also create page images. "
                        if task == "parse+render"
                        else "Page images are not requested. "
                    )
                    + "Separate bitmap bounding-box models are not materialized. Geometry granularity "
                    "differs across packages; the timer includes document loading."
                )
            lines += [
                f"#### {title}",
                "",
                description,
                "",
                "<details>",
                "<summary>Reproduce the measurements for these plots</summary>",
                "",
                "```bash",
                *reproduction_commands(meta, workload, task),
                "```",
                "",
                "</details>",
                "",
            ]
            captions = {
                "runtime": (
                    "Wall time and throughput",
                    "The left panel shows corpus wall time; the right shows pages per second. "
                    "Points are medians, and wall-time bars show the observed minimum and maximum across repetitions.",
                ),
                "scaling": (
                    "Speedup and parallel efficiency",
                    "Speedup divides this profile's one-thread median by its median at each thread count. "
                    "Efficiency divides that speedup by the thread count. Dashed lines show ideal scaling.",
                ),
                "phases": (
                    "Where the time goes",
                    (
                        "These bars sum page worker elapsed by stage. Parallel workers overlap, so the stacked total "
                        "is not corpus wall time or CPU time."
                        if workload == "native"
                        else "These bars separate document construction/loading, page processing and unloading."
                    )
                    + " Each profile uses the measured repetition nearest its median wall time.",
                ),
                "distribution": (
                    "Duration distribution",
                    (
                        "Page worker durations"
                        if workload == "native"
                        else "Whole-document durations"
                    )
                    + " are pooled across repetitions on logarithmic axes. The long tail identifies uneven work; "
                    "it does not by itself identify the costliest internal operation.",
                ),
            }
            for kind, (heading, caption) in captions.items():
                filename = f"{figure_prefix(workload, task)}_{kind}.png"
                if filename in names:
                    lines += [
                        f"##### {heading}",
                        "",
                        caption,
                        "",
                        f"![{title}: {heading}]({filename})",
                        "",
                    ]
    return lines


def plot(
    measurements: list[Measurement], summary: list[dict[str, Any]], output: Path
) -> list[Path]:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    figures = []

    def save(fig: Any, name: str) -> None:
        path = output / f"{name}.png"
        fig.tight_layout()
        fig.savefig(path, dpi=180)
        plt.close(fig)
        figures.append(path)

    for workload in ("native", "replay", "comparison"):
        tasks = sorted({r["task"] for r in summary if r["workload"] == workload})
        for task in tasks:
            rows = [
                r for r in summary if r["workload"] == workload and r["task"] == task
            ]
            suffix = figure_prefix(workload, task)
            fig, axes = plt.subplots(1, 2, figsize=(11, 4))
            for backend in sorted({r["backend"] for r in rows}):
                group = sorted(
                    (r for r in rows if r["backend"] == backend),
                    key=lambda r: r["threads"],
                )
                x = [r["threads"] for r in group]
                wall = [r["median_wall_s"] for r in group]
                error = [
                    [r["median_wall_s"] - r["min_wall_s"] for r in group],
                    [r["max_wall_s"] - r["median_wall_s"] for r in group],
                ]
                axes[0].errorbar(
                    x,
                    wall,
                    yerr=error,
                    marker="o",
                    capsize=3,
                    label=BACKEND_LABELS.get(backend, backend),
                )
                axes[1].plot(
                    x,
                    [r["pages_per_s"] for r in group],
                    "o-",
                    label=BACKEND_LABELS.get(backend, backend),
                )
            for ax, ylabel in zip(axes, ("Corpus wall time (s)", "Pages per second")):
                ax.set(
                    xlabel="Threads",
                    ylabel=ylabel,
                    xticks=sorted({r["threads"] for r in rows}),
                )
                ax.grid(alpha=0.25)
                ax.legend()
            fig.suptitle(plot_title(workload, task), fontsize=12)
            save(fig, suffix + "_runtime")
            scalable = [r for r in rows if r["efficiency_pct"] is not None]
            fig, axes = plt.subplots(1, 2, figsize=(11, 4))
            for backend in sorted({r["backend"] for r in scalable}):
                group = sorted(
                    (r for r in scalable if r["backend"] == backend),
                    key=lambda r: r["threads"],
                )
                x = [r["threads"] for r in group]
                axes[0].plot(
                    x,
                    [r["speedup_vs_docling_1t"] for r in group],
                    "o-",
                    label=BACKEND_LABELS.get(backend, backend),
                )
                axes[1].plot(
                    x,
                    [r["efficiency_pct"] for r in group],
                    "o-",
                    label=BACKEND_LABELS.get(backend, backend),
                )
            ticks = sorted({r["threads"] for r in scalable})
            axes[0].plot(ticks, ticks, "--", color="gray", label="Ideal")
            axes[1].axhline(100, linestyle="--", color="gray")
            for ax, ylabel in zip(
                axes, ("Speedup relative to 1 thread", "Parallel efficiency (%)")
            ):
                ax.set(xlabel="Threads", ylabel=ylabel, xticks=ticks)
                ax.grid(alpha=0.25)
                ax.legend()
            fig.suptitle(plot_title(workload, task), fontsize=12)
            save(fig, suffix + "_scaling")
    for workload in ("native", "replay"):
        for task in sorted({m.task for m in measurements if m.workload == workload}):
            selected = [
                m for m in measurements if m.workload == workload and m.task == task
            ]
            groups: dict[tuple[str, int], list[Measurement]] = defaultdict(list)
            for m in selected:
                groups[m.backend, m.threads].append(m)
            representatives = []
            for key in sorted(groups):
                median = statistics.median(m.wall_s for m in groups[key])
                representatives.append(
                    min(
                        groups[key],
                        key=lambda m: (abs(m.wall_s - median), m.repetition),
                    )
                )
            fig, ax = plt.subplots(figsize=(10, 5))
            bottom = np.zeros(len(representatives))
            for phase in sorted({k for m in selected for k in m.phases}):
                phase_values = np.array(
                    [m.phases.get(phase, 0.0) for m in representatives]
                )
                ax.bar(
                    range(len(phase_values)),
                    phase_values,
                    bottom=bottom,
                    label=phase.removesuffix("_s").replace("_", " "),
                )
                bottom += phase_values
            ax.set_xticks(range(len(representatives)))
            ax.set_xticklabels(
                [
                    f"{BACKEND_LABELS.get(m.backend, m.backend)}\n{m.threads}t"
                    for m in representatives
                ]
            )
            ax.set_ylabel(
                "Summed page worker elapsed (s)"
                if workload == "native"
                else "Summed document phase time (s)"
            )
            ax.set_title(plot_title(workload, task), fontsize=11)
            ax.legend(loc="upper left", bbox_to_anchor=(1, 1))
            save(fig, figure_prefix(workload, task) + "_phases")
            fig, ax = plt.subplots(figsize=(9, 5))
            for (backend, threads), values in sorted(groups.items()):
                times = [v for m in values for v in m.durations if v > 0]
                require(bool(times), "No positive durations for histogram")
                low, high = min(times), max(times)
                bins = np.geomspace(low, high * 1.001 if high > low else low * 1.01, 50)
                ax.hist(
                    times,
                    bins=bins.tolist(),
                    histtype="step",
                    label=f"{BACKEND_LABELS.get(backend, backend)}, {threads}t",
                )
            ax.set_xscale("log")
            ax.set_yscale("log")
            ax.set_xlabel(
                "Page worker elapsed (s)"
                if workload == "native"
                else "Document total (s)"
            )
            ax.set_ylabel("Observations pooled across repetitions")
            ax.set_title(plot_title(workload, task), fontsize=11)
            ax.legend()
            save(fig, figure_prefix(workload, task) + "_distribution")
    return figures


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input", type=Path, help="Launcher output containing metadata.json"
    )
    parser.add_argument("--output-dir", type=Path, help="Default: INPUT/report")
    args = parser.parse_args()
    try:
        inputs = ReportInputs(args.input.resolve())
        measurements = inputs.load()
        output = args.output_dir or inputs.root / "report"
        write_report(inputs, measurements, output)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Cannot produce a complete benchmark report: {error}\n")
    print(
        f"Validated {len(measurements)} corpus measurements. Report: {output / 'summary.md'}"
    )


if __name__ == "__main__":
    main()
