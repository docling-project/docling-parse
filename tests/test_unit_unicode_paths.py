#!/usr/bin/env python
"""Loading PDFs from paths that contain non-ASCII characters.

The filename travels UTF-8 encoded from Python into the native parser, which
must open it with UTF-8 semantics on every platform. On Windows a narrow
byte-path open goes through the ANSI codepage instead, so any path with
characters outside it fails to load (docling-parse#324) — on POSIX the bytes
pass through and the bug is invisible, so these tests guard intent everywhere
but only bite on Windows runners.
"""

from io import BytesIO

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import build_pdf

CONTENT = "BT /F1 12 Tf 72 720 Td (Title Case Workers) Tj ET"


def _tiny_pdf() -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            f"<< /Length {len(CONTENT)} >>\nstream\n{CONTENT}\nendstream",
        ]
    )


def test_load_from_non_ascii_path(tmp_path):
    """A path with CJK directory and filename components loads like any other."""
    cjk_dir = tmp_path / "导出的条目"
    cjk_dir.mkdir()
    pdf_path = cjk_dir / "2005 - Chiou 等 - test.pdf"
    pdf_path.write_bytes(_tiny_pdf())

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=str(pdf_path))

    assert doc.number_of_pages() == 1


def test_load_from_bytesio_is_path_independent():
    """The BytesIO route never touches the filesystem and must always work."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(_tiny_pdf()))

    assert doc.number_of_pages() == 1
