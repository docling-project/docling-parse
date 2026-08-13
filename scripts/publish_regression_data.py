#!/usr/bin/env python
"""Publish files to the regression dataset and print the revision to pin.

`tests/data` is a downloaded snapshot of

    docling-project/regression-dataset-for-docling-parse

pinned by `HF_DATASET_REVISION` in `tests/constants.py`. Downloading it has
always been scripted; putting anything back was a manual step, which is how a
groundtruth refresh ends up published from one machine and pinned from another.

Typical use, after adding a page that reproduces a defect:

    python scripts/publish_regression_data.py tests/data/regression/new-case.pdf
    python scripts/publish_regression_data.py --dry-run tests/data/groundtruth

Credentials come from the environment (`HF_TOKEN` or `HUGGINGFACE_HUB_TOKEN`),
never from this repository. The printed commit sha is what `HF_DATASET_REVISION`
should be set to; nothing here edits that constant, because the tests have to
keep passing against the old snapshot until the new one is reviewed.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tests.constants import HF_DATASET_REPO_ID  # noqa: E402
from tests.data_utils import TEST_DATA_DIR  # noqa: E402


def _relative_to_dataset(path: Path) -> str:
    """Path of `path` inside the dataset repository."""
    resolved = path.resolve()
    try:
        return resolved.relative_to(TEST_DATA_DIR.resolve()).as_posix()
    except ValueError:
        raise SystemExit(
            f"{path} is outside {TEST_DATA_DIR}; a file can only be published "
            "from the place it is downloaded to, so that the layout in the "
            "dataset matches what the tests expect"
        )


def _collect(paths: list[Path]) -> list[tuple[Path, str]]:
    files: list[tuple[Path, str]] = []
    for path in paths:
        if not path.exists():
            raise SystemExit(f"{path} does not exist")
        if path.is_dir():
            for child in sorted(path.rglob("*")):
                if child.is_file():
                    files.append((child, _relative_to_dataset(child)))
        else:
            files.append((path, _relative_to_dataset(path)))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help="files or directories under tests/data to upload",
    )
    parser.add_argument(
        "--repo-id",
        default=HF_DATASET_REPO_ID,
        help=f"dataset repository (default: {HF_DATASET_REPO_ID})",
    )
    parser.add_argument(
        "--message",
        default="Update regression fixtures",
        help="commit message for the dataset revision",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="list what would be uploaded and stop",
    )
    args = parser.parse_args()

    files = _collect(args.paths)
    if not files:
        raise SystemExit("nothing to upload")

    print(f"{len(files)} file(s) -> {args.repo_id}")
    for local, remote in files:
        print(f"  {remote}  ({local.stat().st_size} bytes)")

    if args.dry_run:
        print("\ndry run: nothing was uploaded")
        return 0

    if not (os.getenv("HF_TOKEN") or os.getenv("HUGGINGFACE_HUB_TOKEN")):
        raise SystemExit(
            "no HF_TOKEN or HUGGINGFACE_HUB_TOKEN in the environment; "
            "publishing needs write access to the dataset"
        )

    from huggingface_hub import CommitOperationAdd, HfApi

    api = HfApi()
    commit = api.create_commit(
        repo_id=args.repo_id,
        repo_type="dataset",
        commit_message=args.message,
        operations=[
            CommitOperationAdd(path_in_repo=remote, path_or_fileobj=str(local))
            for local, remote in files
        ],
    )

    revision = getattr(commit, "oid", None) or str(commit)
    print(f"\npublished as {revision}")
    print(
        "set HF_DATASET_REVISION in tests/constants.py to this sha once the "
        "change has been reviewed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
