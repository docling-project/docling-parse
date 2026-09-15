#!/usr/bin/env python
"""ASCII folding of typographic quotes in the cell text sanitizer.

`text_constants::replacements` (src/parse/pdf_sanitators/constants.h) folds the
U+2018..U+201F quote block to ASCII before any text leaves the parser, and the
exported `orig` field is a copy of the folded text, so the fold is the only
form a consumer ever sees. The single quotes rightly become `'`; the double
quotes U+201C, U+201D, U+201E, U+201F must become `"`, not `'`: folding them
to `'` turns `24<U+201D> x 36<U+201D>` (inches) into `24' x 36'` (feet)
(docling-parse#335, docling-project/docling#1433).

The PDF is built in memory with core-14 Helvetica and /WinAnsiEncoding and no
embedded font program, so the glyph decode is not in question: 0x91..0x94 are
quoteleft, quoteright, quotedblleft, quotedblright and 0x84 is quotedblbase in
WinAnsi (PDF 32000-1, Annex D.2).
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


def test_double_quotes_fold_to_double_quote():
    # 24<U+201D> x 36<U+201D>  (quotedblright, WinAnsi 0x94)
    assert _extract(r"24\224 x 36\224") == ['24" x 36"']


def test_single_quotes_fold_to_apostrophe():
    # it<U+2019>s  (quoteright, 0x92) and a literal ASCII apostrophe stay `'`
    assert _extract(r"it\222s, 10' poles") == ["it's, 10' poles"]


def test_quote_family_folds_by_shape():
    # U+2018 a U+2019, U+201C b U+201D, U+201E c U+201C  ->  'a' "b" "c"
    assert _extract(r"\221a\222 \223b\224 \204c\223") == ['\'a\' "b" "c"']


def test_orig_matches_folded_text():
    pdf = simple_page_pdf(
        r"BT /F1 18 Tf 10 100 Td (36\224) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 400 200]",
        extra_objects=[HELVETICA_WINANSI],
    )
    page = parse_page(pdf)
    (cell,) = page.textline_cells
    assert cell.text == '36"'
    assert cell.orig == cell.text


# U+201B and U+201F have no WinAnsi code, so the full block goes through a
# non-embedded Identity-H font whose /ToUnicode maps code 0x0001..0x0008 to
# U+2018..U+201F in order.
TO_UNICODE_QUOTES = """/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CMapName /Test-Quotes def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
1 beginbfrange
<0001> <0008> <2018>
endbfrange
endcmap
CMapName currentdict /CMap defineresource pop
end
end"""


def test_whole_quote_block_via_tounicode():
    content = "BT /F1 20 Tf 20 100 Td <00010002000300040005000600070008> Tj ET"
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream(content),
        "<< /Type /Font /Subtype /Type0 /BaseFont /Test-Quotes "
        "/Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test-Quotes "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 8 0 R >>",
        stream_object("", TO_UNICODE_QUOTES.encode("latin-1")),
        "<< /Type /FontDescriptor /FontName /Test-Quotes /Flags 4 "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        "/CapHeight 700 /StemV 80 >>",
    ]
    page = parse_page(build_pdf(objects))
    text = "".join(cell.text for cell in page.textline_cells)
    # U+2018 U+2019 U+201A U+201B U+201C U+201D U+201E U+201F
    assert text == "'" + "'" + "," + "'" + '"' + '"' + '"' + '"'
