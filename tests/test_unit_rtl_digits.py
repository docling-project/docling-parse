#!/usr/bin/env python
# SPDX-License-Identifier: MIT
"""Numbers inside right-to-left text keep their left-to-right digit order.

Arabic and Persian text is painted in visual order, so the glyphs of a
right-to-left word are joined in reverse. The digits of a number are painted
left to right like any number, and must not be reversed with the letters.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf

# code -> (unicode, width in 1/1000 em)
GLYPHS = {
    "0": ("06F0", 500),  # extended Arabic-Indic (Persian) digits
    "1": ("06F1", 500),
    "2": ("06F2", 500),
    "3": ("06F3", 500),
    "4": ("06F4", 500),
    "5": ("06F5", 500),
    "6": ("06F6", 500),
    "7": ("06F7", 500),
    "8": ("06F8", 500),
    "9": ("06F9", 500),
    "a": ("0660", 500),  # Arabic-Indic digit zero
    "b": ("0661", 500),  # Arabic-Indic digit one
    "c": ("0662", 500),  # Arabic-Indic digit two
    "d": ("0668", 500),  # Arabic-Indic digit eight
    "k": ("066C", 250),  # Arabic thousands separator
    ",": ("060C", 250),  # Arabic comma
    "s": ("0633", 600),  # seen
    "A": ("0627", 300),  # alef
    "l": ("0644", 400),  # lam
    "[": ("005B", 300),
    "]": ("005D", 300),
    " ": ("0020", 280),
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


def _parse_visual_order(visual: str):
    """Paints the codes leftmost first, advancing with the natural widths."""
    pdf = simple_page_pdf(
        f"BT /F1 12 Tf 40 50 Td ({visual}) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 100]",
        extra_objects=_font_objects(),
    )
    doc = DoclingPdfParser(loglevel="fatal").load(
        BytesIO(pdf), decode_config=DecodeConfig(keep_glyphs=True)
    )
    return doc.get_page(1)


def _text(codes: str) -> str:
    return "".join(chr(int(GLYPHS[code][0], 16)) for code in codes)


# The painted codes in visual order, then the expected words and line, in
# logical order.
@pytest.mark.parametrize(
    "visual, words, line",
    [
        # "year 1401": the year stays left to right after the word
        ("1401 lAs", ["1401", "sAl"], "sAl 1401"),
        # Persian digits with thousands separators
        ("9k039k000", ["9k039k000"], "9k039k000"),
        # the Arabic comma makes the word right to left
        (",1165", ["1165,"], "1165,"),
        # Arabic-Indic digits
        ("cabd", ["cabd"], "cabd"),
        # footnote references of one digit each are unchanged
        ("]7[]6[", ["[6][7]"], "[6][7]"),
    ],
)
def test_digits_in_rtl_text_keep_left_to_right_order(visual, words, line):
    page = _parse_visual_order(visual)
    assert [cell.text for cell in page.word_cells] == [_text(w) for w in words]
    assert [cell.text for cell in page.textline_cells] == [_text(line)]
