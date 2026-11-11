#!/usr/bin/env python
"""A declared /MacRomanEncoding decides the meaning of the codes of a text font.

Quartz (macOS PDFContext) writes subsetted TrueType fonts such as ArialMT with
/Encoding /MacRomanEncoding. The metrics resources of docling-parse carry the
Microsoft text fonts (Arial, Times New Roman, Georgia, ...) with the AFM label
`EncodingScheme FontSpecific`, although their codes are laid out in WinAnsi
order. That label made the curated table win over the declared encoding, so
the MacRoman code 0x9F (u-umlaut) was read as WinAnsi Y-diaeresis and "Buero"
came out as "BYro" (docling-parse#449, a regression of #187).

Declared glyph names also govern other simple fonts where the metrics contain
the named glyph, including glyphs without a built-in code. Legacy symbol fonts
keep their own mappings where the declared glyph is absent. /ToUnicode and
/Differences retain precedence, and the MacRoman TrueType exception still
applies when a Latin font's metrics lack the declared glyph.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import parse_page, simple_page_pdf, stream_object

TEXT = "Büro Fläche Straße ÄÖÜ äöü é è"


def _page_text(
    font_dict: str,
    text_bytes: bytes,
    *,
    extra_objects: list[str | bytes] | None = None,
) -> str:
    content = b"BT /F1 12 Tf 10 100 Td (" + text_bytes + b") Tj ET"
    pdf = simple_page_pdf(
        content.decode("latin-1"),
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 600 200]",  # wide enough that no glyph is clipped away
        extra_objects=[font_dict, *(extra_objects or [])],
    )
    page = parse_page(pdf)
    return "".join(cell.text for cell in page.char_cells)


def _squash(text: str) -> str:
    return text.replace(" ", "")


@pytest.mark.parametrize(
    "base_font",
    [
        "ArialMT",
        "Arial-BoldMT",
        "Arial-ItalicMT",
        "TimesNewRomanPSMT",
        "TimesNewRomanPS-BoldMT",
        "Georgia",
        "Verdana",
    ],
)
def test_truetype_macroman_text_font(base_font: str) -> None:
    font = (
        f"<< /Type /Font /Subtype /TrueType /BaseFont /{base_font} "
        "/FirstChar 32 /LastChar 255 /Encoding /MacRomanEncoding >>"
    )
    text = _page_text(font, TEXT.encode("mac_roman"))
    assert _squash(text) == _squash(TEXT)


def test_type1_macroman_control() -> None:
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /MacRomanEncoding >>"
    )
    text = _page_text(font, TEXT.encode("mac_roman"))
    assert _squash(text) == _squash(TEXT)


def test_truetype_winansi_text_font_unchanged() -> None:
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT "
        "/FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding >>"
    )
    text = _page_text(font, TEXT.encode("cp1252"))
    assert _squash(text) == _squash(TEXT)


def test_font_specific_symbol_font_keeps_its_own_table() -> None:
    # Symbol maps 0x61 to alpha and 0x62 to beta whatever the declared base
    # encoding says; a MacRoman declaration must not turn them into 'a', 'b'.
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /Symbol "
        "/Encoding /MacRomanEncoding >>"
    )
    text = _page_text(font, b"ab")
    assert _squash(text) == "αβ"


@pytest.mark.parametrize(
    "encoding",
    [
        pytest.param("/MacRomanEncoding", id="base-encoding"),
        pytest.param(
            "<< /BaseEncoding /MacRomanEncoding /Differences [159 /A] >>",
            id="conflicting-differences",
        ),
    ],
)
def test_tounicode_overrides_macroman(encoding: str) -> None:
    # 0x9F is u-umlaut in MacRoman and Y-diaeresis in the metrics table.
    # /ToUnicode instead specifies C-caron, even when /Differences says A.
    # The unlisted 0x9A must still fall back to MacRoman o-umlaut.
    cmap = """/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /MacRoman-Override-UCS def
/CMapType 2 def
1 begincodespacerange
<00> <FF>
endcodespacerange
1 beginbfchar
<9F> <010C>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end
end"""
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT "
        f"/FirstChar 32 /LastChar 255 /Encoding {encoding} /ToUnicode 6 0 R >>"
    )
    assert (
        _page_text(
            font, b"\x9f\x9a", extra_objects=[stream_object("", cmap.encode("ascii"))]
        )
        == "Čö"
    )


def test_differences_override_only_listed_macroman_codes() -> None:
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT "
        "/FirstChar 32 /LastChar 255 "
        "/Encoding << /BaseEncoding /MacRomanEncoding /Differences [159 /A] >> >>"
    )
    assert _page_text(font, b"\x9f\x9a") == "Aö"


@pytest.mark.parametrize(
    ("subtype", "encoding", "expected"),
    [
        pytest.param("/Type1", "/MacRomanEncoding", "üö", id="type1-macroman"),
        pytest.param("/MMType1", "/MacRomanEncoding", "üö", id="mmtype1-macroman"),
        pytest.param("/TrueType", "/MacExpertEncoding", "Ÿš", id="truetype-macexpert"),
        pytest.param("/TrueType", "/StandardEncoding", "Ÿš", id="truetype-standard"),
        pytest.param("/TrueType", "/WinAnsiEncoding", "Ÿš", id="truetype-winansi"),
    ],
)
def test_other_subtypes_and_encodings_follow_available_glyphs(
    subtype: str, encoding: str, expected: str
) -> None:
    # MacRoman names udieresis/odieresis, both present in Arial. The MacExpert
    # small-cap glyphs and the undefined Standard codes are absent, so those
    # retain the curated Y-diaeresis/s-caron mapping.
    font = (
        f"<< /Type /Font /Subtype {subtype} /BaseFont /ArialMT "
        f"/FirstChar 32 /LastChar 255 /Encoding {encoding} >>"
    )
    assert _page_text(font, b"\x9f\x9a") == expected


@pytest.mark.parametrize(
    "flags",
    [
        pytest.param(32, id="nonsymbolic"),
        pytest.param(96, id="nonsymbolic-italic"),
        pytest.param(4, id="symbolic"),
        pytest.param(68, id="symbolic-italic"),
    ],
)
def test_macroman_truetype_uses_available_glyphs_with_either_flag(flags: int) -> None:
    # An explicitly named glyph that exists in the font takes precedence
    # regardless of the descriptor's Symbolic/Nonsymbolic flag.
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT "
        "/FirstChar 32 /LastChar 255 /Encoding /MacRomanEncoding "
        f"/FontDescriptor << /Type /FontDescriptor /FontName /ArialMT /Flags {flags} "
        "/FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900 /Descent -200 "
        "/CapHeight 700 /StemV 80 >> >>"
    )
    assert _page_text(font, b"\x9f\x9a") == "üö"


@pytest.mark.parametrize(
    ("subtype", "encoding", "codes"),
    [
        pytest.param("/Type1", "/MacRomanEncoding", b"\xde\xdf", id="type1-macroman"),
        pytest.param(
            "/MMType1", "/MacRomanEncoding", b"\xde\xdf", id="mmtype1-macroman"
        ),
        pytest.param(
            "/TrueType", "/MacRomanEncoding", b"\xde\xdf", id="truetype-macroman"
        ),
        pytest.param("/Type1", "/StandardEncoding", b"\xae\xaf", id="type1-standard"),
        pytest.param("/Type1", "/MacExpertEncoding", b"WX", id="type1-macexpert"),
    ],
)
def test_declared_encoding_finds_glyphs_without_builtin_codes(
    subtype: str, encoding: str, codes: bytes
) -> None:
    # Arial's AFM contains fi/fl with C -1. Their presence must be checked by
    # name, independently of whether the metrics assign a built-in code.
    font = (
        f"<< /Type /Font /Subtype {subtype} /BaseFont /ArialMT "
        f"/FirstChar 32 /LastChar 255 /Encoding {encoding} >>"
    )
    assert _page_text(font, codes) == "fifl"


def test_symbol_without_declared_encoding_keeps_builtin_mapping() -> None:
    font = "<< /Type /Font /Subtype /Type1 /BaseFont /Symbol >>"
    assert _page_text(font, b"a\xd7b") == "\u03b1\u22c5\u03b2"


@pytest.mark.parametrize(
    ("flags", "expected"),
    [
        pytest.param(32, "ƒ\u2044·", id="nonsymbolic-macroman-exception"),
        pytest.param(4, "ÄÚá", id="symbolic-curated-fallback"),
    ],
)
def test_macroman_exception_preserved_when_declared_glyph_is_absent(
    flags: int, expected: str
) -> None:
    # LucidaConsole's WinAnsi-style AFM lacks florin/fraction/periodcentered.
    # Retain #453's declared-encoding exception for nonsymbolic TrueType,
    # independently of the new glyph-name lookup.
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /LucidaConsole "
        "/FirstChar 32 /LastChar 255 /Encoding /MacRomanEncoding "
        f"/FontDescriptor << /Type /FontDescriptor /FontName /LucidaConsole /Flags {flags} "
        "/FontBBox [0 -211 1000 789] /ItalicAngle 0 /Ascent 789 /Descent -211 "
        "/CapHeight 700 /StemV 80 >> >>"
    )
    assert _page_text(font, b"\xc4\xda\xe1") == expected
