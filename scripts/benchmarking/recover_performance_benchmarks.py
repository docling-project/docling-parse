#!/usr/bin/env python3
"""Resume missing configurations, reusing validated results in a new bundle."""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from _performance_memory import add_memory_arguments
from _performance_runtime import (
    ROOT,
    atomic_json,
    digest,
    reference_preflight,
    runtime_provenance,
)
from run_performance_report import ReportInputs, write_report


def run_logged(
    command: list[str], path: Path, commands_path: Path | None = None
) -> None:
    print(f"\nRunning: {shlex.join(command)}\nLog: {path}", flush=True)
    with (commands_path or path.parent / "commands.log").open("a") as stream:
        stream.write(shlex.join(command) + "\n")
    with path.open("wb") as log:
        process = subprocess.Popen(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        assert process.stdout is not None
        with process.stdout:
            while chunk := os.read(process.stdout.fileno(), 16384):
                log.write(chunk)
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
        code = process.wait()
    if code:
        raise RuntimeError(
            f"Command failed ({code}); completed checkpoints are preserved: {path}"
        )


def validate_environment(metadata: dict[str, Any], provenance: dict[str, Any]) -> None:
    for key in ("extension_sha256", "python", "platform", "machine"):
        if metadata[key] != provenance[key]:
            raise ValueError(
                f"Recovery cannot reuse core results: {key} changed; collect a fresh full bundle"
            )
    if metadata.get("cpu") and metadata["cpu"] != provenance["cpu"]:
        raise ValueError("Recovery CPU differs from the original run")
    if digest(ROOT / "build/run_scaling.exe") != metadata["native_sha256"]:
        raise ValueError(
            "Native executable differs from the original run; collect a fresh full bundle"
        )
    header = (ROOT / "externals/include/qpdf/DLL.h").read_text()
    version = re.search(r'^#\s*define QPDF_VERSION "([^"+]+)"', header, re.M)
    if version is None or version[1] != metadata["qpdf"]:
        raise ValueError("Bundled QPDF version differs from the original run")
    for package in ("docling-parse", "docling-core", "pypdfium2", "pillow", "rtree"):
        if metadata["packages"][package] != provenance["packages"][package]:
            raise ValueError(
                f"Core package {package} changed; collect a fresh full bundle"
            )


@dataclass(frozen=True)
class CoreJob:
    workload: str
    profile: str
    threads: int
    repetition: int
    artifact: Path

    @property
    def key(self) -> tuple:
        backend = (
            "docling-parse"
            if self.workload == "native"
            else ("pdfium" if self.profile == "pdfium" else "threaded")
        )
        task = self.profile if self.workload == "native" else "render"
        return ((self.workload, backend, task, self.threads), self.repetition)


def core_jobs(metadata: dict[str, Any]) -> list[CoreJob]:
    jobs = []
    for repetition in range(1, metadata["repeats"] + 1):
        for profile in metadata.get("native_profiles", {"render": {}}):
            for threads in metadata["threads"]:
                parent = (
                    Path("native") / profile if profile != "render" else Path("native")
                )
                jobs.append(
                    CoreJob(
                        "native",
                        profile,
                        threads,
                        repetition,
                        parent / f"r{repetition}/t{threads}.csv",
                    )
                )
        for backend, threads in [
            ("pdfium", 1),
            *(("threaded", t) for t in metadata["threads"]),
        ]:
            name = "pdfium" if backend == "pdfium" else f"t{threads}"
            jobs.append(
                CoreJob(
                    "replay",
                    backend,
                    threads,
                    repetition,
                    Path(f"replay/r{repetition}/{name}/report.json"),
                )
            )
    return jobs


def inspect_job(inputs: ReportInputs, job: CoreJob) -> bool:
    path = inputs.root / job.artifact
    if not path.is_file():
        print(f"Pending: {job.artifact} (no completed artifact)", flush=True)
        return False
    try:
        values = (
            inputs.native(path) if job.workload == "native" else inputs.replay(path)
        )
        if [(m.group, m.repetition) for m in values] != [job.key] or any(
            m.wall_s <= 0 for m in values
        ):
            raise ValueError("Unexpected configuration or nonpositive wall time")
    except (ValueError, KeyError, OSError, TypeError) as exc:
        # Source changes are a provenance violation, not an interrupted measurement.
        if any(
            word in str(exc)
            for word in ("source revision", "source diff", "script hash")
        ):
            raise
        print(f"Pending: {job.artifact} (incomplete/invalid: {exc})", flush=True)
        return False
    print(f"Reuse: {job.artifact}", flush=True)
    return True


def link_job(source: Path, output: Path, job: CoreJob) -> None:
    artifact = source / job.artifact
    if job.workload == "replay":
        candidates = [
            artifact.parent,
            *artifact.parent.parent.glob(artifact.parent.name + ".memory.*"),
        ]
    else:
        candidates = list(artifact.parent.glob(artifact.stem + ".*"))
    for path in candidates:
        dest = output / path.relative_to(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.symlink_to(path.resolve(), target_is_directory=path.is_dir())


def validate_resume_scripts(metadata: dict[str, Any], jobs: list[CoreJob]) -> None:
    names = set()
    if any(j.workload == "replay" for j in jobs):
        names.add("run_performance_benchmarking_with_docling_settings.py")
    if jobs and metadata.get("memory_collection", {}).get("mode", "off") != "off":
        names.add("_performance_memory.py")
    for name in names:
        recorded = metadata.get("script_sha256", {}).get("scripts/benchmarking/" + name)
        if recorded is None or digest(ROOT / "scripts/benchmarking" / name) != recorded:
            raise ValueError(
                f"Cannot resume core measurements: runner {name} changed or its original hash is unavailable"
            )
    for key, value in metadata.get("environment", {}).items():
        if key != "SDKROOT" and os.environ.get(key) != value:
            raise ValueError(
                f"Cannot resume core measurements: environment {key} changed"
            )


def prepare_recovery(
    source: Path,
    output: Path,
    provenance: dict[str, Any],
    memory: dict[str, Any] | None = None,
) -> dict[str, Any]:
    inputs = ReportInputs(source)
    metadata = copy.deepcopy(inputs.metadata)
    validate_environment(metadata, provenance)
    jobs = core_jobs(metadata)
    print("Inspecting native and Python replay configurations ...", flush=True)
    complete = [job for job in jobs if inspect_job(inputs, job)]
    pending = [job for job in jobs if job not in complete]
    if pending:
        validate_resume_scripts(metadata, pending)
        if memory is not None and memory != metadata.get(
            "memory_collection", {"mode": "off", "interval_ms": None}
        ):
            raise ValueError(
                "Cannot change memory settings while resuming core measurements; omit flags to inherit the original settings"
            )
    print(
        f"Recovery plan: {len(complete)} core configurations reused, {len(pending)} to run.",
        flush=True,
    )
    print("Verifying original dataset hashes ...", flush=True)
    for item in metadata["dataset"]["files"]:
        if digest(Path(item["path"])) != item["sha256"]:
            raise ValueError(
                f"Recovery dataset differs from original manifest: {item['name']}"
            )
    if metadata["comparison"]:
        metadata["reference_preflight"] = reference_preflight(metadata)
    shutil.copy2(source / "metadata.json", output / "original-metadata.json")
    for name in ("CMakeCache.txt", "native-flags.make"):
        if (source / name).is_file():
            shutil.copy2(source / name, output / name)
    for job in complete:
        link_job(source, output, job)
    # Artifact-specific provenance keeps old replay source identities intact.
    recovered = metadata.setdefault("recovered_replay_provenance", {})
    for job in pending:
        if job.workload == "replay":
            recovered[str(job.artifact)] = provenance
    compatible_memory = memory is None or metadata.get("comparison_memory") == memory
    if (
        metadata["comparison"]
        and metadata.get("comparison_provenance") == provenance
        and compatible_memory
    ):
        for directory in sorted((source / "comparison").glob("r*/*/checkpoints")):
            target = output / directory.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(directory, target)
            memory_source = directory.parent / "memory"
            if memory_source.is_dir():
                shutil.copytree(memory_source, target.parent / "memory")
    if metadata["comparison"]:
        if memory is not None:
            metadata["comparison_memory"] = memory
        metadata["comparison_provenance"] = provenance
    metadata["recovery"] = {
        "source_bundle": str(source),
        "created_at": datetime.now(timezone.utc).isoformat(),
        "core_results": "completed configurations reused; missing/incomplete configurations resumed",
        "pending_core": [str(j.artifact) for j in pending],
        "comparisons": "current provenance; compatible checkpoints only"
        if metadata["comparison"]
        else "not requested",
    }
    atomic_json(output / "metadata.json", metadata)
    return metadata


def core_command(
    metadata: dict[str, Any], job: CoreJob, output: Path, compact: bool
) -> tuple[list[str], Path]:
    artifact = output / job.artifact
    if job.workload == "native":
        log = artifact.with_suffix(".log")
        command = [
            str(ROOT / "build/run_scaling.exe"),
            metadata["dataset"]["root"],
            "--recursive",
            "--mode",
            "render",
            "--threads",
            str(job.threads),
            "--max-concurrent-results",
            str(metadata["max_concurrent_results"]),
            "--scale",
            str(metadata["scale"]),
            "--enable-timing",
            "--timing-csv",
            str(artifact),
        ]
        settings = metadata.get("native_profiles", {}).get(
            job.profile, {"create_word_cells": True, "create_line_cells": True}
        )
        command += [
            f"--{name.replace('_', '-')}={str(value).lower()}"
            for name, value in settings.items()
        ]
        if compact:
            command += ["-v", "WARNING", "--suppress-config", "--suppress-timings"]
    else:
        log = artifact.parent.with_suffix(".log")
        command = [
            sys.executable,
            str(
                ROOT
                / "scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py"
            ),
            metadata["dataset"]["root"],
            "--backend",
            job.profile,
            "--dataset-revision",
            metadata["dataset"]["revision"],
            "--output-dir",
            str(artifact.parent),
        ]
        if job.profile == "threaded":
            command += ["--threads", str(job.threads)]
        if compact:
            command.append("--suppress-config")
    memory = metadata.get("memory_collection", {"mode": "off"})
    if memory["mode"] != "off":
        command = [
            sys.executable,
            str(ROOT / "scripts/benchmarking/_performance_memory.py"),
            "--memory-mode",
            memory["mode"],
            "--memory-interval-ms",
            str(memory.get("interval_ms") or 50),
            "--output",
            str(log.with_suffix(".memory.json")),
            "--",
            *command,
        ]
    return command, log


def resume_core(metadata: dict[str, Any], output: Path, compact: bool) -> None:
    pending = set(metadata["recovery"]["pending_core"])
    for job in core_jobs(metadata):
        if str(job.artifact) not in pending:
            continue
        command, log = core_command(metadata, job, output, compact)
        log.parent.mkdir(parents=True, exist_ok=True)
        print(
            f"Resume: repetition {job.repetition}/{metadata['repeats']}, {job.workload}, {job.profile}, {job.threads} thread(s)",
            flush=True,
        )
        run_logged(command, log, output / "commands.log")
        if not inspect_job(ReportInputs(output), job):
            raise ValueError(f"Resumed configuration failed validation: {job.artifact}")


def comparison_command(
    metadata: dict[str, Any], directory: Path, mode: str, compact: bool
) -> list[str]:
    scene = str(mode == "render").lower()
    command = [
        sys.executable,
        str(ROOT / "scripts/benchmarking/run_performance_benchmarking.py"),
        metadata["dataset"]["root"],
        "--recursive",
        "--mode",
        mode,
        "--threads",
        ",".join(str(t) for t in metadata["threads"]),
        "--scale",
        str(metadata["scale"]),
        "--max-concurrent-results",
        str(metadata["max_concurrent_results"]),
        "--3rd-party-backends",
        "all",
        "--only-threaded",
        "--keep-char-cells=true",
        "--create-word-cells=false",
        "--create-line-cells=false",
        f"--keep-shapes={scene}",
        f"--keep-bitmaps={scene}",
        "--materialize-char-cells=true",
        "--materialize-word-cells=false",
        "--materialize-line-cells=false",
        "--materialize-shapes=false",
        "--materialize-bitmaps=false",
        "--checkpoint-dir",
        str(directory / "checkpoints"),
        "--resume",
        "--output-dir",
        str(directory),
    ]
    if compact:
        command.append("--suppress-config")
    if memory := metadata.get("comparison_memory"):
        command += ["--memory-mode", memory["mode"]]
        if memory["mode"] == "profile":
            command += ["--memory-interval-ms", str(memory["interval_ms"])]
    return command


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output-dir", type=Path)
    add_memory_arguments(parser)
    # None means inherit the source bundle, rather than silently switching to off.
    parser.set_defaults(
        memory_mode=os.environ.get("MEMORY_MODE"),
        memory_interval_ms=os.environ.get("MEMORY_INTERVAL_MS"),
    )
    args = parser.parse_args()
    source = args.source.resolve()
    output_mode = os.environ.get("BENCHMARK_OUTPUT", "compact")
    if output_mode not in ("compact", "full"):
        parser.error("BENCHMARK_OUTPUT must be compact or full")
    if not (source / "metadata.json").is_file():
        parser.error(f"Source bundle has no metadata.json: {source}")
    if args.output_dir:
        output = args.output_dir.resolve()
        if output.exists():
            parser.error("Recovery output already exists; choose a new directory")
        if (
            output.is_relative_to(source)
            or output.is_relative_to(ROOT / "build")
            or output.is_relative_to(ROOT / "externals")
        ):
            parser.error(
                "Recovery output must be outside the original bundle, build/ and externals/"
            )
        output.mkdir(parents=True)
    else:
        output = Path(
            tempfile.mkdtemp(prefix="scratch-performance-recovery-", dir=ROOT)
        )
    os.environ.setdefault("MPLCONFIGDIR", str(output / "matplotlib-cache"))
    print(
        f"Recovery results: {output}\nOriginal bundle: {source}\nKeep the original bundle: core artifacts are linked read-only by this workflow.",
        flush=True,
    )
    try:
        # Keep the installed local project and its binaries; only sync perf deps.
        run_logged(
            [
                "uv",
                "sync",
                "--python",
                sys.executable,
                "--inexact",
                "--no-install-project",
                "--group",
                "build",
                "--group",
                "dev",
                "--group",
                "perf",
            ],
            output / "dependencies.log",
        )
        provenance = runtime_provenance()
        original = json.loads((source / "metadata.json").read_text())
        memory = None
        if args.memory_mode is not None or args.memory_interval_ms is not None:
            saved = original.get(
                "memory_collection", {"mode": "off", "interval_ms": None}
            )
            mode = args.memory_mode or saved["mode"]
            memory = {
                "mode": mode,
                "interval_ms": (
                    args.memory_interval_ms or saved.get("interval_ms") or 50
                )
                if mode == "profile"
                else None,
            }
        metadata = prepare_recovery(source, output, provenance, memory)
        resume_core(metadata, output, output_mode == "compact")
        if metadata["comparison"]:
            for repetition in range(1, metadata["repeats"] + 1):
                for mode in ("parse-without-bitmap", "render"):
                    directory = output / "comparison" / f"r{repetition}" / mode
                    directory.mkdir(parents=True, exist_ok=True)
                    run_logged(
                        comparison_command(
                            metadata, directory, mode, output_mode == "compact"
                        ),
                        directory / "run.log",
                        output / "commands.log",
                    )
        inputs = ReportInputs(output)
        write_report(inputs, inputs.load(), output / "report")
    except (
        RuntimeError,
        ValueError,
        OSError,
        KeyError,
        subprocess.SubprocessError,
    ) as exc:
        retry = output if (output / "metadata.json").is_file() else source
        parser.exit(
            1,
            f"Recovery stopped: {exc}\nResults/checkpoints are preserved in {output}. "
            f"Use RECOVER_FROM={shlex.quote(str(retry))} to retry with matching provenance.\n",
        )
    print(f"Recovery complete: {output / 'report/summary.md'}", flush=True)


if __name__ == "__main__":
    main()
