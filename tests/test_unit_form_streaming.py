"""Nested Form execution while the parent content parser is still active."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import parse_page, render_page, simple_page_pdf, stream_object

FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


def test_repeated_nested_forms_keep_local_resources_and_restore_parent_state():
    # Both Forms shadow /G1. The outer one is translucent; the leaf is opaque.
    # A transform left active in the leaf must not move its parent's next mark.
    outer = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 70 30] "
        "/Matrix [1 0 0 1 20 30] "
        "/Resources << /XObject << /Leaf 6 0 R >> "
        "/ExtGState << /G1 << /ca 0.5 >> >> >>",
        b"/G1 gs /Leaf Do 0 1 0 rg 30 0 10 10 re f",
    )
    leaf = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 10 10] "
        "/Matrix [2 0 0 2 0 0] "
        "/Resources << /ExtGState << /G1 << /ca 1 >> >> >>",
        b"/G1 gs 1 0 0 rg 0 0 10 10 re f 1 0 0 1 50 0 cm",
    )
    pdf = simple_page_pdf(
        "/G1 gs 0 0 1 rg /Outer Do q 1 0 0 1 80 0 cm /Outer Do Q 10 100 10 10 re f",
        resources="/XObject << /Outer 5 0 R >> /ExtGState << /G1 << /ca 1 >> >>",
        extra_objects=[outer, leaf],
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    def pixel(x: int, y: int):
        return image.getpixel((x, image.height - 1 - y))

    for x in (30, 110):
        assert pixel(x, 40) == pytest.approx((255, 0, 0), abs=2)
    for x in (55, 135):
        assert pixel(x, 35) == pytest.approx((128, 255, 128), abs=2)
    assert pixel(15, 105) == pytest.approx((0, 0, 255), abs=2)
    assert pixel(75, 35) == (255, 255, 255)


def test_damaged_form_prefix_survives_repeated_invocation_and_parent_continues():
    # A valid non-final Flate prefix followed by a reserved block type. The
    # first invocation repairs the source; the second must still execute it.
    encoder = zlib.compressobj()
    content = b"BT /F1 12 Tf 5 40 Td (inside) Tj ET\n"
    damaged = encoder.compress(content) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    form = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 100 60] /Filter /FlateDecode",
        damaged,
    )
    pdf = simple_page_pdf(
        "q 1 0 0 1 20 120 cm /Fm Do Q "
        "q 1 0 0 1 20 60 cm /Fm Do Q "
        "BT /F1 12 Tf 20 20 Td (after) Tj ET",
        resources="/XObject << /Fm 5 0 R >> /Font << /F1 6 0 R >>",
        extra_objects=[form, FONT],
    )

    assert [cell.text for cell in parse_page(pdf).textline_cells] == [
        "inside",
        "inside",
        "after",
    ]


@pytest.mark.parametrize("close_scope", [True, False])
def test_form_actual_text_scope_finishes_before_parent_suffix(close_scope: bool):
    content = (
        "/Span << /ActualText (replacement) >> BDC "
        "BT /F1 12 Tf 20 120 Td (shown) Tj ET" + (" EMC" if close_scope else "")
    )
    form = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 200 200]",
        content.encode("latin-1"),
    )
    pdf = simple_page_pdf(
        "/Fm Do BT /F1 12 Tf 20 20 Td (after) Tj ET",
        resources="/XObject << /Fm 5 0 R >> /Font << /F1 6 0 R >>",
        extra_objects=[form, FONT],
    )

    assert [cell.text for cell in parse_page(pdf).textline_cells] == [
        "replacement" if close_scope else "shown",
        "after",
    ]
