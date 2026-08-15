#!/usr/bin/env python
"""Pattern colour spaces (PDF 32000-1, 8.7.3).

A tiling pattern is a content stream replayed on a lattice. Nothing replayed it
before, so a banner painted with one came out empty; and a `scn` naming a
pattern the parser could not resolve left the fill colour at its initial value,
which painted the region solid black -- far worse than leaving it alone.

The lattice is the part worth pinning. A first version derived the cell index
range in one direction only, so the row at index -1 was never emitted and the
banner was tiled across but not down: coverage that looks plausible in total
while half the lattice is missing. These tests compare the halves of the filled
area against each other, which is what that bug breaks.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import coverage_ratio, region_image

CELL = 20.0
INK = 10.0

# the painted area, inset from the page edge, in top-left page coordinates
FILL_BOX = (20.0, 20.0, 180.0, 180.0)

# each cell paints an INK x INK square out of a CELL x CELL step
CELL_COVERAGE = (INK * INK) / (CELL * CELL)


def _tiling_pattern_object(paint: str = "1 0 0 rg") -> bytes:
    return stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"{paint}\n0 0 {INK} {INK} re f\n".encode("latin-1"),
    )


def _pattern_filled_page(pattern_object: bytes) -> bytes:
    content = (
        "/Pattern cs /P0 scn\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
    )
    return simple_page_pdf(
        content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[pattern_object],
    )


def test_tiling_pattern_is_painted():
    """A fill in a tiling pattern space paints the pattern cell, repeatedly."""
    image = region_image(
        render_page(_pattern_filled_page(_tiling_pattern_object())), FILL_BOX
    )

    coverage = coverage_ratio(image)
    assert coverage > 0.05, (
        f"the tiling pattern painted almost nothing ({coverage:.3f} coverage); "
        "the fill region is empty"
    )
    assert coverage == pytest.approx(CELL_COVERAGE, abs=0.08), (
        f"tiled coverage {coverage:.3f} does not match the {CELL_COVERAGE:.3f} "
        "the pattern cell paints out of its step"
    )


def test_tiling_lattice_covers_both_directions():
    """The lattice repeats across *and* down the filled region.

    Comparing the halves catches a lattice that only advances one way: total
    coverage stays believable while an entire direction is missing.
    """
    image = region_image(
        render_page(_pattern_filled_page(_tiling_pattern_object())), FILL_BOX
    )
    width, height = image.size

    left = coverage_ratio(image.crop((0, 0, width // 2, height)))
    right = coverage_ratio(image.crop((width // 2, 0, width, height)))
    top = coverage_ratio(image.crop((0, 0, width, height // 2)))
    bottom = coverage_ratio(image.crop((0, height // 2, width, height)))

    assert left == pytest.approx(right, abs=0.06), (
        f"the lattice does not repeat horizontally: left {left:.3f} vs "
        f"right {right:.3f}"
    )
    assert top == pytest.approx(bottom, abs=0.06), (
        f"the lattice does not repeat vertically: top {top:.3f} vs bottom {bottom:.3f}"
    )


def test_pattern_matrix_is_relative_to_the_page():
    """/Matrix maps pattern space to the page's default space, not the CTM.

    The pattern is anchored to the page, so a `cm` in force when the pattern is
    used must not move the lattice. Painting the same pattern through a
    translated CTM has to leave the same tiles in the same places.
    """
    plain = _pattern_filled_page(_tiling_pattern_object())
    shifted_content = (
        "q\n1 0 0 1 7 11 cm\n"
        "/Pattern cs /P0 scn\n"
        f"{FILL_BOX[0] - 7} {FILL_BOX[0] - 11} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\nQ\n"
    )
    shifted = simple_page_pdf(
        shifted_content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[_tiling_pattern_object()],
    )

    plain_image = region_image(render_page(plain), FILL_BOX)
    shifted_image = region_image(render_page(shifted), FILL_BOX)

    assert coverage_ratio(plain_image) == pytest.approx(
        coverage_ratio(shifted_image), abs=0.05
    ), "a CTM in force when the pattern is used moved the lattice"


def _solid_pattern_object() -> bytes:
    """A cell that paints its whole step, so any leak outside the fill shows."""
    return stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"1 0 0 rg\n0 0 {CELL} {CELL} re f\n".encode("latin-1"),
    )


def test_pattern_is_bounded_by_the_filled_path():
    """The lattice paints only inside the path being filled (8.7.3.1).

    The cells are laid out to cover the page box, so without the filled path
    bounding them the pattern tiles the entire page: a document whose pattern
    fill was one small shape came out with a block of cells in the corner that
    no other renderer draws.
    """
    content = "/Pattern cs /P0 scn\n80 80 40 40 re f\n"
    pdf = simple_page_pdf(
        content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[_solid_pattern_object()],
    )
    page = render_page(pdf)

    inside = coverage_ratio(region_image(page, (85.0, 85.0, 115.0, 115.0)))
    assert inside > 0.9, (
        f"the pattern did not paint inside the filled path ({inside:.3f} coverage)"
    )

    for name, box in (
        ("top-left", (0.0, 0.0, 60.0, 60.0)),
        ("top-right", (140.0, 0.0, 200.0, 60.0)),
        ("bottom-left", (0.0, 140.0, 60.0, 200.0)),
        ("bottom-right", (140.0, 140.0, 200.0, 200.0)),
    ):
        outside = coverage_ratio(region_image(page, box))
        assert outside < 0.02, (
            f"the pattern painted outside the filled path: the {name} corner "
            f"has {outside:.3f} coverage and the fill never reached it"
        )


def test_pattern_bound_honours_the_even_odd_rule():
    """`f*` bounds the pattern by the even-odd rule, so a hole stays a hole.

    Two nested rectangles wound the same way: under even-odd the inner one is a
    hole, under nonzero it is filled. Bounding the pattern with the wrong rule
    fills the hole with cells.
    """
    rects = "40 40 120 120 re\n70 70 60 60 re\n"
    hole = (90.0, 90.0, 110.0, 110.0)
    ring = (50.0, 50.0, 65.0, 65.0)

    even_odd = render_page(
        simple_page_pdf(
            f"/Pattern cs /P0 scn\n{rects}f*\n",
            resources="/Pattern << /P0 5 0 R >>",
            extra_objects=[_solid_pattern_object()],
        )
    )
    nonzero = render_page(
        simple_page_pdf(
            f"/Pattern cs /P0 scn\n{rects}f\n",
            resources="/Pattern << /P0 5 0 R >>",
            extra_objects=[_solid_pattern_object()],
        )
    )

    assert coverage_ratio(region_image(even_odd, ring)) > 0.9, (
        "the pattern did not paint the ring between the two rectangles"
    )
    assert coverage_ratio(region_image(even_odd, hole)) < 0.02, (
        "f* left the inner rectangle filled; the pattern was bounded by the "
        "nonzero rule instead of even-odd"
    )
    assert coverage_ratio(region_image(nonzero, hole)) > 0.9, (
        "f left the inner rectangle empty; the same-wound subpath is not a hole "
        "under the nonzero rule"
    )


def test_unresolved_pattern_paints_nothing():
    """A fill naming a pattern that cannot be resolved is skipped, not blackened.

    `scn` with an unknown pattern name leaves the fill colour where it was --
    initially black -- so painting the fill anyway floods the region. Skipping
    the fill loses the artwork; painting it black loses the page.
    """
    content = (
        "/Pattern cs /DoesNotExist scn\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
    )
    pdf = simple_page_pdf(content, resources="/Pattern << >>")

    image = region_image(render_page(pdf), FILL_BOX)
    assert coverage_ratio(image) < 0.02, (
        "an unresolved pattern fill painted the region; it must paint nothing"
    )


def test_unresolved_pattern_keeps_the_stroke():
    """Only the fill is dropped: a stroke in the same operator still paints."""
    content = (
        "/Pattern cs /DoesNotExist scn\n"
        "0 0 1 RG\n2 w\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re B\n"
    )
    pdf = simple_page_pdf(content, resources="/Pattern << >>")

    image = region_image(render_page(pdf), FILL_BOX)
    coverage = coverage_ratio(image)
    assert 0.0 < coverage < 0.3, (
        f"expected only the outline to paint, got {coverage:.3f} coverage"
    )
