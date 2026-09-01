"""Reading and writing of groundtruth artifacts, with optional gzip.

The groundtruth tree is dominated by JSON: the parser pages and the render
instructions together account for ~4.8 GB of the ~6 GB dataset, almost all of it
whitespace and repeated key names. Both shrink by an order of magnitude when
written compact and gzipped (parser 11.6x, instructions 18.4x), and reading gets
*faster* rather than slower -- gunzip plus parse beats parsing the 2.5x larger
indented text.

Reads accept either encoding, preferring `<name>.gz` when both are present, so a
dataset revision can be migrated one artifact family at a time. Writes follow
`GROUNDTRUTH_FORMAT` and delete the other encoding, so a page never ends up with
two disagreeing copies.

The textline exports (`NEVER_COMPRESS_SUFFIXES`) are the deliberate exception:
they stay uncompressed so a refresh is reviewable as a text diff in the dataset
repo instead of an opaque LFS pointer swap.

Everything here is stdlib-only, so the tooling under `tests/tools/` can use it
without pulling in docling_core.
"""

from __future__ import annotations

import gzip
import json
import os
from pathlib import Path
from typing import Any, Union

StrPath = Union[str, "os.PathLike[str]"]

# "gz" writes compact, gzipped artifacts; "plain" reproduces the legacy
# indent=2, uncompressed form byte for byte. Reads auto-detect either way, so
# this only steers writes -- which is what lets a content-only groundtruth
# refresh stay byte-comparable with the published revision (run it under
# DOCLING_PARSE_GT_FORMAT=plain) and the re-encoding land as a separate,
# reviewable revision.
GROUNDTRUTH_FORMAT = os.environ.get("DOCLING_PARSE_GT_FORMAT", "gz").strip().lower()

if GROUNDTRUTH_FORMAT not in ("gz", "plain"):
    raise ValueError(
        f"DOCLING_PARSE_GT_FORMAT must be 'gz' or 'plain', got {GROUNDTRUTH_FORMAT!r}"
    )

COMPRESS_ON_WRITE = GROUNDTRUTH_FORMAT == "gz"

# Artifacts that stay uncompressed even under "gz", so that a groundtruth
# refresh shows up as a readable text diff in the dataset repo rather than an
# LFS pointer swap. The textline exports are line-oriented (cells are joined by
# a separator ending in a newline), so they diff cell by cell.
#
# This only pays off if the dataset's .gitattributes also stops routing them
# through LFS -- see "Reviewing a refresh in the dataset repo" in tests/README.md.
# It costs ~223 MB of working tree: the three families are 264 MB plain against
# 40 MB gzipped, on a tree that is otherwise ~1.5 GB.
# `.delta.txt` is not groundtruth at all -- it is written next to a failing
# artifact for a human to read, and deleted once the page passes.
NEVER_COMPRESS_SUFFIXES = (".char.txt", ".word.txt", ".line.txt", ".delta.txt")


def compresses(path: StrPath) -> bool:
    """Whether a write to `path` is gzipped.

    False under DOCLING_PARSE_GT_FORMAT=plain, and false for the families in
    NEVER_COMPRESS_SUFFIXES regardless of format.
    """
    if not COMPRESS_ON_WRITE:
        return False
    name = Path(path).name
    if name.endswith(".gz"):
        name = name[:-3]
    return not name.endswith(NEVER_COMPRESS_SUFFIXES)


# Level 6 is the gzip default and sits at the knee of the curve here: level 9
# costs noticeably more write time for ~1% on this data, and writes happen only
# under --update-groundtruth.
GZIP_LEVEL = 6

# JSON separators that drop the space after ':' and ',' as well as the indent.
_COMPACT_SEPARATORS = (",", ":")


def _pair(path: StrPath) -> tuple[Path, Path]:
    """Return the (gzipped, plain) pair of paths for an artifact.

    Accepts either member of the pair, so callers can keep passing the plain
    `.json` name they already build.
    """
    p = Path(path)
    if p.suffix == ".gz":
        return p, p.with_suffix("")
    return p.with_name(p.name + ".gz"), p


def resolve_groundtruth_path(path: StrPath) -> Path:
    """The existing artifact for `path`, preferring the gzipped encoding.

    Falls back to the path that *would* be written when neither exists, so the
    result is always usable in an error message.
    """
    gz_path, plain_path = _pair(path)
    if gz_path.exists():
        return gz_path
    if plain_path.exists():
        return plain_path
    return gz_path if compresses(path) else plain_path


def groundtruth_write_path(path: StrPath) -> Path:
    """The path a write to `path` will actually land on."""
    gz_path, plain_path = _pair(path)
    return gz_path if compresses(path) else plain_path


def groundtruth_exists(path: StrPath) -> bool:
    """True when the artifact exists in either encoding."""
    return any(candidate.exists() for candidate in _pair(path))


def remove_groundtruth(path: StrPath) -> None:
    """Delete both encodings of an artifact, if present."""
    for candidate in _pair(path):
        candidate.unlink(missing_ok=True)


def read_groundtruth_bytes(path: StrPath) -> bytes:
    """Read an artifact in whichever encoding is on disk."""
    resolved = resolve_groundtruth_path(path)
    raw = resolved.read_bytes()
    return gzip.decompress(raw) if resolved.suffix == ".gz" else raw


def read_groundtruth_text(path: StrPath, encoding: str = "utf-8") -> str:
    return read_groundtruth_bytes(path).decode(encoding)


def load_groundtruth_json(path: StrPath) -> Any:
    return json.loads(read_groundtruth_bytes(path))


def write_groundtruth_bytes(path: StrPath, data: bytes) -> Path:
    """Write an artifact, removing the encoding that was not written.

    gzip embeds an mtime in its header by default, which would make every
    regeneration produce different bytes for unchanged content and defeat Git
    LFS deduplication in the dataset repo. `mtime=0` keeps the output a pure
    function of the input. (CPython only defaults to mtime=0 from 3.13; this
    project supports >=3.10, so it has to be explicit.)
    """
    gz_path, plain_path = _pair(path)
    compress = compresses(path)
    target = gz_path if compress else plain_path
    other = plain_path if compress else gz_path

    target.parent.mkdir(parents=True, exist_ok=True)
    payload = gzip.compress(data, GZIP_LEVEL, mtime=0) if compress else data
    target.write_bytes(payload)

    other.unlink(missing_ok=True)
    return target


def write_groundtruth_text(path: StrPath, text: str, encoding: str = "utf-8") -> Path:
    return write_groundtruth_bytes(path, text.encode(encoding))


def dump_groundtruth_json(
    path: StrPath,
    data: Any,
    sort_keys: bool = False,
    trailing_newline: bool = False,
) -> Path:
    """Serialize `data` as JSON and write it.

    Compact rather than indented under "gz": on this corpus indentation is
    2.0x-2.6x of the payload, and it is dead weight in a file that is only ever
    diffed by tooling. Under "plain" the legacy indent=2 form is reproduced
    instead, so that mode is a byte-exact escape hatch rather than a third
    format.

    `trailing_newline` exists because the two writers this replaces disagreed:
    the render side terminated its JSON with a newline and the parser side did
    not. Callers keep their own convention so "plain" stays byte-exact.
    """
    if compresses(path):
        text = json.dumps(data, separators=_COMPACT_SEPARATORS, sort_keys=sort_keys)
    else:
        text = json.dumps(data, indent=2, sort_keys=sort_keys)
    if trailing_newline:
        text += "\n"
    return write_groundtruth_bytes(path, text.encode("utf-8"))


def load_segmented_page(path: StrPath):
    """Load a `SegmentedPdfPage` from either encoding.

    `SegmentedPdfPage.load_from_json()` only takes a filename and opens it
    itself, so it cannot see a gzipped artifact; it is a thin wrapper over
    `model_validate_json()`, which is what this calls instead.

    Imported lazily to keep this module stdlib-only at import time.
    """
    from docling_core.types.doc.page import SegmentedPdfPage

    return SegmentedPdfPage.model_validate_json(read_groundtruth_text(path))
