#!/usr/bin/env python
"""Re-encode groundtruth JSON and text artifacts between plain and gzip.

The groundtruth tree is ~6 GB, ~4.8 GB of which is indented JSON that gzips by
10-18x. Re-encoding is a pure format change: this script never runs the parser
or the renderer, so it cannot alter what the groundtruth *says*. That matters
for review -- a re-encoding revision and a content revision should never be
mixed, or a real regression becomes indistinguishable from a re-serialization.

Round-trips through ``json`` for ``.json`` artifacts (so the compact form is
canonical) and byte-for-byte for ``.txt`` artifacts.

Usage:

    uv run python tests/tools/recompress_groundtruth.py --to gz --dry-run
    uv run python tests/tools/recompress_groundtruth.py --to gz
    uv run python tests/tools/recompress_groundtruth.py --to plain    # undo
"""

from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from tests.groundtruth_io import NEVER_COMPRESS_SUFFIXES  # noqa: E402

GROUNDTRUTH_DIR = REPO_ROOT / "tests" / "data" / "groundtruth"

# Only these are worth compressing. The png and bitmap payloads under
# render/pages and render/bitmap_data are already-compressed formats; gzipping
# them costs time and saves nothing.
PATTERNS = (
    "parser/*.py.json",
    "parser/*.py.json.gz",
    "parser/*.txt",
    "parser/*.txt.gz",
    "parser/*.pdf.json",
    "parser/*.pdf.json.gz",
    "render/instructions/*.json",
    "render/instructions/*.json.gz",
    "render/bitmaps/*.json",
    "render/bitmaps/*.json.gz",
)

GZIP_LEVEL = 6


def _human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if abs(n) < 1024 or unit == "GB":
            return f"{n:,.1f} {unit}" if unit != "B" else f"{n:,} B"
        n = int(float(n) / 1024.0)
    return f"{n:.1f} GB"


def _reencode(raw: bytes, is_json: bool, to_gz: bool) -> bytes:
    """Canonicalize the payload for the target format."""
    if not is_json:
        return raw
    data = json.loads(raw)
    if to_gz:
        return json.dumps(data, separators=(",", ":")).encode("utf-8")
    return json.dumps(data, indent=2).encode("utf-8")


def _wants_gz(path: Path, to_gz: bool) -> bool:
    """Target encoding for one artifact, honouring the never-compress families.

    Those stay plain under `--to gz` (and are converted back if a previous run
    compressed them), so the tool converges on the same layout the test suite
    writes.
    """
    if not to_gz:
        return False
    name = path.name[:-3] if path.name.endswith(".gz") else path.name
    return not name.endswith(NEVER_COMPRESS_SUFFIXES)


def convert(path: Path, to_gz: bool, dry_run: bool) -> tuple[int, int] | None:
    """Convert one artifact. Returns (before, after) sizes, or None if skipped."""
    to_gz = _wants_gz(path, to_gz)
    already_gz = path.suffix == ".gz"
    if already_gz == to_gz:
        return None

    before = path.stat().st_size
    raw = gzip.decompress(path.read_bytes()) if already_gz else path.read_bytes()

    stem = path.with_suffix("") if already_gz else path
    is_json = stem.suffix == ".json"

    try:
        payload = _reencode(raw, is_json, to_gz)
    except json.JSONDecodeError as exc:
        print(f"  !! skipping unparseable {path.name}: {exc}")
        return None

    target = stem.with_name(stem.name + ".gz") if to_gz else stem
    # mtime=0: without it gzip stamps the current time into the header, so an
    # unchanged artifact would produce different bytes on every run and Git LFS
    # would re-upload the whole tree.
    out = gzip.compress(payload, GZIP_LEVEL, mtime=0) if to_gz else payload

    if dry_run:
        return before, len(out)

    tmp = target.with_name(target.name + ".tmp")
    tmp.write_bytes(out)
    tmp.replace(target)
    if path != target:
        path.unlink()
    return before, len(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--to", choices=("gz", "plain"), required=True)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--root", type=Path, default=GROUNDTRUTH_DIR)
    args = ap.parse_args()

    if not args.root.is_dir():
        print(f"no groundtruth at {args.root}", file=sys.stderr)
        return 1

    to_gz = args.to == "gz"
    files: list[Path] = []
    for pattern in PATTERNS:
        files.extend(sorted(args.root.glob(pattern)))

    total_before = total_after = converted = 0
    for path in files:
        result = convert(path, to_gz, args.dry_run)
        if result is None:
            continue
        before, after = result
        total_before += before
        total_after += after
        converted += 1
        if converted % 500 == 0:
            print(f"  ... {converted} files")

    verb = "would convert" if args.dry_run else "converted"
    print(f"{verb} {converted} of {len(files)} artifacts under {args.root}")
    if converted:
        ratio = total_before / total_after if total_after else float("inf")
        print(f"  {_human(total_before)} -> {_human(total_after)}  ({ratio:.2f}x)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
