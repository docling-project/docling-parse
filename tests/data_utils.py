from __future__ import annotations

from pathlib import Path

from huggingface_hub import snapshot_download

from tests.constants import HF_DATASET_REPO_ID, HF_DATASET_REVISION

TESTS_DIR = Path(__file__).resolve().parent
TEST_DATA_DIR = TESTS_DIR / "data"
TEST_DATA_GROUNDTRUTH_DIR = TEST_DATA_DIR / "groundtruth"
PARSER_GROUNDTRUTH_DIR = TEST_DATA_GROUNDTRUTH_DIR / "parser"
RENDER_GROUNDTRUTH_DIR = TEST_DATA_GROUNDTRUTH_DIR / "render"
# One metadata file per page, describing every bitmap the page painted. The
# per-artifact files it replaces ran to tens of thousands, and the dataset is
# stored in Git LFS, which does not cope with more than ~10k entries per
# directory.
RENDER_GROUNDTRUTH_BITMAPS_DIR = RENDER_GROUNDTRUTH_DIR / "bitmaps"
# The bitmap bytes themselves, for the subset that `select_retained_bitmaps()`
# keeps. Every artifact is still verified by hash through the metadata above;
# these files exist so a mismatch can be looked at.
RENDER_GROUNDTRUTH_BITMAP_DATA_DIR = RENDER_GROUNDTRUTH_DIR / "bitmap_data"
RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR = RENDER_GROUNDTRUTH_DIR / "instructions"
RENDER_GROUNDTRUTH_PAGES_DIR = RENDER_GROUNDTRUTH_DIR / "pages"


def ensure_test_data_downloaded(force: bool = False) -> Path:
    TEST_DATA_DIR.mkdir(parents=True, exist_ok=True)

    # A non-empty data dir is only current if it was synced at the pinned
    # revision; otherwise a checkout that predates a HF_DATASET_REVISION bump
    # would keep testing against stale groundtruth.
    revision_marker = TEST_DATA_DIR / ".hf_revision"

    if not force and any(TEST_DATA_DIR.iterdir()):
        cached_revision = (
            revision_marker.read_text().strip() if revision_marker.exists() else None
        )
        if cached_revision == HF_DATASET_REVISION:
            return TEST_DATA_DIR

    snapshot_download(
        repo_id=HF_DATASET_REPO_ID,
        repo_type="dataset",
        revision=HF_DATASET_REVISION,
        local_dir=str(TEST_DATA_DIR),
        force_download=force,
    )
    revision_marker.write_text(HF_DATASET_REVISION + "\n")
    return TEST_DATA_DIR
