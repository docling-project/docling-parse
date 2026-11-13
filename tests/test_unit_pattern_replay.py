"""Immutable pattern source replay and decoded-cell reuse by stencil colour."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object


@pytest.mark.parametrize("lattice", [False, True])
@pytest.mark.parametrize("damaged", [False, True])
def test_repeated_stencil_keeps_source_colour_and_parent_state(
    lattice: bool, damaged: bool
) -> None:
    # Small cells use the decoded-cell cache; cells larger than the page
    # replay directly. Red/blue/red exercises both colour keys and reuse.
    step = 300 if lattice else 20
    source = f"/G1 gs 0 0 {step} {step} re f 1 0 0 1 100 0 cm\n".encode()
    if damaged:
        encoder = zlib.compressobj()
        source = encoder.compress(source) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    pattern = stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 "
        f"/BBox [0 0 {step} {step}] /XStep {step} /YStep {step} "
        "/Resources << /ExtGState << /G1 << /ca 0.5 >> >> >>"
        + (" /Filter /FlateDecode" if damaged else ""),
        source,
    )
    pdf = simple_page_pdf(
        "/G1 gs /PCS cs "
        "1 0 0 /P0 scn 20 20 30 30 re f "
        "0 0 1 /P0 scn 80 20 30 30 re f "
        "1 0 0 /P0 scn 140 20 30 30 re f "
        "0 1 0 rg 20 100 30 30 re f",
        resources="/Pattern << /P0 5 0 R >> /ColorSpace << /PCS [/Pattern /DeviceRGB] >> "
        "/ExtGState << /G1 << /ca 1 >> >>",
        extra_objects=[pattern],
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    def pixel(x: int, y: int):
        return image.getpixel((x, image.height - 1 - y))

    for x in (35, 155):
        assert pixel(x, 35) == pytest.approx((255, 128, 128), abs=3)
    assert pixel(95, 35) == pytest.approx((128, 128, 255), abs=3)
    assert pixel(35, 115) == (0, 255, 0)
    assert pixel(65, 35) == (255, 255, 255)
