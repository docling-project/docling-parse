"""Protect the wall-time boundary and reject misleading benchmark inputs."""

import ast
import csv
import hashlib
import importlib.metadata
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "performance_report_tests",
    Path(__file__).parents[1] / "scripts/benchmarking/run_performance_report.py",
)
assert _SPEC is not None and _SPEC.loader is not None
_REPORT = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _REPORT
sys.path.insert(0, str(Path(__file__).parents[1] / "scripts/benchmarking"))
try:
    _SPEC.loader.exec_module(_REPORT)
finally:
    sys.path.pop(0)

_DATASET_PINS = {
    "bo20": "65b9971b3fa939fe4ef89be678237e6928049b67",
    "bo767": "221c19e716d6ede5d18fd1560bc938ab19cc1a70",
    "bo10k": "f85e203f95879e2f5d77059a6bfeffe667cd359e",
}


def _copy_launcher(tmp_path: Path):
    repo = tmp_path / "repo"
    scripts = repo / "scripts/benchmarking"
    scripts.mkdir(parents=True)
    launcher = scripts / "refresh_performance_benchmarks.sh"
    shutil.copy2(
        Path(__file__).parents[1] / "scripts/benchmarking" / launcher.name, launcher
    )
    return repo, launcher


def _inputs(tmp_path: Path):
    metadata = {
        "schema_version": 1,
        "dataset": {
            "files": [
                {
                    "name": "doc.pdf",
                    "path": "/pdf/doc.pdf",
                    "resolved_path": "/pdf/doc.pdf",
                    "pages": 2,
                }
            ]
        },
        "repeats": 1,
        "threads": [1, 4],
        "comparison": False,
        "source_revision": "revision",
        "source_diff_sha256": "diff",
    }
    (tmp_path / "metadata.json").write_text(json.dumps(metadata))
    return _REPORT.ReportInputs(tmp_path)


def test_memory_does_not_invent_historical_observations(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path = tmp_path / "missing.memory.json"
    assert inputs.memory(path) is None
    existing = inputs.memory(path, {"peak_rss_mib": 12.5})
    assert existing["mode"] == "existing"
    assert existing["peak_rss_bytes"] == int(12.5 * 2**20)
    inputs.metadata["memory_collection"] = {"mode": "profile", "interval_ms": 50}
    with pytest.raises(ValueError, match="missing"):
        inputs.memory(path, {"peak_rss_mib": 12.5})
    # Recovery can reuse uninstrumented core results while measuring new references.
    inputs.metadata["memory_collection"] = {"mode": "off"}
    inputs.metadata["comparison_memory"] = {"mode": "peak"}
    assert inputs.memory(path) is None
    with pytest.raises(ValueError, match="missing"):
        inputs.memory(path, comparison=True)


@pytest.mark.parametrize(
    "workload,task",
    [("native", "render-only"), ("replay", "render"), ("comparison", "parse+render")],
)
def test_reproduction_preserves_memory_settings(tmp_path: Path, workload, task):
    inputs = _profiled_bundle(tmp_path)
    inputs.metadata["memory_collection"] = {"mode": "profile", "interval_ms": 25}
    command = "\n".join(_REPORT.reproduction_commands(inputs.metadata, workload, task))
    assert "--memory-mode profile --memory-interval-ms 25" in command
    if workload == "comparison":
        assert "observe()" not in command
    else:
        assert "observe()" in command
        assert "_performance_memory.py" in command


def _native(tmp_path: Path, profile=None) -> Path:
    native_root = tmp_path / "native"
    if profile:
        native_root /= profile
    path = native_root / "r1/t1.csv"
    path.parent.mkdir(parents=True)
    stages = {
        "total_s": 3,
        "make_page_decoder_s": 0.3,
        "decode_page_s": 0.8,
        "create_word_cells_s": 0.0 if profile == "render-only" else 0.1,
        "create_line_cells_s": 0.0 if profile == "render-only" else 0.05,
        "render_page_s": 1.5,
        "get_canvas_s": 0.1,
        "collect_detail_s": 0.05,
    }
    row = {
        "schema_version": "2",
        "mode": "render",
        "render": "true",
        "success": "true",
        "error_message": "",
        "threads": "1",
        "doc_key": "/pdf/doc.pdf",
        "page_number": "1",
        **{"timing_" + key: value for key, value in stages.items()},
    }
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(row))
        writer.writeheader()
        for page in (1, 2):
            writer.writerow({**row, "page_number": str(page)})
    path.with_suffix(".log").write_text(
        "Benchmark: 1 documents, 2 total pages\n"
        "Timing diagnostics (threads=1, CSV schema 2)\n"
        "  wall_s: 20.000000 (includes collection/CSV)\n"
        + "".join(f"  {key} {2 * value:.6f}\n" for key, value in stages.items())
    )
    return path


def _profiled_bundle(tmp_path: Path):
    inputs = _inputs(tmp_path)
    metadata = inputs.metadata
    metadata.update(
        native_profiles=_REPORT.NATIVE_PROFILES,
        threads=[1],
        repeats=1,
        scale=1.0,
        max_concurrent_results=32,
        qpdf="12.4.2",
        machine="fixture CPU",
        platform="fixture platform",
    )
    metadata["dataset"].update(
        repository="docling-project/performance-dataset-bo20",
        revision="fixture-revision",
        root="/pdf",
    )
    (tmp_path / "metadata.json").write_text(json.dumps(metadata))
    for profile in _REPORT.NATIVE_PROFILES:
        _native(tmp_path, profile)
    for backend, directory in (("pdfium", "pdfium"), ("threaded", "t1")):
        root = tmp_path / "replay/r1" / directory
        root.mkdir(parents=True)
        pages = root / "pages.jsonl"
        pages.write_text(
            "".join(
                json.dumps(
                    {
                        "document": "/pdf/doc.pdf",
                        "page": page,
                        "image_mode": "RGB",
                        "image_size": [200, 200],
                    }
                )
                + "\n"
                for page in (1, 2)
            )
        )
        report = {
            "provenance": {
                "parse_git": {"revision": "revision", "diff_sha256": "diff"}
            },
            "runs": [
                {
                    "settings": {
                        "backend": backend,
                        "requested_scale": 1.0,
                        "parser": {"threads": 1},
                    },
                    "documents": [
                        {
                            "document": "/pdf/doc.pdf",
                            "pages": 2,
                            "construction_load_s": 1,
                            "page_processing_s": 2,
                            "unload_s": 1,
                            "total_s": 4,
                        }
                    ],
                    "pages_file": str(pages),
                }
            ],
        }
        (root / "report.json").write_text(json.dumps(report))
    return _REPORT.ReportInputs(tmp_path)


def test_native_profiles_remain_distinct_and_required(tmp_path: Path):
    inputs = _profiled_bundle(tmp_path)
    measurements = inputs.load()
    native = {m.task: m for m in measurements if m.workload == "native"}
    assert set(native) == {"render-only", "render-with-text"}
    assert native["render-only"].phases["create_word_cells_s"] == 0
    assert native["render-with-text"].phases["create_word_cells_s"] == 0.2
    (tmp_path / "native/render-with-text/r1/t1.csv").unlink()
    with pytest.raises(ValueError, match="render-with-text"):
        inputs.load()


def test_render_only_rejects_word_line_work(tmp_path: Path):
    inputs = _profiled_bundle(tmp_path)
    path = tmp_path / "native/render-only/r1/t1.csv"
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    rows[0]["timing_create_word_cells_s"] = "0.5"
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    with pytest.raises(
        ValueError, match="Word/line cell creation found in render-only"
    ):
        inputs.native(path)


def test_native_profile_speedups_use_their_own_baselines():
    measurements = [
        _REPORT.Measurement(
            "native", "docling-parse", profile, threads, 1, wall, 2, 1, "log", "wall"
        )
        for profile, threads, wall in [
            ("render-only", 1, 20),
            ("render-only", 4, 5),
            ("render-with-text", 1, 40),
            ("render-with-text", 4, 20),
        ]
    ]
    rows = {(r["task"], r["threads"]): r for r in _REPORT.summarize(measurements)}
    assert rows["render-only", 4]["speedup_vs_docling_1t"] == 4
    assert rows["render-with-text", 4]["speedup_vs_docling_1t"] == 2
    assert rows["render-only", 4]["efficiency_pct"] == 100
    assert rows["render-with-text", 4]["efficiency_pct"] == 50


def test_core_only_report_explicitly_excludes_incomplete_comparison(
    tmp_path: Path, monkeypatch
):
    inputs = _profiled_bundle(tmp_path)
    inputs.metadata["comparison"] = True
    with pytest.raises(ValueError, match="Missing/unexpected measurements"):
        inputs.load()
    measurements = inputs.load(include_comparison=False)
    assert len(measurements) == 4
    monkeypatch.setattr(_REPORT, "plot", lambda *_args: [])
    output = tmp_path / "report-core"
    _REPORT.write_report(inputs, measurements, output, core_only=True)
    text = (output / "summary.md").read_text()
    assert "Core results only" in text and "comparisons are excluded" in text
    assert "RUN_COMPARISON=0" in text
    assert "standalone backend-operation replay" in text
    assert "## Understanding the two Python experiments" not in text


def test_summary_refuses_mixed_reference_versions():
    first = _REPORT.Measurement(
        "comparison",
        "pymupdf",
        "parse",
        1,
        1,
        1,
        2,
        1,
        "log",
        "wall",
        package_version="1.28.2",
    )
    second = _REPORT.Measurement(
        "comparison",
        "pymupdf",
        "parse",
        1,
        2,
        1,
        2,
        1,
        "log",
        "wall",
        package_version="1.27.2.3",
    )
    with pytest.raises(ValueError, match="Mixed package versions"):
        _REPORT.summarize([first, second])


@pytest.mark.parametrize("same_version", [True, False])
def test_python_workload_explanation_pairs_only_matching_render_results(same_version):
    def measurement(workload, backend, task, wall, version):
        return _REPORT.Measurement(
            workload,
            backend,
            task,
            1,
            1,
            wall,
            100,
            1,
            "fixture",
            "wall",
            package_version=version,
        )

    measurements = [
        measurement("replay", "pdfium", "render", 20, "pdfium-a"),
        measurement(
            "comparison",
            "pypdfium2",
            "parse+render",
            8,
            "pdfium-a" if same_version else "pdfium-b",
        ),
        measurement("comparison", "pypdfium2", "parse", 1, "pdfium-a"),
    ]
    text = "\n".join(
        _REPORT.python_experiment_explanation(_REPORT.summarize(measurements))
    )
    assert ("| pypdfium2 | pdfium-a | 1 | 5.0 | 12.5 |" in text) == same_version
    assert "| 100.0 |" not in text  # The parse-only rate is not a paired render rate.
    if not same_version:
        assert "No successful render pairs" in text
    for workload in ("replay", "comparison"):
        partial = [m for m in measurements if m.workload == workload]
        assert _REPORT.python_experiment_explanation(_REPORT.summarize(partial)) == []


@pytest.mark.parametrize("tampered", [False, True])
def test_comparison_checkpoint_provenance_controls_version_labels(
    tmp_path: Path, monkeypatch, tampered
):
    monkeypatch.syspath_prepend(str(Path(__file__).parents[1] / "scripts/benchmarking"))
    runtime = __import__("_performance_runtime")
    common = __import__("_common")
    inputs = _inputs(tmp_path)
    inputs.metadata["packages"] = {"pymupdf": "1.28.2"}
    provenance = {"packages": {"pymupdf": "1.27.2.3"}}
    inputs.metadata["comparison_provenance"] = provenance
    directory = tmp_path / "comparison/r1/parse-without-bitmap"
    directory.mkdir(parents=True)
    rows = [
        common.PageRow("pymupdf", "parse", 1, "/pdf/doc.pdf", page, True, 0.5)
        for page in (1, 2)
    ]
    run = SimpleNamespace(
        backend="pymupdf",
        task="parse",
        threads=1,
        wall_s=2.5,
        per_page_source="wall",
        doc_errors=0,
        samples=rows,
    )
    store = runtime.BenchmarkCheckpoints(directory / "checkpoints", {}, provenance, 2)
    store.save(run)
    path = directory / "fixture.csv"
    common.write_page_rows(path, rows)
    (directory / "run.log").write_text(
        "=== PARSE ===\npymupdf (1t)   -   2.500   1.0\n"
    )
    if tampered:
        _, checkpoint = store.paths("pymupdf", "parse", 1)
        state = json.loads(checkpoint.read_text())
        state["provenance"]["packages"]["pymupdf"] = "1.28.2"
        checkpoint.write_text(json.dumps(state))
        with pytest.raises(ValueError, match="provenance differs"):
            inputs.comparison(path)
    else:
        (measurement,) = inputs.comparison(path)
        assert measurement.package_version == "1.27.2.3"


@pytest.mark.parametrize("with_rss_timelines", [False, True])
def test_profiled_report_figures_captions_and_phase_exports(
    tmp_path: Path, with_rss_timelines
):
    pytest.importorskip("matplotlib")
    inputs = _profiled_bundle(tmp_path)
    if with_rss_timelines:
        for profile in _REPORT.NATIVE_PROFILES:
            timeline = tmp_path / "native" / profile / "r1/t1.memory.rss.csv"
            timeline.write_text("elapsed_s,rss_bytes,pid\n0.05,1048576,123\n")
    output = tmp_path / "report"
    _REPORT.write_report(inputs, inputs.load(), output)
    expected = {
        f"{prefix}_{kind}.png"
        for prefix in ("native_render_only", "native_render_with_text", "replay")
        for kind in ("runtime", "scaling", "phases", "distribution")
    }
    assert {p.name for p in output.glob("*.png")} == expected
    assert all((output / name).stat().st_size > 0 for name in expected)
    tables, text = (output / "summary.md").read_text().split("## Figures\n", 1)
    assert [line for line in tables.splitlines() if line.startswith("##")] == [
        "## Parallel Scaling",
        "### Native C++ scaling",
        "### Python scaling with Docling settings",
        "## Process Memory",
    ]
    assert "[Elapsed-time memory traces](memory_traces.md)" in tables
    timing_rows = [
        line
        for line in tables.splitlines()
        if line.startswith(("| docling-parse |", "| pypdfium2 |"))
    ]
    assert [line.split("|")[3].strip() for line in timing_rows] == [
        "Render",
        "Render + text",
        "Backend replay",
        "Backend replay",
    ]
    assert "- **Render**: Render page-images." in tables
    assert (
        "- **Render + text**: Render page-images and construct word/line text geometry."
        in tables
    )
    assert (
        "- **Backend replay**: Page-images, word/line text geometry and bitmap bounding boxes."
        in tables
    )
    assert (
        "No sampled RSS traces are available"
        in (output / "memory_traces.md").read_text()
    )
    assert text.index("### Native C++ scaling") < text.index(
        "### Python scaling with Docling settings"
    )
    assert "#### Render page-images\n" in text
    assert "#### Render page-images and construct word/line text geometry\n" in text
    assert (
        "#### Page-images, word/line text geometry and bitmap bounding boxes\n" in text
    )
    assert "##### Where the time goes" in text and "##### Duration distribution" in text
    assert "--create-word-cells=false --create-line-cells=false" in text
    assert "--create-word-cells=true --create-line-cells=true" in text
    assert "--backend pdfium" in text and "--backend threaded" in text
    assert "mktemp -d" in text
    for name in expected:
        assert f"]({name})" in text
    with (output / "native_phases.csv").open(newline="") as stream:
        rows = {row["task"]: row for row in csv.DictReader(stream)}
    assert set(rows) == {"render-only", "render-with-text"}
    assert float(rows["render-only"]["create_word_cells_s"]) == 0
    assert float(rows["render-with-text"]["create_word_cells_s"]) == 0.2


@pytest.mark.parametrize("has_one_thread", [True, False])
def test_ideal_throughput_uses_one_thread_baseline_and_zero_origin(
    tmp_path: Path, monkeypatch, has_one_thread
):
    figure_class = pytest.importorskip("matplotlib.figure").Figure
    figures = {}

    def capture(figure, path, **_kwargs):
        figures[Path(path).name] = figure

    monkeypatch.setattr(figure_class, "savefig", capture)
    summary = []
    for backend, threads, throughput in (
        ("threaded", 1, 10),
        ("threaded", 4, 33),
        ("threaded", 8, 50),
        ("pdfium", 1, 8),
    ):
        if backend == "threaded" and threads == 1 and not has_one_thread:
            continue
        wall = 800 / throughput
        summary.append(
            {
                "workload": "replay",
                "backend": backend,
                "task": "render",
                "threads": threads,
                "median_wall_s": wall,
                "min_wall_s": wall,
                "max_wall_s": wall,
                "pages_per_s": throughput,
                "speedup_vs_docling_1t": throughput / 10,
                "efficiency_pct": 100 * throughput / (10 * threads)
                if backend == "threaded"
                else None,
            }
        )
    _REPORT.plot([], summary, tmp_path)
    throughput_ax = figures["replay_runtime.png"].axes[1]
    assert throughput_ax.get_xlim()[0] == 0
    assert throughput_ax.get_ylim()[0] == 0
    assert 0 in throughput_ax.get_xticks()
    curves = {line.get_label(): line for line in throughput_ax.lines}
    assert list(curves["docling-parse"].get_ydata()) == (
        [10, 33, 50] if has_one_thread else [33, 50]
    )
    assert list(curves["pypdfium2"].get_ydata()) == [8]
    assert "pypdfium2 ideal" not in curves
    assert not any("linear fit" in label for label in curves)
    if has_one_thread:
        ideal = curves["docling-parse ideal"]
        assert ideal.get_linestyle() == "--"
        assert list(ideal.get_xdata()) == [0, 8]
        assert list(ideal.get_ydata()) == [0, 80]
    else:
        assert "docling-parse ideal" not in curves


def _checkpointed_comparison(
    inputs,
    backend="pymupdf",
    task="parse",
    *,
    failed=False,
    wall=2309.7461801249883,
    repetition=1,
):
    scripts = str(Path(__file__).parents[1] / "scripts/benchmarking")
    sys.path.insert(0, scripts)
    try:
        runtime = __import__("_performance_runtime")
        common = __import__("_common")
    finally:
        sys.path.pop(0)
    provenance = inputs.metadata.setdefault(
        "comparison_provenance",
        {
            "packages": {"pypdf": "6.21.1", "pymupdf": "1.27.2.3"},
            "source_revision": "comparison-revision",
            "source_diff_sha256": "comparison-diff",
        },
    )
    mode = "parse-without-bitmap" if task == "parse" else "render"
    directory = inputs.root / f"comparison/r{repetition}" / mode
    directory.mkdir(parents=True, exist_ok=True)
    rows = [
        common.PageRow(
            backend,
            task,
            1,
            "/pdf/doc.pdf",
            page,
            not (failed and page == 2),
            0.5,
            image_width=200 if task == "parse+render" else 0,
            image_height=200 if task == "parse+render" else 0,
            error_message="fixture page error" if failed and page == 2 else "",
        )
        for page in (1, 2)
    ]
    run = SimpleNamespace(
        backend=backend,
        task=task,
        threads=1,
        wall_s=wall,
        per_page_source="wall",
        doc_errors=0,
        samples=rows,
    )
    store = runtime.BenchmarkCheckpoints(directory / "checkpoints", {}, provenance, 2)
    store.save(run)
    path = directory / "fixture.csv"
    with path.open("a") as stream:
        # Write each new series using the same CSV schema as the real runner.
        temporary = directory / "series.tmp"
        common.write_page_rows(temporary, rows)
        lines = temporary.read_text().splitlines(keepends=True)
        stream.writelines(lines if stream.tell() == 0 else lines[1:])
        temporary.unlink()
    log = directory / "run.log"
    label = "docling threaded 1" if backend == "docling-parse" else f"{backend} (1t) -"
    with log.open("a") as stream:
        if stream.tell() == 0:
            stream.write("=== COMPARISON ===\n")
        stream.write(f"{label} {float(f'{wall:.3f}'):.6g} 1.0\n")
    return path, store


@pytest.mark.parametrize(
    "case",
    ["valid", "provenance", "status", "checksum", "missing-page", "native-failure"],
)
def test_reference_failure_is_reported_without_relaxing_integrity(tmp_path: Path, case):
    inputs = _inputs(tmp_path)
    backend = "docling-parse" if case == "native-failure" else "pypdf"
    path, store = _checkpointed_comparison(inputs, backend, failed=True)
    csv_path, state_path = store.paths(backend, "parse", 1)
    state = json.loads(state_path.read_text())
    if case == "provenance":
        state["provenance"] = {}
    elif case == "status":
        state["complete"] = True
    elif case == "checksum":
        csv_path.write_text("changed checkpoint")
    elif case == "missing-page":
        lines = path.read_text().splitlines(keepends=True)
        path.write_text("".join(lines[:-1]))
    state_path.write_text(json.dumps(state))
    if case != "valid":
        message = {
            "provenance": "provenance differs",
            "status": "success status",
            "checksum": "CSV changed",
            "missing-page": "Incomplete comparison page set",
            "native-failure": "Unsuccessful comparison page",
        }[case]
        with pytest.raises(ValueError, match=message):
            inputs.comparison(path)
        return
    assert inputs.comparison(path) == []
    (excluded,) = inputs.excluded_comparisons
    assert (
        excluded["attempted_pages"],
        excluded["successful_pages"],
        excluded["failed_pages"],
    ) == (2, 1, 1)
    assert excluded["wall_s"] == 2309.7461801249883
    assert inputs.comparison_errors[0]["page"] == 2
    assert inputs.comparison_errors[0]["error_message"] == "fixture page error"


@pytest.mark.parametrize("console", ["2309.75", "2309.746", "2309.756"])
def test_checkpoint_wall_is_exact_and_console_rounding_is_validated(
    tmp_path: Path, console
):
    inputs = _inputs(tmp_path)
    path, _store = _checkpointed_comparison(inputs)
    (path.parent / "run.log").write_text(
        f"=== PARSE ===\npymupdf (1t) - {console} 1.0\n"
    )
    if console == "2309.756":
        with pytest.raises(ValueError, match="wall time mismatch"):
            inputs.comparison(path)
    else:
        (measurement,) = inputs.comparison(path)
        assert measurement.wall_s == 2309.7461801249883
        assert "checkpoint wall time" in measurement.timing_basis


@pytest.mark.parametrize("repeats", [1, 2])
def test_report_preserves_failed_reference_audit_without_performance_metrics(
    tmp_path: Path, monkeypatch, repeats
):
    inputs = _profiled_bundle(tmp_path)
    inputs.metadata["comparison"] = True
    inputs.metadata["repeats"] = repeats
    if repeats == 2:
        for parent in [tmp_path / "replay", *(tmp_path / "native").iterdir()]:
            shutil.copytree(parent / "r1", parent / "r2")
    for repetition in range(1, repeats + 1):
        for backend in (
            "docling-parse",
            "pymupdf",
            "pypdfium2",
            "pdfplumber",
            "pdfminer.six",
            "pypdf",
        ):
            _checkpointed_comparison(
                inputs,
                backend,
                failed=backend == "pypdf" and repetition == 1,
                repetition=repetition,
            )
            if backend not in ("pdfminer.six", "pypdf"):
                _checkpointed_comparison(
                    inputs, backend, "parse+render", repetition=repetition
                )
    (tmp_path / "metadata.json").write_text(json.dumps(inputs.metadata))
    measurements = inputs.load()
    assert (
        len(measurements) == 13 * repeats
    )  # Exclude even a successful pypdf repetition.
    assert not any(m.backend == "pypdf" for m in measurements)
    # A second load must not append duplicate failures or exclusions.
    assert len(inputs.load()) == 13 * repeats
    assert len(inputs.excluded_comparisons) == len(inputs.comparison_errors) == 1
    monkeypatch.setattr(_REPORT, "plot", lambda *_args: [])
    output = tmp_path / "report"
    _REPORT.write_report(inputs, measurements, output)
    with (output / "summary.csv").open() as stream:
        assert not any(row["backend"] == "pypdf" for row in csv.DictReader(stream))
    with (output / "excluded_comparisons.csv").open() as stream:
        (excluded,) = list(csv.DictReader(stream))
    assert excluded["failed_pages"] == "1"
    assert "pages_per_s" not in excluded and "speedup_vs_pypdfium2" not in excluded
    with (output / "comparison_errors.csv").open() as stream:
        (error,) = list(csv.DictReader(stream))
    assert error["document"] == "doc.pdf" and error["page"] == "2"
    text = (output / "summary.md").read_text()
    assert (
        "Reference page failures" in text
        and "## Excluded reference comparisons" in text
    )
    assert "fixture page error" in text
    assert "## Understanding the two Python experiments" in text
    assert text.index("## Understanding the two Python experiments") < text.index(
        "### Python scaling with Docling settings"
    )
    assert "Text and geometry via package APIs" in text
    assert "Images plus character text and bounding boxes" not in text
    # Removing a measured series must still fail, rather than treating it as an exclusion.
    (tmp_path / "comparison/r1/parse-without-bitmap/fixture.csv").unlink()
    with pytest.raises(ValueError, match="Missing/unexpected measurements"):
        inputs.load()


def test_failed_pdfium_reference_does_not_discard_other_renderers(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path, _store = _checkpointed_comparison(
        inputs, "pypdfium2", "parse+render", failed=True
    )
    _checkpointed_comparison(inputs, "pymupdf", "parse+render")
    (measurement,) = inputs.comparison(path)
    assert measurement.backend == "pymupdf"
    assert inputs.excluded_comparisons[0]["backend"] == "pypdfium2"
    assert not inputs.canvas_checks  # No complete reference for dimension checks.


@pytest.mark.skipif(
    os.name == "nt", reason="Reproduction commands target Bash on macOS and Linux"
)
@pytest.mark.parametrize(
    "workload,task,count",
    [
        ("native", "render-only", 4),
        ("native", "render-with-text", 4),
        ("replay", "render", 6),
        ("comparison", "parse", 2),
        ("comparison", "parse+render", 2),
    ],
)
def test_reproduction_commands_keep_process_boundaries_and_fresh_outputs(
    tmp_path: Path, workload, task, count
):
    inputs = _profiled_bundle(tmp_path)
    inputs.metadata.update(threads=[1, 4], repeats=2)
    inputs.metadata["dataset"]["root"] = str(tmp_path / "pdf $(touch UNEXPECTED_FILE)")
    binaries = tmp_path / "bin"
    binaries.mkdir()
    (tmp_path / "build").mkdir()
    calls = tmp_path / "calls.jsonl"
    stub = (
        f"#!{sys.executable}\n"
        + """import json,os,sys
with open(os.environ["FIXTURE_COMMANDS"], "a") as stream:
    stream.write(json.dumps(sys.argv[1:])+"\\n")
print("fixture benchmark")
"""
    )
    for path in (binaries / "uv", tmp_path / "build/run_scaling.exe"):
        path.write_text(stub)
        path.chmod(0o755)
    command = "\n".join(_REPORT.reproduction_commands(inputs.metadata, workload, task))
    result = subprocess.run(
        ["bash", "-c", command],
        cwd=tmp_path,
        env={
            **os.environ,
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "FIXTURE_COMMANDS": str(calls),
        },
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    records = [json.loads(line) for line in calls.read_text().splitlines()]
    assert len(records) == count
    assert not (tmp_path / "UNEXPECTED_FILE").exists()
    for record in records:
        assert inputs.metadata["dataset"]["root"] in record
    if workload == "native":
        enabled = str(task == "render-with-text").lower()
        assert all(
            f"--create-word-cells={enabled}" in r
            and f"--create-line-cells={enabled}" in r
            for r in records
        )
        assert len({r[r.index("--timing-csv") + 1] for r in records}) == count
        assert [r[r.index("--threads") + 1] for r in records] == ["1", "4", "1", "4"]
    elif workload == "replay":
        assert [r[r.index("--backend") + 1] for r in records] == [
            "pdfium",
            "threaded",
            "threaded",
        ] * 2
        assert len({r[r.index("--output-dir") + 1] for r in records}) == count
    else:
        assert all(
            "--materialize-char-cells=true" in r and "--materialize-bitmaps=false" in r
            for r in records
        )
        assert all(
            r[r.index("--mode") + 1]
            == ("render" if task == "parse+render" else "parse-without-bitmap")
            for r in records
        )


def test_native_scaling_uses_corpus_wall_not_summed_worker_time(tmp_path: Path):
    inputs = _inputs(tmp_path)
    (measurement,) = inputs.native(_native(tmp_path))
    assert measurement.wall_s == 20
    assert sum(measurement.durations) == 6
    parallel = _REPORT.Measurement(
        "native",
        "docling-parse",
        "render",
        4,
        1,
        5,
        2,
        1,
        "log",
        "wall",
        durations=[3, 3],
    )
    rows = _REPORT.summarize([measurement, parallel])
    assert rows[1]["speedup_vs_docling_1t"] == 4
    assert rows[1]["speedup_vs_pypdfium2"] is None
    assert rows[1]["efficiency_pct"] == 100
    assert rows[1]["pages_per_s"] == 0.4


def test_replay_speedup_baselines_are_separate_from_native():
    measurements = [
        _REPORT.Measurement(
            "native", "docling-parse", "render", 1, 1, 100, 2, 1, "log", "wall"
        ),
        _REPORT.Measurement(
            "replay", "pdfium", "render", 1, 1, 40, 2, 1, "json", "wall"
        ),
        _REPORT.Measurement(
            "replay", "threaded", "render", 1, 1, 20, 2, 1, "json", "wall"
        ),
        _REPORT.Measurement(
            "replay", "threaded", "render", 4, 1, 5, 2, 1, "json", "wall"
        ),
    ]
    rows = {
        (r["workload"], r["backend"], r["threads"]): r
        for r in _REPORT.summarize(measurements)
    }
    parallel = rows["replay", "threaded", 4]
    assert parallel["speedup_vs_docling_1t"] == 4
    assert parallel["speedup_vs_pypdfium2"] == 8
    assert parallel["efficiency_pct"] == 100
    reference = rows["replay", "pdfium", 1]
    assert reference["speedup_vs_docling_1t"] == 0.5
    assert reference["speedup_vs_pypdfium2"] == 1
    assert reference["efficiency_pct"] is None
    assert rows["native", "docling-parse", 1]["speedup_vs_pypdfium2"] is None


def test_native_rejects_appended_duplicate_pages(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path = _native(tmp_path)
    with path.open("a") as stream:
        stream.write(path.read_text().splitlines()[1] + "\n")
    with pytest.raises(ValueError, match="Duplicate native page"):
        inputs.native(path)


@pytest.mark.parametrize("corrupt_totals", [False, True])
def test_native_reads_precise_sidecar_summary(tmp_path: Path, corrupt_totals):
    inputs = _inputs(tmp_path)
    path = _native(tmp_path)
    log = path.with_suffix(".log")
    counts, summary = log.read_text().split("Timing diagnostics", 1)
    summary = "Timing diagnostics" + summary.replace("20.000000", "20.123456")
    if corrupt_totals:
        summary = summary.replace("decode_page_s 1.600000", "decode_page_s 9.000000")
    sidecar = Path(str(path) + ".summary.log")
    sidecar.write_text(summary)
    log.write_text(counts + "threads=1: 20.123s\n")
    if corrupt_totals:
        with pytest.raises(ValueError, match="stage sum disagrees"):
            inputs.native(path)
    else:
        (measurement,) = inputs.native(path)
        assert measurement.wall_s == 20.123456
        assert measurement.source == str(sidecar.relative_to(tmp_path))


def test_native_rejects_missing_pages(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path = _native(tmp_path)
    path.write_text("\n".join(path.read_text().splitlines()[:-1]) + "\n")
    with pytest.raises(ValueError, match="Incomplete native page set"):
        inputs.native(path)


def test_native_rejects_csv_log_stage_disagreement(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path = _native(tmp_path)
    log = path.with_suffix(".log")
    log.write_text(
        log.read_text().replace("decode_page_s 1.600000", "decode_page_s 9.000000")
    )
    with pytest.raises(ValueError, match="stage sum disagrees"):
        inputs.native(path)


def test_replay_includes_construction_load_and_unload(tmp_path: Path):
    inputs = _inputs(tmp_path)
    path = tmp_path / "replay/r1/t1/report.json"
    path.parent.mkdir(parents=True)
    pages = path.parent / "pages.jsonl"
    pages.write_text(
        "".join(
            json.dumps(
                {
                    "document": "/pdf/doc.pdf",
                    "page": page,
                    "image_mode": "RGB",
                    "image_size": [200, 200],
                }
            )
            + "\n"
            for page in (1, 2)
        )
    )
    path.write_text(
        json.dumps(
            {
                "provenance": {
                    "parse_git": {"revision": "revision", "diff_sha256": "diff"}
                },
                "runs": [
                    {
                        "settings": {
                            "backend": "threaded",
                            "requested_scale": 1.0,
                            "parser": {"threads": 1},
                        },
                        "documents": [
                            {
                                "document": "/pdf/doc.pdf",
                                "pages": 2,
                                "construction_load_s": 7,
                                "page_processing_s": 10,
                                "unload_s": 3,
                                "total_s": 20,
                            }
                        ],
                        "pages_file": str(pages),
                    }
                ],
            }
        )
    )
    (measurement,) = inputs.replay(path)
    assert measurement.wall_s == 20
    assert measurement.phases["page_processing_s"] == 10
    assert measurement.durations == [20]  # document durations, not page times


@pytest.mark.parametrize("change", [None, "patch", "script", "dirty", "missing-commit"])
def test_mid_run_commit_requires_exact_source_equivalence(tmp_path: Path, change):
    repository = tmp_path / "repo"
    repository.mkdir()

    def git(*args):
        return subprocess.check_output(
            ["git", "-c", "user.name=Test", "-c", "user.email=test@example.com", *args],
            cwd=repository,
            text=True,
        ).strip()

    git("init", "-q")
    script = (
        repository
        / "scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py"
    )
    script.parent.mkdir(parents=True)
    script.write_text("print('before')\n")
    git("add", ".")
    git("commit", "-qm", "before")
    original = git("rev-parse", "HEAD")
    script.write_text("print('after')\n")
    patch = git("diff", "HEAD")
    metadata = {
        "source_revision": original,
        "source_diff_sha256": hashlib.sha256(patch.encode()).hexdigest(),
    }
    git("add", ".")
    git("commit", "-qm", "recorded working tree committed during benchmark")
    parse_git = {
        "revision": git("rev-parse", "HEAD"),
        "diff_sha256": hashlib.sha256(b"").hexdigest(),
    }
    provenance = {
        "parse_git": parse_git,
        "script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
    }
    if change == "patch":
        metadata["source_diff_sha256"] = "0" * 64
    elif change == "script":
        provenance["script_sha256"] = "0" * 64
    elif change == "dirty":
        parse_git["diff_sha256"] = "0" * 64
    elif change == "missing-commit":
        parse_git["revision"] = "0" * 40
    assert _REPORT.committed_source_matches(metadata, provenance, repository) == (
        change is None
    )


def test_report_rejects_missing_profiles(tmp_path: Path):
    inputs = _inputs(tmp_path)
    with pytest.raises(ValueError, match="Missing/unexpected measurements"):
        inputs.load()


@pytest.mark.parametrize("value", [float("nan"), float("inf"), -1])
def test_invalid_durations_are_rejected(value):
    with pytest.raises(ValueError, match="Invalid duration"):
        _REPORT.seconds(value)


@pytest.mark.skipif(os.name == "nt", reason="The Bash launcher targets macOS and Linux")
@pytest.mark.parametrize(
    "failed_phase,exit_code", [("dependencies", 17), ("build", 19)]
)
@pytest.mark.parametrize(
    "dataset,revision",
    [
        ("", ""),
        ("bo20", ""),
        ("bo767", ""),
        ("bo10k", ""),
        ("bo20", "fixture-revision"),
    ],
)
def test_launcher_archives_old_builds_and_surfaces_failures(
    tmp_path: Path, failed_phase, exit_code, dataset, revision
):
    # Execute the actual launcher in a disposable repository. Stub only uv and
    # macOS discovery: no project directories, builds or downloads are touched.
    repo, launcher = _copy_launcher(tmp_path)
    for name in ("build", "externals"):
        directory = repo / name
        directory.mkdir()
        (directory / "old-sdk-cache").write_text("preserve this")
    sdk = tmp_path / "SDK With Spaces.sdk"
    sdk.mkdir()
    binaries = tmp_path / "bin"
    binaries.mkdir()
    stubs = {
        "uname": "#!/bin/bash\necho Darwin\n",
        "xcrun": '#!/bin/bash\nprintf "%s\\n" "$FIXTURE_SDK"\n',
        "uv": "#!/bin/bash\n"
        "if [[ $FAIL_PHASE == build && $1 == sync ]]; then exit 0; fi\n"
        'printf "\\rfixture progress: [#-] 1/2" >&2\n'
        'printf "fixture failure; SDKROOT=%s\\n" "$SDKROOT" >&2\n'
        'exit "$FAIL_CODE"\n',
    }
    for name, body in stubs.items():
        path = binaries / name
        path.write_text(body)
        path.chmod(0o755)
    out = tmp_path / "results"
    environment = {
        **os.environ,
        "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
        "OUT": str(out),
        "FIXTURE_SDK": str(sdk),
        "FAIL_PHASE": failed_phase,
        "FAIL_CODE": str(exit_code),
        "REPEATS": "1",
        "THREADS": "1",
        "RUN_COMPARISON": "0",
        "BUILD_THREADS": "1",
        "BUILD_MODE": "clean",
        "DATASET": dataset,
        "DATASET_REVISION": revision,
        "BENCHMARK_OUTPUT": "compact",
    }
    result = subprocess.run(
        ["bash", str(launcher)], env=environment, text=True, capture_output=True
    )
    assert result.returncode == exit_code
    selected = dataset or "bo767"
    assert (
        f"Dataset: docling-project/performance-dataset-{selected} at "
        f"{revision or _DATASET_PINS[selected]}"
    ) in result.stdout
    assert f"FAILED (exit {exit_code})" in result.stderr
    assert str(out / f"{failed_phase}.log") in result.stderr
    assert f"fixture failure; SDKROOT={sdk}" in result.stderr
    assert "fixture progress: [#-] 1/2" in result.stdout
    assert "fixture progress: [#-] 1/2" in (out / f"{failed_phase}.log").read_text()
    assert "Setup 1/5: install" in result.stdout
    if failed_phase == "build":
        assert "Completed: Setup 1/5:" in result.stdout
        assert "s command elapsed)" in result.stdout
        assert "Setup 2/5: build" in result.stdout
    assert "Stopped." in result.stderr
    assert "Complete." not in result.stdout
    assert not (out / "native").exists()
    for name in ("build", "externals"):
        assert not (repo / name).exists()
        assert (out / f"previous-{name}/old-sdk-cache").read_text() == "preserve this"


@pytest.mark.skipif(os.name == "nt", reason="The Bash launcher targets macOS and Linux")
@pytest.mark.parametrize(
    "setting,value,message",
    [
        ("DATASET", "bo200", "DATASET must be bo20, bo767 or bo10k"),
        ("BENCHMARK_OUTPUT", "quiet", "BENCHMARK_OUTPUT must be compact or full"),
        ("BUILD_MODE", "skip", "BUILD_MODE must be incremental or clean"),
    ],
)
def test_launcher_rejects_invalid_settings_before_archiving(
    tmp_path: Path, setting, value, message
):
    repo, launcher = _copy_launcher(tmp_path)
    build = repo / "build"
    build.mkdir()
    marker = build / "old-build"
    marker.write_text("preserve this")
    binaries = tmp_path / "bin"
    binaries.mkdir()
    uv = binaries / "uv"
    uv.write_text("#!/bin/sh\nexit 99\n")
    uv.chmod(0o755)
    out = tmp_path / "results"
    result = subprocess.run(
        ["bash", str(launcher)],
        env={
            **os.environ,
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "DATASET": "bo767",
            "BENCHMARK_OUTPUT": "compact",
            "BUILD_MODE": "incremental",
            "OUT": str(out),
            setting: value,
        },
        text=True,
        capture_output=True,
    )
    assert result.returncode == 2
    assert message in result.stderr
    assert marker.read_text() == "preserve this"
    assert not out.exists()


@pytest.mark.parametrize("dataset", ["bo20", "bo767", "bo10k"])
@pytest.mark.parametrize("default_snapshot", [False, True])
def test_launcher_downloads_selected_repository_and_checks_snapshot_counts(
    tmp_path: Path, monkeypatch, dataset, default_snapshot
):
    # Execute the launcher's actual metadata block with tiny synthetic inputs;
    # mock downloads, page counting and build provenance, never a real benchmark.
    repo, launcher = _copy_launcher(tmp_path)
    source = launcher.read_text().split("<<'PY'\n", 1)[1].split("\nPY\n", 1)[0]
    repository = f"docling-project/performance-dataset-{dataset}"
    revision = _DATASET_PINS[dataset] if default_snapshot else "a" * 40
    snapshot = tmp_path / "snapshots" / revision
    pdfs = snapshot / "pdf"
    pdfs.mkdir(parents=True)
    (pdfs / "b.pdf").write_text("1")
    (pdfs / "a.pdf").write_text("2")
    out = tmp_path / "results"
    out.mkdir()
    extension = repo / "fixture-extension.so"
    extension.write_bytes(b"fixture extension")
    (repo / "build").mkdir()
    (repo / "build/run_scaling.exe").write_bytes(b"fixture native")
    header = repo / "externals/include/qpdf/DLL.h"
    header.parent.mkdir(parents=True)
    header.write_text('#define QPDF_VERSION "12.4.2"\n')
    downloads = []

    def download(**kwargs):
        downloads.append(kwargs)
        return str(snapshot)

    check_output = subprocess.check_output

    def provenance_output(command, **kwargs):
        if command[0] in {"git", "sysctl"}:
            return "fixture"
        return check_output(command, **kwargs)

    class PdfDocument:
        def __init__(self, path):
            self.pages = int(Path(path).read_text())

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def __len__(self):
            return self.pages

    monkeypatch.chdir(repo)
    monkeypatch.setitem(
        sys.modules, "huggingface_hub", SimpleNamespace(snapshot_download=download)
    )
    monkeypatch.setitem(
        sys.modules, "pypdfium2", SimpleNamespace(PdfDocument=PdfDocument)
    )
    monkeypatch.setitem(
        sys.modules, "tqdm", SimpleNamespace(tqdm=lambda items, **_kw: items)
    )
    monkeypatch.setattr(
        importlib.util,
        "find_spec",
        lambda _name: SimpleNamespace(origin=str(extension)),
    )
    monkeypatch.setattr(importlib.metadata, "version", lambda _name: "fixture")
    monkeypatch.setattr(subprocess, "check_output", provenance_output)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "-",
            str(out),
            "fixture-branch",
            "1",
            "1,4",
            "0",
            repository,
            _DATASET_PINS[dataset],
        ],
    )
    if default_snapshot:
        with pytest.raises(SystemExit, match=f"Unexpected counts for {repository}"):
            exec(compile(source, str(launcher), "exec"), {})
        assert not (out / "metadata.json").exists()
    else:
        exec(compile(source, str(launcher), "exec"), {})
        metadata = json.loads((out / "metadata.json").read_text())
        assert metadata["dataset"]["repository"] == repository
        assert metadata["dataset"]["requested_revision"] == "fixture-branch"
        assert metadata["dataset"]["revision"] == revision
        assert [f["name"] for f in metadata["dataset"]["files"]] == ["a.pdf", "b.pdf"]
        assert sum(f["pages"] for f in metadata["dataset"]["files"]) == 3
        assert (
            metadata["dataset"]["files"][0]["sha256"]
            == hashlib.sha256(b"2").hexdigest()
        )
        assert (out / "dataset-revision.txt").read_text().strip() == revision
    assert downloads == [
        {
            "repo_id": repository,
            "repo_type": "dataset",
            "revision": "fixture-branch",
            "allow_patterns": ["pdf/**"],
        }
    ]


@pytest.mark.parametrize("suppress", [False, True])
def test_replay_config_suppression_preserves_parser_initialization(suppress, capsys):
    path = (
        Path(__file__).parents[1]
        / "scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py"
    )
    function = next(
        node
        for node in ast.parse(path.read_text()).body
        if isinstance(node, ast.FunctionDef) and node.name == "print_settings"
    )
    level = SimpleNamespace(name="COMPUTE")
    content = SimpleNamespace(
        **dict.fromkeys(
            (
                "char_cells_content_level",
                "word_cells_content_level",
                "line_cells_content_level",
                "shapes_content_level",
                "bitmaps_content_level",
            ),
            level,
        ),
        include_bitmap_bytes=False,
    )
    config = SimpleNamespace(
        page_content_config=content,
        max_concurrent_results=32,
        boundary_type=SimpleNamespace(value="crop_box"),
        render_config=SimpleNamespace(to_string=lambda: "render settings"),
    )
    initialized = []

    def parser(**kwargs):
        initialized.append(kwargs)
        return SimpleNamespace(
            _parser=SimpleNamespace(
                decode_config=SimpleNamespace(to_string=lambda: "decode settings")
            )
        )

    namespace = {
        "configurations": lambda _count: (config, "decode"),
        "DoclingThreadedPdfParser": parser,
        "sys": sys,
    }
    exec(
        compile(ast.Module(body=[function], type_ignores=[]), str(path), "exec"),
        namespace,
    )
    namespace["print_settings"]("threaded", [1], "1", suppress=suppress)
    assert initialized == [{"parser_config": config, "decode_config": "decode"}]
    output = capsys.readouterr().out
    if suppress:
        assert output == ""
    else:
        assert "Benchmark config:" in output
        assert "decode settings" in output and "render settings" in output


@pytest.mark.skipif(os.name == "nt", reason="The Bash launcher targets macOS and Linux")
@pytest.mark.parametrize("output_mode", ["", "full"])
@pytest.mark.parametrize("build_mode", ["", "clean"])
def test_launcher_routes_output_options_to_all_runners(
    tmp_path: Path, output_mode, build_mode
):
    # Exercise the complete launcher with fake commands. No real build, PDF
    # processing, network access or report generation takes place.
    repo, launcher = _copy_launcher(tmp_path)
    for directory in ("build", "externals"):
        (repo / directory).mkdir()
        (repo / directory / "keep-me").write_text("existing output")
    binaries = tmp_path / "bin"
    binaries.mkdir()
    native = tmp_path / "native-fixture"
    native.write_text(
        '#!/bin/bash\nprintf "native-args: %s\\n" "$*"\nprintf "\\rfixture progress: [##] 2/2\\n"\n'
    )
    native.chmod(0o755)
    stubs = {
        "uname": "#!/bin/sh\necho Linux\n",
        "uv": r"""#!/bin/bash
set -eu
case "$*" in
  *local_build.py*)
    mkdir -p build
    cp "$FIXTURE_NATIVE" build/run_scaling.exe
    : > build/CMakeCache.txt
    ;;
  *"python - "*)
    cat > /dev/null
    printf '%s\n' "$FIXTURE_PDFS" > "$5/dataset-path.txt"
    printf '%s\n' "$6" > "$5/dataset-revision.txt"
    ;;
  *run_performance_benchmarking_with_docling_settings.py*|*run_performance_benchmarking.py*)
    printf 'runner-args: %s\n' "$*"
    printf '\rfixture progress: [##] 2/2\n'
    ;;
  *run_performance_report.py*) printf 'Report ready\n' ;;
  *_performance_runtime.py*) printf 'Reference preflight passed\n' ;;
  sync*|*" cmake "*|*" pytest "*) ;;
  *) exit 99 ;;
esac
""",
    }
    for name, body in stubs.items():
        executable = binaries / name
        executable.write_text(body)
        executable.chmod(0o755)
    out = tmp_path / "results"
    result = subprocess.run(
        ["bash", str(launcher)],
        env={
            **os.environ,
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "OUT": str(out),
            "FIXTURE_NATIVE": str(native),
            "FIXTURE_PDFS": str(tmp_path / "pdf"),
            "REPEATS": "1",
            "THREADS": "1",
            "RUN_COMPARISON": "1",
            "BUILD_THREADS": "1",
            "BUILD_MODE": build_mode,
            "DATASET": "bo20",
            "DATASET_REVISION": "fixture-revision",
            "BENCHMARK_OUTPUT": output_mode,
        },
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr + result.stdout
    assert "Complete. Tables:" in result.stdout
    assert "fixture progress: [##] 2/2" in result.stdout
    compact = output_mode != "full"
    native_log = (out / "native/render-only/r1/t1.log").read_text()
    assert ("--suppress-timings" in native_log) == compact
    assert ("-v WARNING" in native_log) == compact
    logs = [
        out / "native/render-only/r1/t1.log",
        out / "native/render-with-text/r1/t1.log",
        out / "replay/r1/pdfium.log",
        out / "replay/r1/t1.log",
        out / "comparison/r1/render/run.log",
        out / "comparison/r1/parse-without-bitmap/run.log",
    ]
    for log in logs:
        assert ("--suppress-config" in log.read_text()) == compact
    for profile, value in (("render-only", "false"), ("render-with-text", "true")):
        profile_log = (out / f"native/{profile}/r1/t1.log").read_text()
        assert f"--create-word-cells={value}" in profile_log
        assert f"--create-line-cells={value}" in profile_log
    assert "Experiment 6/6" in result.stdout
    assert result.stdout.index("Reference preflight passed") < result.stdout.index(
        "Experiment 1/6"
    )
    for directory in ("build", "externals"):
        if build_mode == "clean":
            assert (
                out / f"previous-{directory}/keep-me"
            ).read_text() == "existing output"
            assert not (repo / directory / "keep-me").exists()
        else:
            assert (repo / directory / "keep-me").read_text() == "existing output"
            assert not (out / f"previous-{directory}").exists()


@pytest.mark.skipif(os.name == "nt", reason="The Bash launcher targets macOS and Linux")
@pytest.mark.parametrize(
    "condition,diagnostic",
    [
        ("compatible", "CMake cache paths are compatible"),
        ("source", "cached source directory"),
        ("python", "PYTHON_EXECUTABLE"),
        ("version", "cached Python version"),
        ("sdk", "cached SDK"),
        ("compiler", "CMAKE_CXX_COMPILER executable is missing"),
        ("compiler-selection", "CXX selects"),
        ("generator", "CMAKE_GENERATOR selects"),
        ("missing-venv", "without .venv/bin/python"),
        ("after-sync", "cached Python version"),
    ],
)
def test_incremental_launcher_checks_cache_before_building(
    tmp_path: Path, condition, diagnostic
):
    repo, launcher = _copy_launcher(tmp_path)
    build = repo / "build"
    build.mkdir()
    marker = build / "keep-me"
    marker.write_text("existing build")
    externals = repo / "externals"
    externals.mkdir()
    (externals / "keep-me").write_text("existing dependencies")
    python = repo / ".venv/bin/python"
    python.parent.mkdir(parents=True)
    if condition != "missing-venv":
        python.symlink_to(sys.executable)
    sdk = tmp_path / "SDK With Spaces.sdk"
    sdk.mkdir()
    binaries = tmp_path / "bin"
    binaries.mkdir()
    compiler = binaries / "fixture-compiler"
    compiler.write_text("#!/bin/sh\nexit 0\n")
    compiler.chmod(0o755)
    other_compiler = binaries / "other-compiler"
    other_compiler.write_text("#!/bin/sh\nexit 0\n")
    other_compiler.chmod(0o755)
    cache = {
        "CMAKE_HOME_DIRECTORY": str(repo),
        "PYTHON_EXECUTABLE": str(python),
        "PYTHON_VERSION": sys.version.split()[0],
        "CMAKE_OSX_SYSROOT": str(sdk),
        "CMAKE_C_COMPILER": str(compiler),
        "CMAKE_CXX_COMPILER": str(compiler),
        "CMAKE_GENERATOR": "Unix Makefiles",
    }
    if condition == "source":
        cache["CMAKE_HOME_DIRECTORY"] = str(tmp_path / "old-checkout")
    elif condition == "python":
        cache["PYTHON_EXECUTABLE"] = str(tmp_path / "old-python")
    elif condition == "version":
        cache["PYTHON_VERSION"] = "0.0.0"
    elif condition == "sdk":
        cache["CMAKE_OSX_SYSROOT"] = str(tmp_path / "Old SDK.sdk")
    elif condition == "compiler":
        cache["CMAKE_CXX_COMPILER"] = str(tmp_path / "old-compiler")
    (build / "CMakeCache.txt").write_text(
        "\n".join(f"{key}:STRING={value}" for key, value in cache.items()) + "\n"
    )
    stubs = {
        "uname": "#!/bin/sh\necho Darwin\n",
        "xcrun": '#!/bin/sh\nprintf "%s\\n" "$FIXTURE_SDK"\n',
        "uv": r"""#!/bin/bash
printf 'fixture uv invoked\n'
if [[ $FIXTURE_AFTER_SYNC == 1 ]]; then
  printf 'PYTHON_VERSION:INTERNAL=0.0.0\n' >> build/CMakeCache.txt
  exit 0
fi
exit 29
""",
    }
    for name, body in stubs.items():
        path = binaries / name
        path.write_text(body)
        path.chmod(0o755)
    out = tmp_path / "results"
    result = subprocess.run(
        ["bash", str(launcher)],
        env={
            **os.environ,
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "OUT": str(out),
            "FIXTURE_SDK": str(sdk),
            "FIXTURE_AFTER_SYNC": "1" if condition == "after-sync" else "0",
            "BUILD_MODE": "incremental",
            "BENCHMARK_OUTPUT": "compact",
            "DATASET": "bo20",
            "DATASET_REVISION": "",
            "THREADS": "1",
            "REPEATS": "1",
            "BUILD_THREADS": "1",
            "RUN_COMPARISON": "0",
            "CC": "",
            "CXX": str(other_compiler) if condition == "compiler-selection" else "",
            "CMAKE_GENERATOR": "Ninja" if condition == "generator" else "",
        },
        text=True,
        capture_output=True,
    )
    assert diagnostic in result.stdout + result.stderr
    if condition == "compatible":
        assert result.returncode == 29
    else:
        assert result.returncode == 1
        assert "BUILD_MODE=clean" in result.stderr
    assert ("fixture uv invoked" in result.stdout) == (
        condition in {"compatible", "after-sync"}
    )
    assert not (out / "build.log").exists()
    assert not (out / "previous-build").exists()
    assert not (out / "previous-externals").exists()
    assert marker.read_text() == "existing build"
    assert (externals / "keep-me").read_text() == "existing dependencies"


@pytest.mark.skipif(
    os.name == "nt", reason="The launcher targets Bash on macOS and Linux"
)
@pytest.mark.parametrize("clean", [False, True])
def test_launcher_recovery_dispatch_keeps_existing_builds(tmp_path: Path, clean):
    repo, launcher = _copy_launcher(tmp_path)
    build = repo / "build"
    build.mkdir()
    (build / "keep-me").write_text("native build")
    binaries = tmp_path / "bin"
    binaries.mkdir()
    uv = binaries / "uv"
    uv.write_text('#!/bin/sh\nprintf "RECOVERY INVOKED\\n"\nprintf "%s\\n" "$@"\n')
    uv.chmod(0o755)
    source = tmp_path / "original bundle"
    output = tmp_path / "new bundle"
    result = subprocess.run(
        ["bash", str(launcher)],
        env={
            **os.environ,
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "RECOVER_FROM": str(source),
            "OUT": str(output),
            "BUILD_MODE": "clean" if clean else "incremental",
        },
        text=True,
        capture_output=True,
    )
    assert (build / "keep-me").read_text() == "native build"
    assert not output.exists()
    if clean:
        assert result.returncode == 2
        assert "recorded native binaries" in result.stderr
        assert "RECOVERY INVOKED" not in result.stdout
    else:
        assert result.returncode == 0
        assert str(source) in result.stdout.splitlines()
        assert str(output) in result.stdout.splitlines()
        assert "scripts/benchmarking/recover_performance_benchmarks.py" in result.stdout


def _recovery_module():
    spec = importlib.util.spec_from_file_location(
        "performance_recovery_tests",
        Path(__file__).parents[1]
        / "scripts/benchmarking/recover_performance_benchmarks.py",
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    sys.path.insert(0, str(Path(__file__).parents[1] / "scripts/benchmarking"))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def _recovery_fixture(tmp_path, monkeypatch):
    recovery = _recovery_module()
    source = tmp_path / "source"
    source.mkdir()
    inputs = _profiled_bundle(source)
    meta = inputs.metadata
    pdf = tmp_path / "doc.pdf"
    pdf.write_bytes(b"fixture PDF identity")
    meta["dataset"]["files"][0].update(path=str(pdf), sha256=recovery.digest(pdf))
    meta["script_sha256"] = {
        "scripts/benchmarking/" + name: recovery.digest(
            recovery.ROOT / "scripts/benchmarking" / name
        )
        for name in (
            "run_performance_benchmarking_with_docling_settings.py",
            "_performance_memory.py",
        )
    }
    meta["memory_collection"] = {"mode": "off", "interval_ms": None}
    meta["comparison_memory"] = dict(meta["memory_collection"])
    (source / "metadata.json").write_text(json.dumps(meta))
    provenance = {
        "source_revision": "recovery-revision",
        "source_diff_sha256": "recovery-diff",
        "script_sha256": {Path(k).name: v for k, v in meta["script_sha256"].items()},
    }
    monkeypatch.setattr(recovery, "validate_environment", lambda *args: None)
    monkeypatch.setattr(
        recovery,
        "reference_preflight",
        lambda *args: pytest.fail("Comparisons were not requested"),
    )
    return recovery, source, meta, provenance


@pytest.mark.parametrize("partial_row", [False, True])
def test_recovery_resumes_only_partial_or_missing_core_results(
    tmp_path, monkeypatch, partial_row
):
    recovery, source, meta, provenance = _recovery_fixture(tmp_path, monkeypatch)
    baseline = tmp_path / "baseline"
    shutil.copytree(source, baseline)
    missing = Path("replay/r1/pdfium/report.json")
    (source / missing).unlink()
    partial = Path("native/render-only/r1/t1.csv")
    if partial_row:
        lines = (source / partial).read_text().splitlines()
        (source / partial).write_text(
            lines[0] + "\n" + ",".join(lines[1].split(",")[:8]) + "\n"
        )
    else:
        (source / partial).write_text("schema_version,mode\n")
    original = {
        p.relative_to(source): p.read_bytes() for p in source.rglob("*") if p.is_file()
    }
    output = tmp_path / "recovered"
    output.mkdir()
    resumed = recovery.prepare_recovery(source, output, provenance)
    assert set(resumed["recovery"]["pending_core"]) == {str(missing), str(partial)}
    assert resumed["comparison"] is False
    assert resumed["threads"] == meta["threads"]
    assert resumed["repeats"] == meta["repeats"]
    assert resumed["memory_collection"] == meta["memory_collection"]
    assert (output / "replay/r1/t1").is_symlink()
    assert not (output / "native").is_symlink()
    executed = []

    def execute(command, log, commands_path=None):
        executed.append(command)
        if "--timing-csv" in command:
            artifact = Path(command[command.index("--timing-csv") + 1])
            shutil.copy2(baseline / partial, artifact)
            shutil.copy2((baseline / partial).with_suffix(".log"), log)
        else:
            directory = Path(command[command.index("--output-dir") + 1])
            shutil.copytree(baseline / missing.parent, directory)
            report = json.loads((directory / "report.json").read_text())
            report["provenance"] = {
                "parse_git": {
                    "revision": provenance["source_revision"],
                    "diff_sha256": provenance["source_diff_sha256"],
                },
                "script_sha256": provenance["script_sha256"][
                    "run_performance_benchmarking_with_docling_settings.py"
                ],
            }
            (directory / "report.json").write_text(json.dumps(report))

    monkeypatch.setattr(recovery, "run_logged", execute)
    recovery.resume_core(resumed, output, True)
    assert len(executed) == 2
    assert len(recovery.ReportInputs(output).load()) == 4
    assert original == {
        p.relative_to(source): p.read_bytes() for p in source.rglob("*") if p.is_file()
    }
    # A second interruption/recovery can reuse results collected in the first recovery.
    next_output = tmp_path / "recovered-again"
    next_output.mkdir()
    again = recovery.prepare_recovery(output, next_output, provenance)
    assert again["recovery"]["pending_core"] == []
    assert len(recovery.ReportInputs(next_output).load()) == 4


def test_recovery_rejects_changed_replay_provenance(tmp_path, monkeypatch):
    recovery, source, _, provenance = _recovery_fixture(tmp_path, monkeypatch)
    report_path = source / "replay/r1/t1/report.json"
    report = json.loads(report_path.read_text())
    report["provenance"]["parse_git"]["diff_sha256"] = "changed"
    report_path.write_text(json.dumps(report))
    output = tmp_path / "out"
    output.mkdir()
    with pytest.raises(ValueError, match="source diff changed"):
        recovery.prepare_recovery(source, output, provenance)
    assert not (output / "metadata.json").exists()


def test_recovery_rejects_dataset_changes(tmp_path, monkeypatch):
    recovery, source, meta, provenance = _recovery_fixture(tmp_path, monkeypatch)
    Path(meta["dataset"]["files"][0]["path"]).write_bytes(b"changed PDF")
    output = tmp_path / "out"
    output.mkdir()
    with pytest.raises(ValueError, match="dataset differs"):
        recovery.prepare_recovery(source, output, provenance)


@pytest.mark.parametrize("change", ["script", "memory", "environment"])
def test_incomplete_recovery_rejects_incompatible_resume_settings(
    tmp_path, monkeypatch, change
):
    recovery, source, meta, provenance = _recovery_fixture(tmp_path, monkeypatch)
    (source / "replay/r1/pdfium/report.json").unlink()
    override = None
    if change == "script":
        meta["script_sha256"][
            "scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py"
        ] = "changed"
    elif change == "memory":
        override = {"mode": "profile", "interval_ms": 50}
    else:
        meta["environment"] = {"DOCLING_PARSE_FALLBACK_FONT": "original-font.ttf"}
        monkeypatch.delenv("DOCLING_PARSE_FALLBACK_FONT", raising=False)
    (source / "metadata.json").write_text(json.dumps(meta))
    output = tmp_path / "out"
    output.mkdir()
    with pytest.raises(ValueError, match=r"changed|memory settings"):
        recovery.prepare_recovery(source, output, provenance, override)
    assert not (output / "metadata.json").exists()


def test_resume_command_preserves_original_profile_instrumentation(
    tmp_path, monkeypatch
):
    recovery, _, meta, _ = _recovery_fixture(tmp_path, monkeypatch)
    meta["memory_collection"] = {"mode": "profile", "interval_ms": 75}
    for job in recovery.core_jobs(meta):
        command, log = recovery.core_command(meta, job, tmp_path, True)
        assert command[command.index("--memory-mode") + 1] == "profile"
        assert command[command.index("--memory-interval-ms") + 1] == "75"
        assert command[command.index("--output") + 1] == str(
            log.with_suffix(".memory.json")
        )
        if job.workload == "native":
            assert (
                f"--create-word-cells={str(job.profile != 'render-only').lower()}"
                in command
            )
        else:
            assert command[command.index("--backend") + 1] == job.profile


@pytest.mark.skipif(os.name == "nt", reason="Bash launcher")
@pytest.mark.parametrize("explicit", [False, True])
def test_recovery_launcher_preserves_inherited_memory_defaults(tmp_path, explicit):
    _, launcher = _copy_launcher(tmp_path)
    binaries = tmp_path / "bin"
    binaries.mkdir()
    uv = binaries / "uv"
    uv.write_text(
        '#!/bin/sh\nprintf "%s\\n" "$@"\nprintf "memory_env=%s\\n" "${MEMORY_MODE-unset}"\n'
    )
    uv.chmod(0o755)
    env = {
        k: v
        for k, v in os.environ.items()
        if k not in ("MEMORY_MODE", "MEMORY_INTERVAL_MS", "BUILD_MODE", "OUT")
    }
    env.update(
        PATH=str(binaries) + os.pathsep + os.environ["PATH"], RECOVER_FROM="source"
    )
    arguments = (
        ["--memory-mode", "profile", "--memory-interval-ms", "75"] if explicit else []
    )
    result = subprocess.run(
        ["bash", str(launcher), *arguments], env=env, text=True, capture_output=True
    )
    assert result.returncode == 0, result.stderr
    assert "memory_env=unset" in result.stdout
    if explicit:
        assert "--memory-mode\nprofile\n--memory-interval-ms\n75" in result.stdout
    else:
        assert "--memory-mode" not in result.stdout
