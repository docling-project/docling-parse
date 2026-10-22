#!/usr/bin/env python
"""A page whose content the file no longer contains must not parse as success.

A PDF cut short by a failed transfer keeps the page dictionaries written
before the cut while their content streams are gone. qpdf reconstructs the
cross-reference table and recovers a document with the full page count, and
every reference to a lost object resolves to null, as the PDF specification
requires. The parser used to decode such a page as a successful blank page:
`success=True`, an empty error message and zero cells, so two thirds of a
document could vanish without any signal (docling-parse#374).

The decoder now checks the page's own /Contents and /Resources references
against the original document before it decodes the page. The threaded
parser reports the page with `success=False` and an error message, the
non-threaded parser raises on access to that page. Legitimately blank pages
(no /Contents, a literal null, an empty array) stay successful, and a file
whose startxref is wrong but whose objects are all present still yields every
page: qpdf repairs it and nothing is missing.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream

# Object numbers in the fixtures below: 1 catalog, 2 pages, 3 font, then one
# page object per page, then one content stream per page. The content streams
# come last so a cut at 60% removes them while every page dictionary survives.
FONT = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"


def _multi_page(number_of_pages: int, *, contents: str = "stream") -> bytes:
    first_page = 4
    first_content = first_page + number_of_pages

    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids ["
        + " ".join(f"{first_page + i} 0 R" for i in range(number_of_pages))
        + f"] /Count {number_of_pages} >>",
        FONT,
    ]
    for i in range(number_of_pages):
        if contents == "stream":
            contents_entry = f" /Contents {first_content + i} 0 R"
        elif contents == "absent":
            contents_entry = ""
        elif contents == "null":
            contents_entry = " /Contents null"
        elif contents == "empty":
            contents_entry = " /Contents []"
        else:
            raise ValueError(contents)
        objects.append(
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200]"
            f" /Resources << /Font << /F1 3 0 R >> >>{contents_entry} >>"
        )
    for i in range(number_of_pages):
        objects.append(
            content_stream(f"BT /F1 12 Tf 20 100 Td (PAGE {i + 1} CONTENT) Tj ET")
        )
    return build_pdf(objects)


def _cut_at_60_percent(pdf: bytes) -> bytes:
    """The cut from the report: inside the header of object 8, the second
    content stream, so objects 8 and 9 are gone and object 7 survives."""
    return pdf[: int(len(pdf) * 0.6)]


def _cut_inside_header_of(pdf: bytes, object_number: int) -> bytes:
    """Cut a few bytes into `N 0 obj`, so that object and all later ones are gone.

    The cut must not fall exactly on an object boundary: qpdf's reconstruction
    discards an object that ends at end-of-file ("EOF after endobj"), which
    would lose the preceding object as well.
    """
    return pdf[: pdf.index(f"{object_number} 0 obj".encode()) + 5]


def _threaded_results(pdf: bytes):
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=1, max_concurrent_results=4
        )
    )
    try:
        parser.load(BytesIO(pdf))
        return sorted(parser.iterate_results(), key=lambda r: r.page_number)
    finally:
        parser.unload_all()


def _cells(result) -> int:
    return len(result.get_page().char_cells)


def test_complete_document_parses_every_page():
    results = _threaded_results(_multi_page(3))

    assert [r.page_number for r in results] == [1, 2, 3]
    assert all(r.success for r in results)
    assert all(_cells(r) > 0 for r in results)


def test_truncated_document_reports_the_lost_pages():
    results = _threaded_results(_cut_at_60_percent(_multi_page(3)))

    # qpdf still recovers all three page dictionaries ...
    assert [r.page_number for r in results] == [1, 2, 3]

    # ... the first page keeps its content ...
    assert results[0].success
    assert _cells(results[0]) > 0

    # ... and the pages whose content streams were cut off are failures,
    # not successful blank pages.
    for result, content_object in zip(results[1:], (8, 9)):
        assert not result.success
        assert f"/Contents {content_object} 0 R" in result.error_message
        assert "truncated or damaged" in result.error_message


def test_truncated_document_raises_on_the_lost_page_when_not_threaded():
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(BytesIO(_cut_at_60_percent(_multi_page(3))))

    assert doc.number_of_pages() == 3
    assert len(doc.get_page(1).char_cells) > 0

    with pytest.raises(RuntimeError, match=r"/Contents 8 0 R.*truncated or damaged"):
        doc.get_page(2)


def test_single_page_document_cut_before_its_content():
    (result,) = _threaded_results(_cut_inside_header_of(_multi_page(1), 5))

    assert not result.success
    assert "/Contents 5 0 R" in result.error_message


@pytest.mark.parametrize("contents", ["absent", "null", "empty"])
def test_pages_without_content_are_valid_blank_pages(contents):
    results = _threaded_results(_multi_page(2, contents=contents))

    assert all(r.success for r in results)
    assert all(_cells(r) == 0 for r in results)


def test_repaired_document_with_all_objects_present_keeps_every_page():
    pdf = _multi_page(3)
    head, _, tail = pdf.rpartition(b"startxref\n")
    offset, _, rest = tail.partition(b"\n")
    wrong = head + b"startxref\n" + str(int(offset) + 7).encode() + b"\n" + rest

    results = _threaded_results(wrong)

    assert all(r.success for r in results)
    assert all(_cells(r) > 0 for r in results)


def _single_page(
    resources_entry: str,
    trailing_objects: list[str | bytes],
    *,
    parent_resources: str = "",
) -> bytes:
    """Catalog, page tree, page and content stream (objects 1-4), then
    `trailing_objects` as objects 5, 6, ... which a cut can take away."""
    pages_entry = f" /Resources {parent_resources}" if parent_resources else ""
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            f"<< /Type /Pages /Kids [3 0 R] /Count 1{pages_entry} >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200]"
            f" /Resources {resources_entry} /Contents 4 0 R >>",
            content_stream("BT /F1 12 Tf 20 100 Td (PAGE 1 CONTENT) Tj ET"),
            *trailing_objects,
        ]
    )


def test_lost_resources_object_is_reported():
    # the content survives the cut, the resources dictionary (5) and the
    # font it names (6) do not
    pdf = _single_page("5 0 R", ["<< /Font << /F1 6 0 R >> >>", FONT])

    (result,) = _threaded_results(_cut_inside_header_of(pdf, 5))

    assert not result.success
    assert "/Resources 5 0 R" in result.error_message


def test_resources_inherited_from_the_page_tree_are_accepted():
    # the page's own resources dictionary (6) is lost, the font (5) and the
    # resources the page tree hands down survive
    pdf = _single_page(
        "6 0 R",
        [FONT, "<< /Font << /F1 5 0 R >> >>"],
        parent_resources="<< /Font << /F1 5 0 R >> >>",
    )

    (result,) = _threaded_results(_cut_inside_header_of(pdf, 6))

    assert result.success
    assert _cells(result) > 0


def test_lost_font_object_is_reported():
    # the decoder would otherwise skip the text set in the unknown font and
    # report a successful page with no cells
    pdf = _single_page("<< /Font << /F1 5 0 R >> >>", [FONT])

    (result,) = _threaded_results(_cut_inside_header_of(pdf, 5))

    assert not result.success
    assert "/Resources/Font/F1 5 0 R" in result.error_message
