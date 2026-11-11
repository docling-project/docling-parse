#!/usr/bin/env python
"""JBIG2 generic regions coded with MMR (ITU-T T.6).

A JBIG2 generic region is either arithmetic-coded or MMR-coded (T.88, 6.2.5.2
and 7.4.6.2, flag bit 0). MMR is the Group 4 coding of ITU-T T.6, the same
coding as /CCITTFaxDecode with /K -1, and scanners and PDF optimisers emit it.
pdfium's JBIG2 decoder hands such regions to its Group 4 row decoder. The copy
vendored here had drifted from upstream: it did not stop at the end of a row,
so a decoded row ran on into the next row's codes, the reference line was read
past its stride and the span bounds check killed the process with SIGTRAP
(issue #440). It also had the T.6 mode codes shifted (pass mode decoded as a
vertical mode), so even a row that did not trap decoded wrong.

The fixtures' region data is written by libtiff through Pillow: a Group 4 TIFF
strip holds exactly the T.6 codes a JBIG2 MMR region carries.
"""

from __future__ import annotations

import io
import struct

import pytest
from PIL import Image, TiffImagePlugin, features

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import flatten_on_white

pytestmark = pytest.mark.skipif(
    not features.check("libtiff"), reason="Pillow without libtiff cannot write Group 4"
)

Bitmap = list[list[int]]  # rows of 0 (white) / 1 (black), JBIG2's convention


def t6_codes(pixels: Bitmap) -> bytes:
    """The T.6 (Group 4) codes for `pixels`, as libtiff writes them."""
    height, width = len(pixels), len(pixels[0])
    bitmap = Image.new("1", (width, height), 0)
    bitmap.putdata([value for row in pixels for value in row])
    tiff = io.BytesIO()
    bitmap.save(tiff, format="TIFF", compression="group4")
    strip = Image.open(io.BytesIO(tiff.getvalue()))
    assert isinstance(strip, TiffImagePlugin.TiffImageFile)
    offsets, byte_counts = strip.tag_v2[273], strip.tag_v2[279]
    assert len(offsets) == 1, "the fixture must fit one TIFF strip"
    return tiff.getvalue()[offsets[0] : offsets[0] + byte_counts[0]]


def jbig2_segment(number: int, segment_type: int, data: bytes) -> bytes:
    """One JBIG2 segment (T.88, 7.2): no referred-to segments, page 1."""
    return struct.pack(">IBBBI", number, segment_type, 0, 1, len(data)) + data


def jbig2_mmr_stream(pixels: Bitmap) -> bytes:
    """An embedded JBIG2 stream (T.88, Annex D.3) with one MMR generic region."""
    height, width = len(pixels), len(pixels[0])
    page_information = struct.pack(">IIIIBH", width, height, 0, 0, 0, 0)
    region_information = struct.pack(">IIIIB", width, height, 0, 0, 0)
    mmr_flags = bytes([0x01])  # generic region flags: MMR = 1
    return (
        jbig2_segment(0, 48, page_information)
        + jbig2_segment(
            1, 38, region_information + mmr_flags + t6_codes(pixels)
        )  # immediate generic region
        + jbig2_segment(2, 49, b"")  # end of page
    )


CELL = 4  # device pixels per image sample, so each sample can be read back
MARGIN = 8


def mmr_image_page(pixels: Bitmap) -> bytes:
    """A page showing `pixels` as a DeviceGray JBIG2 image, CELL points per sample."""
    height, width = len(pixels), len(pixels[0])
    return simple_page_pdf(
        f"q {width * CELL} 0 0 {height * CELL} {MARGIN} {MARGIN} cm /Im0 Do Q",
        resources="/XObject << /Im0 5 0 R >>",
        media_box=f"[0 0 {width * CELL + 2 * MARGIN} {height * CELL + 2 * MARGIN}]",
        extra_objects=[
            stream_object(
                f"/Type /XObject /Subtype /Image /Width {width} /Height {height} "
                "/ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /JBIG2Decode",
                jbig2_mmr_stream(pixels),
            )
        ],
    )


def luminance(page: Image.Image, x: int, y: int) -> int:
    value = page.getpixel((x, y))
    assert isinstance(value, int), "a mode-L image yields int pixels"
    return value


def rendered_bitmap(pixels: Bitmap) -> Bitmap:
    """Render the page for `pixels` and read one sample back per image sample."""
    height, width = len(pixels), len(pixels[0])
    result = render_page(mmr_image_page(pixels), scale=1.0)
    page = flatten_on_white(result.get_image(scale=1.0)).convert("L")
    assert page.size == (width * CELL + 2 * MARGIN, height * CELL + 2 * MARGIN)
    return [
        [
            int(
                luminance(
                    page, MARGIN + x * CELL + CELL // 2, MARGIN + y * CELL + CELL // 2
                )
                < 128
            )
            for x in range(width)
        ]
        for y in range(height)
    ]


def assert_decodes_to(pixels: Bitmap) -> None:
    decoded = rendered_bitmap(pixels)
    wrong = [
        (x, y)
        for y, row in enumerate(pixels)
        for x, value in enumerate(row)
        if decoded[y][x] != value
    ]
    assert not wrong, (
        f"{len(wrong)} of {len(pixels) * len(pixels[0])} samples differ, first {wrong[:8]}"
    )


def bitmap_with_box(
    width: int, height: int, box: tuple[int, int, int, int] | None
) -> Bitmap:
    """A white bitmap with a black box at (left, top, right, bottom), inclusive."""
    pixels = [[0] * width for _ in range(height)]
    if box is not None:
        left, top, right, bottom = box
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                pixels[y][x] = 1
    return pixels


def runs_bitmap(width: int, height: int) -> Bitmap:
    """Rows whose black runs move by 0 to 3 samples, appear, vanish and touch the edges.

    Edges that shift by up to 3 samples against the row above code as the
    vertical modes V0, VR1-3 and VL1-3; a run that starts where the row above
    had none codes as horizontal mode; a run of the row above that lies wholly
    inside a white span of this row codes as pass mode (T.6, 2.2.3).
    """
    base = [(10, 30), (36, 42), (60, 80)]
    shifts = [0, 1, -1, 2, -2, 3, -3, 7, -7, 0]
    pixels: Bitmap = []
    for y in range(height):
        row = [0] * width
        step = y % len(shifts)
        runs = [(a + shifts[step], b + shifts[step]) for a, b in base]
        if y % 4 == 3:
            runs = runs[1:]  # the first run vanishes: pass mode over it
        if y % 5 == 4:
            runs.append((width - 9, width - 1))  # a run that touches the right edge
        if y == height // 2:
            runs = [(0, width - 1)]  # an all-black row
        if y == height // 2 + 1:
            runs = []  # an all-white row
        for a, b in runs:
            for x in range(max(a, 0), min(b, width - 1) + 1):
                row[x] = 1
        pixels.append(row)
    return pixels


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("width", "height", "box"),
    [
        pytest.param(64, 16, (16, 4, 31, 11), id="box-64x16"),  # issue #440's fixture
        pytest.param(
            32, 32, None, id="white-32x32"
        ),  # width equal to the stride in bits
        pytest.param(
            33, 7, (0, 0, 32, 6), id="black-33x7"
        ),  # width just past a byte edge
    ],
)
def test_mmr_generic_region_renders(width: int, height: int, box) -> None:
    """Rendering an MMR region neither kills the process nor loses its samples."""
    assert_decodes_to(bitmap_with_box(width, height, box))


def test_mmr_generic_region_decodes_every_t6_mode() -> None:
    """Pass, horizontal and all vertical modes decode to the source bitmap."""
    assert_decodes_to(runs_bitmap(100, 24))
