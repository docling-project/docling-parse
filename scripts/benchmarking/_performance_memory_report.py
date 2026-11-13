"""Render observed process peaks and independent RSS traces; never estimate gaps."""

from __future__ import annotations

import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any

from tabulate import tabulate


def timeline_rows(path: Path, memory: dict[str, Any]) -> list[tuple[float, int]]:
    result = []
    previous = -1.0
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            elapsed, rss, pid = (
                float(row["elapsed_s"]),
                int(row["rss_bytes"]),
                int(row["pid"]),
            )
            if (
                not math.isfinite(elapsed)
                or elapsed < previous
                or elapsed < 0
                or rss < 0
                or pid != memory["pid"]
            ):
                raise ValueError(f"Invalid RSS sample: {path}")
            result.append((elapsed, rss))
            previous = elapsed
    maximum = max((rss for _, rss in result), default=None)
    if (
        len(result) != memory["sample_count"]
        or maximum != memory["sampled_max_rss_bytes"]
    ):
        raise ValueError(f"RSS timeline summary mismatch: {path}")
    return result


def write_memory_report(
    measurements: list[Any], output: Path, *, include_traces: bool = True
) -> list[str]:
    groups: dict[tuple, list[Any]] = defaultdict(list)
    runs = []
    for m in measurements:
        groups[m.group].append(m)
        memory = m.memory or {}
        runs.append(
            {
                "workload": m.workload,
                "backend": m.backend,
                "task": m.task,
                "threads": m.threads,
                "repetition": m.repetition,
                "peak_rss_bytes": memory.get("peak_rss_bytes"),
                "sampled_max_rss_bytes": memory.get("sampled_max_rss_bytes"),
                "sample_count": memory.get("sample_count"),
                "mode": memory.get("mode", "unavailable"),
                "method": memory.get("method", ""),
                "scope": memory.get("scope", ""),
                "pid": memory.get("pid"),
                "interval_ms": memory.get("interval_ms"),
                "source": memory.get("_source", ""),
            }
        )
    summaries = []
    for (workload, backend, task, threads), values in sorted(groups.items()):
        observed = [m.memory for m in values if m.memory is not None]
        signatures = {
            (m["mode"], m.get("interval_ms"), m["scope"], m["method"]) for m in observed
        }
        if len(signatures) > 1:
            raise ValueError(
                f"Mixed memory instrumentation in series: {(workload, backend, task, threads)}"
            )
        peaks = [m["peak_rss_bytes"] / 2**20 for m in observed]
        complete = len(peaks) == len(values)
        summaries.append(
            {
                "workload": workload,
                "backend": backend,
                "task": task,
                "threads": threads,
                "runs": len(values),
                "observed_peaks": len(peaks),
                "median_peak_mib": statistics.median(peaks) if complete else None,
                "min_peak_mib": min(peaks) if complete else None,
                "max_peak_mib": max(peaks) if complete else None,
                "memory_mode": observed[0]["mode"] if observed else "unavailable",
            }
        )
    for name, rows in (("memory_runs.csv", runs), ("memory_summary.csv", summaries)):
        if rows:
            with (output / name).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)

    def display(value: float | None) -> str:
        return "unavailable" if value is None else f"{value:.2f}"

    lines = [
        "## Process Memory",
        "",
        "OS high-water marks cover one backend process and all its threads, including startup, "
        "loading and teardown. They exclude the monitor/logger and are not parser-only heap sizes. "
        "Timing retains each workload's existing boundaries. Missing observations are unavailable, never zero.",
        "",
        tabulate(
            [
                [
                    r["workload"],
                    r["backend"],
                    r["task"],
                    r["threads"],
                    f"{r['observed_peaks']}/{r['runs']}",
                    display(r["median_peak_mib"]),
                    display(r["min_peak_mib"]),
                    display(r["max_peak_mib"]),
                    r["memory_mode"],
                ]
                for r in summaries
            ],
            headers=[
                "Workload",
                "Backend",
                "Task",
                "Threads",
                "Peaks/runs",
                "Median MiB",
                "Min MiB",
                "Largest peak MiB",
                "Capture",
            ],
            tablefmt="github",
            disable_numparse=True,
        ),
        "",
        "`existing` denotes a previously recorded replay RUSAGE_SELF peak. Reused runs have no new timeline. "
        "Peak/profile comparison series run in fresh processes; memory-off comparisons retain their historical "
        "shared-process execution. Compare timing only under matching instrumentation and isolation. "
        "Sampled maxima are separate from OS peaks and can miss short spikes. See memory_runs.csv for methods and scopes.",
        "",
        "[Elapsed-time memory traces](memory_traces.md).",
        "",
    ]
    trace_sections: list[str] = []

    def write_traces() -> None:
        trace_lines = [
            "# Memory traces",
            "",
            "[Back to Process Memory](summary.md#process-memory).",
            "",
            "Current RSS is sampled over each process's own elapsed-time clock. "
            "Repetitions are plotted independently, without averaging. Samples can miss "
            "short spikes; OS peak measurements are in the summary.",
            "",
            *(
                trace_sections
                or ["No sampled RSS traces are available for this report.", ""]
            ),
        ]
        (output / "memory_traces.md").write_text("\n".join(trace_lines) + "\n")

    if not any(m.memory is not None for m in measurements):
        write_traces()
        return lines
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    def save(fig: Any, filename: str, caption: str) -> list[str]:
        fig.tight_layout()
        fig.savefig(output / filename, dpi=180)
        plt.close(fig)
        return [f"![{caption}]({filename})", ""]

    by_task: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in summaries:
        if row["median_peak_mib"] is not None:
            by_task[row["workload"], row["task"]].append(row)
    for (workload, task), rows in sorted(by_task.items()):
        fig, ax = plt.subplots(figsize=(8, 4))
        for backend in sorted({r["backend"] for r in rows}):
            selected = sorted(
                (r for r in rows if r["backend"] == backend), key=lambda r: r["threads"]
            )
            ax.errorbar(
                [r["threads"] for r in selected],
                [r["median_peak_mib"] for r in selected],
                yerr=[
                    [r["median_peak_mib"] - r["min_peak_mib"] for r in selected],
                    [r["max_peak_mib"] - r["median_peak_mib"] for r in selected],
                ],
                marker="o",
                capsize=3,
                label=backend,
            )
        ax.set(
            xlabel="Threads",
            ylabel="OS peak RSS (MiB)",
            title=f"{workload}: {task}",
            xticks=sorted({0, *(r["threads"] for r in rows)}),
        )
        ax.set_xlim(left=0)
        ax.set_ylim(bottom=0)
        ax.legend()
        lines += save(
            fig,
            f"memory_{workload}_{task.replace('+', '_')}.png",
            "Process peak RSS; median and observed range",
        )
    if not include_traces:
        return [
            line.replace(
                "See memory_runs.csv for methods and scopes.",
                "Methods and scopes are recorded in publication provenance.",
            )
            for line in lines
            if "memory_traces.md" not in line
        ]
    for (workload, backend, task, threads), values in sorted(groups.items()):
        profiled = [m for m in values if m.memory and m.memory.get("_timeline_path")]
        if not profiled:
            continue
        fig, ax = plt.subplots(figsize=(8, 4))
        found = False
        for m in profiled:
            memory = m.memory
            trace = timeline_rows(Path(memory["_timeline_path"]), memory)
            if trace:
                ax.plot(
                    [t for t, _ in trace],
                    [rss / 2**20 for _, rss in trace],
                    label=f"r{m.repetition}",
                )
                found = True
        if not found:
            plt.close(fig)
            continue
        ax.set(
            xlabel="Elapsed process time (s)",
            ylabel="Sampled current RSS (MiB)",
            title=f"{workload}: {backend}, {task}, {threads} thread(s)",
        )
        ax.legend()
        trace_sections += [
            f"## {workload}: {backend}, {task}, {threads} thread(s)",
            "",
        ]
        trace_sections += save(
            fig,
            f"rss_{workload}_{backend}_{task.replace('+', '_')}_t{threads}.png",
            "Current RSS over independent process clocks; repetitions are not averaged",
        )
    write_traces()
    return lines
