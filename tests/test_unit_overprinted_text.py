#!/usr/bin/env python
"""Text painted more than once at the same position decodes once.

Overprinted copies render as one line, so they must decode as one. The copies
of a single line painted one after another were already removed. The copies of
a block of several lines were not: each copy of a line is separated from the
next by the block's other lines, and the duplicate search stopped at the first
cell off its baseline. The controls keep the repetition a reader does see.
"""

from __future__ import annotations

from io import BytesIO

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import simple_page_pdf

HELVETICA = "/Font << /F1 5 0 R >>"
FONT_OBJECT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)
FIRST = "ALPHA BETA GAMMA"
SECOND = "DELTA EPSILON ZETA"


def _lines(draws: list[tuple[int, int, str]]) -> list[str]:
    content = "".join(f"BT /F1 12 Tf {x} {y} Td ({text}) Tj ET\n" for x, y, text in draws)
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )
    doc = DoclingPdfParser(loglevel="fatal").load(path_or_stream=BytesIO(pdf))
    try:
        _, page = next(doc.iterate_pages())
        return [cell.text for cell in page.textline_cells]
    finally:
        doc.unload()


def test_a_line_painted_three_times_in_succession_is_one_line():
    assert _lines([(72, 700, FIRST)] * 3) == [FIRST]


def test_a_block_painted_three_times_is_one_block():
    block = [(72, 700, FIRST), (72, 660, SECOND)]

    assert _lines(block * 3) == [FIRST, SECOND]


def test_the_same_line_at_two_positions_is_kept_twice():
    assert _lines([(72, 700, FIRST), (72, 660, FIRST)]) == [FIRST, FIRST]


def test_a_block_repeated_at_another_position_is_kept():
    draws = [(72, 700, FIRST), (72, 660, SECOND), (72, 620, FIRST), (72, 580, SECOND)]

    assert _lines(draws) == [FIRST, SECOND, FIRST, SECOND]


def test_a_copy_shifted_by_one_point_is_kept():
    assert _lines([(72, 700, FIRST), (73, 700, FIRST)]) == [FIRST, FIRST]


def test_a_different_word_overlapping_within_tolerance_keeps_its_letters():
    """B and P are equally wide, so the A and T of both words lie 0.3 apart."""
    lines = _lines([(72, 700, "BAT"), (72, 660, SECOND), (72.3, 700, "PAT")])

    assert sorted(lines) == sorted(["BAT", SECOND, "PAT"])


def test_different_comparison_operators_are_both_kept():
    assert _lines([(72, 700, "LIMIT >= 42"), (72, 660, "LIMIT > 42")]) == [
        "LIMIT >= 42",
        "LIMIT > 42",
    ]
