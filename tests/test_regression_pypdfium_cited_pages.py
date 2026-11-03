#!/usr/bin/env python
"""Enforced comparison against pypdfium2 on the pages that rendering fixes cite.

`test_regression_pypdfium_render.py` reports how the whole corpus compares with
pypdfium2 but never fails. This file holds the pages a rendering fix was shown
on: each one must stay within its own tolerance of pypdfium2, so the fix cannot
regress unnoticed. Cases record an explicit render scale and tolerance and may
also cover pages protected by an existing workaround. Rendering runs in a child
process with a timeout so a native crash or hang fails the case without
terminating the pytest process.
"""

import subprocess
import sys
from pathlib import Path

import pytest
from PIL import Image

from tests.rendering_regression import (
    ImageTolerance,
    flatten_on_white,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
)
from tests.test_regression_pypdfium_render import PYPDFIUM_RENDER_SCALE

REPO_ROOT = Path(__file__).resolve().parents[1]
REGRESSION_DIR = Path("tests/data/regression")
RENDER_TIMEOUT = 60

RENDER_PAGE = """
import sys
from pathlib import Path

from tests.pdf_builder import render_page

result = render_page(
    Path(sys.argv[1]).read_bytes(),
    page_no=int(sys.argv[2]),
    scale=float(sys.argv[3]),
)
result.get_image().save(sys.argv[4])
"""

CITED_PAGES = [
    # A /CalRGB JPEG image XObject. It was drawn as the semi-transparent yellow
    # "missing image" placeholder (mean_abs_error 5.10, changed_pixels_ratio
    # 0.131 before the fix; 0.26 and 0.0045 after it).
    pytest.param(
        "dln_23cb379bc6159a37e5488b65bb08ad295ce3ff21d9ddf682064330594ad59db3.pdf",
        1,
        PYPDFIUM_RENDER_SCALE,
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
        PYPDFIUM_RENDER_SCALE,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=5.0, changed_pixels_ratio=0.08
        ),
        id="calgray-fax-scan",
    ),
    # Regression #365: scale 3 triggers the affine fill crash; scale 2 misses it.
    pytest.param(
        "31ZH7600.pdf",
        7,
        3.0,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=9.0, changed_pixels_ratio=0.09
        ),
        id="affine-image-clip-edge",
    ),
    # Regression #393: retain the cited subpixel stroke page. Main has the #407
    # curve-flattening workaround; direct native tests exercise the stroker.
    pytest.param(
        "NAB-22-29.pdf",
        29,
        PYPDFIUM_RENDER_SCALE,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=9.0, changed_pixels_ratio=0.09
        ),
        id="subpixel-stroked-curve",
    ),
    # #384: a magazine cover whose photo is a transparency group painted under
    # an ExtGState soft mask, with a second mask inside the group. Without the
    # mask the photo was painted opaque to its bounding box (mean_abs_error
    # 11.89, changed_pixels_ratio 0.326 before the fix; 1.58 and 0.020 after).
    pytest.param(
        "complex_invisible_fonts_01.pdf",
        1,
        PYPDFIUM_RENDER_SCALE,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=3.0, changed_pixels_ratio=0.05
        ),
        id="extgstate-soft-mask",
    ),
    # #378: a card whose rounded border is stroked under a curved clip. The
    # stroke ran past the clip at the corners (mean_abs_error 6.46,
    # changed_pixels_ratio 0.151 before the fix; 5.03 and 0.130 after).
    pytest.param(
        "921558209587984065-1.pdf",
        1,
        PYPDFIUM_RENDER_SCALE,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=5.8, changed_pixels_ratio=0.142
        ),
        id="stroke-through-curved-clip",
    ),
    # #384 again, the common case: a drop shadow under a title is a soft-masked
    # fill. Without the mask it was a solid grey block (mean_abs_error 4.41,
    # changed_pixels_ratio 0.094 before the fix; 1.54 and 0.032 after).
    pytest.param(
        "17898535961965146676-4.pdf",
        1,
        PYPDFIUM_RENDER_SCALE,
        ImageTolerance(
            pixel_threshold=12, mean_abs_error=2.5, changed_pixels_ratio=0.05
        ),
        id="soft-masked-drop-shadow",
    ),
]


@pytest.mark.parametrize(("document", "page", "scale", "tolerance"), CITED_PAGES)
def test_cited_page_matches_pypdfium(
    document: str,
    page: int,
    scale: float,
    tolerance: ImageTolerance,
    tmp_path: Path,
) -> None:
    pytest.importorskip(
        "pypdfium2",
        reason="pypdfium2 is required for the cross-renderer comparison",
    )
    pdf_path = (REGRESSION_DIR / document).resolve()
    output = tmp_path / "rendered-page.png"
    try:
        child = subprocess.run(
            [
                sys.executable,
                "-c",
                RENDER_PAGE,
                str(pdf_path),
                str(page),
                str(scale),
                str(output),
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=RENDER_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        pytest.fail(f"{document}@{page}: rendering exceeded {RENDER_TIMEOUT}s")
    assert child.returncode == 0, (
        f"{document}@{page}: render failed ({child.returncode}): {child.stderr[-2000:]}"
    )

    reference = render_pypdfium_page(pdf_path, page, scale=scale)
    with Image.open(output) as actual:
        comparison = measure_image_pair(
            document,
            page,
            flatten_on_white(actual),
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
