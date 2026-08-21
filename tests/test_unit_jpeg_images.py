#!/usr/bin/env python
"""A four-component JPEG under a colour space that cannot describe it.

`jpeg_retry_logic.pdf` declares /DeviceRGB over a four-component Adobe JPEG.
ISO 32000-1, 8.9.5.1 does not allow that, and it breaks the decode twice over:
libjpeg refuses to convert YCCK to RGB at all ("Unsupported color conversion
request"), and once the data is decoded in its own colour space there is
nothing left in the dictionary to say which way its four bytes read.

For a /DeviceCMYK image there is no such doubt -- the samples are ink amounts
mapped by /Decode, and `test_unit_colorspaces.py::test_cmyk_jpeg_keeps_its_ink`
holds that line, since the corpus has Adobe APP14 JPEGs with transform 2 in
both polarities and the marker settles nothing. It is only here, where the file
has already contradicted itself, that the APP14 marker becomes the one
statement about the samples on offer: Adobe stores 255 minus the ink.
"""

from __future__ import annotations

import io

import pytest
from PIL import Image

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import center_color, region_image

# The image covers the middle 100x100 points of a 200x200 page; this box sits
# well inside it. (left, top, right, bottom) in points from the top-left.
PATCH = (90.0, 90.0, 110.0, 110.0)

# The ink model behind /DeviceCMYK does not send full black to exactly (0,0,0)
# nor blank paper to exactly (255,255,255), so these are the two ends of the
# range rather than exact colours. Reading the samples the wrong way round
# swaps them, which is the failure under test.
DARK = 80.0
LIGHT = 200.0


def adobe_inverted_cmyk_jpeg(ink: tuple[int, int, int, int]) -> bytes:
    """A solid four-component JPEG holding 255 minus the ink, as Adobe writes.

    Pillow saves a "CMYK" image through libjpeg with rawmode "CMYK;I" and an
    APP14 Adobe marker, which is exactly the polarity of the logo in
    `jpeg_retry_logic.pdf`: blank paper comes back out of libjpeg as
    (255, 255, 255, 255).
    """
    buffer = io.BytesIO()
    Image.new("CMYK", (16, 16), ink).save(buffer, format="JPEG", quality=95)

    payload = buffer.getvalue()
    assert b"Adobe" in payload[:256], "the fixture wants an APP14 Adobe marker"
    return payload


def jpeg_page(payload: bytes) -> bytes:
    """One JPEG over the middle of a 200x200 page, declared /DeviceRGB.

    The mismatch is the point: it is what jpeg_retry_logic.pdf says, and it is
    what leaves the samples' polarity to the APP14 marker.
    """
    image = stream_object(
        "/Type /XObject /Subtype /Image /Width 16 /Height 16 "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode",
        payload,
    )

    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


def brightness(rgb: tuple[int, int, int]) -> float:
    return sum(rgb) / 3.0


@pytest.mark.parametrize(
    "ink, what, floor, ceiling",
    [
        ((0, 0, 0, 0), "blank paper", LIGHT, 255.0),
        ((0, 0, 0, 255), "full black", 0.0, DARK),
    ],
)
def test_mismatched_colorspace_reads_adobe_polarity(
    ink: tuple[int, int, int, int], what: str, floor: float, ceiling: float
):
    """Blank paper stays light and full ink stays dark under /DeviceRGB.

    Reading these samples as ink is what turned the logo on page 1 of
    jpeg_retry_logic.pdf into a black box: blank paper decodes out of libjpeg
    as (255, 255, 255, 255), which is every ink at full strength.
    """
    rendered = center_color(
        region_image(render_page(jpeg_page(adobe_inverted_cmyk_jpeg(ink))), PATCH)
    )

    assert floor <= brightness(rendered) <= ceiling, (
        f"{what} rendered as {rendered}; the four bytes of an Adobe JPEG under "
        "a three-component /ColorSpace hold 255 minus the ink"
    )
