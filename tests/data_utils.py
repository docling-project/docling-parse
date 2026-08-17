from __future__ import annotations

from pathlib import Path

from huggingface_hub import snapshot_download

from tests.constants import HF_DATASET_REPO_ID, HF_DATASET_REVISION

TESTS_DIR = Path(__file__).resolve().parent
TEST_DATA_DIR = TESTS_DIR / "data"
TEST_DATA_GROUNDTRUTH_DIR = TEST_DATA_DIR / "groundtruth"
PARSER_GROUNDTRUTH_DIR = TEST_DATA_GROUNDTRUTH_DIR / "parser"
RENDER_GROUNDTRUTH_DIR = TEST_DATA_GROUNDTRUTH_DIR / "render"
RENDER_GROUNDTRUTH_BITMAPS_DIR = RENDER_GROUNDTRUTH_DIR / "bitmaps"
# Rasterised Type3 glyphs come out of the same instruction stream as images,
# but there is one per painted character: a single page of Type3 text yields
# hundreds. They are kept apart so `bitmaps/` stays a directory of pictures.
RENDER_GROUNDTRUTH_GLYPHS_DIR = RENDER_GROUNDTRUTH_DIR / "glyphs"
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
