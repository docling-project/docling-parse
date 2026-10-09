#!/usr/bin/env python
"""A flat stroked cubic that folds back must be drawn to its full extent.

Blend2D strokes a flat (collinear) cubic as a polyline through its cusps, the
roots of a quadratic. It computed them with the textbook formula, which divides
by the quadratic's leading coefficient; that coefficient is zero when the cusp
equation is linear, and the cusp was lost. A curve that goes out and comes back
on itself, P -> Q -> Q -> P, was then stroked as a line from P to P and not
drawn at all, and a cusp past the end point was cut off at the end point
(blend2d/blend2d#272, fixed by blend2d/blend2d#273, carried here as
`cmake/blend2d-stroke-flat-cubic-linear-cusp.patch`).
"""

from __future__ import annotations

import numpy as np
import pytest

from tests.pdf_builder import render_page, simple_page_pdf

SCALE = 2.0
STROKE = "0 G 2 w 1 J 1 j"  # 2 pt wide, round caps and joins: ink reaches 2 px past the curve


@pytest.mark.parametrize(
    ("path", "reach_x_pt"),
    [
        # Out to (60, 50) and back; the curve turns at t = 0.5, x = 50.
        ("20 50 m 60 50 60 50 20 50 c", 50.0),
        # A cusp at t = 0.75, x = 53.75, past the end point at x = 50.
        ("20 30 m 50 30 60 30 50 30 c", 53.75),
    ],
)
def test_flat_cubic_reaches_its_cusp(path: str, reach_x_pt: float) -> None:
    image = render_page(
        simple_page_pdf(f"q {STROKE} {path} S Q", media_box="[0 0 100 100]"),
        scale=SCALE,
    ).get_image()
    gray = np.asarray(image.convert("L"))
    _, xs = np.nonzero(gray < 128)
    assert len(xs) > 0, "the stroke was not drawn"

    left = 20.0 * SCALE - 2.0
    right = reach_x_pt * SCALE + 2.0
    assert abs(xs.min() - left) <= 1.0
    assert abs(xs.max() + 1 - right) <= 1.0, (
        f"ink ends at x = {xs.max() + 1}, the stroke reaches x = {right}"
    )
