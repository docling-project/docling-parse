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

    assert [_rect(cell) for cell in hidden.word_cells] == [
        _rect(cell) for cell in visible.word_cells
    ]
    assert [_rect(cell) for cell in hidden.textline_cells] == [
        _rect(cell) for cell in visible.textline_cells
    ]
    assert all("GLYPH<" not in cell.text for cell in hidden.char_cells)
    assert all("GLYPH<" not in cell.text for cell in hidden.word_cells)
    assert all("GLYPH<" not in cell.text for cell in hidden.textline_cells)
