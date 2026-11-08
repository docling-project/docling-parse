#!/usr/bin/env python
"""A declared /MacRomanEncoding decides the meaning of the codes of a text font.

Quartz (macOS PDFContext) writes subsetted TrueType fonts such as ArialMT with
/Encoding /MacRomanEncoding. The metrics resources of docling-parse carry the
Microsoft text fonts (Arial, Times New Roman, Georgia, ...) with the AFM label
`EncodingScheme FontSpecific`, although their codes are laid out in WinAnsi
order. That label made the curated table win over the declared encoding, so
the MacRoman code 0x9F (u-umlaut) was read as WinAnsi Y-diaeresis and "Buero"
came out as "BYro" (docling-parse#449, a regression of #187).

Genuinely font-specific fonts (Symbol, ZapfDingbats, TeX) must keep their own
table whatever base encoding the PDF declares: that is what the curated tables
are for, and the last test pins it.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import parse_page, simple_page_pdf

TEXT = "Büro Fläche Straße ÄÖÜ äöü é è"


def _page_text(font_dict: str, text_bytes: bytes) -> str:
    content = b"BT /F1 12 Tf 10 100 Td (" + text_bytes + b") Tj ET"
    pdf = simple_page_pdf(
        content.decode("latin-1"),
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 600 200]",  # wide enough that no glyph is clipped away
        extra_objects=[font_dict],
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
