#!/usr/bin/env python3
"""Reuse validated core results and recollect comparisons with explicit provenance."""

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


def prepare_recovery(
    source: Path,
    output: Path,
    provenance: dict[str, Any],
    memory: dict[str, Any] | None = None,
) -> dict[str, Any]:
    inputs = ReportInputs(source)
    metadata = copy.deepcopy(inputs.metadata)
    validate_environment(metadata, provenance)
    print("Validating completed native and Python replay results ...", flush=True)
    measurements = inputs.load(include_comparison=False)
    print(f"Validated {len(measurements)} reusable core measurements.", flush=True)
    print("Verifying original dataset hashes ...", flush=True)
    for item in metadata["dataset"]["files"]:
        if digest(Path(item["path"])) != item["sha256"]:
            raise ValueError(
                f"Recovery dataset differs from original manifest: {item['name']}"
            )
    preflight = reference_preflight(metadata)
    for name in ("native", "replay"):
        (output / name).symlink_to((source / name).resolve(), target_is_directory=True)
    shutil.copy2(source / "metadata.json", output / "original-metadata.json")
    for name in ("CMakeCache.txt", "native-flags.make"):
        if (source / name).is_file():
            shutil.copy2(source / name, output / name)
    old_provenance = metadata.get("comparison_provenance")
    compatible_memory = memory is None or metadata.get("comparison_memory") == memory
    if old_provenance == provenance and compatible_memory:
        for directory in sorted((source / "comparison").glob("r*/*/checkpoints")):
            target = output / directory.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(directory, target)
            memory_source = directory.parent / "memory"
            if memory_source.is_dir():
                shutil.copytree(memory_source, target.parent / "memory")
        print(
            "Compatible comparison checkpoints copied; failed/missing series will be measured.",
            flush=True,
        )
    else:
        print(
            "Comparisons will be recollected with the current source/package versions.",
            flush=True,
        )
    metadata["comparison"] = True
    if memory is not None:
        metadata["comparison_memory"] = memory
    metadata["comparison_provenance"] = provenance
    metadata["reference_preflight"] = preflight
    metadata["recovery"] = {
        "source_bundle": str(source),
        "created_at": datetime.now(timezone.utc).isoformat(),
        "core_results": "reused unchanged",
        "comparisons": "current provenance; compatible checkpoints only",
    }
    atomic_json(output / "metadata.json", metadata)
    return metadata


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
        metadata = prepare_recovery(
            source,
            output,
            provenance,
            {
                "mode": args.memory_mode,
                "interval_ms": args.memory_interval_ms
                if args.memory_mode == "profile"
                else None,
            },
        )
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
