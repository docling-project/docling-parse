#!/usr/bin/env python
"""Targeted regressions for adaptive character-to-word contraction."""

from pathlib import Path

from docling_parse.pdf_parser import DoclingPdfParser


DATA = Path(__file__).parent / "data" / "regression"


def _page(filename: str, page_no: int):
    document = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=DATA / filename,
        lazy=True,
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
