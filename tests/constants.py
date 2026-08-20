from tests.regression_page_selection import REGRESSION_PAGE_SELECTION

HF_DATASET_REPO_ID = "docling-project/regression-dataset-for-docling-parse"
HF_DATASET_REVISION = "b91b0106a7711425b11854618cda54d7651d8fac"

REGRESSION_DIR = "tests/data/regression"

# Individual documents used by the standalone (non-regression) tests. These come
# from the downloaded dataset, not from docs/, so that the test-suite has a
# single source of pdf's.
SAMPLE_PDF = f"{REGRESSION_DIR}/dln-v1.pdf"
LARGE_SAMPLE_PDF = f"{REGRESSION_DIR}/PDF32000_2008.pdf"

# Which pages of each regression document are decoded and verified. Consecutive
# pages of one document mostly repeat each other's code paths, so the suite
# samples a handful per document instead of decoding every page of every
# document. Regenerate with `tests/tools/select_regression_pages.py` after adding
# documents to `tests/data/regression`; that script also documents the rules.
PARSER_PAGE_RESTRICTIONS = REGRESSION_PAGE_SELECTION
