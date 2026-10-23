#!/usr/bin/env python
"""A tiny stroked cubic with a cusp must not draw a streak across the page.

Blend2D offsets a stroked curve piece by piece, taking each piece's start and
end normals from its control legs. Near a cusp, a piece can end at the turning
point of the curve, where its last leg is a few ULPs of rounding noise. Blend2D
used that leg as a direction; when it pointed backwards, the offset control
point landed arbitrarily far away. On this page a hairline cubic of about
0.6 x 0.7 px rendered as a vertical streak over the full height of the canvas.
The fix (blend2d/blend2d#271, carried here as
`cmake/blend2d-stroke-offset-degenerate-legs.patch`) ignores such legs and
joins the offsets of consecutive pieces where the tangent turns back.

The fixture was found by rendering random tiny hairline cubics, shaped like the
page of docling-parse #393, and keeping one whose ink left the path's box.
"""

from __future__ import annotations

import numpy as np

from tests.pdf_builder import render_page, simple_page_pdf

SCALE = 2.0
PAGE_SIZE = 100.0
CTM = (0.0704499, -0.1068014, 53.6724757, 68.6612491)  # a, d, e, f of the `cm`
POINTS = [(0, 0), (3, 5), (7, 4), (6, 7), (6, 4)]  # m, l, c control points
CONTENT = (
    f"q {CTM[0]} 0 0 {CTM[1]} {CTM[2]} {CTM[3]} cm 0 G 0 w 1 j 1 J "
    "0 0 m 3 5 l 7 4 6 7 6 4 c S Q"
)

# A hairline is drawn 1 px wide with round caps, so its ink stays within about
# 1 px of the control points' box; allow 3 px for anti-aliasing.
MARGIN_PX = 3.0


def test_cusp_stroke_stays_near_its_path() -> None:
    image = render_page(
        simple_page_pdf(CONTENT, media_box=f"[0 0 {PAGE_SIZE} {PAGE_SIZE}]"),
        scale=SCALE,
    ).get_image()
    gray = np.asarray(image.convert("L"))
    ys, xs = np.nonzero(gray < 250)
    assert len(xs) > 0, "the stroke was not drawn"

    a, d, e, f = CTM
    dev_x = [(a * x + e) * SCALE for x, _ in POINTS]
    dev_y = [(PAGE_SIZE - (d * y + f)) * SCALE for _, y in POINTS]
    x0, x1 = min(dev_x) - MARGIN_PX, max(dev_x) + MARGIN_PX
    y0, y1 = min(dev_y) - MARGIN_PX, max(dev_y) + MARGIN_PX

    ink = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    assert x0 <= ink[0] and y0 <= ink[1] and ink[2] <= x1 and ink[3] <= y1, (
        f"ink spans {ink}, outside the stroke's box "
        f"({x0:.1f}, {y0:.1f}, {x1:.1f}, {y1:.1f})"
    )
