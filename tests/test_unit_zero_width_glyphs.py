#!/usr/bin/env python
"""Fonts that declare zero glyph widths while positioning every glyph explicitly.

Some generators emit one Tj (plus a Td advance) per character and ship a
/Widths array of zeros: rendering is unaffected, because positioning never
consults the declared widths, but an extractor that trusts them produces
zero-width character cells. Word/line merging then compares real inter-glyph
gaps against a tolerance scaled by the (zero) average cell width, so nothing
merges and every character surfaces as its own word — the "T i t l e C a s e"
artifact of docling-project/docling#4018 and #3882.

These tests build such PDFs in memory and check that words and lines are
reassembled, that honest non-zero /Widths still take precedence, and that a
zero-width space glyph keeps acting as a word boundary.
"""

from io import BytesIO
from typing import List, Tuple

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import build_pdf

# Helvetica advance widths in 1/1000 em (AFM values), for realistic layout.
_HELVETICA_WIDTHS = {
    "T": 611,
    "i": 222,
    "t": 278,
    "l": 222,
    "e": 556,
    "C": 722,
    "a": 556,
    "s": 500,
    "W": 944,
    "o": 556,
    "r": 333,
    "k": 500,
    " ": 278,
}

_FONT_SIZE = 12


def _per_glyph_content(text: str, draw_spaces: bool) -> str:
    """One Tj per glyph, each followed by a Td of the glyph's true advance.

    With draw_spaces=False, word gaps are positioning-only — no space glyph is
    ever painted, as in the #4018 reproduction. With draw_spaces=True the
    space glyph is painted too (at declared width zero).
    """
    ops = [f"BT /F1 {_FONT_SIZE} Tf 72 720 Td"]
    for char in text:
        advance = _HELVETICA_WIDTHS[char] * _FONT_SIZE / 1000.0
        if char == " " and not draw_spaces:
            ops.append(f"{advance:.2f} 0 Td")
            continue
        ops.append(f"({char}) Tj {advance:.2f} 0 Td")
    ops.append("ET")
    return " ".join(ops)


def _build_zero_widths_pdf(content: str, widths: List[int]) -> bytes:
    widths_array = " ".join(str(w) for w in widths)
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Name /F1"
        f" /FirstChar 32 /LastChar 126 /Widths [ {widths_array} ]"
        " /Encoding /WinAnsiEncoding >>"
    )
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            font,
            f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        ]
    )


def _words_and_lines(pdf: bytes) -> Tuple[List[str], List[str]]:
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(pdf))
    _, page = next(doc.iterate_pages())
    words = [cell.text for cell in page.word_cells]
    lines = [cell.text for cell in page.textline_cells]
    return words, lines


ZERO_WIDTHS = [0] * 95  # chars 32..126 all declared width 0


def test_zero_declared_widths_still_merge_into_words():
    """Per-glyph Tj + zero /Widths must not split every character (#4018)."""
    pdf = _build_zero_widths_pdf(
        _per_glyph_content("Title Case Workers", draw_spaces=False), ZERO_WIDTHS
    )
    words, lines = _words_and_lines(pdf)

    assert words == ["Title", "Case", "Workers"]
    assert lines == ["Title Case Workers"]


def test_zero_declared_widths_whole_string_tj():
    """A single whole-string Tj under zero /Widths reads back intact."""
    content = f"BT /F1 {_FONT_SIZE} Tf 72 720 Td (Title Case Workers) Tj ET"
    pdf = _build_zero_widths_pdf(content, ZERO_WIDTHS)
    _, lines = _words_and_lines(pdf)

    assert lines == ["Title Case Workers"]


def test_zero_width_space_glyph_still_separates_words():
    """A painted space glyph at declared width 0 keeps its boundary role."""
    pdf = _build_zero_widths_pdf(
        _per_glyph_content("Title Case Workers", draw_spaces=True), ZERO_WIDTHS
    )
    words, _ = _words_and_lines(pdf)

    assert words == ["Title", "Case", "Workers"]


def test_symbol_font_pua_space_keeps_declared_zero_width():
    """A symbol-encoded space (U+F020, the 0xF000 offset convention) is a
    space in disguise: its declared zero advance must be kept, not replaced
    by a fallback width.

    Regression guard for corpus document 10572911635253446040-13.pdf page 10,
    where a TimesNewRomanPSMT subset paints a zero-width U+F020 spacer whose
    neighbours start at the very same x.
    """
    tounicode = (
        "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
        "/CMapName /Custom def /CMapType 2 def\n"
        "1 begincodespacerange <20> <7E> endcodespacerange\n"
        "1 beginbfchar <20> <F020> endbfchar\n"
        "endcmap CMapName currentdict /CMap defineresource pop end end"
    )
    widths_array = " ".join(str(w) for w in ZERO_WIDTHS)
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Name /F1"
        f" /FirstChar 32 /LastChar 126 /Widths [ {widths_array} ]"
        " /Encoding /WinAnsiEncoding /ToUnicode 6 0 R >>"
    )
    content = _per_glyph_content("Title Case Workers", draw_spaces=True)
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            font,
            f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
            f"<< /Length {len(tounicode)} >>\nstream\n{tounicode}\nendstream",
        ]
    )

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(pdf))
    _, page = next(doc.iterate_pages())

    spacers = [c for c in page.char_cells if c.text == "\uf020"]
    assert len(spacers) == 2, [c.text for c in page.char_cells]
    for cell in spacers:
        poly = cell.rect.to_polygon()
        assert abs(poly[1][0] - poly[0][0]) < 1e-6, poly  # declared 0 kept

    # The zero-width spacer may surface as its own cell or attach to a
    # neighbouring word; either way the real words must neither split into
    # characters nor merge across the spacer.
    words = [cell.text for cell in page.word_cells]
    letter_words = [w.replace("\uf020", "") for w in words]
    assert [w for w in letter_words if w] == ["Title", "Case", "Workers"], words


def test_honest_nonzero_widths_keep_precedence():
    """Non-zero declared widths are still trusted verbatim, not second-guessed.

    Mirrors test_standard_font_widths.py: declared widths override base-font
    metrics whenever they are non-zero, so the fix must only fire on zeros.
    """
    honest = [_HELVETICA_WIDTHS.get(chr(code), 500) for code in range(32, 127)]
    pdf = _build_zero_widths_pdf(
        _per_glyph_content("Title Case Workers", draw_spaces=False), honest
    )
    words, lines = _words_and_lines(pdf)

    assert words == ["Title", "Case", "Workers"]
    assert lines == ["Title Case Workers"]
