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


# A line painted in passes -- each pass repainting the line from its start and going
# further -- must decode exactly like the same line painted once. The single pass is the
# reference: it takes the parser's ordinary path, so nothing here derives the expectation
# from the code under test.

HEBREW_FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /ToUnicode 6 0 R >>"
)
HEBREW_CMAP = (
    "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
    "/CMapName /Repro def\n/CMapType 2 def\n"
    "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
    "1 beginbfchar\n<20> <0020>\nendbfchar\n"
    "1 beginbfrange\n<41> <5A> <05D0>\nendbfrange\n"
    "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n"
)


def _content_lines(content: str, font: str = FONT_OBJECT, cmap: str | None = None) -> list[str]:
    extra: list = [font]
    if cmap is not None:
        from tests.pdf_builder import stream_object

        extra.append(stream_object("", cmap.encode("latin-1")))
    pdf = simple_page_pdf(
        content, resources=HELVETICA, media_box="[0 0 612 792]", extra_objects=extra
    )
    doc = DoclingPdfParser(loglevel="fatal").load(path_or_stream=BytesIO(pdf))
    try:
        _, page = next(doc.iterate_pages())
        return [cell.text for cell in page.textline_cells]
    finally:
        doc.unload()


def _show(origin: str, text: str) -> str:
    return f"BT /F1 12 Tf {origin} ({text}) Tj ET\n"


def _passes(lines: list[tuple[str, list[str]]]) -> tuple[str, str]:
    """(painted once, painted in passes). Each line is (origin operator, [its text as each
    pass paints it]); pass n paints every line's n-th text, all from the line's origin."""
    once = "".join(_show(origin, texts[-1]) for origin, texts in lines)
    depth = max(len(texts) for _, texts in lines)
    passes = "".join(
        _show(origin, texts[min(n, len(texts) - 1)])
        for n in range(depth)
        for origin, texts in lines
    )
    return once, passes


def _assert_passes_read_like_once(lines, **font) -> list[str]:
    once, passes = _passes(lines)
    expected = _content_lines(once, **font)
    assert _content_lines(passes, **font) == expected
    return expected


def test_a_paragraph_painted_in_two_passes_reads_like_one_pass():
    expected = _assert_passes_read_like_once([
        ("72 700 Td", ["ALPHA BETA GAMMA", "ALPHA BETA GAMMA ETA THETA"]),
        ("72 680 Td", ["DELTA EPSILON ZETA", "DELTA EPSILON ZETA IOTA KAPPA"]),
    ])
    assert expected == ["ALPHA BETA GAMMA ETA THETA", "DELTA EPSILON ZETA IOTA KAPPA"]


def test_a_word_split_across_three_passes_reads_like_one_pass():
    """The copied part of the line holds a word space, so the line's spacing is known. (A
    line whose copies all fall inside one word gives no word spacing, and text after a
    space is then left where it was painted rather than joined on a guess.)"""
    _assert_passes_read_like_once([
        ("72 700 Td", ["the calcu", "the calcula", "the calculation step"]),
        ("72 680 Td", ["other line"]),
    ])


def test_a_rotated_paragraph_painted_in_passes_reads_like_one_pass():
    _assert_passes_read_like_once([
        ("0 1 -1 0 300 200 Tm", ["ALPHA BETA", "ALPHA BETA GAMMA"]),
        ("0 1 -1 0 320 200 Tm", ["DELTA EPSILON", "DELTA EPSILON ZETA"]),
    ])


def test_a_right_to_left_paragraph_painted_in_passes_reads_like_one_pass():
    expected = _assert_passes_read_like_once(
        [
            ("72 700 Td", ["ABC DEF", "ABC DEF GHI"]),
            ("72 680 Td", ["JKL MNO", "JKL MNO PQR"]),
        ],
        font=HEBREW_FONT,
        cmap=HEBREW_CMAP,
    )
    assert all("א" <= ch <= "ת" or ch == " " for line in expected for ch in line)


def test_a_subscript_inside_a_line_painted_in_passes_reads_like_one_pass():
    line = "(CO) Tj /F1 8 Tf -2 Ts (2) Tj /F1 12 Tf 0 Ts ( level rises in the) Tj"
    once = f"BT /F1 12 Tf 72 700 Td {line} ET\n"
    first = "BT /F1 12 Tf 72 700 Td (CO) Tj /F1 8 Tf -2 Ts (2) Tj /F1 12 Tf 0 Ts ( level) Tj ET\n"
    other = "BT /F1 12 Tf 72 680 Td (another line) Tj ET\n"
    assert _content_lines(first + other + once) == _content_lines(once + other)


def test_columns_painted_after_a_copied_line_stay_columns():
    """Pass two repaints the left line, then paints the right column across a gutter wider
    than a word space (with no space glyph in it)."""
    left = _show("72 700 Td", "left one") + _show("72 680 Td", "left two")
    right = _show("140 700 Td", "right one") + _show("140 680 Td", "right two")
    painted_by_column = left + right
    copied = left + _show("72 700 Td", "left one") + right
    assert _content_lines(copied) == _content_lines(painted_by_column)


def test_text_overprinted_on_a_shared_prefix_is_not_spliced_into_the_line():
    """Two different numbers painted at one place: the first keeps its own continuation."""
    first = _show("72 700 Td", "NUMBER-0001 TO X") + _show("72 680 Td", "next")
    second = _show("72 700 Td", "NUMBER-0002 TO Y")
    lines = _content_lines(first + second)
    assert lines[:2] == ["NUMBER-0001 TO X", "next"]


def test_a_second_continuation_does_not_replace_the_first():
    content = (
        _show("72 700 Td", "stem word") + _show("72 680 Td", "h line")
        + _show("72 700 Td", "stem word xx") + _show("72 660 Td", "h two")
        + _show("72 700 Td", "stem word yy") + _show("72 640 Td", "h three")
    )
    lines = _content_lines(content)
    assert lines[0] == "stem word xx"
    assert "yy" in lines and "stem word yy" not in lines


def test_a_superscript_inside_a_line_painted_in_passes_reads_like_one_pass():
    line = "(E=mc) Tj /F1 8 Tf 4 Ts (2) Tj /F1 12 Tf 0 Ts ( is the energy of a) Tj"
    once = f"BT /F1 12 Tf 72 700 Td {line} ET\n"
    first = "BT /F1 12 Tf 72 700 Td (E=mc) Tj /F1 8 Tf 4 Ts (2) Tj /F1 12 Tf 0 Ts ( is) Tj ET\n"
    other = "BT /F1 12 Tf 72 680 Td (another line) Tj ET\n"
    assert _content_lines(first + other + once) == _content_lines(once + other)


def test_a_narrow_table_painted_column_by_column_after_a_copy_stays_a_table():
    """Right-column cells 2 pt from the left ones, with no space glyph between them."""
    width_a1 = 12 * (667 + 556) / 1000                     # Helvetica "A" and "1"
    right = f"{72 + width_a1 + 2:.3f}"
    left = _show("72 700 Td", "A1") + _show("72 680 Td", "A2")
    right_column = _show(f"{right} 700 Td", "B1") + _show(f"{right} 680 Td", "B2")
    copied = left + _show("72 700 Td", "A1") + right_column
    assert _content_lines(copied) == _content_lines(left + right_column)


def test_a_copy_of_a_glyph_the_tolerant_scan_removed_loses_no_text():
    """The copied prefix's originals were themselves removed as near copies (0.3 pt away)."""
    content = (
        _show("72 700 Td", "a b") + _show("72.3 700 Td", "a b")
        + _show("72 680 Td", "q") + _show("72.3 700 Td", "a b c")
    )
    text = "".join(_content_lines(content))
    assert "c" in text and "q" in text
