from __future__ import annotations

from pathlib import Path

from huggingface_hub import snapshot_download

HF_DATASET_REPO_ID = "docling-project/regression-dataset-for-docling-parse"
# HF_DATASET_REVISION = "e7870acf502010bba9de54b93f68a5722d8706f5"
HF_DATASET_REVISION = "cd6513e9d541afb6196a1da168f90f4c8e43354d"
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
