#!/usr/bin/env python

import sys
import zipfile
from pathlib import Path


BLEND2D_ASSERTION_MARKERS = (
    b"[Blend2D] ASSERTION FAILURE",
    b"bl_runtime_assertion_failure",
)
NATIVE_SUFFIXES = {".so", ".pyd", ".dll", ".dylib"}


def native_files(path: Path) -> list[Path]:
    if path.is_file():
        if path.suffix.lower() == ".whl" or path.suffix.lower() in NATIVE_SUFFIXES:
            return [path]
        return []
    return [
        item
        for item in path.rglob("*")
        if item.is_file() and item.suffix.lower() in NATIVE_SUFFIXES
    ]


def scan_file(path: Path) -> list[str]:
    if path.suffix.lower() == ".whl":
        findings: list[str] = []
        with zipfile.ZipFile(path) as zf:
            for name in zf.namelist():
                if Path(name).suffix.lower() not in NATIVE_SUFFIXES:
                    continue
                data = zf.read(name)
                if has_blend2d_assertion_marker(data):
                    findings.append(f"{path}!{name}")
        return findings

    try:
        data = path.read_bytes()
    except OSError as exc:
        print(f"warning: could not read {path}: {exc}", file=sys.stderr)
        return []

    return [str(path)] if has_blend2d_assertion_marker(data) else []


def has_blend2d_assertion_marker(data: bytes) -> bool:
    return any(marker in data for marker in BLEND2D_ASSERTION_MARKERS)


def main(argv: list[str]) -> int:
    if not argv:
        print(
            "usage: check_no_blend2d_assertions.py <native-file|wheel|directory> ...",
            file=sys.stderr,
        )
        return 2

    findings: list[str] = []
    for arg in argv:
        path = Path(arg)
        if not path.exists():
            print(f"warning: path does not exist: {path}", file=sys.stderr)
            continue
        for item in native_files(path):
            findings.extend(scan_file(item))

    if findings:
        print("Blend2D assertion machinery found in release artifact:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1

    print("No Blend2D assertion machinery found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
