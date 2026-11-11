#!/usr/bin/env python
"""The /AcroForm export of a signed, tagged document stays inside the form.

A certification signature carries /Reference[0]/Data, which Acrobat and iText
point back at the catalog. Serialising the form used to follow that reference
into the whole document, and from the structure tree back into every page
once per element that named it, so loading a 90-page signed file took minutes
and gigabytes (docling-parse#399). The fixture is the smallest document with
that shape: one signature field, one structure element per page, pages with a
font.
"""

import json
import threading
from io import BytesIO

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream


def _signed_tagged_pdf(number_of_pages: int = 1) -> bytes:
    # object numbers, in the order build_pdf assigns them
    first_page = 3
    first_content = first_page + number_of_pages
    field = first_content + number_of_pages
    signature = field + 1
    struct_tree_root = signature + 1
    first_element = struct_tree_root + 1
    font = first_element + number_of_pages

    def page(i: int) -> str:
        annots = f" /Annots [{field} 0 R]" if i == 0 else ""
        return (
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            f"/Contents {first_content + i} 0 R "
            f"/Resources << /Font << /F1 {font} 0 R >> >>{annots} "
            f"/StructParents {i} >>"
        )

    kids = " ".join(f"{first_page + i} 0 R" for i in range(number_of_pages))
    elements = " ".join(f"{first_element + i} 0 R" for i in range(number_of_pages))
    parent_tree = " ".join(
        f"{i} [{first_element + i} 0 R]" for i in range(number_of_pages)
    )

    objects: list[str | bytes] = [
        # 1: the form, next to the structure tree it must not drag in
        "<< /Type /Catalog /Pages 2 0 R /MarkInfo << /Marked true >> "
        f"/StructTreeRoot {struct_tree_root} 0 R "
        f"/AcroForm << /Fields [{field} 0 R] /SigFlags 3 >> >>",
        # 2
        f"<< /Type /Pages /Kids [{kids}] /Count {number_of_pages} >>",
    ]
    objects.extend(page(i) for i in range(number_of_pages))
    objects.extend(
        content_stream("BT /F1 12 Tf 20 100 Td (signed) Tj ET")
        for _ in range(number_of_pages)
    )
    objects.extend(
        [
            # the signature field, merged with its widget on the first page
            "<< /FT /Sig /T (Signature1) /Type /Annot /Subtype /Widget /F 4 "
            f"/Rect [0 0 0 0] /P {first_page} 0 R /V {signature} 0 R >>",
            # the signature; /Data is the catalog, as Acrobat and iText write it
            "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
            "/ByteRange [0 0 0 0] /Contents <3030> /M (D:20261016120000Z) "
            "/Reference [<< /Type /SigRef /TransformMethod /DocMDP "
            "/TransformParams << /Type /TransformParams /P 2 /V /1.2 >> "
            "/DigestMethod /SHA256 /Data 1 0 R >>] >>",
            f"<< /Type /StructTreeRoot /K [{elements}] "
            f"/ParentTree << /Nums [{parent_tree}] >> >>",
        ]
    )
    objects.extend(
        f"<< /Type /StructElem /S /P /P {struct_tree_root} 0 R "
        f"/Pg {first_page + i} 0 R /K [0] >>"
        for i in range(number_of_pages)
    )
    objects.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    assert len(objects) == font
    return build_pdf(objects)


def _assert_form_stays_inside_the_form(form: dict) -> None:
    (field,) = form["/Fields"]
    assert field["/FT"] == "/Sig"
    assert field["/T"] == "Signature1"
    assert field["/P"] == "[skipping /P]"

    signature = field["/V"]
    assert signature["/Filter"] == "/Adobe.PPKLite"
    assert signature["/ByteRange"] == [0, 0, 0, 0]

    (reference,) = signature["/Reference"]
    assert reference["/TransformMethod"] == "/DocMDP"
    # /P is skipped wherever it occurs, so the permission level is not exported
    assert reference["/TransformParams"]["/V"] == "/1.2"
    assert reference["/Data"] == "[skipping /Catalog object '1 0 R']"

    # nothing from behind the catalog: no page tree, no page resources, no
    # structure tree
    dumped = json.dumps(form)
    for key in ("/Pages", "/Kids", "/Resources", "/BaseFont", "/StructTreeRoot", "/Pg"):
        assert key not in dumped, key


def _form(parser: DoclingThreadedPdfParser, doc_key: str) -> dict:
    annotations = parser.get_annotations(doc_key)
    assert annotations is not None
    assert annotations.form is not None
    return annotations.form


def test_signature_reference_does_not_expand_the_document():
    pdf_doc = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=BytesIO(_signed_tagged_pdf()), lazy=True
    )
    annotations = pdf_doc.get_annotations()
    assert annotations is not None

    _assert_form_stays_inside_the_form(annotations.form)

    # the page behind the signature still decodes as before
    page = pdf_doc.get_page(1)
    assert [cell.text for cell in page.textline_cells] == ["signed"]
    pdf_doc.unload()


def test_annotations_are_served_while_the_workers_copy_pages():
    """The form is requested while several workers copy pages out of the document.

    Once from the main thread for every result, and continuously from a second
    thread: the first request contends with a page write, the cached ones must
    not queue behind one, and none of them may disturb the pages.
    """
    number_of_pages = 12
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=4, max_concurrent_results=2
        )
    )
    doc_key = parser.load(BytesIO(_signed_tagged_pdf(number_of_pages)))

    results = parser.iterate_results()
    # the first result starts the workers: from here on, pages are being
    # copied out of the document while the annotations are requested
    pages = [next(results)]

    done = threading.Event()
    failures: list[Exception] = []

    def request_annotations_continuously() -> None:
        try:
            while not done.is_set():
                _assert_form_stays_inside_the_form(_form(parser, doc_key))
        except Exception as exc:  # reported on the main thread below
            failures.append(exc)

    requester = threading.Thread(target=request_annotations_continuously)
    requester.start()
    try:
        for result in results:
            pages.append(result)
            _assert_form_stays_inside_the_form(_form(parser, doc_key))
    finally:
        done.set()
        requester.join()

    assert not failures, failures
    assert sorted(page.page_number for page in pages) == list(
        range(1, number_of_pages + 1)
    )
    for page in pages:
        assert page.success, page.error_message
        assert [cell.text for cell in page.get_page().textline_cells] == ["signed"]
    parser.unload_all()
