#!/usr/bin/env python
"""Typographic dashes survive the cell text sanitizer.

`text_constants::replacements` (src/parse/pdf_sanitators/constants.h) used to
fold the whole U+2010..U+2015 block and U+2212 to `-`. The fold runs
unconditionally and `orig` is a copy of the folded text, so nothing downstream
could recover an en dash in `pages 3<U+2013>5` or `2024<U+2013>2026`, or a
minus sign in `<U+2212>3` (docling-parse#362). Only the hyphen variants U+2010
and U+2011 keep folding, because the line-joining dehyphenation in docling
recognises an ASCII hyphen alone.

The WinAnsi cases use core-14 Helvetica with /WinAnsiEncoding and no embedded
font program, so the glyph decode is not in question: 0x96 is endash and 0x97
is emdash (PDF 32000-1, Annex D.2). The code points without a WinAnsi code go
through a non-embedded Identity-H font whose /ToUnicode maps them directly.
"""

from tests.pdf_builder import (
    build_pdf,
    content_stream,
    parse_page,
    simple_page_pdf,
    stream_object,
)

HELVETICA_WINANSI = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


def _extract(text: str) -> list[str]:
    pdf = simple_page_pdf(
        f"BT /F1 18 Tf 10 100 Td ({text}) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 400 200]",
        extra_objects=[HELVETICA_WINANSI],
    )
    page = parse_page(pdf)
    return [cell.text for cell in page.textline_cells]


def test_en_and_em_dash_are_kept():
    # Nutshell <U+2014> pages 3<U+2013>5  (emdash 0x97, endash 0x96)
    assert _extract(r"Nutshell \227 pages 3\2265") == ["Nutshell \u2014 pages 3\u20135"]


def test_ascii_hyphen_is_unchanged():
    assert _extract("well-known, 3-5") == ["well-known, 3-5"]


def test_orig_matches_text():
    pdf = simple_page_pdf(
        r"BT /F1 18 Tf 10 100 Td (3\2265) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 400 200]",
        extra_objects=[HELVETICA_WINANSI],
    )
    page = parse_page(pdf)
    (cell,) = page.textline_cells
    assert cell.text == "3\u20135"
    assert cell.orig == cell.text


# U+2010..U+2015 and U+2212 through a /ToUnicode cmap: codes 0x0001..0x0006
# map to U+2010..U+2015 in order, and 0x0007 to U+2212.
TO_UNICODE_DASHES = """/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CMapName /Test-Dashes def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
1 beginbfrange
<0001> <0006> <2010>
endbfrange
1 beginbfchar
<0007> <2212>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end
end"""


def _extract_via_tounicode(codes: str) -> str:
    content = f"BT /F1 20 Tf 20 100 Td <{codes}> Tj ET"
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream(content),
        "<< /Type /Font /Subtype /Type0 /BaseFont /Test-Dashes "
        "/Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test-Dashes "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 8 0 R >>",
        stream_object("", TO_UNICODE_DASHES.encode("latin-1")),
        "<< /Type /FontDescriptor /FontName /Test-Dashes /Flags 4 "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        "/CapHeight 700 /StemV 80 >>",
    ]
    page = parse_page(build_pdf(objects))
    return "".join(cell.text for cell in page.textline_cells)


def test_hyphen_variants_fold_to_ascii_hyphen():
    # U+2010 hyphen, U+2011 non-breaking hyphen
    assert _extract_via_tounicode("00010002") == "--"


def test_dashes_and_minus_are_kept():
    # U+2012 figure dash, U+2013 en dash, U+2014 em dash, U+2015 horizontal
    # bar, U+2212 minus sign
    assert (
        _extract_via_tounicode("00030004000500060007")
        == "\u2012\u2013\u2014\u2015\u2212"
    )
