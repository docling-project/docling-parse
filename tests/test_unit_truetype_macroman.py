#!/usr/bin/env python
"""A declared /MacRomanEncoding decides the meaning of the codes of a text font.

Quartz (macOS PDFContext) writes subsetted TrueType fonts such as ArialMT with
/Encoding /MacRomanEncoding. The metrics resources of docling-parse carry the
Microsoft text fonts (Arial, Times New Roman, Georgia, ...) with the AFM label
`EncodingScheme FontSpecific`, although their codes are laid out in WinAnsi
order. That label made the curated table win over the declared encoding, so
the MacRoman code 0x9F (u-umlaut) was read as WinAnsi Y-diaeresis and "Buero"
came out as "BYro" (docling-parse#449, a regression of #187).

The exception is limited to nonsymbolic TrueType fonts with an explicit
MacRoman encoding. Other subtypes, encodings and symbolic fonts keep their
existing mappings, and /ToUnicode and /Differences retain precedence.
Genuinely font-specific fonts (Symbol, ZapfDingbats, TeX) keep their own tables.
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
    ("subtype", "encoding"),
    [
        pytest.param("/Type1", "/MacRomanEncoding", id="type1-macroman"),
        pytest.param("/MMType1", "/MacRomanEncoding", id="mmtype1-macroman"),
        pytest.param("/TrueType", "/MacExpertEncoding", id="truetype-macexpert"),
        pytest.param("/TrueType", "/StandardEncoding", id="truetype-standard"),
        pytest.param("/TrueType", "/WinAnsiEncoding", id="truetype-winansi"),
    ],
)
def test_other_subtypes_and_encodings_keep_existing_mapping(
    subtype: str, encoding: str
) -> None:
    # Outside the MacRoman TrueType fix, preserve the existing Arial metrics
    # mapping: these bytes are Y-diaeresis/s-caron, not u-/o-umlaut.
    font = (
        f"<< /Type /Font /Subtype {subtype} /BaseFont /ArialMT "
        f"/FirstChar 32 /LastChar 255 /Encoding {encoding} >>"
    )
    assert _page_text(font, b"\x9f\x9a") == "Ÿš"


@pytest.mark.parametrize(
    ("flags", "expected"),
    [
        pytest.param(32, "üö", id="nonsymbolic"),
        pytest.param(96, "üö", id="nonsymbolic-italic"),
        pytest.param(4, "Ÿš", id="symbolic"),
        pytest.param(68, "Ÿš", id="symbolic-italic"),
    ],
)
def test_macroman_truetype_respects_symbolic_flag(flags: int, expected: str) -> None:
    # Use the same Latin font name for both flags, so the symbolic boundary
    # is exercised independently of the legacy Symbol font's curated table.
    font = (
        "<< /Type /Font /Subtype /TrueType /BaseFont /ArialMT "
        "/FirstChar 32 /LastChar 255 /Encoding /MacRomanEncoding "
        f"/FontDescriptor << /Type /FontDescriptor /FontName /ArialMT /Flags {flags} "
        "/FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900 /Descent -200 "
        "/CapHeight 700 /StemV 80 >> >>"
    )
    assert _page_text(font, b"\x9f\x9a") == expected
