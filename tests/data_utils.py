from __future__ import annotations

from pathlib import Path

from huggingface_hub import snapshot_download

HF_DATASET_REPO_ID = "docling-project/regression-dataset-for-docling-parse"
# HF_DATASET_REVISION = "5d7c3d7b575397ca5b2a943171b0da4fe08c5a5b"
HF_DATASET_REVISION = "9a3713bd2e7b5b55ad9dde9d85953a0f5eb5150e"
TESTS_DIR = Path(__file__).resolve().parent
TEST_DATA_DIR = TESTS_DIR / "data"


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
