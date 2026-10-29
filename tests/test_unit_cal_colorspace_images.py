#!/usr/bin/env python
"""Images in the CIE-based /CalGray and /CalRGB colour spaces.

/CalGray and /CalRGB (ISO 32000-1, 8.6.5.2 and 8.6.5.3) are always written as
an array, `[/CalGray << ... >>]`, because the dictionary of white point and
gamma is what makes them calibrated. The image path recognised only the
ICCBased, Separation, DeviceN and Indexed arrays; a calibrated space was left
as its JSON dump, reported as unsupported, and the image was dropped for the
renderer's yellow placeholder. PDF/A image-to-PDF converters write scanned
bilevel pages as /CalGray, since PDF/A wants device-independent colour, so a
whole scanned page came out as that placeholder and OCR run on it found
nothing (docling#4861).

The samples of either space read as those of its device counterpart; the
calibration only refines how they look, which the renderer does not model.
"""

from __future__ import annotations

import zlib

import pytest

from tests.image_fixtures import g4_codes, middle_half
from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import assert_color_near, center_color, region_image

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)

# Every fixture draws its image over the middle 100x100 points of a 200x200
# page and paints the middle half of the image in both directions, so the paint
# lands on the middle 50x50 points of the page. Boxes are (left, top, right,
# bottom) in points from the top-left.
PAINT = (90.0, 90.0, 110.0, 110.0)
MARGIN = (55.0, 55.0, 70.0, 70.0)

CAL_GRAY = "[/CalGray << /WhitePoint [0.9505 1.0 1.089] /Gamma 2.2 >>]"
CAL_RGB = (
    "[/CalRGB << /WhitePoint [0.9505 1.0 1.089] /Gamma [2.2 2.2 2.2] "
    "/Matrix [0.4124 0.2126 0.0193 0.3576 0.7152 0.1192 0.1805 0.0722 0.9505] >>]"
)


def image_page(dictionary: str, payload: bytes) -> bytes:
    """A page drawing one image XObject over the middle of a 200x200 page."""
    image = stream_object(f"/Type /XObject /Subtype /Image {dictionary}", payload)
    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


def painted(x: int, y: int, width: int, height: int) -> bool:
    return x in middle_half(width) and y in middle_half(height)


def assert_painted(result, color: tuple[int, int, int], what: str) -> None:
    """The paint is a block in the middle, with white page around it."""
    assert_color_near(
        center_color(region_image(result, PAINT)),
        color,
        tolerance=30,
        what=f"paint of {what}",
    )
    assert_color_near(
        center_color(region_image(result, MARGIN)),
        WHITE,
        tolerance=30,
        what=f"margin of {what}",
    )


@pytest.mark.parametrize(
    "decode_parms, decode",
    [
        ("<< /K -1 /Columns 64 >>", ""),
        # How image-to-PDF converters tend to write a fax page: the codes'
        # black runs come out as 1 bits, and /Decode turns them back into ink.
        ("<< /K -1 /Columns 64 /BlackIs1 true >>", "/Decode [1 0]"),
    ],
    ids=["default-decode", "blackis1-with-inverting-decode"],
)
def test_calgray_fax_scan_is_drawn(decode_parms: str, decode: str):
    """The scanned page of docling#4861: a Group 4 image in /CalGray."""
    width = height = 64
    codes = g4_codes(width, height, [middle_half(width)], middle_half(height))
    pdf = image_page(
        f"/Width {width} /Height {height} /ColorSpace {CAL_GRAY} "
        f"/BitsPerComponent 1 /Filter /CCITTFaxDecode "
        f"/DecodeParms {decode_parms} {decode}",
        codes,
    )
    assert_painted(render_page(pdf), BLACK, "a /CalGray fax image")


def test_calrgb_samples_are_read_as_rgb():
    width = height = 16
    samples = b"".join(
        bytes(RED if painted(x, y, width, height) else WHITE)
        for y in range(height)
        for x in range(width)
    )
    pdf = image_page(
        f"/Width {width} /Height {height} /ColorSpace {CAL_RGB} "
        f"/BitsPerComponent 8 /Filter /FlateDecode",
        zlib.compress(samples),
    )
    assert_painted(render_page(pdf), RED, "a /CalRGB image")


def test_indexed_image_over_a_calrgb_base_is_drawn():
    width = height = 16
    palette = bytes(WHITE) + bytes(RED)
    indices = bytes(
        1 if painted(x, y, width, height) else 0
        for y in range(height)
        for x in range(width)
    )
    pdf = image_page(
        f"/Width {width} /Height {height} "
        f"/ColorSpace [/Indexed {CAL_RGB} 1 <{palette.hex()}>] "
        f"/BitsPerComponent 8",
        indices,
    )
    assert_painted(render_page(pdf), RED, "an /Indexed image over a /CalRGB base")
