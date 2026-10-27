#!/usr/bin/env python
"""A CIDFontType2 draws the glyph its /CIDToGIDMap stream names (9.7.4.2).

Subset TrueType programs embedded under a /Type0 font often carry no `cmap`
(and no `post`) table: the PDF addresses glyphs by CID, and a /CIDToGIDMap
stream turns each CID into a glyph index -- two big-endian bytes per CID. Blend2D
refuses such a face outright (no character mapping), and the FreeType path only
honoured an /Identity map, so every glyph fell through to a system font, or,
with none installed, to a stroked outline box. Annual reports set in subset
Times New Roman came out as rows of blue rectangles.

The fixture maps CID 1 to GID 2, a large solid rectangle, while GID 1 is a
small square: an identity reading, a system substitute for the ToUnicode 'A',
and an outline box all draw something else.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import flatten_on_white

EM = 1000
FONT_SIZE = 100.0
TEXT_X = 20.0
TEXT_Y = 60.0
MEASURE_SCALE = 4.0

# GID 2: the glyph CID 1 must reach. 0.8 x 0.7 em of solid ink.
BIG = (100, 0, 900, 700)
# GID 1: what an identity CID->GID reading would draw instead.
SMALL = (100, 0, 200, 100)


def _rect_glyph(box):
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    x0, y0, x1, y1 = box
    pen = TTGlyphPen(None)
    pen.moveTo((x0, y0))
    pen.lineTo((x0, y1))
    pen.lineTo((x1, y1))
    pen.lineTo((x1, y0))
    pen.closePath()
    return pen.glyph()


def _cmapless_font() -> bytes:
    """A TrueType with only the tables a CID subset keeps: no cmap, no post."""
    pytest.importorskip("fontTools", reason="building the test font needs fontTools")

    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    builder = FontBuilder(EM, isTTF=True)
    builder.setupGlyphOrder([".notdef", "small", "big"])
    builder.setupGlyf(
        {
            ".notdef": TTGlyphPen(None).glyph(),
            "small": _rect_glyph(SMALL),
            "big": _rect_glyph(BIG),
        }
    )
    builder.setupHorizontalMetrics(
        {".notdef": (EM, 0), "small": (EM, SMALL[0]), "big": (EM, BIG[0])}
    )
    builder.setupHorizontalHeader(ascent=800, descent=-200)

    out = BytesIO()
    builder.save(out)

    from fontTools.ttLib import TTFont

    font = TTFont(BytesIO(out.getvalue()))
    assert "cmap" not in font and "post" not in font

    return out.getvalue()


def _page() -> bytes:
    program = _cmapless_font()

    content = f"BT /F1 {FONT_SIZE} Tf {TEXT_X} {TEXT_Y} Td <0001> Tj ET\n"

    # CID 0 -> GID 0, CID 1 -> GID 2, CID 2 -> GID 1
    cid_to_gid = bytes([0, 0, 0, 2, 0, 1])

    to_unicode = (
        b"/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
        b"/CMapName /Adobe-Identity-UCS def /CMapType 2 def\n"
        b"1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
        b"1 beginbfchar <0001> <0041> endbfchar\n"
        b"endcmap CMapName currentdict /CMap defineresource pop end end\n"
    )

    objects: list[str | bytes] = [
        # 5: the Type0 font
        "<< /Type /Font /Subtype /Type0 /BaseFont /AAAAAA+DoclingCidMapped "
        "/Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 10 0 R >>",
        # 6: its CIDFontType2 descendant
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /AAAAAA+DoclingCidMapped "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 7 0 R /DW 1000 /CIDToGIDMap 9 0 R >>",
        # 7
        "<< /Type /FontDescriptor /FontName /AAAAAA+DoclingCidMapped /Flags 4 "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        "/CapHeight 700 /StemV 80 /FontFile2 8 0 R >>",
        # 8
        stream_object(f"/Length1 {len(program)}", program),
        # 9
        stream_object("", cid_to_gid),
        # 10
        stream_object("", to_unicode),
    ]

    return simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        extra_objects=objects,
    )


def test_cid_to_gid_map_stream_selects_the_glyph():
    """CID 1 draws GID 2 from the embedded program, not a box or a substitute."""
    result = render_page(_page(), scale=MEASURE_SCALE)

    image = flatten_on_white(result.get_image(scale=MEASURE_SCALE)).convert("L")
    mask = image.point(lambda value: 255 if value < 128 else 0)

    box = mask.getbbox()
    assert box is not None, "the page came out blank"

    width = (box[2] - box[0]) / MEASURE_SCALE
    height = (box[3] - box[1]) / MEASURE_SCALE
    assert width == pytest.approx((BIG[2] - BIG[0]) / EM * FONT_SIZE, abs=4.0)
    assert height == pytest.approx((BIG[3] - BIG[1]) / EM * FONT_SIZE, abs=4.0)

    # A stroked outline box spans a similar area but is almost all white.
    inked = mask.crop(box).histogram()[255]
    area = (box[2] - box[0]) * (box[3] - box[1])
    assert inked / area > 0.9, "the glyph cell was stroked, not filled"
