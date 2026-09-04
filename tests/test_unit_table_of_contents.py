"""Focused tests for page-aware PDF outlines."""

from pathlib import Path

import pytest

from docling_parse.pdf_parser import DoclingPdfParser, DoclingThreadedPdfParser


def _make_outlined_pdf(path: Path) -> None:
    canvas_module = pytest.importorskip("reportlab.pdfgen.canvas")
    canvas = canvas_module.Canvas(str(path))
    for key, title, level in (("page1", "Chapter 1", 0), ("page2", "Section 1.1", 1)):
        canvas.bookmarkPage(key)
        canvas.addOutlineEntry(title, key, level=level)
        canvas.drawString(72, 720, title)
        canvas.showPage()
    canvas.save()


def test_sequential_outline_exposes_nested_target_pages(tmp_path: Path) -> None:
    path = tmp_path / "outlined.pdf"
    _make_outlined_pdf(path)

    document = DoclingPdfParser(loglevel="fatal").load(path, lazy=True)
    try:
        toc = document.get_table_of_contents()
        assert toc is not None
        assert [(entry.text, entry.page) for entry in toc.children] == [
            ("Chapter 1", 0)
        ]
        assert [(entry.text, entry.page) for entry in toc.children[0].children] == [
            ("Section 1.1", 1)
        ]
    finally:
        document.unload()


def test_threaded_outline_matches_sequential_outline(tmp_path: Path) -> None:
    path = tmp_path / "outlined.pdf"
    _make_outlined_pdf(path)

    parser = DoclingThreadedPdfParser()
    key = parser.load(path)
    try:
        toc = parser.get_table_of_contents(key)
        assert toc is not None
        assert [(entry.title, entry.page, entry.level) for entry in toc] == [
            ("Chapter 1", 0, 0),
            ("Section 1.1", 1, 1),
        ]
    finally:
        parser.unload(key)
