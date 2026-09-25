#!/usr/bin/env python
"""Counting pages must not require loading the document.

`pdf_parser.get_number_of_pages` and `get_number_of_pages_from_bytesio` are
static: they open a local QPDF, walk the page tree and drop it again, without
registering a decoder or touching any page content. The point is that callers
who only need the page count pay for the page tree and nothing else, so the
tests here check the count itself, that a broken document reports the -1
sentinel rather than raising, and that the count agrees with what a full load
reports for the same bytes.
"""

from io import BytesIO
from pathlib import Path

import pytest
from docling_parse.pdf_parsers import pdf_parser  # type: ignore[import]

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import build_pdf, content_stream

GARBAGE = b"%PDF-1.4\nthis is not a valid pdf at all"

PAGE_COUNTS = [1, 3, 7]


@pytest.fixture(autouse=True)
def quiet_logging():
    """Silence the qpdf/loguru error stream of the deliberately broken fixtures.

    loguru's verbosity is process-global and the static counters have no
    instance to configure, so a parser is constructed purely for its side
    effect on that global.
    """
    DoclingPdfParser(loglevel="fatal")


def multi_page_pdf(number_of_pages: int) -> bytes:
    """A document of `number_of_pages` flat pages, each with its own content.

    Objects: 1 catalog, 2 page-tree root, 3..2+N the pages, 3+N..2+2N their
    content streams.
    """
    first_page = 3
    first_content = first_page + number_of_pages

    kids = " ".join(f"{first_page + i} 0 R" for i in range(number_of_pages))
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        f"<< /Type /Pages /Kids [{kids}] /Count {number_of_pages} >>",
    ]
    objects.extend(
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        f"/Contents {first_content + i} 0 R >>"
        for i in range(number_of_pages)
    )
    objects.extend(
        content_stream(f"0 0 1 rg 10 {10 + 10 * i} 50 20 re f")
        for i in range(number_of_pages)
    )
    return build_pdf(objects)


def nested_page_tree_pdf() -> bytes:
    """Three pages behind two intermediate /Pages nodes, with a lying /Count.

    The count comes from walking the tree (as qpdf's getAllPages does), not
    from trusting /Count on the root, and this fixture is the difference: a
    reader of /Count alone answers 99.
    """
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 99 >>",
        "<< /Type /Pages /Parent 2 0 R /Kids [5 0 R 6 0 R] /Count 2 >>",
        "<< /Type /Pages /Parent 2 0 R /Kids [7 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 3 0 R /MediaBox [0 0 200 200] /Contents 8 0 R >>",
        "<< /Type /Page /Parent 3 0 R /MediaBox [0 0 200 200] /Contents 8 0 R >>",
        "<< /Type /Page /Parent 4 0 R /MediaBox [0 0 200 200] /Contents 8 0 R >>",
        content_stream("0 0 1 rg 10 10 50 20 re f"),
    ]
    return build_pdf(objects)


def write_pdf(tmp_path: Path, name: str, payload: bytes) -> str:
    path = tmp_path / name
    path.write_bytes(payload)
    return str(path)


@pytest.mark.parametrize("number_of_pages", PAGE_COUNTS)
def test_page_count_from_file(tmp_path, number_of_pages):
    filename = write_pdf(
        tmp_path, f"{number_of_pages}p.pdf", multi_page_pdf(number_of_pages)
    )

    assert pdf_parser.get_number_of_pages(filename) == number_of_pages


@pytest.mark.parametrize("number_of_pages", PAGE_COUNTS)
def test_page_count_from_bytesio(number_of_pages):
    stream = BytesIO(multi_page_pdf(number_of_pages))

    assert pdf_parser.get_number_of_pages_from_bytesio(stream) == number_of_pages


@pytest.mark.parametrize("number_of_pages", PAGE_COUNTS)
def test_matches_a_full_load(number_of_pages):
    """The cheap count and the loaded document must never disagree."""
    payload = multi_page_pdf(number_of_pages)

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(payload))

    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(payload)) == (
        doc.number_of_pages()
    )


def test_file_and_bytesio_agree(tmp_path):
    payload = multi_page_pdf(4)
    filename = write_pdf(tmp_path, "four.pdf", payload)

    assert pdf_parser.get_number_of_pages(filename) == 4
    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(payload)) == 4


def test_nested_page_tree_is_walked():
    assert (
        pdf_parser.get_number_of_pages_from_bytesio(BytesIO(nested_page_tree_pdf()))
        == 3
    )


def test_counting_leaves_the_parser_untouched():
    """No decoder is registered: the static call has no document state."""
    parser = DoclingPdfParser(loglevel="fatal")

    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(multi_page_pdf(2))) == 2
    assert parser.list_loaded_keys() == []


def test_bytesio_is_rewound_before_reading():
    """A stream already read to the end is still counted, and the caller's own
    read position is not what the count depends on."""
    stream = BytesIO(multi_page_pdf(3))
    stream.read()

    assert pdf_parser.get_number_of_pages_from_bytesio(stream) == 3


def test_garbage_file_returns_the_sentinel(tmp_path):
    filename = write_pdf(tmp_path, "garbage.pdf", GARBAGE)

    assert pdf_parser.get_number_of_pages(filename) == -1


def test_garbage_bytesio_returns_the_sentinel():
    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(GARBAGE)) == -1


def test_missing_file_returns_the_sentinel(tmp_path):
    assert pdf_parser.get_number_of_pages(str(tmp_path / "does-not-exist.pdf")) == -1


def test_empty_bytesio_returns_the_sentinel():
    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(b"")) == -1


def test_non_stream_argument_raises():
    """bytes has no read(): that is a caller error, not a broken document."""
    with pytest.raises(RuntimeError, match="Expected a BytesIO object"):
        pdf_parser.get_number_of_pages_from_bytesio(multi_page_pdf(1))


def test_unicode_filename(tmp_path):
    """The filename reaches qpdf as UTF-8 (docling-parse#324)."""
    filename = write_pdf(tmp_path, "página-ünïcode-文書.pdf", multi_page_pdf(2))

    assert pdf_parser.get_number_of_pages(filename) == 2


def _encrypted_pdf(password: str, number_of_pages: int) -> bytes:
    pypdf = pytest.importorskip("pypdf", reason="pypdf writes the encrypted fixture")

    writer = pypdf.PdfWriter(clone_from=BytesIO(multi_page_pdf(number_of_pages)))
    writer.encrypt(password)

    out = BytesIO()
    writer.write(out)
    return out.getvalue()


def test_encrypted_document_with_password(tmp_path):
    payload = _encrypted_pdf("secret", 3)
    filename = write_pdf(tmp_path, "encrypted.pdf", payload)

    assert pdf_parser.get_number_of_pages(filename, password="secret") == 3
    assert (
        pdf_parser.get_number_of_pages_from_bytesio(BytesIO(payload), password="secret")
        == 3
    )


def test_encrypted_document_without_password(tmp_path):
    payload = _encrypted_pdf("secret", 3)
    filename = write_pdf(tmp_path, "encrypted.pdf", payload)

    assert pdf_parser.get_number_of_pages(filename) == -1
    assert pdf_parser.get_number_of_pages_from_bytesio(BytesIO(payload)) == -1
