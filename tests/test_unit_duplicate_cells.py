#!/usr/bin/env python
"""Removal of glyphs drawn twice in the same place.

A producer without a bold face fakes one by drawing the run twice -- stroked,
then filled -- at the same origin, and the parser drops the second copy so the
text is not read twice. On very small type the next occurrence of the same
letter in a word ("mm", "tt", "ee") is itself less than half a point further
along the line, and must not be mistaken for such a copy.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import simple_page_pdf

TEXT = "Committee Participant benefits illegible"
TIMES = "/Font << /F1 5 0 R >>"
FONT_OBJECT = "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>"


def _parsed_lines(content: str) -> list[str]:
    pdf = simple_page_pdf(
        content,
        resources=TIMES,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(pdf))
    _, page = next(doc.iterate_pages())
    return [line.text for line in page.textline_cells]


@pytest.mark.parametrize("size", [12, 2, 1, 0.8, 0.5])
def test_repeated_letters_survive_at_any_text_size(size):
    lines = _parsed_lines(f"BT /F1 {size} Tf 50 400 Td ({TEXT}) Tj ET\n")
    assert lines == [TEXT]


@pytest.mark.parametrize("size", [12, 1])
def test_run_drawn_twice_in_place_is_read_once(size):
    run = f"/F1 {size} Tf 50 400 Td ({TEXT}) Tj"
    lines = _parsed_lines(f"BT 1 Tr {run} ET\nBT 0 Tr {run} ET\n")
    assert lines == [TEXT]
