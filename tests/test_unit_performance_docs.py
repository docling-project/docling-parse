"""Publication must preserve setup isolation and reject oversized/stale output."""

import importlib.util
import json
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "performance_docs",
    Path(__file__).parents[1] / "scripts/tools/update_performance_docs.py",
)
assert _SPEC and _SPEC.loader
_DOCS = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_DOCS)
LIMITS = json.loads((_DOCS.ARTIFACTS / "performance_docs_limits.json").read_text())


def fixture_run(tmp_path, monkeypatch):
    meta = {
        "dataset": {
            "repository": "docling-project/performance-dataset-bo20",
            "revision": "pin",
        },
        "cpu": "Test CPU",
        "machine": "arm64",
        "platform": "TestOS",
        "memory_bytes": "68719476736",
        "threads": [1, 4],
        "repeats": 3,
        "scale": 1.0,
        "max_concurrent_results": 32,
    }
    bundle = tmp_path / "scratch-performance-refresh-20261112-123829-example"
    bundle.mkdir()
    (bundle / "metadata.json").write_text(json.dumps(meta))
    measurement = SimpleNamespace(workload="native", memory=None)
    inputs = SimpleNamespace(
        metadata=meta,
        files=[{"name": "a.pdf", "pages": 2, "sha256": "hash"}],
        expected={1, 2},
        excluded_comparisons=[],
        canvas_checks=[],
        load=lambda: [measurement],
    )

    def render(inputs, measurements, output):
        (output / "plot.png").write_bytes(b"png")
        (output / "rss_unused.png").write_bytes(b"diagnostic")
        return {
            "native": "## Native\n\n![Plot](assets/plot.png)",
            "comparison": "## Comparison\n\nNot collected in this run.",
        }

    monkeypatch.setitem(
        sys.modules,
        "run_performance_report",
        SimpleNamespace(ReportInputs=lambda _: inputs, publication_sections=render),
    )
    index = (
        "<!-- performance:availability:start -->\n<!-- performance:availability:end -->"
    )
    return bundle, {_DOCS.INDEX: index.encode()}, meta


def test_slots_preserve_authored_text_and_skip_missing(capsys):
    text = "Intro\n<!-- performance:native:start -->old<!-- performance:native:end -->\nTail"
    assert _DOCS.slot(text, "native", "new") == text.replace(">old<", ">\nnew\n<")
    assert _DOCS.slot(text, "memory", "ignored") == text
    assert "Skip absent" in capsys.readouterr().out
    with pytest.raises(ValueError, match="Malformed"):
        _DOCS.slot(text + "<!-- performance:native:end -->", "native", "x")


def test_publication_is_repeatable_and_preserves_other_setups(tmp_path, monkeypatch):
    bundle, before, meta = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, dict(before), None, False)
    _DOCS.validate(after, LIMITS)
    assert before.keys() == {_DOCS.INDEX}
    assert not any("rss_unused" in str(p) for p in after)
    assert _DOCS.plan(bundle, dict(after), None, False) == after
    meta["cpu"] = "Other CPU"
    other = _DOCS.plan(bundle, dict(after), None, False)
    assert len([p for p in other if p.name == "publication.json"]) == 2
    for p in after:
        if p != _DOCS.INDEX:
            assert other[p] == after[p]


def test_older_run_and_machine_collision_fail(tmp_path, monkeypatch):
    bundle, before, meta = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, before, "stable", False)
    manifest = next(p for p in after if p.name == "publication.json")
    value = json.loads(after[manifest])
    value["measured_at"] = "2099-01-01T00:00:00"
    after[manifest] = _DOCS.encoded(value)
    with pytest.raises(ValueError, match="older"):
        _DOCS.plan(bundle, dict(after), "stable", False)
    _DOCS.plan(bundle, dict(after), "stable", True)
    meta["cpu"] = "Different"
    with pytest.raises(ValueError, match="identity differs"):
        _DOCS.plan(bundle, dict(after), "stable", True)


@pytest.mark.parametrize(
    "key",
    ["markdown_bytes", "image_bytes", "json_bytes", "publication_bytes", "tree_bytes"],
)
def test_each_size_budget_is_enforced(tmp_path, monkeypatch, key):
    bundle, before, _ = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, before, None, False)
    with pytest.raises(ValueError, match="exceeds"):
        _DOCS.validate(after, {**LIMITS, key: 1})


def test_stale_owned_assets_removed_unowned_preserved(tmp_path, monkeypatch):
    bundle, before, _ = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, before, None, False)
    manifest = next(p for p in after if p.name == "publication.json")
    base = manifest.parent
    value = json.loads(after[manifest])
    value["files"]["assets/stale.png"] = _DOCS.digest(b"stale")
    after[base / "assets/stale.png"] = b"stale"
    after[base / "notes.md"] = b"Author notes"
    after[manifest] = _DOCS.encoded(value)
    updated = _DOCS.plan(bundle, after, None, False)
    assert base / "assets/stale.png" not in updated
    assert updated[base / "notes.md"] == b"Author notes"


def test_modified_asset_and_broken_link_fail(tmp_path, monkeypatch):
    bundle, before, _ = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, before, None, False)
    image = next(p for p in after if p.suffix == ".png")
    bad = {**after, image: b"manual edit"}
    with pytest.raises(ValueError, match="modified"):
        _DOCS.validate(bad, LIMITS)
    bad = {**after, _DOCS.INDEX: b"[Missing](does-not-exist.md)"}
    with pytest.raises(ValueError, match="Broken"):
        _DOCS.validate(bad, LIMITS)


def test_path_traversal_rejected():
    for value in ["../outside.png", "/absolute.png"]:
        with pytest.raises(ValueError, match="Unsafe"):
            _DOCS.checked_relative(value)


def test_check_cli_uses_no_performance_dependencies():
    import subprocess

    result = subprocess.run(
        [
            sys.executable,
            "-S",
            str(_DOCS.TOOLS / "update_performance_docs.py"),
            "--check",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr


def test_cli_preview_and_budget_failure_never_write_docs(tmp_path, monkeypatch):
    bundle, before, _ = fixture_run(tmp_path, monkeypatch)
    entry = tmp_path / _DOCS.INDEX
    entry.parent.mkdir(parents=True)
    entry.write_bytes(before[_DOCS.INDEX])
    monkeypatch.setattr(_DOCS, "ROOT", tmp_path)
    monkeypatch.setattr(
        sys, "argv", ["publisher", "--performance-run-dir", str(bundle)]
    )
    _DOCS.main()
    assert _DOCS.snapshot(tmp_path) == before
    planned = _DOCS.plan(bundle, dict(before), None, False)
    planned[_DOCS.INDEX] = b"x" * (LIMITS["markdown_bytes"] + 1)
    monkeypatch.setattr(_DOCS, "plan", lambda *args: planned)
    monkeypatch.setattr(
        sys, "argv", ["publisher", "--performance-run-dir", str(bundle), "--apply"]
    )
    with pytest.raises(SystemExit):
        _DOCS.main()
    assert _DOCS.snapshot(tmp_path) == before


def test_missing_machine_identity_needs_explicit_setup():
    meta = {
        "dataset": {"repository": "docling-project/performance-dataset-bo20"},
        "machine": "arm64",
        "platform": "macOS",
    }
    with pytest.raises(ValueError, match="Incomplete"):
        _DOCS.identity(meta, None)
    assert _DOCS.identity(meta, "historical-machine")[1] == "historical-machine"


def test_historical_snapshot_uses_current_template_on_refresh(tmp_path, monkeypatch):
    bundle, before, _ = fixture_run(tmp_path, monkeypatch)
    after = _DOCS.plan(bundle, before, None, False)
    path = next(p for p in after if p.name == "publication.json")
    value = json.loads(after[path])
    value["migrated_snapshot"] = True
    after[path] = _DOCS.encoded(value)
    after[path.parent / "index.md"] = b"# Historical snapshot without markers"
    updated = _DOCS.plan(bundle, after, None, False)
    assert b"<!-- performance:native:start -->" in updated[path.parent / "index.md"]
    assert "migrated_snapshot" not in json.loads(updated[path])
    _DOCS.validate(updated, LIMITS)


def test_unknown_ram_is_unavailable_not_zero():
    assert _DOCS.memory_label({}) == "unknown RAM"


@pytest.mark.parametrize(
    "arguments",
    [
        [],
        ["RUN"],
        ["--apply"],
        ["--performance-run-dir"],
        ["--check", "--performance-run-dir", "RUN"],
        ["--check", "--apply"],
    ],
)
def test_cli_requires_named_run_directory_or_check(monkeypatch, arguments):
    monkeypatch.setattr(sys, "argv", ["publisher", *arguments])
    with pytest.raises(SystemExit) as error:
        _DOCS.main()
    assert error.value.code == 2


@pytest.mark.parametrize(
    "dataset, label", [("bo20", "Bo20"), ("bo767", "Bo767"), ("bo10k", "Bo10K")]
)
def test_generated_title_identifies_dataset_and_setup(
    tmp_path, monkeypatch, dataset, label
):
    bundle, before, meta = fixture_run(tmp_path, monkeypatch)
    meta["dataset"]["repository"] = f"docling-project/performance-dataset-{dataset}"
    after = _DOCS.plan(bundle, before, None, False)
    page = next(p for p in after if p.name == "index.md")
    heading = f"# Performance results: {label} / Test CPU / 64 GiB / TestOS"
    assert heading.encode() in after[page]
    # A previously generated generic title upgrades without losing authored content.
    after[page] = b"# Performance results\n\nAuthor notes.\n"
    updated = _DOCS.plan(bundle, after, None, False)
    assert heading.encode() in updated[page]
    assert b"Author notes." in updated[page]
    _DOCS.validate(updated, LIMITS)
