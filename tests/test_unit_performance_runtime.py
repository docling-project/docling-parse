"""Durable reference checkpoints and safe recovery of completed core results."""

import copy
import importlib
import json
import os
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

_SCRIPTS = Path(__file__).parents[1] / "scripts/benchmarking"
sys.path.insert(0, str(_SCRIPTS))
try:
    _RUNTIME = importlib.import_module("_performance_runtime")
    _RECOVERY = importlib.import_module("recover_performance_benchmarks")
    _COMMON = importlib.import_module("_common")
finally:
    sys.path.pop(0)


def _run(backend="pymupdf", success=True):
    return SimpleNamespace(
        backend=backend,
        task="parse+render",
        threads=1,
        wall_s=1.23456789,
        per_page_source="per-page-wall",
        doc_errors=0,
        samples=[
            _COMMON.PageRow(backend, "parse+render", 1, "doc.pdf", 1, success, 0.5)
        ],
    )


def test_checkpoint_roundtrip_and_package_guard(tmp_path: Path):
    store = _RUNTIME.BenchmarkCheckpoints(
        tmp_path, {"input": "sha"}, {"packages": {"pymupdf": "1.27.2.3"}}, 1
    )
    store.save(_run())
    cached = store.load("pymupdf", "parse+render", 1)
    assert cached["wall_s"] == 1.23456789
    assert cached["samples"][0].page_number == 1
    changed = _RUNTIME.BenchmarkCheckpoints(
        tmp_path, {"input": "sha"}, {"packages": {"pymupdf": "1.28.2"}}, 1
    )
    with pytest.raises(ValueError, match="packages changed"):
        changed.load("pymupdf", "parse+render", 1)
    changed = _RUNTIME.BenchmarkCheckpoints(
        tmp_path, {"input": "new-sha"}, store.provenance, 1
    )
    with pytest.raises(ValueError, match=r"inputs|settings"):
        changed.load("pymupdf", "parse+render", 1)


def test_incomplete_or_corrupted_checkpoint_is_remeasured(tmp_path: Path):
    store = _RUNTIME.BenchmarkCheckpoints(tmp_path, {}, {}, 1)
    store.save(_run(success=False))
    assert store.load("pymupdf", "parse+render", 1) is None
    store.save(_run())
    csv_path, _ = store.paths("pymupdf", "parse+render", 1)
    csv_path.write_text("incomplete write")
    assert store.load("pymupdf", "parse+render", 1) is None


def test_dotted_package_checkpoints_do_not_collide(tmp_path: Path):
    store = _RUNTIME.BenchmarkCheckpoints(tmp_path, {}, {}, 1)
    first = store.paths("pdfminer.six", "parse", 1)
    second = store.paths("pdfminer.six", "parse+render", 1)
    assert first != second
    assert first[0].name == "pdfminer.six-parse-1.csv"


def test_completed_checkpoint_survives_abrupt_process_exit(tmp_path: Path):
    code = """import os,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from _performance_runtime import BenchmarkCheckpoints
from _common import PageRow
from types import SimpleNamespace
run=SimpleNamespace(backend="pymupdf",task="parse",threads=1,wall_s=2.5,per_page_source="wall",doc_errors=0,
                    samples=[PageRow("pymupdf","parse",1,"doc.pdf",1,True,0.5)])
BenchmarkCheckpoints(Path(sys.argv[2]),{}, {},1).save(run)
os._exit(134)
"""
    result = subprocess.run([sys.executable, "-c", code, str(_SCRIPTS), str(tmp_path)])
    assert result.returncode == 134
    saved = _RUNTIME.BenchmarkCheckpoints(tmp_path, {}, {}, 1).load(
        "pymupdf", "parse", 1
    )
    assert saved["wall_s"] == 2.5


@pytest.mark.parametrize("returncode", [0, -6])
def test_reference_preflight_handles_native_abort(
    tmp_path: Path, monkeypatch, returncode
):
    pdf = tmp_path / "1293644.pdf"
    pdf.write_bytes(b"fixture PDF")
    meta = {
        "dataset": {
            "files": [
                {"name": pdf.name, "path": str(pdf), "sha256": _RUNTIME.digest(pdf)}
            ]
        }
    }
    monkeypatch.setattr(
        _RUNTIME.importlib.metadata, "version", lambda _: "fixture-version"
    )
    calls = []

    def probe(command, **kwargs):
        calls.append((command, kwargs))
        return subprocess.CompletedProcess(command, returncode, "", "native abort")

    monkeypatch.setattr(_RUNTIME.subprocess, "run", probe)
    if returncode:
        with pytest.raises(RuntimeError, match=r"1293644\.pdf page 3.*SIGABRT"):
            _RUNTIME.reference_preflight(meta)
    else:
        assert _RUNTIME.reference_preflight(meta)["status"] == "passed"
    assert "doc[2]" in calls[0][0][-2]
    assert calls[0][1]["timeout"] == 30


@pytest.mark.skipif(
    os.name == "nt", reason="The recovery workflow uses Unix directory symlinks"
)
def test_recovery_forks_core_without_relabeling_versions(tmp_path: Path, monkeypatch):
    source = tmp_path / "original"
    source.mkdir()
    output = tmp_path / "recovery"
    output.mkdir()
    for name in ("native", "replay"):
        (source / name).mkdir()
        (source / name / "raw").write_text("unchanged measurements")
    pdf = tmp_path / "doc.pdf"
    pdf.write_bytes(b"PDF")
    packages = dict.fromkeys(
        ("docling-parse", "docling-core", "pypdfium2", "pillow", "rtree"), "fixture"
    )
    metadata = {
        "extension_sha256": "extension",
        "native_sha256": "native",
        "python": "python",
        "platform": "platform",
        "machine": "machine",
        "cpu": "cpu",
        "qpdf": "12.4.2",
        "packages": {**packages, "pymupdf": "1.28.2"},
        "dataset": {
            "files": [
                {"name": pdf.name, "path": str(pdf), "sha256": _RUNTIME.digest(pdf)}
            ]
        },
    }
    (source / "metadata.json").write_text(json.dumps(metadata))
    original = (source / "metadata.json").read_bytes()
    provenance = {
        **{
            k: metadata[k]
            for k in ("extension_sha256", "python", "platform", "machine", "cpu")
        },
        "packages": {**packages, "pymupdf": "1.27.2.3"},
    }

    class Inputs:
        def __init__(self, _source):
            self.metadata = copy.deepcopy(metadata)

        def load(self, *, include_comparison):
            assert not include_comparison
            return ["validated core"]

    monkeypatch.setattr(_RECOVERY, "ReportInputs", Inputs)
    monkeypatch.setattr(_RECOVERY, "validate_environment", lambda meta, prov: None)
    monkeypatch.setattr(
        _RECOVERY, "reference_preflight", lambda meta: {"status": "passed"}
    )
    result = _RECOVERY.prepare_recovery(source, output, provenance)
    assert result["packages"]["pymupdf"] == "1.28.2"
    assert result["comparison_provenance"]["packages"]["pymupdf"] == "1.27.2.3"
    assert (source / "metadata.json").read_bytes() == original
    assert (output / "original-metadata.json").read_bytes() == original
    for name in ("native", "replay"):
        assert (output / name).is_symlink()
        assert (output / name / "raw").read_text() == "unchanged measurements"


@pytest.mark.parametrize(
    "changed",
    [
        "extension_sha256",
        "python",
        "platform",
        "machine",
        "cpu",
        "pillow",
        "native_sha256",
        "qpdf",
    ],
)
def test_recovery_rejects_changed_core_environment(
    tmp_path: Path, monkeypatch, changed
):
    native = tmp_path / "build/run_scaling.exe"
    native.parent.mkdir()
    native.write_bytes(b"native")
    header = tmp_path / "externals/include/qpdf/DLL.h"
    header.parent.mkdir(parents=True)
    header.write_text('#define QPDF_VERSION "12.4.2"\n')
    monkeypatch.setattr(_RECOVERY, "ROOT", tmp_path)
    packages = dict.fromkeys(
        ("docling-parse", "docling-core", "pypdfium2", "pillow", "rtree"), "same"
    )
    metadata = {
        "extension_sha256": "same",
        "python": "same",
        "platform": "same",
        "machine": "same",
        "cpu": "same",
        "native_sha256": _RUNTIME.digest(native),
        "qpdf": "12.4.2",
        "packages": packages,
    }
    provenance = copy.deepcopy(metadata)
    if changed == "pillow":
        provenance["packages"]["pillow"] = "changed"
    elif changed in ("native_sha256", "qpdf"):
        metadata[changed] = "changed"
    else:
        provenance[changed] = "changed"
    with pytest.raises(ValueError):
        _RECOVERY.validate_environment(metadata, provenance)
