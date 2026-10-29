#!/usr/bin/env python
"""Enforced comparison against pypdfium2 on the pages that rendering fixes cite.

`test_regression_pypdfium_render.py` reports how the whole corpus compares with
pypdfium2 but never fails. This file holds the pages a rendering fix was shown
on: each one must stay within its own tolerance of pypdfium2, so the fix cannot
regress unnoticed. A fix adds its pages here, with a tolerance that fails on the
code before the fix.
"""

from pathlib import Path

import pytest

from tests.rendering_regression import (
    ImageTolerance,
    flatten_on_white,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
)
from tests.test_regression_pypdfium_render import (
    PYPDFIUM_RENDER_SCALE,
    _make_pypdfium_render_config,
)
from tests.test_regression_threaded_render import _make_parser

REGRESSION_DIR = Path("tests/data/regression")

CITED_PAGES = [
    # A /CalRGB JPEG image XObject. It was drawn as the semi-transparent yellow
    # "missing image" placeholder (mean_abs_error 5.10, changed_pixels_ratio
    # 0.131 before the fix; 0.26 and 0.0045 after it).
    pytest.param(
        "dln_23cb379bc6159a37e5488b65bb08ad295ce3ff21d9ddf682064330594ad59db3.pdf",
        1,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=1.0, changed_pixels_ratio=0.02
        ),
        id="calrgb-image",
    ),
    # docling#4861: a scanned invoice written as a /CalGray CCITT Group 4 image
    # by an image-to-PDF converter. It was drawn as the yellow placeholder, so
    # OCR on the rendered page found nothing (mean_abs_error 38.1, changed_pixels_ratio
    # 0.111 before the fix; 3.30 and 0.051 after it, the rest being how the 1-bit
    # scan is anti-aliased when downsampled).
    pytest.param(
        "APT002_issue_calibrated_gray_colorspace.pdf",
        1,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=5.0, changed_pixels_ratio=0.08
        ),
        id="calgray-fax-scan",
    ),
]


@pytest.mark.parametrize(("document", "page", "tolerance"), CITED_PAGES)
def test_cited_page_matches_pypdfium(
    document: str, page: int, tolerance: ImageTolerance
) -> None:
    pytest.importorskip(
        "pypdfium2",
        reason="pypdfium2 is required for the cross-renderer comparison",
    )
    pdf_path = REGRESSION_DIR / document

    parser = _make_parser(render_config=_make_pypdfium_render_config())
    doc_key = parser.load(str(pdf_path), page_numbers=[page])
    results = list(parser.iterate_results())
    parser.unload_all()

    assert len(results) == 1
    result = results[0]
    assert result.doc_key == doc_key
    assert result.success, result.error_message

    reference = render_pypdfium_page(pdf_path, page, scale=PYPDFIUM_RENDER_SCALE)
    comparison = measure_image_pair(
        document,
        page,
        flatten_on_white(result.get_image()),
        flatten_on_white(reference),
        tolerance=tolerance,
    )

    assert not image_comparison_failed(comparison, tolerance=tolerance), (
        f"{document}@{page}: mean_abs_error={comparison.mean_abs_error:.4f} "
        f"(limit {tolerance.mean_abs_error}), "
        f"changed_pixels_ratio={comparison.changed_pixels_ratio:.4f} "
        f"(limit {tolerance.changed_pixels_ratio}), "
        f"size_matches={comparison.size_matches}"
    )
