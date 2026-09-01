#!/usr/bin/env python
"""Clipping to paths that are not axis-aligned rectangles (PDF 32000-1, 8.5.4).

`W n` intersects the clip with an arbitrary path. Blend2D can only clip to a
rectangle, so a curved clip used to be dropped with a warning and the clipped
content spilled over its whole bounding box: a crescent came out as the box
around it, and a circular badge as a square.

The renderer now rasterises such a clip into a coverage mask and paints the
fill -- or the image -- through it. These tests assert the shape of what lands
on the page rather than its pixels: a disc inscribed in its bounding box covers
pi/4 of it and leaves the corners empty, while the box that replaced it covers
everything.

The page-wide regression cannot see this. Restoring the old behaviour cost the
corpus page that first showed it 0.231 mean_abs_error, against a tolerance of 9.
"""

from __future__ import annotations

import math

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import (
    center_color,
    coverage_ratio,
    quadrant_coverage,
    region_image,
)

# quarter-circle bezier constant: 4/3 * (sqrt(2) - 1)
KAPPA = 0.5522847498

DISC_AREA_RATIO = math.pi / 4
CENTER = 100.0
RADIUS = 50.0

# the box the circle is inscribed in, in top-left page coordinates on a 200pt
# page; identical to the filled rectangle, so any coverage above the disc ratio
# is the clip having been ignored
DISC_BOX = (CENTER - RADIUS, CENTER - RADIUS, CENTER + RADIUS, CENTER + RADIUS)


def circle_path(cx: float, cy: float, r: float) -> str:
    """Four-bezier circle, as a PDF path without a painting operator."""
    k = KAPPA * r
    return (
        f"{cx + r} {cy} m\n"
        f"{cx + r} {cy + k} {cx + k} {cy + r} {cx} {cy + r} c\n"
        f"{cx - k} {cy + r} {cx - r} {cy + k} {cx - r} {cy} c\n"
        f"{cx - r} {cy - k} {cx - k} {cy - r} {cx} {cy - r} c\n"
        f"{cx + k} {cy - r} {cx + r} {cy - k} {cx + r} {cy} c\n"
        "h\n"
    )


def _red_image_object() -> bytes:
    return stream_object(
        "/Type /XObject /Subtype /Image /Width 4 /Height 4 "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8",
        bytes([255, 0, 0] * 16),
    )


def _assert_is_disc(image, *, what: str) -> None:
    coverage = coverage_ratio(image)
    assert coverage == pytest.approx(DISC_AREA_RATIO, abs=0.06), (
        f"{what}: filled {coverage:.3f} of the bounding box, expected "
        f"{DISC_AREA_RATIO:.3f} for a disc (1.0 means the clip was ignored)"
    )

    quadrants = quadrant_coverage(image)
    assert quadrants["center"] > 0.95, (
        f"{what}: the middle of the disc is not filled ({quadrants['center']:.3f})"
    )
    for corner in ("top_left", "top_right", "bottom_left", "bottom_right"):
        assert quadrants[corner] < 0.05, (
            f"{what}: {corner} carries ink ({quadrants[corner]:.3f}); the clip "
            "did not cut the corners off the bounding box"
        )


def test_circular_clip_shapes_a_fill():
    """A rectangle filled through a circular clip is a disc, not a rectangle."""
    content = (
        "q\n" + circle_path(CENTER, CENTER, RADIUS) + "W n\n"
        "1 0 0 rg\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {2 * RADIUS} {2 * RADIUS} re f\n"
        "Q\n"
    )
    result = render_page(simple_page_pdf(content))
    _assert_is_disc(region_image(result, DISC_BOX), what="circular clip on a fill")


def test_circular_clip_shapes_an_image():
    """An image drawn through a circular clip is a disc too.

    Images take a different route through the renderer than shapes, and the
    bounding-box spill was first noticed on an image.
    """
    content = (
        "q\n" + circle_path(CENTER, CENTER, RADIUS) + "W n\n"
        f"{2 * RADIUS} 0 0 {2 * RADIUS} {CENTER - RADIUS} {CENTER - RADIUS} cm\n"
        "/Im0 Do\n"
        "Q\n"
    )
    pdf = simple_page_pdf(
        content,
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[_red_image_object()],
    )
    _assert_is_disc(
        region_image(render_page(pdf), DISC_BOX), what="circular clip on an image"
    )


def test_rectangular_clip_beside_a_curved_one_still_cuts():
    """A clip holding both a rectangle and a circle applies both.

    The two are handled by different mechanisms -- the rectangle through the
    context's own clip, the circle through the coverage mask -- and an early
    version of the mask only ran when *no* rectangle was present, so a clip
    state carrying both silently kept the bounding box.
    """
    content = (
        "q\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {2 * RADIUS} {RADIUS} re\n"
        "W n\n" + circle_path(CENTER, CENTER, RADIUS) + "W n\n"
        "1 0 0 rg\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {2 * RADIUS} {2 * RADIUS} re f\n"
        "Q\n"
    )
    image = region_image(render_page(simple_page_pdf(content)), DISC_BOX)

    coverage = coverage_ratio(image)
    # the rectangle keeps the lower half of the circle: half a disc
    assert coverage == pytest.approx(DISC_AREA_RATIO / 2, abs=0.06), (
        f"rectangle and circle clip together filled {coverage:.3f}, expected "
        f"{DISC_AREA_RATIO / 2:.3f} for the lower half of a disc"
    )

    quadrants = quadrant_coverage(image)
    assert quadrants["top_left"] < 0.05 and quadrants["top_right"] < 0.05, (
        "the rectangular half of the clip was not applied"
    )
    assert quadrants["bottom_left"] < 0.05 and quadrants["bottom_right"] < 0.05, (
        "the circular half of the clip was not applied"
    )


def test_axis_aligned_clip_is_unaffected():
    """The rectangular fast path keeps working.

    The mask is only for what `clip_to_rect` cannot express; a plain
    rectangular clip must still cut exactly, with no coverage lost to the
    offscreen layer.
    """
    content = (
        "q\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {RADIUS} {2 * RADIUS} re W n\n"
        "0 0 1 rg\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {2 * RADIUS} {2 * RADIUS} re f\n"
        "Q\n"
    )
    image = region_image(render_page(simple_page_pdf(content)), DISC_BOX)

    coverage = coverage_ratio(image)
    assert coverage == pytest.approx(0.5, abs=0.02), (
        f"rectangular clip filled {coverage:.3f} of the box, expected half"
    )
    assert center_color(image.crop((0, 0, image.width // 2, image.height)))[2] > 200, (
        "the kept half of a rectangular clip lost its colour"
    )


def test_clip_outside_the_shape_paints_nothing():
    """A clip that shares no area with the fill leaves the page blank."""
    content = (
        "q\n" + circle_path(20, 20, 10) + "W n\n"
        "1 0 0 rg\n"
        f"{CENTER - RADIUS} {CENTER - RADIUS} {2 * RADIUS} {2 * RADIUS} re f\n"
        "Q\n"
    )
    image = region_image(render_page(simple_page_pdf(content)), DISC_BOX)
    assert coverage_ratio(image) < 0.01, (
        "a fill whose clip lies elsewhere still reached the page"
    )


# The corner the leaking instructions are confined to, well clear of MIDDLE.
CORNER_CLIP = (10.0, 160.0, 30.0, 30.0)  # PDF x y w h
CORNER_CIRCLE = (20.0, 175.0, 6.0)  # centre and radius, inside CORNER_CLIP
MISSING_TARGET = (30.0, 162.0, 8.0, 8.0)  # inside the clip, clear of the circle
MIDDLE = (50.0, 50.0, 150.0, 150.0)  # top-left coordinates, the page's middle


def _corner_then_middle(leaking_body: str, resources: str = "", extra=None) -> bytes:
    """A page that paints something clipped to nothing, then something plain.

    `leaking_body` is painted under two clips at once: a rectangle, which the
    renderer applies to the context, and a circle, which it can only apply as
    a coverage mask. The two share no area with what is painted, so the mask
    comes out empty and the instruction is abandoned -- the exact path that
    used to return while the rectangle's `save()` was still on the context.
    Everything after it in the page then inherited a clip nobody asked for.
    """
    x, y, w, h = CORNER_CLIP
    cx, cy, r = CORNER_CIRCLE

    content = (
        "q\n"
        f"{x} {y} {w} {h} re W n\n"
        "q\n" + circle_path(cx, cy, r) + "W n\n"
        f"{leaking_body}"
        "Q\n"
        "Q\n"
        "0 0 1 rg\n"
        "50 50 100 100 re f\n"
    )

    return simple_page_pdf(content, resources=resources, extra_objects=extra)


def test_a_shape_whose_clip_is_empty_does_not_clip_the_rest_of_the_page():
    """Abandoning one fill must not leave its clip behind.

    The clip is pushed onto the renderer's context with a save, so the return
    that gives up on an empty clip has to undo it. Leaving it stranded costs
    far more than the one fill: every later restore pops the wrong state, the
    clip keeps narrowing, and the rest of the page paints into nothing. Page
    752 of the PDF specification lost an entire figure that way -- a tiling
    pattern whose cell lattice steps past the filled path leaks one save per
    off-target cell, and there are hundreds.
    """
    mx, my, mw, mh = MISSING_TARGET
    pdf = _corner_then_middle(f"1 0 0 rg\n{mx} {my} {mw} {mh} re f\n")

    image = region_image(render_page(pdf), MIDDLE)

    coverage = coverage_ratio(image)
    assert coverage > 0.95, (
        f"the fill painted after an abandoned one covered {coverage:.3f} of its "
        "own box; a clip left on the context is cutting it"
    )
    assert center_color(image)[2] > 200, (
        "the fill painted after an abandoned one lost its colour"
    )


def test_an_image_whose_clip_is_empty_does_not_clip_the_rest_of_the_page():
    """The same, for images: they take their own route through the renderer."""
    mx, my, mw, mh = MISSING_TARGET
    pdf = _corner_then_middle(
        f"q\n{mw} 0 0 {mh} {mx} {my} cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra=[_red_image_object()],
    )

    image = region_image(render_page(pdf), MIDDLE)

    coverage = coverage_ratio(image)
    assert coverage > 0.95, (
        f"the fill painted after an abandoned image covered {coverage:.3f} of "
        "its own box; a clip left on the context is cutting it"
    )
