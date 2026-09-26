#!/usr/bin/env python
"""Reading text through the builtin encoding of an embedded font program.

A symbolic simple font without a declared base encoding maps its character
codes through the builtin encoding of its font program (PDF 32000-1, 9.6.6.2
and 9.6.6.4): the Adobe custom encoding of a Type 1 / CFF program, or the
(3,0) symbol cmap of a TrueType program. The standard Latin tables say
nothing about such codes: reading them there turned the Cyrillic of a
subsetted CFF into Latin-1 look-alikes and glyph indices into digits. The
reading text comes from the glyph the program selects, through its
PostScript name (AGL, uniXXXX) or the program's Unicode cmap; a glyph with
neither stays a GLYPH marker, and a blank glyph reads as a space.

The fonts are built in memory with fontTools.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from tests.pdf_builder import parse_page, simple_page_pdf, stream_object

pytest.importorskip("fontTools", reason="building the test fonts needs fontTools")


def _box(pen) -> None:
    pen.moveTo((50, 0))
    pen.lineTo((450, 0))
    pen.lineTo((450, 700))
    pen.lineTo((50, 700))
    pen.closePath()


def _symbol_truetype(*, glyph_names: bool) -> bytes:
    """A TrueType subset with only a (3,0) symbol cmap at 0xF000 + code.

    Code 0x23 draws Ccaron, 0x32 draws two, 0x20 a blank glyph and 0x41 a
    glyph whose name (g4) is a bare index. Without `glyph_names` the post
    table is format 3, so nothing but the cmap describes the glyphs.
    """
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib.tables._c_m_a_p import CmapSubtable, table__c_m_a_p

    order = [".notdef", "Ccaron", "two", "space", "g4"]
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(order)

    cmap = table__c_m_a_p()
    cmap.tableVersion = 0
    subtable = CmapSubtable.newSubtable(4)
    subtable.platformID, subtable.platEncID, subtable.language = 3, 0, 0
    subtable.cmap = {0xF023: "Ccaron", 0xF032: "two", 0xF020: "space", 0xF041: "g4"}
    cmap.tables = [subtable]
    builder.font["cmap"] = cmap

    def glyph(blank: bool = False):
        pen = TTGlyphPen(None)
        if not blank:
            _box(pen)
        return pen.glyph()

    builder.setupGlyf(
        {
            ".notdef": glyph(blank=True),
            "Ccaron": glyph(),
            "two": glyph(),
            "space": glyph(blank=True),
            "g4": glyph(),
        }
    )
    builder.setupHorizontalMetrics(dict.fromkeys(order, (500, 0)))
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable(
        {
            "familyName": "DoclingSymbol",
            "styleName": "Regular",
            "psName": "DoclingSymbol",
        }
    )
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200)
    builder.setupPost()
    if not glyph_names:
        builder.font["post"].formatType = 3.0

    out = BytesIO()
    builder.save(out)
    return out.getvalue()


def _custom_cff() -> bytes:
    """A bare CFF whose builtin encoding puts Cyrillic and Arabic at Latin codes.

    0xE8 draws afii10074 (U+0438), 0x32 draws two and 0x23 draws uni0634.
    """
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.t2CharStringPen import T2CharStringPen

    order = [".notdef", "afii10074", "two", "uni0634"]
    builder = FontBuilder(1000, isTTF=False)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({0x0438: "afii10074", 0x32: "two", 0x0634: "uni0634"})

    charstrings = {}
    for name in order:
        pen = T2CharStringPen(500, None)
        if name != ".notdef":
            _box(pen)
        charstrings[name] = pen.getCharString()

    builder.setupCFF(
        "DoclingCyrillic", {"FullName": "Docling Cyrillic"}, charstrings, {}
    )
    builder.setupHorizontalMetrics(dict.fromkeys(order, (500, 0)))
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable(
        {
            "familyName": "DoclingCyrillic",
            "styleName": "Regular",
            "psName": "DoclingCyrillic",
        }
    )
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200)
    builder.setupPost()

    encoding = [".notdef"] * 256
    encoding[0xE8] = "afii10074"
    encoding[0x32] = "two"
    encoding[0x23] = "uni0634"
    builder.font["CFF "].cff.topDictIndex[0].Encoding = encoding

    out = BytesIO()
    builder.font["CFF "].cff.compile(out, builder.font)
    return out.getvalue()


def _page(
    program: bytes,
    text: str,
    *,
    subtype: str,
    font_file: str,
    base_font: str = "/ABCDEF+DoclingTestFace",
    encoding: str = "",
    flags: int = 4,
) -> bytes:
    """One line of `text` (a PDF string literal body) in the embedded font."""
    widths = " ".join(["500"] * 256)
    font = (
        f"<< /Type /Font /Subtype {subtype} /BaseFont {base_font} "
        f"/FirstChar 0 /LastChar 255 /Widths [{widths}] "
        f"{encoding}/FontDescriptor 6 0 R >>"
    )
    descriptor = (
        f"<< /Type /FontDescriptor /FontName {base_font} /Flags {flags} "
        "/FontBBox [0 -200 1000 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
        f"/CapHeight 700 /StemV 80 {font_file} 7 0 R >>"
    )
    if font_file == "/FontFile2":
        stream = stream_object(f"/Length1 {len(program)}", program)
    else:
        stream = stream_object("/Subtype /Type1C", program)

    return simple_page_pdf(
        f"BT /F1 24 Tf 20 100 Td ({text}) Tj ET\n",
        resources="/Font << /F1 5 0 R >>",
        extra_objects=[font, descriptor, stream],
    )


def _text(pdf_bytes: bytes) -> str:
    page = parse_page(pdf_bytes)
    return "".join(cell.text for cell in page.textline_cells)


def _truetype_page(text: str, *, glyph_names: bool = True, **kwargs) -> bytes:
    return _page(
        _symbol_truetype(glyph_names=glyph_names),
        text,
        subtype="/TrueType",
        font_file="/FontFile2",
        **kwargs,
    )


def test_symbol_cmap_codes_read_through_the_glyph_names():
    # 0x23 is '#' in every standard table, but this program draws Ccaron there.
    text = _text(_truetype_page("#2"))
    assert text == "Č2"


def test_glyphs_without_names_or_unicode_stay_markers():
    text = _text(_truetype_page("#2", glyph_names=False))
    assert "#" not in text
    assert "GLYPH<35>" in text and "GLYPH<50>" in text


def test_blank_glyph_reads_as_a_space():
    text = _text(_truetype_page("# 2", glyph_names=False))
    assert text == "GLYPH<35> GLYPH<50>"


def test_index_style_glyph_name_stays_a_marker():
    text = _text(_truetype_page("A"))
    assert text == "GLYPH<65>"


def test_cff_custom_encoding_reads_its_own_glyphs():
    # 0xE8 is egrave in Latin-1 and Lslash in StandardEncoding; the program
    # says it is Cyrillic i. 0x23 names its glyph uni0634.
    pdf = _page(
        _custom_cff(),
        "\\3502#",
        subtype="/Type1",
        font_file="/FontFile3",
        base_font="/ABCDEF+DoclingCyrillic",
    )
    assert _text(pdf) == "и2ش"


def test_builtin_encoding_outranks_a_core_14_alias():
    # A subset named after a core-14 alias used to resolve through the
    # Helvetica metrics table, which fabricates '#' for a symbol-coded glyph.
    text = _text(_truetype_page("#2", base_font="/ABCDEF+Arial"))
    assert text == "Č2"


def test_declared_base_encoding_still_wins():
    # A symbolic-flagged font that declares /WinAnsiEncoding keeps it.
    text = _text(_truetype_page("#2", encoding="/Encoding /WinAnsiEncoding "))
    assert text == "#2"


def test_differences_override_only_the_codes_they_list():
    # /Differences without /BaseEncoding: the listed code follows the array,
    # the others still read through the builtin encoding (9.6.6.1).
    text = _text(
        _truetype_page("#2", encoding="/Encoding << /Differences [35 /Zcaron] >> ")
    )
    assert text == "Ž2"


def test_nonsymbolic_font_is_untouched():
    # Flags bit 6 (nonsymbolic): the standard tables apply as before.
    text = _text(_truetype_page("#2", flags=32))
    assert text == "#2"
