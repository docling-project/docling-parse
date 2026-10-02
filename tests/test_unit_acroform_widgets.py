#!/usr/bin/env python
"""AcroForm widget extraction across sequential and threaded parsers."""

from io import BytesIO

import pytest
from docling_core.types.doc.page import PdfPageBoundaryType, PdfWidget

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream


def _form_pdf() -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm "
            "<< /Fields [5 0 R 7 0 R 8 0 R 9 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Contents 4 0 R /Annots [6 0 R 7 0 R 8 0 R 9 0 R 10 0 R] >>",
            content_stream(""),
            "<< /FT /Tx /T (contact) /TU (Full name) /Ff 2 "
            "/V (Ada Lovelace) /Kids [6 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /Parent 5 0 R "
            "/T (name) /Rect [10 160 100 180] >>",
            "<< /Type /Annot /Subtype /Widget /FT /Btn /T (checked) "
            "/Rect [10 120 30 140] /V /Yes /AS /Yes >>",
            "<< /Type /Annot /Subtype /Widget /FT /Btn /T (unchecked) "
            "/Rect [10 80 30 100] /V /Off /AS /Off >>",
            "<< /Type /Annot /Subtype /Widget /FT /Btn /T (appearance_only) "
            "/Rect [10 40 30 60] /AS /On >>",
            "<< /Type /Annot /Subtype /Link /Rect [120 160 190 180] "
            "/A << /S /URI /URI (https://example.com) >> >>",
        ]
    )


def _widget_data(widget: PdfWidget) -> dict[str, object]:
    return {
        "rect": (
            widget.rect.r_x0,
            widget.rect.r_y0,
            widget.rect.r_x2,
            widget.rect.r_y2,
        ),
        "text": widget.widget_text,
        "description": widget.widget_description,
        "field_name": widget.widget_field_name,
        "field_type": widget.widget_field_type,
        "field_flags": widget.widget_field_flags,
        "appearance_state": widget.widget_appearance_state,
    }


def test_acroform_widget_contract_matches_threaded_parser():
    pdf = _form_pdf()
    decode_config = DecodeConfig(keep_qpdf_warnings=False)

    document = DoclingPdfParser(loglevel="fatal").load(
        BytesIO(pdf), decode_config=decode_config
    )
    sequential = document.get_page(1).widgets

    threaded_parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=1, max_concurrent_results=2
        ),
        decode_config=decode_config,
    )
    threaded_parser.load(BytesIO(pdf), page_numbers=[1])
    result = next(threaded_parser.iterate_results())
    assert result.success, result.error_message
    threaded = result.get_page().widgets
    threaded_parser.unload_all()

    expected = [
        {
            "rect": (10.0, 160.0, 100.0, 180.0),
            "text": "Ada Lovelace",
            "description": "Full name",
            "field_name": "contact.name",
            "field_type": "/Tx",
            "field_flags": 2,
            "appearance_state": None,
        },
        {
            "rect": (10.0, 120.0, 30.0, 140.0),
            "text": "/Yes",
            "description": None,
            "field_name": "checked",
            "field_type": "/Btn",
            "field_flags": 0,
            "appearance_state": "/Yes",
        },
        {
            "rect": (10.0, 80.0, 30.0, 100.0),
            "text": "/Off",
            "description": None,
            "field_name": "unchecked",
            "field_type": "/Btn",
            "field_flags": 0,
            "appearance_state": "/Off",
        },
        {
            "rect": (10.0, 40.0, 30.0, 60.0),
            "text": None,
            "description": None,
            "field_name": "appearance_only",
            "field_type": "/Btn",
            "field_flags": 0,
            "appearance_state": "/On",
        },
    ]

    assert [_widget_data(widget) for widget in sequential] == expected
    assert [_widget_data(widget) for widget in threaded] == expected


def _shifted_crop_box_pdf(rotate: int) -> bytes:
    # Media box at the origin, crop box at (50, 40). The widget and the link
    # both frame the word ``Hello`` drawn at user-space (100, 120).
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
            f"/CropBox [50 40 350 240] /Rotate {rotate} "
            "/Resources << /Font << /F1 5 0 R >> >> "
            "/Contents 4 0 R /Annots [6 0 R 7 0 R] >>",
            content_stream("BT /F1 12 Tf 100 120 Td (Hello) Tj ET\n"),
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) "
            "/Rect [95 110 145 140] >>",
            "<< /Type /Annot /Subtype /Link /Rect [95 110 145 140] "
            "/A << /S /URI /URI (https://example.com) >> >>",
        ]
    )


@pytest.mark.parametrize("rotate", [0, 90, 180, 270])
@pytest.mark.parametrize(
    "boundary_type", [PdfPageBoundaryType.CROP_BOX, PdfPageBoundaryType.MEDIA_BOX]
)
def test_widgets_and_hyperlinks_share_the_text_cells_boundary_frame(
    rotate: int, boundary_type: PdfPageBoundaryType
):
    document = DoclingPdfParser(loglevel="fatal").load(
        BytesIO(_shifted_crop_box_pdf(rotate)),
        boundary_type=boundary_type,
        decode_config=DecodeConfig(keep_qpdf_warnings=False),
    )
    page = document.get_page(1)

    word = next(c for c in page.word_cells if c.text == "Hello").rect.to_bounding_box()
    (widget,) = page.widgets
    (hyperlink,) = page.hyperlinks

    for annot in (widget, hyperlink):
        box = annot.rect.to_bounding_box()
        assert (annot.rect.r_x0, annot.rect.r_y0) == pytest.approx((box.l, box.b))
        assert sorted((box.width, box.height)) == pytest.approx([30.0, 50.0])
        assert box.l <= word.l and word.r <= box.r
        assert box.b <= min(word.b, word.t) and max(word.b, word.t) <= box.t
