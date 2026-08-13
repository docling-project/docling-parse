#!/usr/bin/env python
"""Type3 fonts, whose glyphs are content streams (PDF 32000-1, 9.6.5).

A Type3 glyph is drawn by running its charproc, so a renderer that only knows
how to draw glyphs from a face draws nothing at all: pages set in a Type3 font
came out blank. Charprocs come in two kinds -- an inline image mask, or ordinary
path painting -- and both had to be executed.

Orientation is the part that is easy to get subtly wrong: glyph space is
y-up like user space, and taking the image-mask corner order from the wrong
place renders every glyph upside down, which still looks like text. So these
fixtures use deliberately lopsided glyphs and check which way up they land.
"""

from __future__ import annotations

from tests.pdf_builder import build_pdf, content_stream, render_page, stream_object
from tests.rendering_regression import (
    coverage_ratio,
    ink_bounds,
    quadrant_coverage,
    region_image,
)

FONT_SIZE = 100
GLYPH_BOX = (50.0, 50.0, 150.0, 150.0)


def _type3_pdf(charproc: bytes, *, resources: str = "<< >>") -> bytes:
    """One page drawing a single Type3 glyph 100pt tall at (50, 50)."""
    text = f"BT\n/T3 {FONT_SIZE} Tf\n50 50 Td\n(a) Tj\nET\n"
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources << /Font << /T3 5 0 R >> >> /Contents 4 0 R >>",
            content_stream(text),
            "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
            "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs 6 0 R "
            "/Encoding << /Type /Encoding /Differences [97 /square] >> "
            "/FirstChar 97 /LastChar 97 /Widths [1000] "
            f"/Resources {resources} >>",
            "<< /square 7 0 R >>",
            charproc,
        ]
    )


def test_vector_charproc_is_painted():
    """A charproc that fills a path puts ink on the page."""
    charproc = stream_object("", b"1000 0 0 0 1000 1000 d1\n0 0 1000 1000 re f\n")
    image = region_image(render_page(_type3_pdf(charproc)), GLYPH_BOX)

    assert coverage_ratio(image) > 0.8, (
        f"a Type3 glyph filling its whole box covered only "
        f"{coverage_ratio(image):.3f} of it"
    )


def test_vector_charproc_is_not_upside_down():
    """A glyph inked only in its top half lands in the top half of the page.

    Glyph space is y-up. Getting the corner order wrong flips the glyph, which
    is invisible on a symmetrical shape and wrong on every real one.
    """
    charproc = stream_object("", b"1000 0 0 0 1000 1000 d1\n0 500 1000 500 re f\n")
    image = region_image(render_page(_type3_pdf(charproc)), GLYPH_BOX)

    bounds = ink_bounds(image)
    assert bounds is not None, "the glyph painted nothing"
    _, top, _, bottom = bounds
    assert bottom <= image.height * 0.6, (
        f"the ink sits at rows {top}..{bottom} of {image.height}; a glyph inked "
        "in its upper half rendered in the lower half, so it is upside down"
    )


def test_charproc_graphics_state_is_balanced():
    """`q`/`Q` inside a charproc do not leak into the rest of the glyph.

    The first square is drawn under a scaling transform; the second is drawn
    after it is popped and must land where it was asked to, not where the
    leaked transform would put it.
    """
    charproc = stream_object(
        "",
        b"1000 0 0 0 1000 1000 d1\n"
        b"q\n0.5 0 0 0.5 0 0 cm\n0 0 1000 1000 re f\nQ\n"
        b"500 500 500 500 re f\n",
    )
    image = region_image(render_page(_type3_pdf(charproc)), GLYPH_BOX)

    quadrants = quadrant_coverage(image)
    # scaled square covers the lower-left quarter, the second one the upper right
    assert quadrants["bottom_left"] > 0.9, (
        "the transformed part of the charproc did not paint"
    )
    assert quadrants["top_right"] > 0.9, (
        "the part after Q did not paint where it was asked to; the charproc's "
        "graphics state leaked"
    )
    assert quadrants["top_left"] < 0.1 and quadrants["bottom_right"] < 0.1, (
        "the charproc painted outside both squares"
    )


def test_image_mask_charproc_is_painted():
    """A charproc whose glyph is an inline image mask paints too.

    The mask's set bits are the ink; an all-ones mask fills the glyph box.
    """
    charproc = stream_object(
        "",
        b"1000 0 0 0 1000 1000 d1\n"
        b"q\n1000 0 0 1000 0 0 cm\n"
        b"BI /IM true /W 8 /H 8 /BPC 1 /D [1 0] ID\n"
        + bytes([0xFF] * 8)
        + b"\nEI\nQ\n",
    )
    image = region_image(render_page(_type3_pdf(charproc)), GLYPH_BOX)

    assert coverage_ratio(image) > 0.8, (
        f"an image-mask charproc covered only {coverage_ratio(image):.3f} of "
        "the glyph box"
    )
