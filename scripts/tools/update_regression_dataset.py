#!/usr/bin/env python3
"""Copy local regression PDFs and groundtruth into the Hugging Face dataset.

Run with:
    python scripts/tools/update_regression_dataset.py

Existing groundtruth changes require [y/n/a] approval. No files are deleted,
and no changes are committed or pushed.
"""

from __future__ import annotations

import difflib
import filecmp
import gzip
import hashlib
import re
import runpy
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATASET = ROOT / ".references/regression-dataset-for-docling-parse"
DATASET_URL = (
    "https://huggingface.co/datasets/"
    "docling-project/regression-dataset-for-docling-parse"
)


def warn(message: str) -> None:
    print(f"WARNING: {message}", file=sys.stderr)


def ensure_dataset() -> None:
    if not DATASET.exists():
        DATASET.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["git", "clone", "--branch", "main", DATASET_URL, str(DATASET)],
            check=True,
        )
    if not (DATASET / ".git").exists():
        raise RuntimeError(f"Dataset is not a Git checkout: {DATASET}")
    branch = subprocess.check_output(
        ["git", "-C", str(DATASET), "branch", "--show-current"], text=True
    ).strip()
    if branch != "main":
        subprocess.run(["git", "-C", str(DATASET), "switch", "main"], check=True)


def files_under(directory: Path) -> dict[Path, Path]:
    return {
        path.relative_to(directory): path
        for path in sorted(directory.rglob("*"))
        if path.is_file()
    }


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    print(f"Copied: {source} -> {destination}")


def sync_pdfs(source: Path, destination: Path) -> None:
    local = files_under(source)
    remote = files_under(destination)
    for directory, files in ((source, local), (destination, remote)):
        for relative in files:
            if relative.suffix.lower() != ".pdf":
                warn(f"Non-PDF file: {directory / relative}")
    for relative, path in local.items():
        if relative.suffix.lower() == ".pdf" and relative not in remote:
            copy_file(path, destination / relative)
    for relative in remote:
        if relative.suffix.lower() == ".pdf" and relative not in local:
            warn(f"Dataset PDF has no local counterpart: {destination / relative}")


def read_content(path: Path) -> bytes:
    if path.suffix.lower() == ".gz":
        with gzip.open(path, "rb") as stream:
            return stream.read()
    return path.read_bytes()


def show_diff(existing: Path, incoming: Path) -> None:
    old = read_content(existing)
    new = read_content(incoming)
    if old == new:
        print("Decompressed contents match; compressed bytes differ.")
        return
    try:
        old_text = old.decode("utf-8")
        new_text = new.decode("utf-8")
        if "\0" in old_text or "\0" in new_text:
            raise ValueError("Binary content")
    except (UnicodeDecodeError, ValueError):
        print(f"Binary files differ: {existing} -> {incoming}")
        for label, content in (("existing", old), ("incoming", new)):
            print(
                f"  {label}: {len(content)} bytes, "
                f"SHA-256 {hashlib.sha256(content).hexdigest()}"
            )
        return
    for line in difflib.unified_diff(
        old_text.splitlines(keepends=True),
        new_text.splitlines(keepends=True),
        fromfile=str(existing),
        tofile=str(incoming),
    ):
        print(line, end="" if line.endswith("\n") else "\n")


def ask_replace(path: Path) -> str:
    while True:
        answer = (
            input(
                f"Replace {path}? [y/n/a] "
                "(a = replace this and all remaining changes): "
            )
            .strip()
            .lower()
        )
        if answer in {"y", "n", "a"}:
            return answer
        print("Please enter y, n, or a.")


def selected_groundtruth(
    directory: Path, selection: dict[str, list[int]]
) -> dict[Path, Path]:
    selected = {}
    for relative, path in files_under(directory).items():
        match = re.fullmatch(r"(.+\.pdf)\.page_no_(\d+)\..+", path.name)
        if match is None:
            warn(f"Skipping unrecognized groundtruth filename: {path}")
            continue
        document, page = match.groups()
        pages = selection.get(document)
        if pages is None:
            warn(
                f"PDF absent from REGRESSION_PAGE_SELECTION: {path}; "
                "allowing all pages, as the regression tests do"
            )
        elif int(page) not in pages:
            warn(
                f"Skipping unselected groundtruth page: {path}; selected pages: {pages}"
            )
            continue
        selected[relative] = path
    return selected


def sync_groundtruth(
    source: Path, destination: Path, selection: dict[str, list[int]]
) -> None:
    local = selected_groundtruth(source, selection)
    remote = selected_groundtruth(destination, selection)
    accept_all = False
    for relative, incoming in local.items():
        existing = destination / relative
        if relative not in remote:
            copy_file(incoming, existing)
            continue
        if filecmp.cmp(incoming, existing, shallow=False):
            continue
        if not accept_all:
            print(f"\nChanged groundtruth: {relative}")
            show_diff(existing, incoming)
            answer = ask_replace(relative)
            if answer == "n":
                print(f"Skipped: {relative}")
                continue
            accept_all = answer == "a"
        copy_file(incoming, existing)
    for relative in remote.keys() - local.keys():
        warn(f"Dataset groundtruth has no local counterpart: {destination / relative}")


def main() -> int:
    try:
        regression = ROOT / "tests/data/regression"
        groundtruth = ROOT / "tests/data/groundtruth"
        for directory in (regression, groundtruth):
            if not directory.is_dir():
                raise RuntimeError(f"Missing local source directory: {directory}")
        selection = runpy.run_path(str(ROOT / "tests/regression_page_selection.py"))[
            "REGRESSION_PAGE_SELECTION"
        ]
        ensure_dataset()
        sync_pdfs(regression, DATASET / "regression")
        sync_groundtruth(groundtruth, DATASET / "groundtruth", selection)
    except (EOFError, KeyboardInterrupt):
        print("\nStopped. Previously copied files remain.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
