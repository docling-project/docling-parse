#!/usr/bin/env python
# SPDX-License-Identifier: MIT
"""Keep a real composite-font glyph from being treated as an AFM space."""

from pathlib import Path

import pytest

from tests.pdf_builder import render_page
from tests.rendering_regression import (
    ImageTolerance,
    flatten_on_white,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
)

REGRESSION_DIR = Path(__file__).parent / "data" / "regression"


def test_composite_cid_32_keeps_formula_equals() -> None:
    pytest.importorskip("pypdfium2")
    document = "10749817875312063576_009.pdf"
    pdf_path = REGRESSION_DIR / document
    page_no, scale = 5, 3.0

    # "Nonideal Flow Models in Homogeneous Chemical Reactors", equation (9).
    # The embedded SymbolMT subset maps CID 32 to '='. Applying the standard
    # font's numeric AFM slot 32 gives it a zero-height space bbox (#466).
    # Focus on the first '=', which otherwise contributes too few pixels to
    # fail a whole-page image comparison. Coordinates are top-left PDF points.
    left, top, right, bottom = 100, 124, 114, 139
    crop = (
        round(left * scale),
        round(top * scale),
        round(right * scale),
        round(bottom * scale),
    )
    result = render_page(pdf_path.read_bytes(), page_no=page_no, scale=scale)
    actual = flatten_on_white(result.get_image()).crop(crop)
    reference = flatten_on_white(
        render_pypdfium_page(pdf_path, page_no, scale=scale)
    ).crop(crop)
    assert sum(reference.convert("L").histogram()[:128]) > 0, (
        "the reference '=' must have visible ink"
    )
    tolerance = ImageTolerance(
        pixel_threshold=12, mean_abs_error=3.0, changed_pixels_ratio=0.08
    )
    comparison = measure_image_pair(
        document, page_no, actual, reference, tolerance=tolerance
    )

    assert not image_comparison_failed(comparison, tolerance=tolerance), (
        f"the formula's '=' does not match PDFium: "
        f"mean_abs_error={comparison.mean_abs_error:.4f} "
        f"(limit {tolerance.mean_abs_error}), "
        f"changed_pixels_ratio={comparison.changed_pixels_ratio:.4f} "
        f"(limit {tolerance.changed_pixels_ratio})"
    )


@pytest.mark.parametrize(
    ("document", "page_no", "tolerance"),
    [
        pytest.param(
            "15643741724817381873-1.pdf",
            1,
            ImageTolerance(
                pixel_threshold=12, mean_abs_error=5.0, changed_pixels_ratio=0.15
            ),
            id="embedded-times",
        ),
        pytest.param(
            "926659986622553168-1.pdf",
            2,
            ImageTolerance(
                pixel_threshold=12, mean_abs_error=2.5, changed_pixels_ratio=0.07
            ),
            id="embedded-arial",
        ),
        pytest.param(
            "indexed_iccbased.pdf",
            1,
            ImageTolerance(
                pixel_threshold=12, mean_abs_error=4.0, changed_pixels_ratio=0.14
            ),
            id="embedded-cff",
        ),
    ],
)
def test_composite_font_rendering_control_pages(
    document: str, page_no: int, tolerance: ImageTolerance
) -> None:
    """Keep the small edge-pixel differences in #466 from hiding larger regressions.

    These controls pass on main too. All their text fonts are embedded, so the
    comparison does not depend on the host's substitute fonts. The observed
    MAEs at scale 2 were 4.42, 2.11, and 3.62; allow rasterization differences
    without using the much looser corpus-wide reporting tolerance.
    """
    pytest.importorskip("pypdfium2")
    pdf_path = REGRESSION_DIR / document
    result = render_page(pdf_path.read_bytes(), page_no=page_no, scale=2.0)
    comparison = measure_image_pair(
        document,
        page_no,
        flatten_on_white(result.get_image()),
        flatten_on_white(render_pypdfium_page(pdf_path, page_no, scale=2.0)),
        tolerance=tolerance,
    )

    assert not image_comparison_failed(comparison, tolerance=tolerance), (
        f"{document}@{page_no}: mean_abs_error={comparison.mean_abs_error:.4f} "
        f"(limit {tolerance.mean_abs_error}), "
        f"changed_pixels_ratio={comparison.changed_pixels_ratio:.4f} "
        f"(limit {tolerance.changed_pixels_ratio})"
    )
