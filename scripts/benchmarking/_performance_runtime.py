"""Reference preflight, provenance and completed-series checkpoints."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import platform
import signal
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from _common import read_page_rows, safe_name, write_page_rows

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = (
    "docling-parse",
    "docling-core",
    "pypdfium2",
    "pillow",
    "rtree",
    "pymupdf",
    "pdfplumber",
    "pdfminer.six",
    "pypdf",
)


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def atomic_json(path: Path, value: Any) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def runtime_provenance() -> dict[str, Any]:
    def git(*args: str) -> str:
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()

    spec = importlib.util.find_spec("docling_parse.pdf_parsers")
    if spec is None or spec.origin is None:
        raise RuntimeError("The installed docling-parse extension could not be located")
    packages = {}
    for name in PACKAGES:
        try:
            packages[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            packages[name] = "not installed"
    cpu = platform.processor()
    if sys.platform == "darwin":
        cpu = subprocess.check_output(
            ["sysctl", "-n", "machdep.cpu.brand_string"], text=True
        ).strip()
    return {
        "packages": packages,
        "python": sys.version,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "cpu": cpu,
        "source_revision": git("rev-parse", "HEAD"),
        "source_diff_sha256": hashlib.sha256(git("diff", "HEAD").encode()).hexdigest(),
        "extension_sha256": digest(Path(spec.origin)),
        "script_sha256": {
            p.name: digest(p) for p in sorted(Path(__file__).parent.glob("*.py"))
        },
    }


def reference_preflight(metadata: dict[str, Any]) -> dict[str, Any]:
    """Catch the known MuPDF abort in a disposable subprocess before a sweep."""
    item = next(
        (
            f
            for f in metadata["dataset"]["files"]
            if Path(f["name"]).name == "1293644.pdf"
        ),
        None,
    )
    version = importlib.metadata.version("pymupdf")
    if item is None:
        print(
            f"PyMuPDF {version} preflight: Bo767 regression PDF is absent from this corpus.",
            flush=True,
        )
        return {"version": version, "status": "not-in-corpus"}
    path = Path(item["path"])
    if digest(path) != item["sha256"]:
        raise RuntimeError(f"Preflight PDF differs from the dataset manifest: {path}")
    code = """import faulthandler, os, sys
faulthandler.enable()
if os.name == "posix":
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
import pymupdf
with pymupdf.open(sys.argv[1]) as doc:
    page = doc[2]
    page.get_text("rawdict")
    pix = page.get_pixmap(matrix=pymupdf.Matrix(1, 1))
    assert min(pix.pil_image().size) > 0
"""
    print(f"PyMuPDF {version} preflight: 1293644.pdf, page 3 ...", flush=True)
    try:
        result = subprocess.run(
            [sys.executable, "-X", "faulthandler", "-c", code, str(path)],
            text=True,
            capture_output=True,
            timeout=30,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("PyMuPDF preflight timed out on 1293644.pdf page 3") from exc
    if result.returncode:
        status = (
            signal.Signals(-result.returncode).name
            if result.returncode < 0
            else str(result.returncode)
        )
        raise RuntimeError(
            f"PyMuPDF {version} preflight failed on 1293644.pdf page 3 ({status}). "
            "No corpus measurements have started. Sync the pinned perf dependencies.\n"
            + result.stderr[-6000:]
        )
    print("PyMuPDF regression preflight passed.", flush=True)
    return {"version": version, "status": "passed", "document": item["name"], "page": 3}


class BenchmarkCheckpoints:
    """Save after a complete series, outside that series' measured wall time."""

    def __init__(
        self,
        directory: Path,
        identity: dict[str, Any],
        provenance: dict[str, Any],
        total_pages: int,
    ):
        self.directory = directory
        self.identity = identity
        directory.mkdir(parents=True, exist_ok=True)
        self.provenance = provenance
        self.total_pages = total_pages
        self.fingerprint = hashlib.sha256(
            json.dumps(
                {"identity": identity, "provenance": provenance}, sort_keys=True
            ).encode()
        ).hexdigest()

    def paths(self, backend: str, task: str, threads: int) -> tuple[Path, Path]:
        prefix = self.directory / safe_name(f"{backend}-{task}-{threads}")
        return Path(str(prefix) + ".csv"), Path(str(prefix) + ".json")

    def save(self, run: Any) -> None:
        csv_path, json_path = self.paths(run.backend, run.task, run.threads)
        temporary = csv_path.with_suffix(".csv.tmp")
        write_page_rows(temporary, run.samples)
        temporary.replace(csv_path)
        atomic_json(
            json_path,
            {
                "schema_version": 1,
                "fingerprint": self.fingerprint,
                "provenance": self.provenance,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "csv_sha256": digest(csv_path),
                "complete": run.doc_errors == 0
                and len(run.samples) == self.total_pages
                and all(row.success for row in run.samples),
                "run": {
                    **{
                        key: getattr(run, key)
                        for key in (
                            "backend",
                            "task",
                            "threads",
                            "wall_s",
                            "per_page_source",
                            "doc_errors",
                        )
                    },
                    **(
                        {"memory": run.memory}
                        if getattr(run, "memory", None) is not None
                        else {}
                    ),
                },
            },
        )

    def load(self, backend: str, task: str, threads: int) -> dict[str, Any] | None:
        csv_path, json_path = self.paths(backend, task, threads)
        if not csv_path.exists() or not json_path.exists():
            return None
        try:
            state = json.loads(json_path.read_text())
            if not state["complete"] or digest(csv_path) != state["csv_sha256"]:
                return None
        except (OSError, ValueError, KeyError):
            return None
        if state["schema_version"] != 1 or state["fingerprint"] != self.fingerprint:
            raise ValueError(
                f"Checkpoint inputs, settings, source or packages changed: {json_path}; use a fresh output directory"
            )
        run = state["run"]
        memory = run.get("memory")
        requested = self.identity.get("memory", {"mode": "off"})
        if requested["mode"] != "off" and memory is None:
            return None
        if memory is not None:
            from _performance_memory import read_memory

            relative = Path(memory["artifact"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"Invalid checkpoint memory path: {json_path}")
            artifact = self.directory.parent / relative
            if not artifact.is_file() or digest(artifact) != memory["artifact_sha256"]:
                return None
            try:
                if read_memory(artifact, requested) is None:
                    return None
            except (OSError, ValueError):
                return None
        rows = read_page_rows(csv_path)
        if (run["backend"], run["task"], run["threads"]) != (backend, task, threads):
            raise ValueError(f"Checkpoint series identity mismatch: {json_path}")
        if len(rows) != self.total_pages or not all(row.success for row in rows):
            return None
        return {**run, "samples": rows}


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Preflight reference rendering before expensive corpus measurements"
    )
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        metadata = json.loads(args.manifest.read_text())
        result = reference_preflight(metadata)
        provenance = runtime_provenance()
        if provenance["extension_sha256"] != metadata["extension_sha256"]:
            raise RuntimeError(
                "The Python extension changed after build metadata was collected"
            )
        metadata["reference_preflight"] = result
        metadata["comparison_provenance"] = provenance
        atomic_json(args.manifest, metadata)
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Reference preflight failed: {exc}\n")


if __name__ == "__main__":
    main()
