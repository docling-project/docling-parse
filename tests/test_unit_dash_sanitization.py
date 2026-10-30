#!/usr/bin/env python
"""`do_char_normalization` controls the typographic dashes in cell text.

`text_constants::dash_replacements` (src/parse/pdf_sanitators/constants.h) folds
U+2012..U+2015 and U+2212 to `-`, and `orig` is a copy of the folded text. By
default the fold runs, so existing callers see the same text. With
`do_char_normalization=False` the characters that the document encodes stay, so
an en dash in `pages 3<U+2013>5` or a minus sign in `<U+2212>3` survives
(docling-parse#362). The hyphen variants U+2010 and U+2011 fold either way,
because the line-joining dehyphenation in docling recognises an ASCII hyphen
alone.

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


def _winansi_page(text: str, *, do_char_normalization: bool = True):
    pdf = simple_page_pdf(
        f"BT /F1 18 Tf 10 100 Td ({text}) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 400 200]",
        extra_objects=[HELVETICA_WINANSI],
    )
    return parse_page(pdf, do_char_normalization=do_char_normalization)


# Nutshell <U+2014> pages 3<U+2013>5  (emdash 0x97, endash 0x96)
EN_AND_EM_DASH = r"Nutshell \227 pages 3\2265"


def test_en_and_em_dash_fold_by_default():
    page = _winansi_page(EN_AND_EM_DASH)
    assert [cell.text for cell in page.textline_cells] == ["Nutshell - pages 3-5"]


def test_en_and_em_dash_are_kept_without_normalization():
    page = _winansi_page(EN_AND_EM_DASH, do_char_normalization=False)
    assert [cell.text for cell in page.textline_cells] == [
        "Nutshell \u2014 pages 3\u20135"
    ]


def test_ascii_hyphen_is_unchanged():
    for do_char_normalization in (True, False):
        page = _winansi_page(
            "well-known, 3-5", do_char_normalization=do_char_normalization
        )
        assert [cell.text for cell in page.textline_cells] == ["well-known, 3-5"]


def test_orig_matches_text_without_normalization():
    page = _winansi_page(r"3\2265", do_char_normalization=False)
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


def _extract_via_tounicode(codes: str, *, do_char_normalization: bool = True) -> str:
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
    page = parse_page(build_pdf(objects), do_char_normalization=do_char_normalization)
    return "".join(cell.text for cell in page.textline_cells)


def test_hyphen_variants_fold_to_ascii_hyphen():
    # U+2010 hyphen, U+2011 non-breaking hyphen
    assert _extract_via_tounicode("00010002") == "--"
    assert _extract_via_tounicode("00010002", do_char_normalization=False) == "--"


# U+2012 figure dash, U+2013 en dash, U+2014 em dash, U+2015 horizontal bar,
# U+2212 minus sign
DASH_CODES = "00030004000500060007"


def test_dashes_and_minus_fold_by_default():
    assert _extract_via_tounicode(DASH_CODES) == "-----"


def test_dashes_and_minus_are_kept_without_normalization():
    assert (
        _extract_via_tounicode(DASH_CODES, do_char_normalization=False)
        == "\u2012\u2013\u2014\u2015\u2212"
    )
