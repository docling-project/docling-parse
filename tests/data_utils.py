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
RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR = RENDER_GROUNDTRUTH_DIR / "instructions"
RENDER_GROUNDTRUTH_PAGES_DIR = RENDER_GROUNDTRUTH_DIR / "pages"


def ensure_test_data_downloaded(force: bool = False) -> Path:
    TEST_DATA_DIR.mkdir(parents=True, exist_ok=True)

    if not force and any(TEST_DATA_DIR.iterdir()):
        return TEST_DATA_DIR

    snapshot_download(
        repo_id=HF_DATASET_REPO_ID,
        repo_type="dataset",
        revision=HF_DATASET_REVISION,
        local_dir=str(TEST_DATA_DIR),
        force_download=force,
    )
    return TEST_DATA_DIR
