#!/usr/bin/env python
"""Repeated text is preserved unless duplicate removal is requested.

Opting in removes exact copies even across intervening lines, plus nearby
same-line copies handled by the tolerant scan. Controls preserve distinct text
under both settings. Duplicate removal is independent of word/line creation
and must behave the same in sequential, threaded and rendering workflows.
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


def test_a_different_word_overlapping_within_tolerance_keeps_its_letters(decode_config):
    """B and P are equally wide, so the A and T of both words lie 0.3 apart."""
    lines = _lines(
        [(72, 700, "BAT"), (72, 660, SECOND), (72.3, 700, "PAT")], decode_config
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
@pytest.mark.parametrize("offset", [0.0, 0.3], ids=["exact", "nearby"])
def test_default_preserves_copies(do_sanitization, offset):
    single = _page(
        _pdf([(72, 700, FIRST)]), DecodeConfig(do_sanitization=do_sanitization)
    )
    repeated = _page(
        _pdf([(72, 700, FIRST), (72 + offset, 700, FIRST)]),
        DecodeConfig(do_sanitization=do_sanitization),
    )
    assert [c.text for c in repeated.char_cells] == [
        c.text for c in single.char_cells
    ] * 2


def test_omitted_decode_config_preserves_copies():
    page = _page(_pdf([(72, 700, FIRST)] * 2))
    assert [c.text for c in page.textline_cells] == [FIRST, FIRST]


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
    pdf = _pdf(block * 2)
    expected = _page(pdf, decode_config)
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
