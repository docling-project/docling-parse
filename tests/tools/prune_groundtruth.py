#!/usr/bin/env python
"""Move groundtruth artifacts that no longer belong to the regression corpus.

Parser and renderer groundtruth files are named after the document they were
produced from (``<pdf-name>.page_no_<n>....``). When a document leaves
``tests/data/regression`` its groundtruth stays behind and is never read again.
This script moves such files to ``tests/data/groundtruth-legacy``, mirroring
the directory layout, so nothing is lost and no test globs them any more.

By default only whole documents that are gone from the corpus are moved. With
``--pages`` the pages that ``PARSER_PAGE_RESTRICTIONS`` no longer selects are
moved as well.

Usage:

    uv run python tests/tools/prune_groundtruth.py [--pages] [--dry-run]
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from tests.constants import PARSER_PAGE_RESTRICTIONS  # noqa: E402

REGRESSION_DIR = REPO_ROOT / "tests" / "data" / "regression"
GROUNDTRUTH_DIR = REPO_ROOT / "tests" / "data" / "groundtruth"
LEGACY_DIR = REPO_ROOT / "tests" / "data" / "groundtruth-legacy"

# `<pdf-name>.page_no_<n>.<whatever>`, the shared prefix of every parser and
# renderer artifact.
ARTIFACT_RE = re.compile(r"^(?P<doc>.+?\.pdf)\.page_no_(?P<page>\d+)\D")


def classify(path: Path, known_docs: set[str], prune_pages: bool) -> str | None:
    """Return why `path` is stale, or None when it is still in use."""
    match = ARTIFACT_RE.match(path.name)
    if match is None:
        return None

    doc = match.group("doc")
    if doc not in known_docs:
        return "document left the corpus"

    if not prune_pages:
        return None

    selected = PARSER_PAGE_RESTRICTIONS.get(doc)
    if selected is not None and int(match.group("page")) not in selected:
        return "page no longer selected"

    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pages",
        action="store_true",
        help="also move artifacts of pages the page selection no longer covers",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="report what would move without touching the filesystem",
    )
    args = parser.parse_args()

    if not REGRESSION_DIR.is_dir():
        raise SystemExit(f"missing regression corpus: {REGRESSION_DIR}")

    known_docs = {p.name for p in REGRESSION_DIR.glob("*.pdf")}

    reasons: dict[str, int] = {}
    docs: set[str] = set()
    moved = 0

    for path in sorted(GROUNDTRUTH_DIR.rglob("*")):
        if not path.is_file():
            continue

        # `parser-legacy` and friends are already-archived snapshots of an
        # older groundtruth format; they are not read by any test and are not
        # this script's business.
        if any(
            part.endswith("-legacy") for part in path.relative_to(GROUNDTRUTH_DIR).parts
        ):
            continue

        reason = classify(path, known_docs, args.pages)
        if reason is None:
            continue

        reasons[reason] = reasons.get(reason, 0) + 1
        docs.add(ARTIFACT_RE.match(path.name).group("doc"))  # type: ignore[union-attr]
        moved += 1

        if args.dry_run:
            continue

        target = LEGACY_DIR / path.relative_to(GROUNDTRUTH_DIR)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(path), str(target))

    verb = "would move" if args.dry_run else "moved"
    print(f"{verb} {moved} file(s) from {len(docs)} document(s) to {LEGACY_DIR}")
    for reason, count in sorted(reasons.items()):
        print(f"  {count:6d}  {reason}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
