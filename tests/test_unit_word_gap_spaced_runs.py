#!/usr/bin/env python
"""Unmarked word gaps on lines that carry space glyphs.

The space glyphs mark the words; the threshold for the unmarked gaps between
them is 0.35 neighbouring advances, and rises on a letter-spaced line.
"""

from io import BytesIO

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import simple_page_pdf

_HELVETICA = "/Font << /F1 5 0 R >>"
_HELVETICA_OBJECT = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Name /F1 >>"


def _parse(content: str):
    pdf = simple_page_pdf(
        content,
        resources=_HELVETICA,
        media_box="[0 0 300 100]",
        extra_objects=[_HELVETICA_OBJECT],
    )
    document = DoclingPdfParser(loglevel="fatal").load(path_or_stream=BytesIO(pdf))
    try:
        _, page = next(document.iterate_pages())
    finally:
        document.unload()
    return page


def test_letter_spaced_words_with_a_space_glyph_stay_whole():
    # 2.5 pt of character spacing at 12 pt opens every glyph gap to between
    # half and most of an advance, past the 0.35 threshold. The space glyph is
    # authoritative; the opened glyph gaps must not add words.
    page = _parse("BT /F1 12 Tf 2.5 Tc 20 50 Td (article info) Tj ET")

    assert [cell.text for cell in page.word_cells] == ["article", "info"]
    assert [cell.text for cell in page.textline_cells] == ["article info"]


def test_leader_dots_do_not_pass_for_letter_spacing():
    # A contents line: words positioned by TJ offsets of 0.3 em (about 0.6
    # advances), leader dots 0.25 em apart (0.9 of a dot advance), and a page
    # number after a space glyph. The dot gaps are below one advance; counted
    # as letter spacing they would raise the threshold past the word gaps.
    page = _parse(
        "BT /F1 12 Tf 20 50 Td "
        "[(maximum) -300 (mass) -300 (of) -300 (vehicle:) -300 "
        "(.) -250 (.) -250 (.) -250 (.)] TJ ( 12) Tj ET"
    )

    assert [cell.text for cell in page.textline_cells] == [
        "maximum mass of vehicle: . . . . 12"
    ]
