"""Soft-mask callbacks while the page and nested Form parsers are active."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object


@pytest.mark.parametrize("damaged", [False, True])
@pytest.mark.parametrize("subtype", ["Alpha", "Luminosity"])
def test_transformed_mask_keeps_local_resources_and_recovers_once(
    damaged: bool, subtype: str
) -> None:
    # /G1 means the mask at page level and half-opacity within its group.
    # The group's matrix and the CTM at gs together place its white rectangle
    # at [30,30,70,50]. Replaying a recovered prefix twice makes it too opaque.
    source = b"/G1 gs /Leaf Do\n"
    if damaged:
        encoder = zlib.compressobj()
        source = encoder.compress(source) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    group = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 30 20] "
        "/Matrix [2 0 0 1 10 20] /Group << /S /Transparency /CS /DeviceGray >> "
        "/Resources << /ExtGState << /G1 << /ca 0.5 >> >> "
        "/XObject << /Leaf 6 0 R >> >>" + (" /Filter /FlateDecode" if damaged else ""),
        source,
    )
    leaf = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 20 20]",
        b"1 g 0 0 20 20 re f 1 0 0 1 100 0 cm",
    )
    pdf = simple_page_pdf(
        "1 0 0 rg 0 0 200 200 re f 0 0 1 rg "
        "q 1 0 0 1 20 10 cm /G1 gs 0 20 30 20 re f "
        "/G1 gs 30 20 40 20 re f Q 150 20 10 10 re f",
        resources=f"/ExtGState << /G1 << /SMask << /S /{subtype} /G 5 0 R >> >> >>",
        extra_objects=[group, leaf],
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    def pixel(x: int, y: int):
        return image.getpixel((x, image.height - 1 - y))

    for x in (40, 60):
        assert pixel(x, 40) == pytest.approx((128, 0, 128), abs=3)
    # One point outside the group's box and one inside its unpainted portion.
    for x in (25, 80):
        assert pixel(x, 40) == (255, 0, 0)
    assert pixel(155, 25) == (0, 0, 255)
    assert pixel(40, 70) == (255, 0, 0)
