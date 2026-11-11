"""Appearance callback execution after its resource scope and matrix are set."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import (
    build_pdf,
    content_stream,
    parse_page,
    render_page,
    stream_object,
)

FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


@pytest.mark.parametrize("widget", [False, True])
def test_transformed_appearance_can_invoke_a_form_with_local_graphics_state(
    widget: bool,
):
    # The rotated BBox fits a 100x100 /Rect. Its nested Form must inherit the
    # appearance's half opacity rather than the page's opaque /G1 resource.
    catalog_fields = " /AcroForm << /Fields [5 0 R] >>" if widget else ""
    subtype = "/Widget /FT /Btn /T (field)" if widget else "/Stamp"
    pdf = build_pdf(
        [
            f"<< /Type /Catalog /Pages 2 0 R{catalog_fields} >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Contents 4 0 R /Annots [5 0 R] /Resources << "
            "/XObject << /Fm 7 0 R >> /ExtGState << /G1 << /ca 1 >> >> >> >>",
            content_stream(""),
            f"<< /Type /Annot /Subtype {subtype} /F 4 /Rect [20 40 120 140] "
            "/AP << /N 6 0 R >> >>",
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [10 20 110 70] "
                "/Matrix [0 1 -1 0 70 -10] "
                "/Resources << /ExtGState << /G1 << /ca 0.5 >> >> >>",
                b"/G1 gs /Fm Do",
            ),
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [10 20 110 70]",
                b"1 0 0 rg 20 30 40 20 re f",
            ),
        ]
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    # The mark maps to page x=[60,100], y=[50,90]. Interior samples avoid edges.
    assert image.getpixel((80, 129)) == pytest.approx((255, 128, 128), abs=2)
    assert image.getpixel((30, 69)) == (255, 255, 255)


@pytest.mark.parametrize("font_scope", ["appearance", "acroform", "page"])
def test_shared_selected_widget_appearance_uses_the_font_fallback_chain(
    font_scope: str,
):
    dr = " /DR << /Font << /F1 9 0 R >> >>" if font_scope == "acroform" else ""
    page_resources = (
        " /Resources << /Font << /F1 9 0 R >> >>" if font_scope == "page" else ""
    )
    ap_resources = (
        " /Resources << /Font << /F1 9 0 R >> >>" if font_scope == "appearance" else ""
    )
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm "
            f"<< /Fields [5 0 R 6 0 R]{dr} >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            f"/Contents 4 0 R /Annots [5 0 R 6 0 R]{page_resources} >>",
            content_stream(""),
            "<< /Type /Annot /Subtype /Widget /FT /Btn /T (first) /F 4 "
            "/Rect [20 120 120 160] /AS /Yes /AP << /N << /Off 7 0 R /Yes 8 0 R >> >> >>",
            "<< /Type /Annot /Subtype /Widget /FT /Btn /T (second) /F 4 "
            "/Rect [20 60 120 100] /AS /Yes /AP << /N << /Off 7 0 R /Yes 8 0 R >> >> >>",
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [0 0 100 40] "
                "/Resources << /Font << /F1 9 0 R >> >>",
                b"BT /F1 12 Tf 10 15 Td (wrong) Tj ET",
            ),
            stream_object(
                f"/Type /XObject /Subtype /Form /BBox [0 0 100 40]{ap_resources}",
                b"BT /F1 12 Tf 10 15 Td (inside) Tj ET",
            ),
            FONT,
        ]
    )

    assert [cell.text for cell in parse_page(pdf).textline_cells] == [
        "inside",
        "inside",
    ]


def test_damaged_appearance_prefix_does_not_capture_the_next_annotation():
    encoder = zlib.compressobj()
    content = (
        b"/Span << /ActualText (wrong) >> BDC BT /F1 12 Tf 10 15 Td (before) Tj ET\n"
    )
    damaged = encoder.compress(content) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Contents 4 0 R /Annots [5 0 R 6 0 R] "
            "/Resources << /Font << /F1 9 0 R >> >> >>",
            content_stream(""),
            "<< /Type /Annot /Subtype /Stamp /F 4 /Rect [20 120 120 160] "
            "/AP << /N 7 0 R >> >>",
            "<< /Type /Annot /Subtype /Stamp /F 4 /Rect [20 60 120 100] "
            "/AP << /N 8 0 R >> >>",
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [0 0 100 40] /Filter /FlateDecode",
                damaged,
            ),
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [0 0 100 40]",
                b"BT /F1 12 Tf 10 15 Td (after) Tj ET",
            ),
            FONT,
        ]
    )

    assert [cell.text for cell in parse_page(pdf).textline_cells] == ["before", "after"]
