HF_DATASET_REPO_ID = "docling-project/regression-dataset-for-docling-parse"
HF_DATASET_REVISION = "590465e48f45e7ac7b95317d970ab297f496750f"

REGRESSION_DIR = "tests/data/regression"

# Individual documents used by the standalone (non-regression) tests. These come
# from the downloaded dataset, not from docs/, so that the test-suite has a
# single source of pdf's.
SAMPLE_PDF = f"{REGRESSION_DIR}/dln-v1.pdf"
LARGE_SAMPLE_PDF = f"{REGRESSION_DIR}/PDF32000_2008.pdf"

# Restricts, for pdf's with many pages, which pages are decoded and verified.
# Documents absent from this map are verified in full.
PARSER_PAGE_RESTRICTIONS = {
    "deep-mediabox-inheritance.pdf": [2],
    "font_06.pdf": [1],
    "font_07.pdf": [1],
    "font_08.pdf": [1],
    "font_09.pdf": [1],
    "font_10.pdf": [1],
    "font_11.pdf": [1, 2],
    "2508.13113v2.pdf": [2, 9, 17],
    "dln-v1.pdf": [1],
    "PDF32000_2008.pdf": [1, 2],
}
