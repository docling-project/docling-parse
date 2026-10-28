#!/usr/bin/env python
"""A re-render at a finer scale must not upscale bitmaps decoded for a coarser one.

The render pipeline decodes an oversampled /DCTDecode image at 1/2, 1/4 or 1/8
of its stored resolution when the configured render scale needs no more.
`get_image(scale=...)` later renders the same page again; when that scale is
finer, the image has to be decoded again, or the reduced samples are blown up
and the page comes out blurred -- as it did for OCR crops taken at scale 3 from
a parse rendered at scale 1.
"""

from __future__ import annotations

import io
import random

from PIL import Image, ImageChops, ImageStat

from tests.pdf_builder import render_page, simple_page_pdf, stream_object

SOURCE_EXTENT = 512


def noise_jpeg_page() -> bytes:
    """512x512 grey noise drawn into 64x64 points of a 200x200 page.

    At scale 1 the image is 8x oversampled and decodes at 1/8 (64x64); at
    scale 3 it decodes at 1/2 (256x256). Noise has detail at every frequency,
    so an upscale of the 1/8 decode is easy to tell apart from the 1/2 decode.
    """
    noise = random.Random(0).randbytes(SOURCE_EXTENT * SOURCE_EXTENT)
    buffer = io.BytesIO()
    Image.frombytes("L", (SOURCE_EXTENT, SOURCE_EXTENT), noise).save(
        buffer, format="JPEG", quality=90
    )
    image = stream_object(
        f"/Type /XObject /Subtype /Image /Width {SOURCE_EXTENT} "
        f"/Height {SOURCE_EXTENT} /ColorSpace /DeviceGray /BitsPerComponent 8 "
        "/Filter /DCTDecode",
        buffer.getvalue(),
    )
    return simple_page_pdf(
        "q\n64 0 0 64 68 68 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


def mean_abs_difference(a: Image.Image, b: Image.Image) -> float:
    assert a.size == b.size
    return ImageStat.Stat(ImageChops.difference(a.convert("L"), b.convert("L"))).mean[0]


def test_rerender_at_finer_scale_matches_native_render():
    pdf = noise_jpeg_page()

    rerendered = render_page(pdf, scale=1.0).get_image(scale=3.0)
    native = render_page(pdf, scale=3.0).get_image()
    image_area = (204, 204, 396, 396)  # the 64x64 pt image at scale 3

    # Same samples through the same rasteriser, so the two match. Before the
    # fix the re-render upscaled the 64x64 decode and differed by ~18 grey
    # levels on average over the image.
    assert (
        mean_abs_difference(rerendered.crop(image_area), native.crop(image_area)) < 0.5
    )
