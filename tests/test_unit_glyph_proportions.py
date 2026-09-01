#!/usr/bin/env python
"""A glyph is drawn as its font program designed it (PDF 32000-1, 9.4.4).

The /Widths of a font dictionary say how far the pen moves, not how wide the
glyph is. The two normally agree, but nothing requires it: the CID fonts in
`rotated_page_02.pdf` declare /DW 1000 over a CFF whose glyphs advance 504, and
that is a conforming file.

The renderer draws a run at one size, taken from the cell's height edge, so the
horizontal factor has to come from somewhere. Taking it from the ratio between
the cell and the face's own advances made every glyph of that file twice as
wide as it should be, overlapping its neighbours. The only thing that scales a
glyph horizontally is the text state: the horizontal scaling Th of `Tz`, and
any anisotropy of the text matrix and CTM.

A substituted face is a different matter -- its advances have nothing to do
with the PDF's, and fitting the run to the cell is what keeps the layout of a
page whose font is missing, so that path still measures.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import flatten_on_white

# The test glyph is a rectangle 400 units wide in a 1000-unit em, so its ink is
# 0.4 em: unmistakable next to any fitting the renderer might do.
EM = 1000
INK_LEFT = 50
INK_RIGHT = 450
INK_TOP = 700
INK_WIDTH_EM = (INK_RIGHT - INK_LEFT) / EM

FONT_SIZE = 100.0
TEXT_X = 20.0
TEXT_Y = 60.0

MEASURE_SCALE = 4.0


def _rectangle_font(advance: int) -> bytes:
    """A one-glyph TrueType whose 'A' is a rectangle advancing `advance`."""
    pytest.importorskip("fontTools", reason="building the test font needs fontTools")

    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    builder = FontBuilder(EM, isTTF=True)
    builder.setupGlyphOrder([".notdef", "A"])
    builder.setupCharacterMap({0x41: "A"})

    pen = TTGlyphPen(None)
    pen.moveTo((INK_LEFT, 0))
    pen.lineTo((INK_RIGHT, 0))
    pen.lineTo((INK_RIGHT, INK_TOP))
    pen.lineTo((INK_LEFT, INK_TOP))
    pen.closePath()

    builder.setupGlyf({".notdef": TTGlyphPen(None).glyph(), "A": pen.glyph()})
    builder.setupHorizontalMetrics({".notdef": (advance, 0), "A": (advance, INK_LEFT)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable(
        {
            "familyName": "DoclingRectangle",
            "styleName": "Regular",
            "uniqueFontIdentifier": "DoclingRectangle-Regular",
            "fullName": "DoclingRectangle Regular",
            "psName": "DoclingRectangle-Regular",
            "version": "1.0",
        }
    )
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200)
    builder.setupPost()

    out = BytesIO()
    builder.save(out)

    return out.getvalue()


def _page(*, declared_width: int, font_advance: int, tz: int | None = None) -> bytes:
    """One 'A' drawn at 100pt, with /Widths and the font's advance set apart."""
    program = _rectangle_font(font_advance)

    scaling = f"{tz} Tz " if tz is not None else ""
    content = f"BT /F1 {FONT_SIZE} Tf {scaling}{TEXT_X} {TEXT_Y} Td (A) Tj ET\n"

    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /DoclingRectangle "
        f"/FirstChar 65 /LastChar 65 /Widths [{declared_width}] "
        "/Encoding /WinAnsiEncoding /FontDescriptor 6 0 R >>"
    )
    descriptor = (
        "<< /Type /FontDescriptor /FontName /DoclingRectangle /Flags 32 "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        "/CapHeight 700 /StemV 80 /FontFile2 7 0 R >>"
    )
    font_file = stream_object(f"/Length1 {len(program)}", program)

    return simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        extra_objects=[font, descriptor, font_file],
    )


def _ink_width_pt(result) -> float:
    """Width of everything drawn on the page, in points."""
    image = flatten_on_white(result.get_image(scale=MEASURE_SCALE)).convert("L")
    mask = image.point(lambda value: 255 if value < 128 else 0)

    box = mask.getbbox()
    assert box is not None, "the page came out blank"

    return (box[2] - box[0]) / MEASURE_SCALE


def test_glyph_keeps_its_own_width_when_the_pdf_declares_another():
    """/Widths 1000 over a glyph that advances 500 must not widen the glyph.

    This is `rotated_page_02.pdf` in miniature: fitting the run to the cell
    doubled every glyph, so the characters ran into each other.
    """
    result = render_page(_page(declared_width=1000, font_advance=500))

    expected = INK_WIDTH_EM * FONT_SIZE  # 40pt of ink, whatever /Widths says
    assert _ink_width_pt(result) == pytest.approx(expected, abs=4.0), (
        "the glyph was scaled to the width the PDF lays out by; only the pen "
        "follows /Widths"
    )


def test_glyph_keeps_its_width_when_the_two_agree():
    """The same glyph, with /Widths matching the font: unchanged either way."""
    result = render_page(_page(declared_width=500, font_advance=500))

    expected = INK_WIDTH_EM * FONT_SIZE
    assert _ink_width_pt(result) == pytest.approx(expected, abs=4.0)


def test_horizontal_scaling_still_condenses_the_glyph():
    """`Tz 50` halves the glyph itself, not only its displacement (9.4.4).

    Th belongs to the text rendering matrix, so it is the one thing that may
    scale a glyph horizontally -- and it has to survive the fix above.
    """
    result = render_page(_page(declared_width=500, font_advance=500, tz=50))

    expected = 0.5 * INK_WIDTH_EM * FONT_SIZE  # 20pt
    assert _ink_width_pt(result) == pytest.approx(expected, abs=4.0), (
        "Tz no longer condenses the glyph"
    )
