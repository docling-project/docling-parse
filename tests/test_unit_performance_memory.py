"""Independent RSS observations, worker isolation, and recoverable memory artifacts."""

import argparse
import csv
import importlib
import json
import os
import subprocess
import sys
from pathlib import Path
from textwrap import dedent
from types import SimpleNamespace

import pytest

_SCRIPTS = Path(__file__).parents[1] / "scripts/benchmarking"
sys.path.insert(0, str(_SCRIPTS))
try:
    _MEMORY = importlib.import_module("_performance_memory")
    _REPORT = importlib.import_module("_performance_memory_report")
    _RUNTIME = importlib.import_module("_performance_runtime")
    _WORKER = importlib.import_module("_performance_worker")
    _COMMON = importlib.import_module("_common")
finally:
    sys.path.pop(0)

_SUPPORTED = sys.platform == "darwin" or sys.platform.startswith("linux")


@pytest.mark.parametrize("platform,factor", [("darwin", 1), ("linux", 1024)])
def test_os_high_water_units(platform, factor):
    assert _MEMORY.peak_bytes(123, platform) == 123 * factor
    with pytest.raises(ValueError):
        _MEMORY.peak_bytes(-1, platform)


@pytest.mark.parametrize(
    "variable,value", [("MEMORY_MODE", "invalid"), ("MEMORY_INTERVAL_MS", "0")]
)
def test_invalid_environment_is_rejected(monkeypatch, variable, value):
    monkeypatch.setenv(variable, value)
    parser = argparse.ArgumentParser()
    _MEMORY.add_memory_arguments(parser)
    with pytest.raises(SystemExit) as error:
        parser.parse_args([])
    assert error.value.code == 2


@pytest.mark.skipif(not _SUPPORTED, reason="OS RSS collection supports macOS/Linux")
def test_peak_belongs_to_each_fresh_child(tmp_path: Path):
    # Linux can retain pre-exec RSS inherited from the spawning process. Keep
    # pytest's imports out of the measured children's startup high-water mark.
    # Both children still share one monitor so a cumulative RUSAGE_CHILDREN
    # measurement would incorrectly retain the large child's peak for the small.
    monitor = dedent("""\
        import os
        import sys
        from pathlib import Path

        sys.path.insert(0, sys.argv[1])
        from _performance_memory import run_measured

        output = Path(sys.argv[2])
        command = [sys.executable, "-c", "import sys; b=bytearray(int(sys.argv[1]))"]
        for name, size in (("large", 64 * 2**20), ("small", 1024)):
            run_measured([*command, str(size)], output / (name + ".json"), mode="peak")
        print(os.getpid())
        """)
    monitor_pid = int(
        subprocess.check_output(
            [sys.executable, "-c", monitor, str(_SCRIPTS), str(tmp_path)], text=True
        )
    )
    large = _MEMORY.read_memory(tmp_path / "large.json")
    small = _MEMORY.read_memory(tmp_path / "small.json")
    assert len({large["pid"], small["pid"], monitor_pid, os.getpid()}) == 4
    command = [sys.executable, "-c", "import sys; b=bytearray(int(sys.argv[1]))"]
    assert large["peak_rss_bytes"] > small["peak_rss_bytes"] + 32 * 2**20
    assert _MEMORY.read_memory(tmp_path / "small.json") == small
    with pytest.raises(ValueError, match="exists"):
        _MEMORY.run_measured(command, tmp_path / "small.json", mode="peak")


@pytest.mark.skipif(not _SUPPORTED, reason="OS RSS collection supports macOS/Linux")
def test_profile_is_external_and_preserves_os_peak(tmp_path: Path):
    path = tmp_path / "profile.json"
    result = _MEMORY.run_measured(
        [
            sys.executable,
            "-c",
            "import time; b=bytearray(16*1024*1024); time.sleep(.2)",
        ],
        path,
        mode="profile",
        interval_ms=10,
    )
    assert result["pid"] != os.getpid()
    assert result["sample_count"] >= 2
    assert result["peak_rss_bytes"] > 16 * 2**20
    validated = _MEMORY.read_memory(path, {"mode": "profile", "interval_ms": 10})
    timeline = path.parent / validated["timeline"]
    rows = _REPORT.timeline_rows(timeline, validated)
    assert len(rows) == result["sample_count"]
    assert max(rss for _, rss in rows) == result["sampled_max_rss_bytes"]
    with pytest.raises(ValueError, match="settings differ"):
        _MEMORY.read_memory(path, {"mode": "profile", "interval_ms": 50})
    timeline.write_text(timeline.read_text() + "0,0,0\n")
    with pytest.raises(ValueError, match="changed"):
        _MEMORY.read_memory(path)


@pytest.mark.skipif(not _SUPPORTED, reason="OS RSS collection supports macOS/Linux")
def test_failed_child_keeps_diagnostic_peak_but_is_not_valid(tmp_path: Path):
    path = tmp_path / "failed.json"
    result = _MEMORY.run_measured(
        [sys.executable, "-c", "raise SystemExit(7)"], path, mode="peak"
    )
    assert result["returncode"] == 7
    assert result["complete"] is False
    assert result["peak_rss_bytes"] > 0
    with pytest.raises(ValueError, match="incomplete"):
        _MEMORY.read_memory(path)


@pytest.mark.skipif(not _SUPPORTED, reason="OS RSS collection supports macOS/Linux")
def test_sampler_failure_terminates_and_reaps_child(tmp_path: Path, monkeypatch):
    def fail(_pid):
        raise OSError("sampler unavailable")

    monkeypatch.setattr(_MEMORY, "rss_reader", lambda: fail)
    path = tmp_path / "failed.json"
    with pytest.raises(OSError, match="sampler unavailable"):
        _MEMORY.run_measured(
            [sys.executable, "-c", "import time; time.sleep(60)"],
            path,
            mode="profile",
        )
    result = json.loads(path.read_text())
    assert result["complete"] is False
    with pytest.raises(ChildProcessError):
        os.waitpid(result["pid"], os.WNOHANG)


def test_worker_transports_options_and_rows_without_changing_timing(
    tmp_path: Path, monkeypatch
):
    calls = []

    def backend(schedule, total_pages, **options):
        calls.append((schedule, total_pages, options))
        return SimpleNamespace(
            backend="fixture",
            task="parse+render",
            threads=4,
            wall_s=1.234,
            per_page_source="native-worker",
            doc_errors=0,
            samples=[
                _COMMON.PageRow("fixture", "parse+render", 4, "doc.pdf", 1, True, 0.2)
            ],
        )

    monkeypatch.setitem(
        sys.modules,
        "run_performance_benchmarking",
        SimpleNamespace(COMPARISON_BACKENDS={"fixture": (backend, ())}),
    )
    request = tmp_path / "request.json"
    response = tmp_path / "response.json"
    options = {
        "render": True,
        "threads": 4,
        "scale": 1.5,
        "bytesio": True,
        "decode_options": {"keep_shapes": True},
        "materialization_options": {"include_bitmap_bytes": False},
        "max_concurrent_results": 8,
    }
    request.write_text(
        json.dumps(
            {
                "backend": "fixture",
                "pdf_schedule": [["doc.pdf", [1]]],
                "total_pages": 1,
                **options,
            }
        )
    )
    monkeypatch.setattr(sys, "argv", ["worker", str(request), str(response)])
    _WORKER.main()
    assert calls == [([(Path("doc.pdf"), [1])], 1, options)]
    result = json.loads(response.read_text())
    assert result["wall_s"] == 1.234
    assert result["samples"][0]["elapsed_s"] == 0.2
    assert result["threads"] == 4


def _artifact(path: Path, *, mode="profile"):
    path.parent.mkdir(parents=True, exist_ok=True)
    value = {
        "schema_version": 1,
        "mode": mode,
        "units": "bytes",
        "method": "wait4.ru_maxrss",
        "scope": "process lifetime",
        "pid": 123,
        "complete": True,
        "returncode": 0,
        "peak_rss_bytes": 2**20,
        "sample_count": 2 if mode == "profile" else 0,
        "interval_ms": 50 if mode == "profile" else None,
        "sampled_max_rss_bytes": 512 if mode == "profile" else None,
        "timeline": path.with_suffix(".rss.csv").name if mode == "profile" else None,
    }
    if mode == "profile":
        timeline = path.parent / value["timeline"]
        timeline.write_text("elapsed_s,rss_bytes,pid\n0.01,256,123\n0.05,512,123\n")
        value["timeline_sha256"] = _MEMORY.file_digest(timeline)
    path.write_text(json.dumps(value))
    return value


def test_resume_recollects_when_profile_artifact_is_lost(tmp_path: Path):
    artifact = tmp_path / "memory/run.json"
    memory = _artifact(artifact)
    memory.update(artifact="memory/run.json", artifact_sha256=_RUNTIME.digest(artifact))
    store = _RUNTIME.BenchmarkCheckpoints(
        tmp_path / "checkpoints",
        {"memory": {"mode": "profile", "interval_ms": 50}},
        {},
        1,
    )
    run = SimpleNamespace(
        backend="fixture",
        task="parse",
        threads=1,
        wall_s=1,
        per_page_source="wall",
        doc_errors=0,
        memory=memory,
        samples=[_COMMON.PageRow("fixture", "parse", 1, "doc.pdf", 1, True, 0.5)],
    )
    store.save(run)
    assert store.load("fixture", "parse", 1)["memory"]["pid"] == 123
    (artifact.parent / memory["timeline"]).unlink()
    assert store.load("fixture", "parse", 1) is None


@pytest.mark.parametrize("damage", ["pid", "order", "count"])
def test_timeline_rejects_inconsistent_samples(tmp_path: Path, damage):
    path = tmp_path / "memory.json"
    memory = _artifact(path)
    timeline = path.parent / memory["timeline"]
    if damage == "pid":
        timeline.write_text(timeline.read_text().replace(",123", ",456"))
    elif damage == "order":
        timeline.write_text("elapsed_s,rss_bytes,pid\n1,256,123\n0,512,123\n")
    else:
        memory["sample_count"] += 1
    with pytest.raises(ValueError, match="RSS"):
        _REPORT.timeline_rows(timeline, memory)


def test_missing_memory_is_unavailable_and_mixed_modes_are_rejected(tmp_path: Path):
    def measurement(repetition, memory):
        return SimpleNamespace(
            group=("native", "docling-parse", "render", 1),
            workload="native",
            backend="docling-parse",
            task="render",
            threads=1,
            repetition=repetition,
            memory=memory,
        )

    lines = _REPORT.write_memory_report([measurement(1, None)], tmp_path)
    assert "unavailable" in "\n".join(lines)
    assert (
        "No sampled RSS traces are available"
        in (tmp_path / "memory_traces.md").read_text()
    )
    with (tmp_path / "memory_summary.csv").open() as stream:
        row = next(csv.DictReader(stream))
    assert row["median_peak_mib"] == ""
    profile = _artifact(tmp_path / "profile.json")
    peak = _artifact(tmp_path / "peak.json", mode="peak")
    with pytest.raises(ValueError, match="Mixed memory"):
        _REPORT.write_memory_report(
            [measurement(1, profile), measurement(2, peak)], tmp_path
        )


@pytest.mark.parametrize("mode", ["peak", "profile"])
def test_profile_report_exports_observed_peaks_and_timelines(
    tmp_path: Path, mode, monkeypatch
):
    figure_class = pytest.importorskip("matplotlib.figure").Figure
    savefig = figure_class.savefig
    figures = {}

    def capture(figure, path, **kwargs):
        figures[Path(path).name] = figure
        savefig(figure, path, **kwargs)

    monkeypatch.setattr(figure_class, "savefig", capture)
    path = tmp_path / "profile.json"
    memory = _artifact(path, mode=mode)
    if mode == "profile":
        memory["_timeline_path"] = str(path.parent / memory["timeline"])
    memory["_source"] = path.name
    measurement = SimpleNamespace(
        group=("replay", "pdfium", "render", 1),
        workload="replay",
        backend="pdfium",
        task="render",
        threads=1,
        repetition=1,
        memory=memory,
    )
    lines = "\n".join(_REPORT.write_memory_report([measurement], tmp_path))
    assert lines.startswith("## Process Memory\n")
    assert "memory_replay_render.png" in lines
    assert "[Elapsed-time memory traces](memory_traces.md)" in lines
    assert "rss_replay_pdfium_render_t1.png" not in lines
    assert (tmp_path / "memory_replay_render.png").is_file()
    peak_axes = figures["memory_replay_render.png"].axes[0]
    assert peak_axes.get_xlim()[0] == 0
    assert peak_axes.get_ylim()[0] == 0
    assert 0 in peak_axes.get_xticks()
    traces = (tmp_path / "memory_traces.md").read_text()
    assert "[Back to Process Memory](summary.md#process-memory)" in traces
    if mode == "profile":
        assert "rss_replay_pdfium_render_t1.png" in traces
        assert (tmp_path / "rss_replay_pdfium_render_t1.png").is_file()
    else:
        assert "No sampled RSS traces are available" in traces
    with (tmp_path / "memory_runs.csv").open() as stream:
        row = next(csv.DictReader(stream))
    assert row["peak_rss_bytes"] == str(2**20)
    assert row["sampled_max_rss_bytes"] == ("512" if mode == "profile" else "")
    assert row["source"] == "profile.json"


@pytest.mark.parametrize("comparison", [False, True])
def test_launcher_observes_actual_process_or_delegates_series(
    tmp_path: Path, comparison
):
    source = (_SCRIPTS / "refresh_performance_benchmarks.sh").read_text()
    function = source.split("run_experiment() {", 1)[1].split("\n}\n", 1)[0]
    log = tmp_path / (
        "comparison/r1/render/run.log" if comparison else "replay/r1/t1.log"
    )
    command = "\n".join(
        [
            "set -euo pipefail",
            "REPO_ROOT=/fixture",
            "MEMORY_MODE=profile",
            "MEMORY_INTERVAL_MS=25",
            "experiment=0",
            "total_experiments=1",
            "run_logged() { printf '%s\\n' \"$@\"; }",
            "run_experiment() {" + function + "\n}",
            'run_experiment "$1" fixture uv run --no-sync python runner.py --threads 4',
        ]
    )
    result = subprocess.run(
        ["bash", "-c", command, "fixture", str(log)],
        capture_output=True,
        text=True,
        check=True,
    )
    arguments = result.stdout.splitlines()[2:]
    assert "--memory-mode" in arguments and "profile" in arguments
    assert "--memory-interval-ms" in arguments and "25" in arguments
    if comparison:
        assert arguments[:5] == ["uv", "run", "--no-sync", "python", "runner.py"]
        assert "_performance_memory.py" not in " ".join(arguments)
    else:
        observed = arguments[arguments.index("--") + 1 :]
        assert observed == ["/fixture/.venv/bin/python", "runner.py", "--threads", "4"]
