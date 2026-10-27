#!/usr/bin/env python
"""A failed load must say why, so a caller can tell a password from a broken file.

qpdf reports why it refused a document: an invalid password, a file it could
not repair, an unsupported feature. The loaders discarded that and every
failure surfaced as the same `RuntimeError("Failed to load document with key
...")`, so docling's default backend could not distinguish an encrypted PDF
from a corrupt one (docling#4829).

The parsers now keep qpdf's error code and message when a load fails and
raise PdfLoadError, or PdfPasswordError for an encrypted document. Both are
subclasses of RuntimeError, so handlers written for the old error keep
working, and the message keeps its old prefix.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    PdfLoadError,
    PdfLoadErrorCategory,
    PdfPasswordError,
)
from tests.pdf_builder import simple_page_pdf

GARBAGE = b"%PDF-1.4\nthis is not a valid pdf at all"


@pytest.fixture(autouse=True)
def quiet_logging():
    """Silence the qpdf/loguru error stream of the deliberately broken fixtures."""
    DoclingPdfParser(loglevel="fatal")


def _plain_pdf() -> bytes:
    return simple_page_pdf(
        "BT /F1 12 Tf 20 100 Td (ok) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        extra_objects=["<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"],
    )


def _encrypted_pdf(password: str) -> bytes:
    pypdf = pytest.importorskip("pypdf", reason="pypdf writes the encrypted fixture")

    writer = pypdf.PdfWriter(clone_from=BytesIO(_plain_pdf()))
    writer.encrypt(password)

    out = BytesIO()
    writer.write(out)
    return out.getvalue()


def _plain_parser():
    return DoclingPdfParser(loglevel="fatal")


def _threaded_parser():
    return DoclingThreadedPdfParser()


PARSERS = pytest.mark.parametrize(
    "make_parser", [_plain_parser, _threaded_parser], ids=["plain", "threaded"]
)


def _load(parser, pdf: bytes, password: str | None = None):
    return parser.load(BytesIO(pdf), password=password)


def _page_count(parser, loaded) -> int:
    if isinstance(parser, DoclingThreadedPdfParser):
        return parser.page_count(loaded)
    return loaded.number_of_pages()


@PARSERS
def test_encrypted_document_without_password(make_parser):
    parser = make_parser()

    with pytest.raises(PdfPasswordError) as exc_info:
        _load(parser, _encrypted_pdf("secret"))

    exc = exc_info.value
    assert exc.category is PdfLoadErrorCategory.PASSWORD
    assert exc.password_supplied is False
    assert "requires a password" in str(exc)
    assert exc.detail  # qpdf's own wording is kept for diagnostics


@PARSERS
def test_encrypted_document_with_wrong_password(make_parser):
    parser = make_parser()

    with pytest.raises(PdfPasswordError) as exc_info:
        _load(parser, _encrypted_pdf("secret"), password="not-it")

    exc = exc_info.value
    assert exc.password_supplied is True
    assert "incorrect" in str(exc)


@PARSERS
def test_encrypted_document_with_the_right_password_loads(make_parser):
    parser = make_parser()

    loaded = _load(parser, _encrypted_pdf("secret"), password="secret")

    assert _page_count(parser, loaded) == 1


@PARSERS
def test_damaged_document_is_not_mistaken_for_a_password_problem(make_parser):
    parser = make_parser()

    with pytest.raises(PdfLoadError) as exc_info:
        _load(parser, GARBAGE)

    exc = exc_info.value
    assert not isinstance(exc, PdfPasswordError)
    assert exc.category is PdfLoadErrorCategory.DAMAGED
    assert exc.detail
    # the old generic prefix and type survive for existing handlers
    assert isinstance(exc, RuntimeError)
    assert str(exc).startswith("Failed to load document with key ")


@PARSERS
def test_missing_file_is_an_io_failure(make_parser, tmp_path):
    parser = make_parser()
    missing = tmp_path / "missing.pdf"

    with pytest.raises(PdfLoadError) as exc_info:
        parser.load(str(missing))

    exc = exc_info.value
    assert exc.category is PdfLoadErrorCategory.IO
    assert "missing.pdf" in str(exc)


@PARSERS
def test_a_failure_does_not_taint_the_next_load_of_the_same_bytes(make_parser):
    parser = make_parser()
    pdf = _encrypted_pdf("secret")

    with pytest.raises(PdfPasswordError):
        _load(parser, pdf)

    loaded = _load(parser, pdf, password="secret")

    assert _page_count(parser, loaded) == 1
