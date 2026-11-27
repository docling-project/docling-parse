#!/usr/bin/env python
"""Duplicate removal preserves legitimate repeated letters and distinct text.

The default removes exact copies even across intervening lines, plus nearby
same-line glyph copies and nearly coincident complete words and lines.
Opting out preserves drawing passes. Controls preserve distinct text under
both settings. Cleanup behaves the same with and without cached lines, in
sequential, threaded and rendering workflows.
"""

from __future__ import annotations

from io import BytesIO

import pytest
from docling_core.types.doc.page import SegmentedPdfPage

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import simple_page_pdf

HELVETICA = "/Font << /F1 5 0 R >>"
FONT_OBJECT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)
FIRST = "ALPHA BETA GAMMA"
SECOND = "DELTA EPSILON ZETA"


def _pdf(draws: list[tuple[float, float, str]]) -> bytes:
    content = "".join(
        f"BT /F1 12 Tf {x} {y} Td ({text}) Tj ET\n" for x, y, text in draws
    )
    return simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )


def _page(pdf: bytes, decode_config: DecodeConfig | None = None) -> SegmentedPdfPage:
    doc = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=BytesIO(pdf), decode_config=decode_config
    )
    try:
        _, page = next(doc.iterate_pages())
        return page
    finally:
        doc.unload()


def _lines(draws: list[tuple[float, float, str]], config: DecodeConfig) -> list[str]:
    return [cell.text for cell in _page(_pdf(draws), config).textline_cells]


@pytest.fixture(params=[False, True], ids=["preserve", "deduplicate"])
def decode_config(request) -> DecodeConfig:
    return DecodeConfig(do_remove_duplicate_cells=request.param)


def test_a_line_painted_three_times_in_succession(decode_config):
    expected = [FIRST] if decode_config.do_remove_duplicate_cells else [FIRST] * 3
    assert _lines([(72, 700, FIRST)] * 3, decode_config) == expected


def test_dense_overprinting_keeps_one_complete_line():
    text = "Committee Participant benefits illegible"
    page = _page(_pdf([(72, 700, text)] * 1024))
    assert [cell.text for cell in page.textline_cells] == [text]
    assert "".join(cell.text for cell in page.char_cells) == text


@pytest.mark.parametrize("offset", [0.0, 0.3])
def test_a_single_letter_painted_twice(offset):
    page = _page(_pdf([(72, 700, "A"), (72 + offset, 700, "A")]))
    assert [cell.text for cell in page.textline_cells] == ["A"]
    assert [cell.text for cell in page.char_cells] == ["A"]


@pytest.mark.parametrize("adjustment", [667, 650], ids=["exact", "shifted"])
def test_overlapping_letters_inside_one_text_show(adjustment, decode_config):
    content = f"BT /F1 12 Tf 72 100 Td [(A) {adjustment} (ABC)] TJ ET"
    page = _page(
        simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT]),
        decode_config,
    )
    expected = "ABC" if decode_config.do_remove_duplicate_cells else "AABC"
    assert [cell.text for cell in page.textline_cells] == [expected]


@pytest.mark.parametrize("clipped_stroke", [False, True])
def test_stroke_and_fill_interleaved_with_text(clipped_stroke):
    stroke = "BT /F1 12 Tf 1 Tr 1 0 0 1 80.004 100 Tm (B) Tj ET"
    if clipped_stroke:
        stroke = f"q 0 0 150 150 re W n {stroke} Q"
    content = (
        f"BT /F1 12 Tf 1 0 0 1 72 100 Tm (A) Tj ET {stroke} "
        "BT /F1 12 Tf 0 Tr 1 0 0 1 80.004 100 Tm (BC) Tj ET"
    )
    page = _page(
        simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT])
    )
    assert [cell.text for cell in page.textline_cells] == ["ABC"]


def test_distinct_source_quotes_keep_both_glyphs_after_normalization():
    content = r"BT /F1 12 Tf 72 100 Td [(\224) 320 (\223)] TJ ET"
    page = _page(
        simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT])
    )
    assert [cell.text for cell in page.textline_cells] == ['""']


def test_coincident_tagged_digits_keep_their_character_cells(decode_config):
    content = (
        "/P << /MCID 0 >> BDC BT /F1 12 Tf 72 100 Td (0) Tj ET EMC "
        "/P << /MCID 1 >> BDC BT /F1 12 Tf 72 100 Td (0) Tj ET EMC"
    )
    page = _page(
        simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT]),
        decode_config,
    )
    expected = "0" if decode_config.do_remove_duplicate_cells else "00"
    assert [cell.text for cell in page.textline_cells] == [expected]
    assert [cell.text for cell in page.char_cells] == ["0", "0"]


def test_shifted_paint_pass_can_continue_into_the_next_fragment():
    page = _page(
        _pdf(
            [
                (72, 700, "AB"),
                (71.76, 699.76, "AB"),
                (72, 699.76, "AB"),
                (71.76, 700, "ABC"),
            ]
        )
    )
    assert [cell.text for cell in page.textline_cells] == ["ABC"]


def test_stroke_and_fill_preserve_distinct_overlapping_labels():
    content = (
        "BT /F1 12 Tf 1 Tr 1 0 0 1 72 100 Tm (ALPHA) Tj ET "
        "BT /F1 12 Tf 0 Tr 1 0 0 1 72 100 Tm (ALPINE) Tj ET"
    )
    page = _page(
        simple_page_pdf(content, resources=HELVETICA, extra_objects=[FONT_OBJECT])
    )
    assert [cell.text for cell in page.textline_cells] == ["ALPHA", "ALPINE"]


def _show(content: str) -> SegmentedPdfPage:
    return _page(
        simple_page_pdf(
            content,
            resources=HELVETICA,
            media_box="[0 0 612 792]",
            extra_objects=[FONT_OBJECT],
        )
    )


# Helvetica advances at 10 pt: "(Exercise" is 41.67 pt and the space 2.78 pt.
REPAINTED_NUMBER = {
    "Tj-Td-Tj": "BT /F1 10 Tf 72 700 Td (\\(Exercise 32) Tj {x:.3f} 0 Td "
    "(32\\) was proposed) Tj ET",
    "TJ-TD-TJ": "BT /F1 10 Tf 72 700 Td [(\\(Exercise) -396.4 (32)] TJ {x:.3f} 0 TD "
    "[(32\\)) -396.5 (was proposed)] TJ ET",
    "one-TJ": "BT /F1 10 Tf 72 700 Td [(\\(Exercise 32) {back:g} "
    "(32\\) was proposed)] TJ ET",
}


@pytest.mark.parametrize("offset", [0.0, 0.02], ids=["exact", "shifted"])
@pytest.mark.parametrize("shape", REPAINTED_NUMBER)
def test_a_repainted_number_that_continues_the_line_is_read_once(shape, offset):
    to_number = 44.45 if shape == "Tj-Td-Tj" else 41.67 + 3.964
    content = REPAINTED_NUMBER[shape].format(
        x=to_number + offset, back=1112 - 100 * offset
    )
    page = _show(content)
    assert [c.text for c in page.textline_cells] == ["(Exercise 32) was proposed"]
    assert [c.text for c in page.word_cells] == ["(Exercise", "32)", "was", "proposed"]


@pytest.mark.parametrize("offset", [0.0, 0.02], ids=["exact", "shifted"])
def test_a_repainted_digit_does_not_fuse_into_the_next_word(offset):
    page = _show(
        f"BT /F1 10 Tf 72 700 Td (\\(Exercise 2) Tj {44.45 + offset:.2f} 0 Td "
        "(2\\);) Tj ET"
    )
    assert [c.text for c in page.textline_cells] == ["(Exercise 2);"]


def test_a_number_repeated_at_its_own_advance_is_kept():
    page = _show("BT /F1 10 Tf 72 700 Td (\\(Exercise 32 ) Tj (32\\) was) Tj ET")
    assert [c.text for c in page.textline_cells] == ["(Exercise 32 32) was"]


def test_a_show_overprinting_only_part_of_the_glyphs_before_it_is_kept():
    # PHI starts on the P of ALPHA, but its I is not the A drawn there.
    page = _show("BT /F1 10 Tf 72 700 Td (ALPHA) Tj 12.23 0 Td (PHI) Tj ET")
    assert "".join(c.text for c in page.char_cells) == "ALPHAPHI"


def test_partial_copies_across_an_intervening_line_keep_every_letter():
    page = _show(
        "BT /F1 12 Tf 72 700 Td (ENFORCED FOR ALL ACCESS) Tj ET\n"
        "BT /F1 12 Tf 72 680 Td (Another line in between) Tj ET\n"
        "BT /F1 12 Tf 0.01 Tc 72 700 Td (ENFORCED FOR ALL ACCESS) Tj 0 Tc ET\n"
        "BT /F1 12 Tf 72 600 Td (S) Tj ET\n"
        "BT /F1 12 Tf 99.348 600 Td (ifi) Tj ET\n"
        "BT /F1 12 Tf 72 580 Td (Another line in between) Tj ET\n"
        "BT /F1 12 Tf 72 600 Td (Specifies that the row permission is enabled) Tj ET\n"
    )
    assert [c.text for c in page.textline_cells] == [
        "ENFORCED FOR ALL ACCESS",
        "Another line in between",
        "S ifi",
        "Another line in between",
        "Specifies that the row permission is enabled",
    ]


def test_a_block_painted_three_times(decode_config):
    block = [(72, 700, FIRST), (72, 660, SECOND)]

    expected = [FIRST, SECOND]
    if not decode_config.do_remove_duplicate_cells:
        expected *= 3
    assert _lines(block * 3, decode_config) == expected


def test_the_same_line_at_two_positions_is_kept_twice(decode_config):
    assert _lines([(72, 700, FIRST), (72, 660, FIRST)], decode_config) == [FIRST, FIRST]


def test_a_block_repeated_at_another_position_is_kept(decode_config):
    draws = [(72, 700, FIRST), (72, 660, SECOND), (72, 620, FIRST), (72, 580, SECOND)]

    assert _lines(draws, decode_config) == [FIRST, SECOND, FIRST, SECOND]


def test_a_copy_shifted_by_one_point_is_kept(decode_config):
    assert _lines([(72, 700, FIRST), (73, 700, FIRST)], decode_config) == [FIRST, FIRST]


@pytest.mark.parametrize("offset", [0.0, 0.3])
def test_a_different_word_overlapping_within_tolerance_keeps_its_letters(
    decode_config, offset
):
    """B and P are equally wide, so the A and T of both words lie 0.3 apart."""
    lines = _lines(
        [(72, 700, "BAT"), (72, 660, SECOND), (72 + offset, 700, "PAT")], decode_config
    )

    assert sorted(lines) == sorted(["BAT", SECOND, "PAT"])


def test_different_comparison_operators_are_both_kept(decode_config):
    assert _lines(
        [(72, 700, "LIMIT >= 42"), (72, 660, "LIMIT > 42")], decode_config
    ) == [
        "LIMIT >= 42",
        "LIMIT > 42",
    ]


@pytest.mark.parametrize("do_sanitization", [False, True])
def test_shifted_line_is_deduplicated_after_contraction(decode_config, do_sanitization):
    draws = [(72, 700, FIRST), (72, 660, SECOND), (72.01, 700.01, FIRST)]
    config = decode_config.model_copy(update={"do_sanitization": do_sanitization})
    page = _page(_pdf(draws), config)
    # The intervening line prevents tolerant character cleanup, and no glyph
    # is exactly repeated. Only the word/line passes can remove this copy.
    assert "".join(c.text for c in page.char_cells) == FIRST + SECOND + FIRST
    expected_lines = [FIRST, SECOND]
    expected_words = FIRST.split() + SECOND.split()
    if not config.do_remove_duplicate_cells:
        expected_lines.append(FIRST)
        expected_words.extend(FIRST.split())
    assert [c.text for c in page.textline_cells] == expected_lines
    assert [c.text for c in page.word_cells] == expected_words


def test_word_deduplication_keeps_distinct_complete_lines(decode_config):
    draws = [(72, 700, "TOKEN ALPHA"), (72, 660, SECOND), (72.01, 700.01, "TOKEN BETA")]
    page = _page(_pdf(draws), decode_config)
    assert [c.text for c in page.textline_cells] == [
        "TOKEN ALPHA",
        SECOND,
        "TOKEN BETA",
    ]
    words = [c.text for c in page.word_cells]
    assert words.count("TOKEN") == (1 if decode_config.do_remove_duplicate_cells else 2)
    assert "ALPHA" in words and "BETA" in words


def test_rotated_shifted_line_is_deduplicated(decode_config):
    content = (
        "BT /F1 12 Tf 0 1 -1 0 300 200 Tm (ALPHA BETA) Tj ET\n"
        "BT /F1 12 Tf 0 1 -1 0 320 200 Tm (OTHER LINE) Tj ET\n"
        "BT /F1 12 Tf 0 1 -1 0 300.01 200.01 Tm (ALPHA BETA) Tj ET\n"
    )
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )
    page = _page(pdf, decode_config)
    expected = ["ALPHA BETA", "OTHER LINE"]
    if not decode_config.do_remove_duplicate_cells:
        expected.append("ALPHA BETA")
    assert [c.text for c in page.textline_cells] == expected
    assert [c.text for c in page.word_cells].count("ALPHA") == (
        1 if decode_config.do_remove_duplicate_cells else 2
    )


def test_small_words_are_not_removed_for_partial_overlap(decode_config):
    content = (
        "BT /F1 1 Tf 72 700 Td (WORD) Tj ET\n"
        "BT /F1 12 Tf 72 660 Td (OTHER LINE) Tj ET\n"
        "BT /F1 1 Tf 72.25 700.25 Td (WORD) Tj ET\n"
    )
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )
    page = _page(pdf, decode_config)
    assert [c.text for c in page.textline_cells] == ["WORD", "OTHER LINE", "WORD"]
    assert [c.text for c in page.word_cells].count("WORD") == 2


def test_different_clips_preserve_overlapping_text(decode_config):
    content = (
        "q 0 0 300 792 re W n BT /F1 12 Tf 72 700 Td (TOKEN) Tj ET Q\n"
        "BT /F1 12 Tf 72 660 Td (OTHER LINE) Tj ET\n"
        "q 0 0 400 792 re W n BT /F1 12 Tf 72.01 700.01 Td (TOKEN) Tj ET Q\n"
    )
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[FONT_OBJECT],
    )
    page = _page(pdf, decode_config)
    assert [c.text for c in page.textline_cells] == ["TOKEN", "OTHER LINE", "TOKEN"]
    assert [c.text for c in page.word_cells].count("TOKEN") == 2


def test_different_fonts_preserve_overlapping_text(decode_config):
    content = (
        "BT /F1 12 Tf 72 700 Td (.) Tj ET\n"
        "BT /F1 12 Tf 72 660 Td (OTHER LINE) Tj ET\n"
        "BT /F2 12 Tf 72.01 700.01 Td (.) Tj ET\n"
    )
    pdf = simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R /F2 6 0 R >>",
        media_box="[0 0 612 792]",
        extra_objects=[
            FONT_OBJECT,
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
        ],
    )
    page = _page(pdf, decode_config)
    assert [c.text for c in page.textline_cells] == [".", "OTHER LINE", "."]
    assert [c.text for c in page.word_cells].count(".") == 2


@pytest.mark.parametrize("do_sanitization", [False, True])
@pytest.mark.parametrize("offset", [0.0, 0.3], ids=["exact", "nearby"])
def test_default_removes_copies(do_sanitization, offset):
    single = _page(
        _pdf([(72, 700, FIRST)]), DecodeConfig(do_sanitization=do_sanitization)
    )
    repeated = _page(
        _pdf([(72, 700, FIRST), (72 + offset, 700, FIRST)]),
        DecodeConfig(do_sanitization=do_sanitization),
    )
    assert [c.text for c in repeated.char_cells] == [c.text for c in single.char_cells]


def test_omitted_decode_config_removes_copies():
    page = _page(_pdf([(72, 700, FIRST)] * 2))
    assert [c.text for c in page.textline_cells] == [FIRST]


@pytest.mark.parametrize("size", [12, 2, 1, 0.8, 0.5])
@pytest.mark.parametrize("copies", [1, 2], ids=["single-pass", "fake-bold"])
def test_small_text_keeps_repeated_letters(size, copies):
    text = "Committee Participant benefits illegible"
    run = f"/F1 {size} Tf 50 400 Td ({text}) Tj"
    content = f"BT 0 Tr {run} ET\n"
    if copies == 2:
        content = f"BT 1 Tr {run} ET\n" + content
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box="[0 0 612 792]",
        extra_objects=[
            "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman "
            "/Encoding /WinAnsiEncoding >>"
        ],
    )
    assert [c.text for c in _page(pdf).textline_cells] == [text]


@pytest.mark.parametrize("do_sanitization", [False, True])
@pytest.mark.parametrize("offset", [0.0, 0.3], ids=["exact", "nearby"])
def test_explicit_setting_controls_both_scans(decode_config, do_sanitization, offset):
    config = decode_config.model_copy(update={"do_sanitization": do_sanitization})
    # Round-trip a saved public configuration before it reaches the native decoder.
    config = DecodeConfig.model_validate_json(config.model_dump_json())
    single = _page(_pdf([(72, 700, FIRST)]), config)
    repeated = _page(_pdf([(72, 700, FIRST), (72 + offset, 700, FIRST)]), config)
    expected = [c.text for c in single.char_cells]
    if not config.do_remove_duplicate_cells:
        expected *= 2
    assert [c.text for c in repeated.char_cells] == expected


@pytest.mark.parametrize("render", [False, True], ids=["parser", "renderer"])
def test_threaded_workflows_honor_duplicate_setting(decode_config, render):
    block = [(72, 700, FIRST), (72, 660, SECOND)]
    shifted = [(x + 0.01, y + 0.01, text) for x, y, text in block]
    pdf = _pdf(block + shifted)
    expected = _page(pdf, decode_config)
    assert [c.text for c in expected.textline_cells] == (
        [FIRST, SECOND]
        if decode_config.do_remove_duplicate_cells
        else [FIRST, SECOND] * 2
    )
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=2,
            render_config=RenderConfig() if render else None,
        ),
        decode_config=decode_config,
    )
    key = parser.load(BytesIO(pdf))
    try:
        results = list(parser.iterate_results())
        assert len(results) == 1
        result = results[0]
        assert result.success, result.error_message
        page = result.get_page()
        for field in ("char_cells", "word_cells", "textline_cells"):
            assert [c.text for c in getattr(page, field)] == [
                c.text for c in getattr(expected, field)
            ]
        if render:
            assert result.get_image().size == (612, 792)
    finally:
        parser.unload(key)
