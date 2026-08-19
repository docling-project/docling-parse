#!/usr/bin/env python
"""Non-embedded CJK fonts: reading the codes, and finding a face to draw them.

A Type0 font with a predefined CMap and no font program (`/Encoding
/GBK-EUC-H`, a /CIDFontType2 descendant with no /FontFile2) says nothing about
glyphs at all. The codes are GBK byte pairs, the CMap turns them into Adobe-GB1
CIDs, the CID collection turns those into Unicode, and a face installed on the
host has to supply the shapes.

The last step is where a page of simplified Chinese fell apart. A face was kept
per script as soon as it shaped *anything* of that script, and the host's
Korean faces shape most of the Han characters a Chinese page uses -- but not
这, 两, 个, 学, 研 or 确, which are simplified-only. Every one of those landed on
the page as a .notdef box while the characters around them came out fine.
"""

from __future__ import annotations

from io import BytesIO

import pytest
from PIL import ImageChops, ImageStat

from tests.pdf_builder import render_page, simple_page_pdf
from tests.rendering_regression import coverage_ratio, region_image

# 中 is in every CJK face, including the Japanese and Korean ones; the other
# three are simplified-only, and are exactly what the Korean faces on a stock
# macOS install are missing.
SAMPLE = "中这两个"

FONT_SIZE = 24.0
ORIGIN_X = 20.0
BASELINE = 100.0
PAGE = 200.0

# /DW 1000 at 24pt: one character per 24 points.
ADVANCE = FONT_SIZE


def _char_box(index: int) -> tuple[float, float, float, float]:
    """The page area one character occupies, in top-left coordinates."""
    left = ORIGIN_X + index * ADVANCE
    # ascent 859 and descent -141 of the descriptor below, at 24pt
    top = PAGE - (BASELINE + 0.859 * FONT_SIZE)
    bottom = PAGE - (BASELINE - 0.141 * FONT_SIZE)
    return (left + 1.0, top, left + ADVANCE - 1.0, bottom)


def _gbk_page(text: str) -> bytes:
    """A page drawing `text` through a non-embedded /GBK-EUC-H Type0 font."""
    # The content stream carries the raw GBK bytes; pdf_builder encodes the
    # content as latin-1, so they survive byte for byte.
    codes = text.encode("gbk").decode("latin-1")

    content = f"BT /F1 {FONT_SIZE} Tf {ORIGIN_X} {BASELINE} Td ({codes}) Tj ET\n"

    type0 = (
        "<< /Type /Font /Subtype /Type0 /BaseFont /SimSun "
        "/Encoding /GBK-EUC-H /DescendantFonts [6 0 R] >>"
    )
    cid_font = (
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /SimSun "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 2 >> "
        "/FontDescriptor 7 0 R /DW 1000 >>"
    )
    # No /FontFile2: the shapes have to come from a face on the host.
    descriptor = (
        "<< /Type /FontDescriptor /FontName /SimSun /Flags 4 "
        "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 859 /Descent -141 "
        "/CapHeight 674 /StemV 91 >>"
    )

    return simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        extra_objects=[type0, cid_font, descriptor],
    )


def _extract_text(text: str) -> str:
    from docling_parse.pdf_parser import DoclingPdfParser

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(_gbk_page(text)))
    _, page = next(doc.iterate_pages())

    return "".join(cell.text for cell in page.textline_cells)


def _images_differ(first, second) -> float:
    """Mean absolute difference between two crops, 0 when identical."""
    diff = ImageChops.difference(first.convert("L"), second.convert("L"))
    return ImageStat.Stat(diff).mean[0]


def test_gbk_euc_codes_decode_to_their_characters():
    """The predefined CMap chain reads the byte pairs correctly.

    /GBK-EUC-H maps the code to an Adobe-GB1 CID and the CID collection maps
    that to Unicode; neither is in the file, both ship with the parser.
    """
    assert _extract_text(SAMPLE).replace(" ", "") == SAMPLE


def test_a_cid_with_several_code_points_extracts_the_ideograph():
    """CID 3821 is listed as `2f46,65e0`: a Kangxi radical and 无.

    The two draw alike, so taking the first went unnoticed on the page while
    the extracted text carried U+2F46 KANGXI RADICAL WITHOUT -- a character
    that no search for 无 matches.
    """
    text = _extract_text("无一").replace(" ", "")

    assert text == "无一", (
        f"extracted {[hex(ord(c)) for c in text]}; U+2f46 or U+2f00 here means "
        "the compatibility radical was preferred over the ideograph"
    )


def test_every_character_of_a_cjk_run_gets_a_real_glyph():
    """No character of the run may fall back to a .notdef box.

    A face that draws most of a run and boxes the rest used to be accepted,
    because the check asked whether the face shaped *anything*. The four
    characters here are distinct, so distinct shapes have to reach the page:
    boxes are all the same rectangle, and identical crops are what that
    failure looks like.
    """
    result = render_page(_gbk_page(SAMPLE))
    cells = [region_image(result, _char_box(i)) for i in range(len(SAMPLE))]

    for index, cell in enumerate(cells):
        assert coverage_ratio(cell) > 0.01, f"{SAMPLE[index]} drew nothing at all"

    # Tolerance: two renderings of the same glyph differ only by antialiasing.
    distinct = [
        (i, j)
        for i in range(len(cells))
        for j in range(i + 1, len(cells))
        if _images_differ(cells[i], cells[j]) > 2.0
    ]

    if not distinct:
        pytest.skip("no CJK-capable font on this host: every character is a box")

    missing = [
        (SAMPLE[i], SAMPLE[j])
        for i in range(len(cells))
        for j in range(i + 1, len(cells))
        if (i, j) not in distinct
    ]

    assert not missing, (
        "these characters rendered as the same shape, i.e. as .notdef boxes, "
        f"while others on the same run rendered properly: {missing}"
    )
