#!/usr/bin/env python
# SPDX-License-Identifier: MIT
"""A letter and its combining mark in one /ActualText span stay together.

Chrome writes an Arabic or Persian letter that has a combining mark (a kasra,
the hamza of the ezafe) as one /ActualText span that paints the zero-width
mark first and the letter second. The characters of the span must not be
given to the glyphs in paint order, or the letter gets the zero width of the
mark and is cut out of its word.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf

# code -> (unicode, width in 1/1000 em)
GLYPHS = {
    "k": ("0650", 0),  # kasra, a zero-width mark
    "H": ("0654", 0),  # hamza above, a zero-width mark
    "g": ("06AF", 900),  # gaf
    "h": ("0647", 500),  # heh
    "r": ("0631", 400),  # reh
    "z": ("0632", 400),  # zain
    "b": ("0628", 900),  # beh
    "n": ("0646", 500),  # noon
    "A": ("0627", 300),  # alef
    "x": ("062E", 700),  # khah
}


def _font_objects() -> list:
    codes = sorted(GLYPHS, key=ord)
    first, last = ord(codes[0]), ord(codes[-1])
    widths = " ".join(
        str(GLYPHS[chr(c)][1]) if chr(c) in GLYPHS else "0"
        for c in range(first, last + 1)
    )
    bfchars = "\n".join(f"<{ord(c):02X}> <{GLYPHS[c][0]}>" for c in codes)
    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Test-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
        f"{len(codes)} beginbfchar\n{bfchars}\nendbfchar\n"
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend"
    )
    return [
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        f"/FirstChar {first} /LastChar {last} /Widths [{widths}] "
        "/ToUnicode 6 0 R >>",
        content_stream(cmap),
    ]


def _text(codes: str) -> str:
    return "".join(chr(int(GLYPHS[code][0], 16)) for code in codes)


def _utf16(codes: str) -> str:
    return "FEFF" + "".join(GLYPHS[code][0] for code in codes)


def _parse(content: str):
    pdf = simple_page_pdf(
        f"BT /F1 20 Tf 40 50 Td {content} ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 100]",
        extra_objects=_font_objects(),
    )
    doc = DoclingPdfParser(loglevel="fatal").load(
        BytesIO(pdf), decode_config=DecodeConfig(keep_glyphs=True)
    )
    return doc.get_page(1)


def _span(actual: str, painted: str) -> str:
    glyphs = " ".join(f"({code}) Tj" for code in painted)
    return f"/Span << /ActualText <{_utf16(actual)}> >> BDC {glyphs} EMC"


# The span (its /ActualText and the codes it paints, mark first), the codes
# painted after it in visual order, and the expected word in logical order.
@pytest.mark.parametrize(
    "actual, painted, after, word",
    [
        # «بزرگِ»: gaf with a kasra
        ("gk", "kg", "rzb", "bzrgk"),
        # «خانهٔ»: heh with the hamza of the ezafe
        ("hH", "Hh", "nAx", "xAnhH"),
    ],
)
def test_mark_painted_first_stays_in_its_word(actual, painted, after, word):
    rest = " ".join(f"({code}) Tj" for code in after)
    page = _parse(f"{_span(actual, painted)} {rest}")
    assert [cell.text for cell in page.word_cells] == [_text(word)]
    assert [cell.text for cell in page.textline_cells] == [_text(word)]


def test_span_without_mark_keeps_glyph_geometry():
    # the /ActualText has one character per glyph and no mark: each character
    # keeps the box of its own glyph
    page = _parse(f"{_span('br', 'br')} (z) Tj")
    boxes = {cell.text: cell.rect.to_bounding_box() for cell in page.char_cells}
    assert set(boxes) == {_text("r"), _text("b"), _text("z")}
    assert boxes[_text("b")].r <= boxes[_text("r")].l + 1e-6
    assert boxes[_text("b")].r - boxes[_text("b")].l == pytest.approx(18.0)
    assert boxes[_text("r")].r - boxes[_text("r")].l == pytest.approx(8.0)
