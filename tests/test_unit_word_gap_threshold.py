#!/usr/bin/env python
"""Word boundaries on lines positioned without explicit space glyphs."""

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


def test_word_gaps_survive_a_wider_section_number_gap():
    # A LaTeX-style section heading: glyphs within a word touch, the words are
    # separated by one space width (333/1000 em) of TJ displacement, and the
    # section number by a full em. The em gap is almost three times the word
    # gap, but both separate words.
    page = _parse(
        "BT /F1 12 Tf 20 50 Td "
        "[(5.1) -1000 (Hyper) -333 (Parameter) -333 (Optimization)] TJ ET"
    )

    assert [cell.text for cell in page.word_cells] == [
        "5.1",
        "Hyper",
        "Parameter",
        "Optimization",
    ]
    assert [cell.text for cell in page.textline_cells] == [
        "5.1 Hyper Parameter Optimization"
    ]


def test_letter_spaced_words_stay_whole():
    # A tracked heading: 2.5 pt of character spacing opens every glyph gap to
    # between a third and a whole advance, while the word gap is wider still.
    page = _parse("BT /F1 12 Tf 2.5 Tc 20 50 Td [(article) -800 (info)] TJ ET")

    assert [cell.text for cell in page.word_cells] == ["article", "info"]
    assert [cell.text for cell in page.textline_cells] == ["article info"]
