#!/usr/bin/env python
"""Palette images and their masks (PDF 32000-1, 8.6.6.3 and 8.9.6).

An /Indexed image at fewer than 8 bits per component packs several indices into
a byte. Reading those bytes as if each held one sample drops most of the image
and mis-colours the rest -- flags and logos came out as flat blocks.

Unpacking them exposed a second trap: the default /Decode for an /Indexed image
is [0, 2^bpc - 1], not [0, hival]. Synthesising the wrong one rescales every
index -- at 4 bpc with a 8-entry palette, by 7/15 -- so the picture survives but
every colour is drawn from the wrong palette slot.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import assert_color_near, center_color, region_image

# three vertical bands, sampled away from the boundaries
LEFT_BAND = (55.0, 60.0, 75.0, 140.0)
MIDDLE_BAND = (90.0, 60.0, 110.0, 140.0)
RIGHT_BAND = (125.0, 60.0, 145.0, 140.0)

RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)

PALETTE = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255])  # entries 0, 1, 2
HIVAL = 2


def _packed_rows(indices_per_row: list[int], bits: int, width: int) -> bytes:
    """Pack one row of indices, most significant bits first, and repeat it."""
    row = bytearray()
    accumulator = 0
    filled = 0
    for index in indices_per_row:
        accumulator = (accumulator << bits) | index
        filled += bits
        while filled >= 8:
            filled -= 8
            row.append((accumulator >> filled) & 0xFF)
    if filled:
        row.append((accumulator << (8 - filled)) & 0xFF)
    assert len(indices_per_row) == width
    return bytes(row) * width


def _indexed_page(bits: int, *, decode: str = "") -> bytes:
    """A 3-band palette image scaled over the middle of the page."""
    per_band = max(1, 3 // 3)
    indices = [0] * per_band + [1] * per_band + [2] * per_band
    width = len(indices)
    payload = _packed_rows(indices, bits, width)
    decode_entry = f" /Decode {decode}" if decode else ""

    image = stream_object(
        "/Type /XObject /Subtype /Image "
        f"/Width {width} /Height {width} "
        f"/ColorSpace [/Indexed /DeviceRGB {HIVAL} 6 0 R] "
        f"/BitsPerComponent {bits}{decode_entry}",
        payload,
    )
    palette = stream_object("", PALETTE)

    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image, palette],
    )


@pytest.mark.parametrize("bits", [1, 2, 4, 8])
def test_indexed_image_bands_use_their_palette_entries(bits: int):
    """Each band renders the palette entry its index names.

    At 1 bpc only indices 0 and 1 exist, so that case checks the two it can
    address; the point is the same at every depth -- the packed indices are
    unpacked rather than read one per byte.
    """
    result = render_page(_indexed_page(bits))

    assert_color_near(
        center_color(region_image(result, LEFT_BAND)),
        RED,
        tolerance=30,
        what=f"palette entry 0 at {bits} bpc",
    )
    assert_color_near(
        center_color(region_image(result, MIDDLE_BAND)),
        GREEN,
        tolerance=30,
        what=f"palette entry 1 at {bits} bpc",
    )
    if bits > 1:
        assert_color_near(
            center_color(region_image(result, RIGHT_BAND)),
            BLUE,
            tolerance=30,
            what=f"palette entry 2 at {bits} bpc",
        )


def test_default_decode_does_not_rescale_indices():
    """The default /Decode spans the bit depth, not the palette.

    Writing [0, hival] instead maps index 2 of a 4-bit image to palette entry
    0: the image keeps its shape and loses its colours. Stating the correct
    range explicitly must therefore render identically to omitting it.
    """
    implicit = render_page(_indexed_page(4))
    explicit = render_page(_indexed_page(4, decode="[0 15]"))

    for box, name in (
        (LEFT_BAND, "entry 0"),
        (MIDDLE_BAND, "entry 1"),
        (RIGHT_BAND, "entry 2"),
    ):
        assert_color_near(
            center_color(region_image(implicit, box)),
            center_color(region_image(explicit, box)),
            tolerance=8,
            what=f"{name} with an implicit vs explicit /Decode",
        )


def test_sub_byte_soft_mask_is_applied():
    """A 1-bpc /SMask cuts the image it belongs to.

    The mask is coarser than the image, so it has to be resolved onto the
    finer grid; taking it a byte at a time left stencil text as solid bars.
    """
    image = stream_object(
        "/Type /XObject /Subtype /Image /Width 2 /Height 2 "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 6 0 R",
        bytes([255, 0, 0] * 4),
    )
    # left column opaque, right column transparent
    mask = stream_object(
        "/Type /XObject /Subtype /Image /Width 2 /Height 2 "
        "/ColorSpace /DeviceGray /BitsPerComponent 1",
        bytes([0b10000000, 0b10000000]),
    )
    pdf = simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image, mask],
    )

    result = render_page(pdf)
    kept = center_color(region_image(result, (55.0, 60.0, 75.0, 140.0)))
    dropped = center_color(region_image(result, (125.0, 60.0, 145.0, 140.0)))

    assert kept[0] > 200 and kept[1] < 90, (
        f"the opaque half of the soft mask did not paint: {kept}"
    )
    assert min(dropped) > 200, (
        f"the transparent half of the soft mask still painted: {dropped}"
    )
