"""Shared constants for the test-suite.

Single source of truth for the regression dataset pin, the data folders and the
per-document page restrictions, so that the sequential and threaded parser tests
can never drift apart.
"""

from __future__ import annotations

HF_DATASET_REPO_ID = "docling-project/regression-dataset-for-docling-parse"
HF_DATASET_REVISION = "4981ead2ee1fead1e9c278feb657172b1ea40e91"

GROUNDTRUTH_DIR = "tests/data/groundtruth"

# The parser groundtruth (.py.json and the derived .{unit}.txt files) lives in
# its own sub-folder, next to the render groundtruth in `<GROUNDTRUTH_DIR>/render`.
# Never write to GROUNDTRUTH_DIR itself: files at the root are read by nothing and
# shadow the real groundtruth.
PARSER_GROUNDTRUTH_FOLDER = f"{GROUNDTRUTH_DIR}/parser"

RENDER_GROUNDTRUTH_FOLDER = f"{GROUNDTRUTH_DIR}/render"
RENDER_PAGES_GROUNDTRUTH_FOLDER = f"{RENDER_GROUNDTRUTH_FOLDER}/pages"

# The png's in `render/pages` were rendered at this scale. The render regression
# test has to use the same one, or every comparison is a size mismatch.
RENDER_SCALE = 2.0

# Rendering is deterministic on a given machine, but fonts that are not embedded
# are resolved against the system fonts, so the rasterization differs slightly
# between mac / linux / windows. Per-pixel channel differences up to
# RENDER_MAX_CHANNEL_DELTA are ignored, and at most RENDER_MAX_FRACTION_DIFFERING
# of the pixels of a page may exceed it.
RENDER_MAX_CHANNEL_DELTA = 8
RENDER_MAX_FRACTION_DIFFERING = 0.01

REGRESSION_DIR = "tests/data/regression"
REGRESSION_FOLDER = f"{REGRESSION_DIR}/*.pdf"

# Individual documents used by the standalone (non-regression) tests. These come
# from the downloaded dataset, not from docs/, so that the test-suite has a
# single source of pdf's.
SAMPLE_PDF = f"{REGRESSION_DIR}/dln-v1.pdf"
LARGE_SAMPLE_PDF = f"{REGRESSION_DIR}/PDF32000_2008.pdf"

# Restricts, for pdf's with many pages, which pages are compared against the
# groundtruth. Pages that are not listed here are still parsed, only their
# verification is skipped. Documents absent from this map are verified in full.
PARSER_PAGE_RESTRICTIONS = {
    "deep-mediabox-inheritance.pdf": [2],
    "font_06.pdf": [1],
    "font_07.pdf": [1],
    "font_08.pdf": [1],
    "font_09.pdf": [1],
    "font_10.pdf": [1],
    "2508.13113v2.pdf": [2, 9, 17],
    "dln-v1.pdf": [1, 2],
    "PDF32000_2008.pdf": [1, 2],
}
