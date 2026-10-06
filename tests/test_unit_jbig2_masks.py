#!/usr/bin/env python
"""Images stencilled by a JBIG2-coded /Mask.

An image XObject's /Mask may be a stream: a 1-bit /ImageMask whose 0-samples
let the image through and whose 1-samples knock it out (ISO 32000-1, 8.9.6.3).
MRC-compressed scans are built this way: a JPEG 2000 foreground that carries
the text colour, stencilled by a JBIG2 mask holding the glyph shapes. QPDF
cannot decode JBIG2, so the mask has to be decoded here like the page's other
JBIG2 images, or the foreground is painted across the whole page.
"""

from __future__ import annotations

import struct
import zlib

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import assert_color_near, center_color, region_image

WHITE = (255, 255, 255)
RED = (255, 0, 0)

# Every fixture draws its image over the middle 100x100 points of a 200x200
# page. The mask lets the image through over the middle half of the image in
# both directions, so the painted part lands on the middle 50x50 points of the
# page. Boxes are (left, top, right, bottom) in points from the top-left.
OPENING = (90.0, 90.0, 110.0, 110.0)
# Inside the image, outside the opening: masked out, so the page shows.
MASKED = (55.0, 55.0, 70.0, 70.0)


def middle_half(extent: int) -> range:
    return range(extent // 4, 3 * extent // 4)


# ---------------------------------------------------------------------------
# A JBIG2 encoder for the fixtures: an MQ arithmetic coder (T.88, Annex E) and
# a generic region coded with template 0 and the nominal adaptive pixels
# (6.2.5.3). This is how MRC scanners code their masks. The control test below
# draws the same codestream through the renderer's existing JBIG2 image path.
# ---------------------------------------------------------------------------

# (Qe, NMPS, NLPS, SWITCH), Table E.1.
MQ_STATES = [
    (0x5601, 1, 1, 1),
    (0x3401, 2, 6, 0),
    (0x1801, 3, 9, 0),
    (0x0AC1, 4, 12, 0),
    (0x0521, 5, 29, 0),
    (0x0221, 38, 33, 0),
    (0x5601, 7, 6, 1),
    (0x5401, 8, 14, 0),
    (0x4801, 9, 14, 0),
    (0x3801, 10, 14, 0),
    (0x3001, 11, 17, 0),
    (0x2401, 12, 18, 0),
    (0x1C01, 13, 20, 0),
    (0x1601, 29, 21, 0),
    (0x5601, 15, 14, 1),
    (0x5401, 16, 14, 0),
    (0x5101, 17, 15, 0),
    (0x4801, 18, 16, 0),
    (0x3801, 19, 17, 0),
    (0x3401, 20, 18, 0),
    (0x3001, 21, 19, 0),
    (0x2801, 22, 19, 0),
    (0x2401, 23, 20, 0),
    (0x2201, 24, 21, 0),
    (0x1C01, 25, 22, 0),
    (0x1801, 26, 23, 0),
    (0x1601, 27, 24, 0),
    (0x1401, 28, 25, 0),
    (0x1201, 29, 26, 0),
    (0x1101, 30, 27, 0),
    (0x0AC1, 31, 28, 0),
    (0x09C1, 32, 29, 0),
    (0x08A1, 33, 30, 0),
    (0x0521, 34, 31, 0),
    (0x0441, 35, 32, 0),
    (0x02A1, 36, 33, 0),
    (0x0221, 37, 34, 0),
    (0x0141, 38, 35, 0),
    (0x0111, 39, 36, 0),
    (0x0085, 40, 37, 0),
    (0x0049, 41, 38, 0),
    (0x0025, 42, 39, 0),
    (0x0015, 43, 40, 0),
    (0x0009, 44, 41, 0),
    (0x0005, 45, 42, 0),
    (0x0001, 45, 43, 0),
    (0x5601, 46, 46, 0),
]


class MQEncoder:
    """The encoder of T.88 E.2, with the byte before the output as a dummy."""

    def __init__(self) -> None:
        self.a, self.c, self.ct = 0x8000, 0, 12
        self.out = bytearray([0])
        self.state: dict[int, int] = {}
        self.mps: dict[int, int] = {}

    def encode(self, bit: int, cx: int) -> None:
        index, mps = self.state.get(cx, 0), self.mps.get(cx, 0)
        qe, nmps, nlps, switch = MQ_STATES[index]
        self.a -= qe
        if bit == mps:
            if self.a & 0x8000:
                self.c += qe
                return
            if self.a < qe:
                self.a = qe
            else:
                self.c += qe
            self.state[cx] = nmps
        else:
            if self.a < qe:
                self.c += qe
            else:
                self.a = qe
            if switch:
                self.mps[cx] = 1 - mps
            self.state[cx] = nlps
        self.renormalise()

    def renormalise(self) -> None:
        while True:
            self.a = (self.a << 1) & 0xFFFF
            self.c = (self.c << 1) & 0xFFFFFFFF
            self.ct -= 1
            if self.ct == 0:
                self.byte_out()
            if self.a & 0x8000:
                return

    def byte_out(self) -> None:
        if self.out[-1] == 0xFF:
            self.out.append(self.c >> 20)
            self.c &= 0xFFFFF
            self.ct = 7
            return
        if self.c >= 0x8000000:
            self.out[-1] += 1
            self.c &= 0x7FFFFFF
            if self.out[-1] == 0xFF:
                self.out.append(self.c >> 20)
                self.c &= 0xFFFFF
                self.ct = 7
                return
        self.out.append(self.c >> 19)
        self.c &= 0x7FFFF
        self.ct = 8

    def flush(self) -> bytes:
        top = self.c + self.a
        self.c |= 0xFFFF
        if self.c >= top:
            self.c -= 0x8000
        self.c = (self.c << self.ct) & 0xFFFFFFFF
        self.byte_out()
        self.c = (self.c << self.ct) & 0xFFFFFFFF
        self.byte_out()
        if self.out[-1] != 0xFF:
            self.out.append(0xFF)
        self.out.append(0xAC)
        return bytes(self.out[1:])


# Template 0's nominal adaptive pixels A1..A4, as (dx, dy).
TEMPLATE0_AT = [(3, -1), (-3, -1), (2, -2), (-2, -2)]


def generic_region_codes(pixels: list[list[int]]) -> bytes:
    """Arithmetic-code a bitmap (1 = black) as a template-0 generic region."""
    height, width = len(pixels), len(pixels[0])

    def pixel(x: int, y: int) -> int:
        return pixels[y][x] if 0 <= x < width and 0 <= y < height else 0

    def run(y: int, first: int, last: int, x: int) -> int:
        value = 0
        for dx in range(first, last + 1):
            value = (value << 1) | pixel(x + dx, y)
        return value

    (a1x, a1y), (a2x, a2y), (a3x, a3y), (a4x, a4y) = TEMPLATE0_AT
    encoder = MQEncoder()
    for y in range(height):
        for x in range(width):
            context = (
                run(y, -4, -1, x)
                | pixel(x + a1x, y + a1y) << 4
                | run(y - 1, -2, 2, x) << 5
                | pixel(x + a2x, y + a2y) << 10
                | pixel(x + a3x, y + a3y) << 11
                | run(y - 2, -1, 1, x) << 12
                | pixel(x + a4x, y + a4y) << 15
            )
            encoder.encode(pixels[y][x], context)
    return encoder.flush()


def jbig2_segment(number: int, segment_type: int, data: bytes) -> bytes:
    """One JBIG2 segment (T.88, 7.2): no referred-to segments, page 1."""
    return struct.pack(">IBBBI", number, segment_type, 0, 1, len(data)) + data


def jbig2_stream(width: int, height: int) -> bytes:
    """An embedded JBIG2 stream (T.88, Annex D.3), black over the middle half.

    JBIG2 reads a black pixel as 1 and the PDF filter hands it back as 0, the
    sample that paints (or, in a /Mask, lets the image through).
    """
    pixels = [
        [
            int(x in middle_half(width) and y in middle_half(height))
            for x in range(width)
        ]
        for y in range(height)
    ]
    page_information = struct.pack(">IIIIBH", width, height, 0, 0, 0, 0)
    region_information = struct.pack(">IIIIB", width, height, 0, 0, 0)
    # Flags 0: arithmetic coding, template 0, no typical prediction.
    region_header = bytes([0x00]) + struct.pack(
        ">8b", *(v for at in TEMPLATE0_AT for v in at)
    )

    return (
        jbig2_segment(0, 48, page_information)
        + jbig2_segment(
            1,
            38,  # immediate generic region
            region_information + region_header + generic_region_codes(pixels),
        )
        + jbig2_segment(2, 49, b"")  # end of page
    )


def jbig2_mask_object(width: int, height: int) -> bytes:
    return stream_object(
        f"/Type /XObject /Subtype /Image /Width {width} /Height {height} "
        "/ImageMask true /BitsPerComponent 1 /Filter /JBIG2Decode",
        jbig2_stream(width, height),
    )


def masked_image_page(
    *, image_size: int, mask_size: int, compress_image: bool = False
) -> bytes:
    """A red image stencilled by a JBIG2 /Mask, over the middle of the page."""
    samples = bytes(RED) * (image_size * image_size)
    filter_entry = ""
    if compress_image:
        samples = zlib.compress(samples)
        filter_entry = " /Filter /FlateDecode"

    return simple_page_pdf(
        "q 100 0 0 100 50 50 cm /Im0 Do Q",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[
            stream_object(
                f"/Type /XObject /Subtype /Image /Width {image_size} "
                f"/Height {image_size} /ColorSpace /DeviceRGB "
                f"/BitsPerComponent 8 /Mask 6 0 R{filter_entry}",
                samples,
            ),
            jbig2_mask_object(mask_size, mask_size),
        ],
    )


def assert_opening_rendered(result, what: str) -> None:
    assert_color_near(
        center_color(region_image(result, OPENING)),
        RED,
        tolerance=30,
        what=f"opening of {what}",
    )
    assert_color_near(
        center_color(region_image(result, MASKED)),
        WHITE,
        tolerance=30,
        what=f"masked-out part of {what}",
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_jbig2_image_mask_paints_its_black_pixels():
    """The fixture's codestream, drawn as a plain /ImageMask, paints the opening.

    This path decoded JBIG2 already; it keeps the bits the /Mask tests below
    rely on honest.
    """
    size = 64
    pdf = simple_page_pdf(
        "q 1 0 0 rg 100 0 0 100 50 50 cm /Im0 Do Q",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[jbig2_mask_object(size, size)],
    )
    assert_opening_rendered(render_page(pdf), "a JBIG2 /ImageMask")


def test_jbig2_mask_stencils_an_image_of_the_same_size():
    result = render_page(masked_image_page(image_size=64, mask_size=64))
    assert_opening_rendered(result, "an image under a same-size JBIG2 /Mask")


def test_jbig2_mask_stencils_a_lower_resolution_image():
    """An MRC foreground is coarser than its mask; the mask keeps its detail."""
    result = render_page(
        masked_image_page(image_size=2, mask_size=64, compress_image=True)
    )
    assert_opening_rendered(result, "a 2x2 image under a 64x64 JBIG2 /Mask")
