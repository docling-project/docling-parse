#!/usr/bin/env python
"""The `sh` operator and shading dictionaries (PDF 32000-1, 8.7.4.3).

`sh` was ignored, so every panel, banner and gradient background painted with
one came out blank. Painting them needs an extent, and a shading dictionary
often carries no /BBox: the extent then comes from the clip in force.

That is where the risk sits. The clip a page happens to be left with can be the
whole page, and treating it as the shading's extent sheets an opaque gradient
over everything already drawn -- which erased pages outright the first time.
What is asserted here is only what the spec fixes: the gradient is painted,
and it is bounded by the clip in force. How a shading with no extent at all
should be treated, and whether a page-wide one should be trusted once content
is down, are policy calls that belong with the shading implementation rather
than with these tests.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf
from tests.rendering_regression import (
    center_color,
    coverage_ratio,
    region_image,
)

RED = (255, 0, 0)
BLUE = (0, 0, 255)

# a horizontal red-to-blue ramp across the middle of the page
AXIAL_SHADING = (
    "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [20 0 180 0] "
    "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> "
    "/Extend [true true] >>"
)

LEFT_BOX = (25.0, 80.0, 45.0, 120.0)
RIGHT_BOX = (155.0, 80.0, 175.0, 120.0)
BAND_BOX = (20.0, 60.0, 180.0, 140.0)


def _shading_page(content: str, shading: str = AXIAL_SHADING) -> bytes:
    return simple_page_pdf(
        content,
        resources="/Shading << /Sh0 5 0 R >>",
        extra_objects=[shading],
    )


def test_axial_shading_is_painted():
    """`sh` inside a clip paints the gradient across the clipped band."""
    content = "q\n20 60 160 80 re W n\n/Sh0 sh\nQ\n"
    result = render_page(_shading_page(content))

    left = center_color(region_image(result, LEFT_BOX))
    right = center_color(region_image(result, RIGHT_BOX))

    assert left[0] > left[2] + 60, f"the red end of the gradient is not red: {left}"
    assert right[2] > right[0] + 60, (
        f"the blue end of the gradient is not blue: {right}"
    )


def test_shading_takes_its_extent_from_the_clip():
    """Without a /BBox the clip bounds the shading, and nothing outside it."""
    content = "q\n20 60 160 80 re W n\n/Sh0 sh\nQ\n"
    result = render_page(_shading_page(content))

    inside = coverage_ratio(region_image(result, BAND_BOX))
    assert inside > 0.9, (
        f"the clipped band was not filled by the shading ({inside:.3f})"
    )

    above = coverage_ratio(region_image(result, (20.0, 10.0, 180.0, 40.0)))
    assert above < 0.02, (
        f"the shading painted outside its clip ({above:.3f} coverage above the band)"
    )


def test_page_wide_shading_at_page_start_is_a_background():
    """A page-wide shading before anything else is drawn is the background.

    Refusing every page-wide shading would drop legitimate gradient
    backgrounds, which is why the gate is about what is already on the page
    rather than about size alone.
    """
    content = "q\n0 0 200 200 re W n\n/Sh0 sh\nQ\n"
    result = render_page(_shading_page(content))

    left = center_color(region_image(result, LEFT_BOX))
    right = center_color(region_image(result, RIGHT_BOX))
    assert left[0] > left[2] + 60 and right[2] > right[0] + 60, (
        f"a page-wide shading at page start did not paint the background: "
        f"left {left}, right {right}"
    )
