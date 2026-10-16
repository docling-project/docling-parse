#!/usr/bin/env python
"""The /AcroForm export of a signed, tagged document stays inside the form.

A certification signature carries /Reference[0]/Data, which Acrobat and iText
point back at the catalog. Serialising the form used to follow that reference
into the whole document, and from the structure tree back into every page
once per element that named it, so loading a 90-page signed file took minutes
and gigabytes (docling-parse#399). The fixture is the smallest document with
that shape: one signature field, one structure element, one page with a font.
"""

import json
from io import BytesIO

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream


def _signed_tagged_pdf() -> bytes:
    return build_pdf(
        [
            # 1: the form, next to the structure tree it must not drag in
            "<< /Type /Catalog /Pages 2 0 R /MarkInfo << /Marked true >> "
            "/StructTreeRoot 7 0 R /AcroForm << /Fields [5 0 R] /SigFlags 3 >> >>",
            # 2
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            # 3
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
            "/Resources << /Font << /F1 9 0 R >> >> /Annots [5 0 R] /StructParents 0 >>",
            # 4
            content_stream("BT /F1 12 Tf 20 100 Td (signed) Tj ET"),
            # 5: the signature field, merged with its widget
            "<< /FT /Sig /T (Signature1) /Type /Annot /Subtype /Widget /F 4 "
            "/Rect [0 0 0 0] /P 3 0 R /V 6 0 R >>",
            # 6: the signature; /Data is the catalog, as Acrobat and iText write it
            "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
            "/ByteRange [0 0 0 0] /Contents <3030> /M (D:20261016120000Z) "
            "/Reference [<< /Type /SigRef /TransformMethod /DocMDP "
            "/TransformParams << /Type /TransformParams /P 2 /V /1.2 >> "
            "/DigestMethod /SHA256 /Data 1 0 R >>] >>",
            # 7
            "<< /Type /StructTreeRoot /K [8 0 R] /ParentTree << /Nums [0 [8 0 R]] >> >>",
            # 8
            "<< /Type /StructElem /S /P /P 7 0 R /Pg 3 0 R /K [0] >>",
            # 9
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        ]
    )


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


def test_threaded_parser_exports_the_same_form():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=1, max_concurrent_results=2
        )
    )
    doc_key = parser.load(BytesIO(_signed_tagged_pdf()), page_numbers=[1])

    # ask for the form after the workers have started: it is built on request now
    result = next(parser.iterate_results())
    assert result.success, result.error_message

    annotations = parser.get_annotations(doc_key)
    assert annotations is not None
    _assert_form_stays_inside_the_form(annotations.form)
    parser.unload_all()
