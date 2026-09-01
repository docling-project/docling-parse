#!/usr/bin/env python
"""Composite-font encoding and the text it yields (PDF 32000-1, 9.7).

A Type0 font addresses glyphs by two-byte codes. Splitting the string a byte at
a time doubles the character count and turns the page into mojibake, and a
subset font that carries its /ToUnicode on a sibling in the same resource
dictionary leaves the codes unmapped unless the mapping is shared.

Two more failures showed up as text rather than as glyphs: an unmapped code
used to be written out as its internal `glyph[.notdef]` marker, which put
renderer diagnostics into extracted text; and a CID font without /DW was given
a default advance of 500 instead of the 1000 the spec states, so every cell of
such a font came out half as wide as it is.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import build_pdf, content_stream, stream_object

FONT_SIZE = 20

TO_UNICODE_AB = """/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CMapName /Test-UCS2 def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
2 beginbfchar
<0001> <0041>
<0002> <0042>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end
end"""


def _identity_h_pdf(
    *,
    text_bytes: str,
    to_unicode: bool,
    default_width: str = "",
) -> bytes:
    """A page setting two-byte codes in a non-embedded Identity-H font."""
    content = f"BT\n/F1 {FONT_SIZE} Tf\n20 100 Td\n<{text_bytes}> Tj\nET\n"
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream(content),
        "<< /Type /Font /Subtype /Type0 /BaseFont /Test-Identity "
        "/Encoding /Identity-H /DescendantFonts [6 0 R]"
        + (" /ToUnicode 7 0 R" if to_unicode else "")
        + " >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test-Identity "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        f"/FontDescriptor 8 0 R{default_width} >>",
        stream_object("", TO_UNICODE_AB.encode("latin-1")),
        "<< /Type /FontDescriptor /FontName /Test-Identity /Flags 4 "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        "/CapHeight 700 /StemV 80 >>",
    ]
    return build_pdf(objects)


def _page(pdf: bytes):
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        path_or_stream=BytesIO(pdf),
        decode_config=DecodeConfig(do_sanitization=True, keep_glyphs=True),
    )
    _, page = next(doc.iterate_pages())
    return page


def _text(pdf: bytes) -> str:
    return "".join(cell.text for cell in _page(pdf).textline_cells)


def test_two_byte_codes_are_not_split():
    """Two codes produce two characters, not four.

    Reading an Identity-H string one byte at a time yields twice as many
    characters, each mapped from half a code -- the page still has text on it,
    and all of it is wrong.
    """
    text = _text(_identity_h_pdf(text_bytes="00010002", to_unicode=True))
    assert text == "AB", f"expected the two mapped codes to give 'AB', got {text!r}"


def test_unmapped_codes_do_not_emit_the_notdef_marker():
    """An unmappable code contributes no diagnostic text.

    `glyph[.notdef]` is the renderer's internal name for a missing glyph.
    Emitting it as extracted text put that string into documents.
    """
    text = _text(_identity_h_pdf(text_bytes="00090009", to_unicode=False))
    assert "notdef" not in text, (
        f"the .notdef marker leaked into the extracted text: {text!r}"
    )
    assert "glyph[" not in text, (
        f"an internal glyph name leaked into the extracted text: {text!r}"
    )


def test_cid_font_without_dw_advances_by_1000():
    """A CID font with no /DW advances a full em per glyph.

    The spec's default is 1000; using 500 halves every advance, so the cells
    of such a font come out overlapping and half as wide as the glyphs drawn.
    """
    page = _page(_identity_h_pdf(text_bytes="00010002", to_unicode=True))
    cells = list(page.textline_cells)
    assert cells, "the run produced no text cell"

    box = cells[0].rect.to_bounding_box()
    width = abs(box.r - box.l)

    # two glyphs, one em each, at FONT_SIZE
    assert width == pytest.approx(2 * FONT_SIZE, rel=0.2), (
        f"two glyphs of a /DW-less CID font measured {width:.1f}pt; a full-em "
        f"default gives about {2 * FONT_SIZE}pt, a 500 default about half that"
    )


def test_explicit_dw_is_honoured():
    """An explicit /DW still wins over the default."""
    page = _page(
        _identity_h_pdf(
            text_bytes="00010002", to_unicode=True, default_width=" /DW 500"
        )
    )
    cells = list(page.textline_cells)
    assert cells, "the run produced no text cell"

    box = cells[0].rect.to_bounding_box()
    width = abs(box.r - box.l)
    assert width == pytest.approx(FONT_SIZE, rel=0.2), (
        f"two glyphs at /DW 500 measured {width:.1f}pt, expected about {FONT_SIZE}pt"
    )
