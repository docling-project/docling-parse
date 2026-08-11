#!/usr/bin/env python
"""Minimal pypdf-shadow tests and report-only text comparison against pypdf."""

from io import BytesIO
from pathlib import Path

import pytest

from docling_parse import PdfReader, PdfWriter
from tests.constants import SAMPLE_PDF
from tests.test_pypdfium_parse import (
    _collect_text_comparisons,
    _print_text_report,
)


def test_pdf_reader_extract_text_plain_and_layout():
    reader = PdfReader(SAMPLE_PDF)

    assert len(reader.pages) > 0
    page = reader.pages[0]

    plain = page.extract_text()
    assert isinstance(plain, str)
    assert plain

    up = page.extract_text(0)
    assert isinstance(up, str)

    up_and_left = page.extract_text((0, 90))
    assert isinstance(up_and_left, str)

    layout = page.extract_text(extraction_mode="layout")
    assert isinstance(layout, str)
    assert layout


def test_pdf_reader_accepts_bytesio():
    data = Path(SAMPLE_PDF).read_bytes()
    reader = PdfReader(BytesIO(data))

    assert len(reader.pages) > 0
    assert reader.pages[0].extract_text()


def test_pdf_reader_rejects_invalid_orientation():
    reader = PdfReader(SAMPLE_PDF)

    with pytest.raises(ValueError):
        reader.pages[0].extract_text(45)


def test_pdf_writer_append_write_roundtrip(tmp_path):
    output = tmp_path / "merged.pdf"

    writer = PdfWriter()
    writer.append(SAMPLE_PDF, pages=(0, 1))
    writer.write(output)

    roundtrip = PdfReader(output)
    assert len(roundtrip.pages) == 1
    assert roundtrip.pages[0].extract_text()


def test_pdf_writer_add_page_from_reader(tmp_path):
    output = tmp_path / "single-page.pdf"
    reader = PdfReader(SAMPLE_PDF)

    writer = PdfWriter()
    writer.add_page(reader.pages[0])
    writer.write(output)

    roundtrip = PdfReader(output)
    assert len(roundtrip.pages) == 1


def test_pdf_writer_form_field_methods_are_native_placeholders():
    writer = PdfWriter()

    with pytest.raises(NotImplementedError):
        writer.add_form_field(0, "name", "text", (0, 0, 100, 20))


def extract_pypdf_text(pdf_path: Path, page_no: int) -> str:
    from pypdf import PdfReader as PyPdfReader

    reader = PyPdfReader(str(pdf_path))
    text = reader.pages[page_no - 1].extract_text()
    return text or ""


def test_line_text_matches_pypdf() -> None:
    """Report docling-parse line text similarity against pypdf."""
    pytest.importorskip("pypdf", reason="pypdf is required for the pypdf comparison")

    comparisons, skipped = _collect_text_comparisons(
        reference_label="pypdf",
        extract_reference=extract_pypdf_text,
    )

    _print_text_report(comparisons, skipped, reference_label="pypdf")
    assert len(comparisons) > 0, "no page could be compared against pypdf"
