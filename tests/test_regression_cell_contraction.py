#!/usr/bin/env python
"""Targeted regressions for adaptive character-to-word contraction."""

from pathlib import Path

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser

DATA = Path(__file__).parent / "data" / "regression"


def _page(filename: str, page_no: int, *, keep_glyphs: bool = False):
    document = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=DATA / filename,
        lazy=True,
        decode_config=DecodeConfig(keep_glyphs=keep_glyphs),
    )
    try:
        return document.get_page(page_no)
    finally:
        document.unload()


def test_newspaper_uses_local_dilation_for_word_boundaries():
    page = _page("newspaper-00.pdf", 1)
    words = [cell.text for cell in page.word_cells]
    lines = [cell.text for cell in page.textline_cells]

    start = words.index("Heute")
    assert words[start : start + 6] == ["Heute", "lest", "ihr", "die", "letzte", "Aus-"]
    assert "Heute lest ihr die letzte Aus-" in lines
    assert all(" " not in word for word in words)


def test_elsevier_reference_words_do_not_contract_together():
    page = _page("elsevier-00.pdf", 9)
    words = [cell.text for cell in page.word_cells]
    lines = [cell.text for cell in page.textline_cells]

    assert "References" in lines
    reference_start = words.index("Cristianini,")
    assert words[reference_start : reference_start + 6] == [
        "Cristianini,",
        "N.,",
        "Taylor,",
        "J.S.,",
        "2000.",
        "An",
    ]
    assert all(" " not in word for word in words)


def _rect(cell) -> tuple[float, ...]:
    return (
        cell.rect.r_x0,
        cell.rect.r_y0,
        cell.rect.r_x1,
        cell.rect.r_y1,
        cell.rect.r_x2,
        cell.rect.r_y2,
        cell.rect.r_x3,
        cell.rect.r_y3,
    )


def test_suppressed_glyphs_still_contribute_to_word_and_line_geometry():
    filename = "9acb62b4-4449-48d0-8127-f2be26349a6a-6.pdf"
    hidden = _page(filename, 1, keep_glyphs=False)
    visible = _page(filename, 1, keep_glyphs=True)

    visible_word_rects = {_rect(cell) for cell in visible.word_cells}
    visible_line_rects = {_rect(cell) for cell in visible.textline_cells}
    assert all(_rect(cell) in visible_word_rects for cell in hidden.word_cells)
    assert all(_rect(cell) in visible_line_rects for cell in hidden.textline_cells)
    assert all("GLYPH<" not in cell.text for cell in hidden.char_cells)
    assert all("GLYPH<" not in cell.text for cell in hidden.word_cells)
    assert all("GLYPH<" not in cell.text for cell in hidden.textline_cells)


def test_absolute_table_positions_break_words_even_when_the_line_has_spaces():
    sports = _page("11096950916667132630-5.pdf", 2)
    sports_words = [cell.text for cell in sports.word_cells]
    assert "SRZoe" not in sports_words
    assert "JRAmy" not in sports_words
    assert "Zoe" in sports_words
    assert "Amy" in sports_words

    safety = _page("10869285887791127292_005.pdf", 9)
    safety_words = [cell.text for cell in safety.word_cells]
    assert "hoursWater" not in safety_words
    assert "hoursRainbow" not in safety_words
    assert "hours" in safety_words
    assert "Water" in safety_words


def test_math_limits_contract_with_the_base_operator_geometry():
    page = _page("2508.13113v2.pdf", 4)
    words = [cell.text for cell in page.word_cells]

    assert "∑Kj=1" in words


def test_positioned_math_components_do_not_absorb_preceding_prose():
    page = _page("stream_parameter_misinterpretation_01.pdf", 1)
    words = [cell.text for cell in page.word_cells]

    assert words.count("where") == 2
    assert all(not word.startswith("where√") for word in words)
    assert any("√" in word and len(word) > 1 for word in words)


def test_decimal_codepoint_glyph_names_recover_cover_text():
    page = _page("298064347821956345-83.pdf", 1)
    words = [cell.text for cell in page.word_cells]

    assert "März" in words
    assert "piqueteros" in words
    assert "cacerolazos" in words
    assert "&" in words
    assert all("GLYPH<" not in cell.text for cell in page.char_cells)


def test_standard_symbol_glyph_indices_recover_table_characters():
    page = _page("PDF32000_2008.pdf", 678)
    characters = {cell.text for cell in page.char_cells}

    assert {"⊥", "φ", "ϕ", "∏", "∑", "√", "∪"} <= characters  # noqa: RUF001
    assert all("GLYPH<" not in cell.text for cell in page.char_cells)


def test_distant_vertical_figure_labels_do_not_contract():
    cases = [
        ("2203.01017v2.pdf", 1, {"22"}),
        ("2203.01017v2.pdf", 3, {"010K", "per.Graph"}),
        ("2206.01062.pdf", 3, {"8%Scientific", "Laws16%"}),
        ("2206.01062.pdf", 5, {"are:(1)", "ABCD"}),
    ]

    for filename, page_no, forbidden in cases:
        words = {cell.text for cell in _page(filename, page_no).word_cells}
        assert forbidden.isdisjoint(words)


def test_suppressed_only_aggregates_are_not_public_text_cells():
    for filename, page_no in [
        ("10976580690960943929_004.pdf", 4),
        ("16134591854657313890-2.pdf", 2),
        ("298064347821956345-83.pdf", 1),
        ("5462902444445925132-4.pdf", 3),
    ]:
        hidden = _page(filename, page_no, keep_glyphs=False)
        assert all(cell.text for cell in hidden.word_cells)
        assert all(cell.text for cell in hidden.textline_cells)


def test_visible_unmapped_glyphs_still_create_words_and_lines():
    page = _page("10976580690960943929_004.pdf", 1)

    assert len(page.word_cells) > 40
    assert len(page.textline_cells) > 10
    assert all(cell.text for cell in page.word_cells)
    assert all(cell.text for cell in page.textline_cells)
    assert any("\ufffd" in cell.text for cell in page.word_cells)
    assert all("GLYPH<" not in cell.text for cell in page.word_cells)
    assert all(cell.text.strip() for cell in page.word_cells)


def test_arabic_marks_contract_with_their_base_letter():
    """The /F13 Arial subset maps four codes to U+0020 (widths 278, 333, 333
    and 375). Selecting one of them by unordered-map iteration order made the
    reported space width platform dependent, and that width is the advance
    fallback for zero-advance combining marks, so the marks were split off
    their base letter on one platform and kept on the other."""
    page = _page("17791c05056ff856_0022.pdf", 1)
    words = [cell.text for cell in page.word_cells]

    assert "ْرا" in words  # sukun + reh + alef
    assert words.count("ْب") == 3  # sukun + beh
