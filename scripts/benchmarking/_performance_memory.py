"""Independent process RSS collection for manual benchmarks (macOS/Linux).

The monitor owns wait4 for exactly one child. Its own memory and the logger's
memory are excluded. Sampling is external to Python/native worker execution.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import errno
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable


def positive_interval(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("Memory interval must be a positive integer")
    return number


def add_memory_arguments(parser: argparse.ArgumentParser) -> None:
    def memory_mode(value: str) -> str:
        if value not in ("off", "peak", "profile"):
            raise argparse.ArgumentTypeError("Memory mode must be off, peak or profile")
        return value

    parser.add_argument(
        "--memory-mode",
        choices=("off", "peak", "profile"),
        type=memory_mode,
        default=os.environ.get("MEMORY_MODE", "off"),
        help="off (default), per-process OS peak, or peak plus sampled RSS",
    )
    parser.add_argument(
        "--memory-interval-ms",
        type=positive_interval,
        default=os.environ.get("MEMORY_INTERVAL_MS", "50"),
        help="Current-RSS sampling interval in milliseconds (profile mode)",
    )


def peak_bytes(value: int, platform: str) -> int:
    if value < 0:
        raise ValueError("Negative OS peak RSS")
    if platform == "darwin":
        return value
    if platform.startswith("linux"):
        return value * 1024
    raise ValueError("Memory collection supports macOS and Linux")


def rss_reader() -> Callable[[int], int | None]:
    if sys.platform.startswith("linux"):
        page_size = os.sysconf("SC_PAGE_SIZE")

        def linux(pid: int) -> int | None:
            try:
                fields = Path(f"/proc/{pid}/statm").read_text().split()
            except (FileNotFoundError, ProcessLookupError):
                return None
            return int(fields[1]) * page_size

        return linux
    if sys.platform == "darwin":
        # Layout/constant from the SDK's sys/proc_info.h, proc_taskinfo and
        # PROC_PIDTASKINFO. Only resident_size is read; all fields preserve ABI.
        class TaskInfo(ctypes.Structure):
            _fields_ = [
                (name, ctypes.c_uint64)
                for name in (
                    "virtual_size",
                    "resident_size",
                    "total_user",
                    "total_system",
                    "threads_user",
                    "threads_system",
                )
            ] + [
                (name, ctypes.c_int32)
                for name in (
                    "policy",
                    "faults",
                    "pageins",
                    "cow_faults",
                    "messages_sent",
                    "messages_received",
                    "syscalls_mach",
                    "syscalls_unix",
                    "csw",
                    "threadnum",
                    "numrunning",
                    "priority",
                )
            ]

        library = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
        query = library.proc_pidinfo
        query.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint64,
            ctypes.c_void_p,
            ctypes.c_int,
        ]
        query.restype = ctypes.c_int

        def darwin(pid: int) -> int | None:
            info = TaskInfo()
            ctypes.set_errno(0)
            size = query(pid, 4, 0, ctypes.byref(info), ctypes.sizeof(info))
            if size == ctypes.sizeof(info):
                return int(info.resident_size)
            error = ctypes.get_errno()
            if size == 0 and error in (0, errno.ESRCH):
                return None  # Child exited between wait4 and the RSS query.
            raise OSError(error, "proc_pidinfo could not sample child RSS")

        return darwin
    raise ValueError("Memory collection supports macOS and Linux")


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_measured(
    command: list[str],
    output: Path,
    *,
    mode: str,
    interval_ms: int = 50,
    cwd: Path | None = None,
) -> dict[str, Any]:
    if mode not in ("peak", "profile") or interval_ms <= 0 or not command:
        raise ValueError(
            "Expected peak/profile mode, a positive interval and a command"
        )
    peak_bytes(0, sys.platform)  # Fail before launching on unsupported platforms.
    read_rss = rss_reader() if mode == "profile" else None
    output.parent.mkdir(parents=True, exist_ok=True)
    timeline = output.with_suffix(".rss.csv")
    if output.exists() or (mode == "profile" and timeline.exists()):
        raise ValueError(f"Memory artifact exists; choose a fresh run path: {output}")
    result: dict[str, Any] = {
        "schema_version": 1,
        "mode": mode,
        "units": "bytes",
        "method": "wait4.ru_maxrss",
        "scope": "process lifetime including startup and teardown",
        "command": command,
        "cwd": str(cwd or Path.cwd()),
        "platform": sys.platform,
        "pid": None,
        "complete": False,
        "returncode": None,
        "peak_rss_bytes": None,
        "sampled_max_rss_bytes": None,
        "sample_count": 0,
        "interval_ms": interval_ms if read_rss else None,
        "timeline": timeline.name if read_rss else None,
        "sampling_method": (
            "proc_pidinfo.PROC_PIDTASKINFO"
            if sys.platform == "darwin"
            else "/proc/PID/statm"
        )
        if read_rss
        else None,
    }
    process: subprocess.Popen | None = None
    stream = None
    started = time.monotonic()

    def reap(block: bool) -> bool:
        assert process is not None
        while True:
            try:
                pid, status, usage = os.wait4(process.pid, 0 if block else os.WNOHANG)
                break
            except InterruptedError:
                continue
        if not pid:
            return False
        process.returncode = os.waitstatus_to_exitcode(status)
        result["returncode"] = process.returncode
        result["peak_rss_bytes"] = peak_bytes(usage.ru_maxrss, sys.platform)
        result["complete"] = process.returncode == 0
        return True

    try:
        writer = None
        if read_rss:
            stream = timeline.open("x", newline="")
            writer = csv.writer(stream)
            writer.writerow(["elapsed_s", "rss_bytes", "pid"])
        process = subprocess.Popen(command, cwd=cwd)
        result["pid"] = process.pid
        if read_rss is None:
            reap(True)
        else:
            deadline = started
            while not reap(False):
                rss = read_rss(process.pid)
                if rss is not None:
                    assert writer is not None and stream is not None
                    writer.writerow([time.monotonic() - started, rss, process.pid])
                    stream.flush()
                    result["sample_count"] += 1
                    result["sampled_max_rss_bytes"] = max(
                        result["sampled_max_rss_bytes"] or 0, rss
                    )
                deadline = max(deadline + interval_ms / 1000, time.monotonic())
                time.sleep(max(0.0, deadline - time.monotonic()))
    except BaseException as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        if process is not None and process.returncode is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
            reap(True)
        result["complete"] = False
        raise
    finally:
        if stream is not None:
            stream.close()
            result["timeline_sha256"] = file_digest(timeline)
        result["process_elapsed_s"] = time.monotonic() - started
        temporary = output.with_suffix(output.suffix + ".tmp")
        temporary.write_text(json.dumps(result, indent=2) + "\n")
        temporary.replace(output)
    return result


def read_memory(
    path: Path, expected: dict[str, Any] | None = None
) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"Invalid memory measurement: {path}")
    peak = value.get("peak_rss_bytes")
    if (
        value.get("schema_version") != 1
        or value.get("units") != "bytes"
        or value.get("mode") not in ("peak", "profile")
        or not value.get("complete")
        or value.get("returncode") != 0
        or type(peak) is not int
        or peak < 0
        or not isinstance(value.get("scope"), str)
        or value.get("method") != "wait4.ru_maxrss"
        or type(value.get("pid")) is not int
        or value["pid"] <= 0
        or type(value.get("sample_count")) is not int
        or value["sample_count"] < 0
    ):
        raise ValueError(f"Invalid/incomplete memory measurement: {path}")
    if expected and expected.get("mode", "off") != "off":
        if value["mode"] != expected["mode"] or (
            value["mode"] == "profile"
            and value.get("interval_ms") != expected["interval_ms"]
        ):
            raise ValueError(f"Memory collection settings differ: {path}")
    if value["mode"] == "profile":
        maximum = value.get("sampled_max_rss_bytes")
        if (
            type(value.get("interval_ms")) is not int
            or value["interval_ms"] <= 0
            or (value["sample_count"] == 0 and maximum is not None)
            or (value["sample_count"] > 0 and (type(maximum) is not int or maximum < 0))
        ):
            raise ValueError(f"Invalid memory sampling metadata: {path}")
        name = value.get("timeline")
        if not isinstance(name, str) or Path(name).name != name:
            raise ValueError(f"Invalid memory timeline path: {path}")
        timeline = path.parent / name
        if file_digest(timeline) != value.get("timeline_sha256"):
            raise ValueError(f"Memory timeline changed: {timeline}")
    elif (
        value["sample_count"] != 0
        or value.get("interval_ms") is not None
        or value.get("sampled_max_rss_bytes") is not None
        or value.get("timeline") is not None
    ):
        raise ValueError(f"Unexpected RSS samples in peak-only measurement: {path}")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    add_memory_arguments(parser)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("A measured command is required after --")
    if args.memory_mode == "off":
        raise SystemExit(subprocess.call(command))
    try:
        result = run_measured(
            command,
            args.output,
            mode=args.memory_mode,
            interval_ms=args.memory_interval_ms,
        )
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Memory collection failed: {exc}\n")
    code = result["returncode"]
    raise SystemExit(code if code >= 0 else 128 - code)


if __name__ == "__main__":
    main()
