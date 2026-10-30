#!/usr/bin/env python
"""A stroked cubic that is smaller than a pixel must render, not hang.

docling-parse #393: page 29 of a dense vector map never finished rendering at
scale 2. The page holds a hairline (`0 w`) with round caps and joins, a line
followed by a cubic, scaled by about 0.096, so the cubic's control polygon is
about 0.6 x 0.4 px on the canvas. Blend2D approximates the cubic with quads
and offsets each quad by splitting it at the parameter where its tangent has
turned by a fixed angle. One quad was nearly collinear and turned back on
itself; after a split landed on the turning point, the derivative of the rest
was only rounding noise, the next split parameter was computed from that noise,
and the split returned the same curve. The loop never terminated, inside the
native call, so neither Python nor Docling's document timeout could stop it
(blend2d/blend2d#269, fixed by blend2d/blend2d#270, carried here as
`cmake/blend2d-stroke-offset-quad-stall.patch`).

The fixture is the reporter's one-page reproducer, stroked in black instead of
white so the test can also check where the ink lands. The render runs in a
child process, so a regression shows up as a timeout instead of stalling the
whole test run.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from tests.pdf_builder import simple_page_pdf

REPO_ROOT = Path(__file__).resolve().parents[1]

SCALE = 2.0
MEDIA_BOX = "[0 0 595.32 841.92]"  # renders to 1191 x 1684 px, so the scale is 2.0006
EXPECTED_SIZE = (1191, 1684)
CONTENT = (
    "q 1 0 0 1 56.9459229 0 cm 0.095993 0 0 -0.0959473 254.7695923 485.9789734 cm "
    "0 G 0 w 1 j 1 J 0 0 m 4 4 l 7 3 5 4 7 2 c S Q"
)

# The stroke spans about (623.6, 711.9) - (625.0, 712.7) on the canvas; with
# a 1 px hairline and round caps, all of its ink falls inside this box.
INK_BOX = (620, 708, 629, 717)

RENDER_CHILD = """
import sys
from pathlib import Path

from tests.pdf_builder import render_page

image = render_page(Path(sys.argv[1]).read_bytes(), scale=float(sys.argv[2])).get_image()
gray = image.convert("L")
ink_box = gray.point(lambda v: 255 if v < 250 else 0).getbbox() or (-1, -1, -1, -1)
print(*gray.size, *ink_box, gray.getextrema()[0])
"""


def test_subpixel_stroked_cubic_renders(tmp_path: Path) -> None:
    pdf_path = tmp_path / "subpixel_cubic.pdf"
    pdf_path.write_bytes(simple_page_pdf(CONTENT, media_box=MEDIA_BOX))

    try:
        completed = subprocess.run(
            [sys.executable, "-c", RENDER_CHILD, str(pdf_path), str(SCALE)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        pytest.fail(
            "rendering a sub-pixel stroked cubic did not return (blend2d/blend2d#269)"
        )

    assert completed.returncode == 0, completed.stderr[-2000:]
    width, height, x0, y0, x1, y1, darkest = (int(v) for v in completed.stdout.split())
    assert (width, height) == EXPECTED_SIZE
    assert darkest < 128, "the stroke was not drawn"
    bx0, by0, bx1, by1 = INK_BOX
    assert bx0 <= x0 and by0 <= y0 and x1 <= bx1 and y1 <= by1, (
        f"ink spans ({x0}, {y0}) - ({x1}, {y1}), outside the stroke's box {INK_BOX}"
    )
