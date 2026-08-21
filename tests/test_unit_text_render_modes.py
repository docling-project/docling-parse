#!/usr/bin/env python
"""Text render mode and anisotropic text scaling (PDF 32000-1, 9.3.4 and 9.3.6).

`Tr` selects whether a glyph run is filled, stroked, both, or neither. Only the
fill was drawn, so mode 3 leaked invisible text onto the page and the
fill-plus-stroke modes -- how a producer without a bold face makes text bold --
rendered as plain weight.

`Tz` scales text horizontally only. Drawing such a run at a single isotropic
size makes the glyphs square: condensed headline text came out overflowing its
column.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf
from tests.rendering_regression import coverage_ratio, ink_bounds, region_image

TEXT_BOX = (10.0, 60.0, 190.0, 120.0)
HELVETICA = "/Font << /F1 5 0 R >>"
FONT_OBJECT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


def _text_pdf(setup: str, *, text: str = "HHHHH", size: int = 36) -> bytes:
    content = f"BT\n{setup}\n/F1 {size} Tf\n20 100 Td\n({text}) Tj\nET\n"
    return simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT])


def _ink(setup: str, **kwargs) -> float:
    image = region_image(render_page(_text_pdf(setup, **kwargs)), TEXT_BOX)
    return coverage_ratio(image)


def test_filled_text_paints():
    """The baseline: mode 0 text is on the page at all."""
    assert _ink("0 Tr") > 0.01, "plain filled text painted nothing"


def test_invisible_text_render_mode_paints_nothing():
    """Mode 3 is invisible -- it is how scanned pages carry their OCR layer."""
    assert _ink("3 Tr") < 0.002, (
        "text in render mode 3 was painted; it must stay invisible"
    )


def test_stroking_render_mode_is_heavier_than_fill():
    """Fill-plus-stroke puts down more ink than fill alone.

    This is how a document without a bold face asks for bold, so losing the
    stroke silently drops the emphasis from every such heading.
    """
    filled = _ink("0 Tr")
    stroked = _ink("2 Tr 1.5 w")

    assert stroked > filled * 1.1, (
        f"fill-plus-stroke ({stroked:.4f}) is not heavier than fill alone "
        f"({filled:.4f}); the stroke component was dropped"
    )


def test_stroke_only_render_mode_paints():
    """Mode 1 strokes the outline without filling it."""
    assert _ink("1 Tr 1.5 w") > 0.005, "outlined text painted nothing"


def test_horizontal_scaling_narrows_the_run():
    """`Tz` scales the run horizontally and leaves its height alone.

    Rendering it isotropically instead keeps the width and inflates the
    glyphs, so the run's height is what separates the two.
    """
    normal = ink_bounds(region_image(render_page(_text_pdf("100 Tz")), TEXT_BOX))
    condensed = ink_bounds(region_image(render_page(_text_pdf("50 Tz")), TEXT_BOX))

    assert normal is not None and condensed is not None, "the text did not paint"

    normal_width = normal[2] - normal[0]
    condensed_width = condensed[2] - condensed[0]
    normal_height = normal[3] - normal[1]
    condensed_height = condensed[3] - condensed[1]

    assert condensed_width == pytest.approx(normal_width / 2, rel=0.25), (
        f"50% Tz gave a run {condensed_width}px wide against {normal_width}px "
        "unscaled; it should be about half"
    )
    assert condensed_height == pytest.approx(normal_height, abs=3), (
        f"50% Tz changed the run's height ({condensed_height}px against "
        f"{normal_height}px); horizontal scaling must not touch it"
    )
