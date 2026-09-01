#!/usr/bin/env python
"""Load failures must be reported, not deferred as a -1 page count.

When qpdf cannot parse a document, the C++ decoder returns false with its
number_of_pages still at the -1 sentinel. The pybind loaders used to ignore
that return value and report success, so the failure only surfaced later as
number_of_pages() == -1 — which downstream docling propagated as
"Inconsistent number of pages: N!=-1" and a silently rejected document
(docling-parse#239, docling#3031).

load() must raise at load time, and the failed key must not linger in the
parser (is_loaded() reporting true for a document that never parsed).
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DoclingPdfParser, DoclingThreadedPdfParser

GARBAGE = b"%PDF-1.4\nthis is not a valid pdf at all"


def _minimal_pdf() -> bytes:
    content = "BT /F1 24 Tf 72 700 Td (ok) Tj ET"
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]

    out = b"%PDF-1.4\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


def test_bytesio_garbage_raises_at_load():
    parser = DoclingPdfParser(loglevel="fatal")
    with pytest.raises(RuntimeError, match="Failed to load document"):
        parser.load(path_or_stream=BytesIO(GARBAGE))


def test_file_garbage_raises_at_load(tmp_path):
    bad = tmp_path / "garbage.pdf"
    bad.write_bytes(GARBAGE)

    parser = DoclingPdfParser(loglevel="fatal")
    with pytest.raises(RuntimeError, match="Failed to load document"):
        parser.load(path_or_stream=str(bad))


def test_failed_load_leaves_no_stale_key():
    parser = DoclingPdfParser(loglevel="fatal")
    with pytest.raises(RuntimeError):
        parser.load(path_or_stream=BytesIO(GARBAGE))

    # a decoder that never parsed must not stay registered: it would report
    # is_loaded()=true and number_of_pages()=-1
    assert parser.list_loaded_keys() == []


def test_valid_document_still_loads():
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(_minimal_pdf()))
    assert doc.number_of_pages() == 1


def test_threaded_bytesio_garbage_raises_at_load():
    parser = DoclingThreadedPdfParser()
    with pytest.raises(RuntimeError, match="Failed to load document"):
        parser.load(path_or_stream=BytesIO(GARBAGE))


def test_threaded_valid_document_still_loads():
    parser = DoclingThreadedPdfParser()
    key = parser.load(path_or_stream=BytesIO(_minimal_pdf()))
    assert parser.page_count(key) == 1
