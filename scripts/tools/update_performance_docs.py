#!/usr/bin/env python3
"""Publish a validated performance run. Preview by default; --apply writes docs.

Use --check without a run to enforce publication size budgets and local links.
No benchmarks, builds, downloads, commits, or pushes are performed.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
ARTIFACTS = TOOLS / "update_performance_docs_artifacts"
TREE = Path("docs/performance_benchmarks")
INDEX = Path("docs/performance_benchmarks.md")


def encoded(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def digest(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def slot(text: str, name: str, content: str) -> str:
    start = f"<!-- performance:{name}:start -->"
    end = f"<!-- performance:{name}:end -->"
    if start not in text and end not in text:
        print(f"Skip absent placeholder: {name}")
        return text
    if (
        text.count(start) != 1
        or text.count(end) != 1
        or text.index(start) > text.index(end)
    ):
        raise ValueError(f"Malformed/duplicate placeholder: {name}")
    before, rest = text.split(start)
    _, after = rest.split(end)
    return before + start + "\n" + content.strip() + "\n" + end + after


def slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    if not result:
        raise ValueError("Empty setup identifier")
    return result


def memory_label(meta: dict[str, Any]) -> str:
    value = meta.get("memory_bytes")
    return f"{int(value) / 2**30:g} GiB" if value else "unknown RAM"


def identity(
    meta: dict[str, Any], override: str | None
) -> tuple[str, str, dict[str, Any]]:
    dataset = meta["dataset"]["repository"].rsplit("-", 1)[-1].lower()
    if dataset not in {"bo20", "bo767", "bo10k"}:
        raise ValueError(f"Unsupported dataset: {dataset}")
    machine = {k: meta.get(k) for k in ("cpu", "machine", "platform", "memory_bytes")}
    if not override and not all(machine.values()):
        raise ValueError("Incomplete machine metadata; provide --setup-id explicitly")
    ram = int(meta.get("memory_bytes") or 0)
    machine["memory_bytes"] = ram or None
    setup = override or f"{meta['cpu']}-{ram / 2**30:g}gib-{meta['platform']}"
    if override and slug(override) != override:
        raise ValueError(
            "--setup-id must contain lowercase letters, digits and hyphens"
        )
    return dataset, slug(setup), machine


def snapshot(root: Path) -> dict[Path, bytes]:
    files = {}
    for p in [root / INDEX, *(root / TREE).rglob("*")]:
        if p.is_symlink():
            raise ValueError(f"Symlink in publication tree: {p}")
        if p.is_file():
            files[p.relative_to(root)] = p.read_bytes()
    return files


def checked_relative(value: str) -> Path:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"Unsafe publication path: {value}")
    return path


def validate(files: dict[Path, bytes], limits: dict[str, int]) -> None:
    total = 0
    for path, content in files.items():
        key = {
            ".md": "markdown_bytes",
            ".png": "image_bytes",
            ".json": "json_bytes",
        }.get(path.suffix)
        if key is None:
            # Historical documentation contains extensionless files.
            key = "image_bytes"
        if len(content) > limits[key]:
            raise ValueError(
                f"{path}: {len(content):,} bytes exceeds {key}={limits[key]:,}"
            )
        total += len(content)
    if total > limits["tree_bytes"]:
        raise ValueError(
            f"Performance tree: {total:,} bytes exceeds tree_bytes={limits['tree_bytes']:,}"
        )
    for path, content in files.items():
        if path.name != "publication.json":
            continue
        manifest = json.loads(content)
        base = path.parent
        size = sum(len(data) for p, data in files.items() if p.is_relative_to(base))
        if size > limits["publication_bytes"]:
            raise ValueError(
                f"{base}: {size:,} bytes exceeds publication_bytes={limits['publication_bytes']:,}"
            )
        for name, sha in manifest["files"].items():
            owned = base / checked_relative(name)
            if owned not in files or digest(files[owned]) != sha:
                raise ValueError(f"Missing or modified generated file: {owned}")
    # Check links in managed pages and the entry page, including images.
    pages = {INDEX} | {
        p.parent / "index.md" for p in files if p.name == "publication.json"
    }
    for page in pages:
        if page not in files:
            continue
        for link in re.findall(r"\]\(([^)]+)\)", files[page].decode()):
            if ":" in link or link.startswith("#"):
                continue
            target = link.split("#", 1)[0]
            resolved = Path(os.path.normpath(page.parent / target))
            if resolved not in files and not (ROOT / resolved).is_file():
                raise ValueError(f"Broken documentation link in {page}: {link}")


def availability(files: dict[Path, bytes]) -> str:
    lines = ["| Dataset | Setup | Run | Workloads |", "|---|---|---|---|"]
    for p, data in sorted(files.items()):
        if p.name != "publication.json":
            continue
        m = json.loads(data)
        link = (p.parent / "index.md").relative_to("docs")
        lines.append(
            f"| {m['dataset']} | [{m['label']}]({link}) | {m['run']} | {', '.join(m['workloads'])} |"
        )
    return "\n".join(lines)


def measured_at(bundle: Path) -> str:
    # Launcher names encode the collection start. Fallback supports custom OUT names.
    match = re.search(r"(\d{8}-\d{6})", bundle.name)
    if match:
        return datetime.strptime(match[1], "%Y%m%d-%H%M%S").isoformat()
    return (
        datetime.fromtimestamp((bundle / "metadata.json").stat().st_mtime, timezone.utc)
        .replace(tzinfo=None)
        .isoformat()
    )


def provenance(
    inputs: Any, bundle: Path, memories: list[dict[str, Any]]
) -> dict[str, Any]:
    meta = inputs.metadata
    ordered = [{k: f[k] for k in ("name", "pages", "sha256")} for f in inputs.files]
    keys = (
        "source_revision",
        "source_diff_sha256",
        "native_sha256",
        "extension_sha256",
        "packages",
        "qpdf",
        "python",
        "script_sha256",
        "launcher_sha256",
        "threads",
        "repeats",
        "scale",
        "max_concurrent_results",
        "native_profiles",
        "build_mode",
        "memory_collection",
        "comparison_memory",
        "comparison_provenance",
        "recovery",
        "recovered_replay_provenance",
    )
    result = {k: meta[k] for k in keys if k in meta}
    result.update(
        {
            "schema_version": 1,
            "run": bundle.name,
            "measured_at": measured_at(bundle),
            "dataset": {
                k: v for k, v in meta["dataset"].items() if k not in ("files", "root")
            },
            "documents": len(inputs.files),
            "pages": len(inputs.expected),
            "ordered_file_identity_sha256": digest(
                json.dumps(ordered, sort_keys=True, separators=(",", ":")).encode()
            ),
            "identity_serialization": "UTF-8 JSON list in dataset order; name/pages/sha256; sort_keys=True; separators=(comma,colon)",
            "machine": {
                k: meta.get(k) for k in ("cpu", "machine", "platform", "memory_bytes")
            },
            "excluded_comparisons": inputs.excluded_comparisons,
            "canvas_checks": inputs.canvas_checks,
            "memory_methods": sorted(
                {
                    (m.get("mode", ""), m.get("method", ""), m.get("scope", ""))
                    for m in memories
                }
            ),
        }
    )
    return result


def plan(
    bundle: Path, files: dict[Path, bytes], setup_id: str | None, allow_older: bool
) -> dict[Path, bytes]:
    # Lazy import keeps --check independent of performance packages/native extension.
    sys.path.insert(0, str(ROOT / "scripts/benchmarking"))
    from run_performance_report import ReportInputs, publication_sections

    inputs = ReportInputs(bundle)
    measurements = inputs.load()
    memories = [m.memory for m in measurements if m.memory]
    meta = inputs.metadata
    dataset, setup, machine = identity(meta, setup_id)
    base = TREE / dataset / setup
    manifest_path = base / "publication.json"
    old = json.loads(files[manifest_path]) if manifest_path in files else None
    if old:
        if old["machine"] != machine:
            raise ValueError(
                "Setup identity differs from existing publication; choose a different --setup-id"
            )
        if measured_at(bundle) < old["measured_at"] and not allow_older:
            raise ValueError(
                "Incoming run is older; use --allow-older to replace explicitly"
            )
    with tempfile.TemporaryDirectory(prefix="performance-publication-") as tmp:
        output = Path(tmp)
        sections = publication_sections(inputs, measurements, output)
        dataset_label = {"bo20": "Bo20", "bo767": "Bo767", "bo10k": "Bo10K"}[dataset]
        sections["title"] = (
            f"# Performance results: {dataset_label} / {meta.get('cpu', setup)} / "
            f"{memory_label(meta)} / {meta['platform'].split('-')[0]}"
        )
        sections["setup"] = "\n".join(
            [
                "## Setup",
                "",
                f"Run: `{bundle.name}`; collected {measured_at(bundle)}.",
                "",
                f"CPU: **{meta.get('cpu', 'unknown')}**; architecture: {meta['machine']}; "
                f"RAM: {memory_label(meta)}; OS: {meta['platform']}.",
                "",
                f"Dataset: `{meta['dataset']['repository']}` at `{meta['dataset']['revision']}`; "
                f"{len(inputs.files):,} PDFs / {len(inputs.expected):,} pages.",
                "",
                f"Repetitions: {meta['repeats']}; threads: {meta['threads']}; render scale: {meta['scale']}; "
                f"queue capacity: {meta['max_concurrent_results']}.",
                "",
                "Values are medians with observed min-max ranges. One repetition provides no variability estimate. "
                "Speedup and efficiency use the same workload/task one-thread baseline.",
                "",
            ]
        )
        sections["provenance"] = (
            "## Provenance\n\n[Dataset, source, build, packages and memory methods](provenance.json). "
            "Raw measurements and detailed RSS timelines remain in the local run bundle.\n"
        )
        template = (ARTIFACTS / "performance_page_template.md").read_bytes()
        page = (
            template
            if old and old.get("migrated_snapshot")
            else files.get(base / "index.md", template)
        ).decode()
        # Upgrade pages created before the generated title placeholder existed.
        if page.startswith("# Performance results\n"):
            page = page.replace(
                "# Performance results\n",
                "<!-- performance:title:start -->\n# Performance results\n"
                "<!-- performance:title:end -->\n",
                1,
            )
        for name, content in sections.items():
            page = slot(page, name, content)
        generated = {
            "index.md": page.encode(),
            "provenance.json": encoded(provenance(inputs, bundle, memories)),
        }
        for name in re.findall(r"\]\(assets/([^/)]+\.png)\)", page):
            generated["assets/" + name] = (output / name).read_bytes()
    if old:
        for name, sha in old["files"].items():
            path = base / checked_relative(name)
            if path in files and digest(files[path]) != sha and path.name != "index.md":
                raise ValueError(f"Generated asset was edited: {path}")
            files.pop(path, None)
    for name, data in generated.items():
        dest = base / name
        if dest in files and dest not in (base / "index.md",) and files[dest] != data:
            raise ValueError(f"Refusing to overwrite unowned file: {dest}")
        files[dest] = data
    manifest = {
        "schema_version": 1,
        "dataset": dataset,
        "setup": setup,
        "machine": machine,
        "label": f"{meta.get('cpu', setup)} / {memory_label(meta)} / {meta['platform']}",
        "run": bundle.name,
        "measured_at": measured_at(bundle),
        "workloads": sorted({m.workload for m in measurements}),
        "files": {name: digest(data) for name, data in generated.items()},
    }
    files[manifest_path] = encoded(manifest)
    files[INDEX] = slot(
        files[INDEX].decode(), "availability", availability(files)
    ).encode()
    return files


def preview(before: dict[Path, bytes], after: dict[Path, bytes]) -> list[Path]:
    changed = []
    for path in sorted(before.keys() | after.keys()):
        old, new = before.get(path, b""), after.get(path, b"")
        if old == new:
            continue
        changed.append(path)
        print(f"{path}: {len(old):,} -> {len(new):,} bytes")
        if path.suffix == ".md":
            print(
                "".join(
                    difflib.unified_diff(
                        old.decode().splitlines(True),
                        new.decode().splitlines(True),
                        fromfile=str(path),
                        tofile=str(path),
                    )
                ),
                end="",
            )
    print(
        f"{len(changed)} changed files; total {sum(map(len, after.values())):,} bytes; "
        f"net change {sum(map(len, after.values())) - sum(map(len, before.values())):+,} bytes"
    )
    for path in sorted(after):
        if path.name == "publication.json":
            size = sum(
                len(data) for p, data in after.items() if p.is_relative_to(path.parent)
            )
            print(f"Publication {path.parent}: {size:,} bytes")
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--performance-run-dir",
        type=Path,
        metavar="DIRECTORY",
        help="Completed performance launcher/recovery output directory",
    )
    parser.add_argument(
        "--apply", action="store_true", help="Apply the validated publication plan"
    )
    mode.add_argument(
        "--check",
        action="store_true",
        help="Check existing sizes, manifests and links only",
    )
    parser.add_argument(
        "--setup-id",
        help="Explicit stable identifier for incomplete/ambiguous machine metadata",
    )
    parser.add_argument("--allow-older", action="store_true")
    args = parser.parse_args()
    if args.check and args.apply:
        parser.error("--check cannot be combined with --apply")
    try:
        limits = json.loads((ARTIFACTS / "performance_docs_limits.json").read_text())
        before = snapshot(ROOT)
        if args.check:
            validate(before, limits)
            print(
                f"Performance docs OK: {len(before)} files, {sum(map(len, before.values())):,} bytes"
            )
            return
        after = plan(
            args.performance_run_dir.resolve(),
            dict(before),
            args.setup_id,
            args.allow_older,
        )
        validate(after, limits)
        changed = preview(before, after)
        if args.apply:
            # Catch edits made during validation/rendering, before any docs are written.
            if snapshot(ROOT) != before:
                raise ValueError(
                    "Documentation changed during publication; rerun the preview"
                )
            for path in changed:
                dest = ROOT / path
                if path in after:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(after[path])
                else:
                    dest.unlink()
            print("Applied publication.")
        else:
            print("Preview only. Use --apply to write this publication.")
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Cannot publish performance docs: {error}\n")


if __name__ == "__main__":
    main()
