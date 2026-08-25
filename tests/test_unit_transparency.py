#!/usr/bin/env python
"""Blend modes and transparency groups (ISO 32000-1, 11.3.5 and 11.6.6).

Two ExtGState parameters decide how paint reaches the page rather than what
colour it is, and both are invisible until something is already underneath.

`/BM` picks the blend function. Anything but Normal reads the backdrop, so a
reader that ignores it does not merely shift a colour -- it hides whatever the
paint was supposed to interact with.

A form XObject carrying `/Group << /S /Transparency >>` is composited as a
unit: its contents paint with a fresh alpha and blend mode, and the *result* is
put on the page with the alpha and blend mode in force at the `Do`. Content
inside such a group routinely resets `/ca` to 1, which a reader that inlines
the group would take at face value and paint opaque.

These tests assert on rendered pixels, because compositing has no other output.
"""

from io import BytesIO
from typing import List, Tuple

import pytest
from PIL import Image as PILImage

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

PAGE_WIDTH = 200
PAGE_HEIGHT = 100

# Blend2D composites in 8-bit; a code value either way is rounding.
COLOR_TOLERANCE = 2

# painted inside the transparency group: opaque black over the whole page,
# with /ca explicitly reset to 1 the way a real group's contents do
GROUP_CONTENT = f"q 0 0 0 rg /GSn gs 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f Q"


def _build_pdf(content: str) -> bytes:
    """One page with three ExtGStates and one transparency-group form."""
    form = (
        "<< /Type /XObject /Subtype /Form "
        f"/BBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Group << /Type /Group /S /Transparency /CS /DeviceRGB >> "
        "/Resources << /ExtGState << /GSn 8 0 R >> >> "
        f"/Length {len(GROUP_CONTENT)} >>\nstream\n{GROUP_CONTENT}\nendstream"
    )

    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Resources << /ExtGState << /GSm 6 0 R /GSa 7 0 R /GSn 8 0 R >> "
        "/XObject << /Fm0 9 0 R >> >> /Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        "<< >>",
        "<< /Type /ExtGState /BM /Multiply /ca 1.0 >>",
        "<< /Type /ExtGState /BM /Normal /ca 0.5 >>",
        "<< /Type /ExtGState /BM /Normal /ca 1.0 >>",
        form,
    ]

    out = b"%PDF-1.4\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


def _render(content: str) -> PILImage.Image:
    render_config = RenderConfig()
    render_config.scale = 1.0

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=1,
            render_config=render_config,
        ),
        decode_config=DecodeConfig(),
    )
    key = parser.load(BytesIO(_build_pdf(content)))
    try:
        results = list(parser.iterate_results())
        assert len(results) == 1
        return results[0].get_image().convert("RGB")
    finally:
        parser.unload(key)


def _center_pixel(image: PILImage.Image) -> Tuple[int, int, int]:
    pixel = image.getpixel((image.width // 2, image.height // 2))
    assert isinstance(pixel, tuple) and len(pixel) >= 3, (
        f"expected an RGB pixel, got {pixel!r}"
    )
    red, green, blue = pixel[:3]
    return (red, green, blue)


def _assert_color(
    actual: Tuple[int, int, int],
    expected: Tuple[int, int, int],
    where: str,
) -> None:
    deltas = [abs(a - e) for a, e in zip(actual, expected)]
    assert max(deltas) <= COLOR_TOLERANCE, (
        f"{where}: expected ~{expected}, got {actual} (max delta {max(deltas)})"
    )


def _cover(gs: str, color: str) -> str:
    return (
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f\n"
        f"q /{gs} gs {color} rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f Q\n"
    )


# ---------------------------------------------------------------------------
# /BM reads the backdrop
# ---------------------------------------------------------------------------


def test_multiply_blends_with_what_is_underneath():
    """Multiply is the product of backdrop and source, per channel.

    A mid grey over red keeps the red channel (255 x 128 / 255) and zeroes the
    other two, which they already were -- so the answer is only reachable by
    reading the backdrop.
    """
    image = _render(_cover("GSm", "0.5 0.5 0.5"))

    _assert_color(_center_pixel(image), (128, 0, 0), "grey multiplied into red")


def test_normal_blend_replaces_what_is_underneath():
    """The control: without /BM the same paint covers the backdrop."""
    image = _render(_cover("GSn", "0.5 0.5 0.5"))

    _assert_color(_center_pixel(image), (128, 128, 128), "grey over red")


@pytest.mark.parametrize(
    "source, expected, description",
    [
        ("1 1 1", (255, 0, 0), "white is the identity of Multiply"),
        ("0 0 0", (0, 0, 0), "black absorbs everything"),
        ("0 1 1", (0, 0, 0), "a colour with no red kills the red backdrop"),
    ],
)
def test_multiply_edge_colours(source, expected, description):
    image = _render(_cover("GSm", source))

    _assert_color(_center_pixel(image), expected, description)


# ---------------------------------------------------------------------------
# a transparency group is composited as a unit
# ---------------------------------------------------------------------------


def test_group_alpha_survives_its_contents_resetting_it():
    """11.6.6: `/ca` at the `Do` applies to the group, not to its operators.

    The group paints opaque black and sets `/ca 1` while doing so. What lands
    on the page is still half-transparent, because the 0.5 belongs to the
    group's result.
    """
    image = _render("q /GSa gs /Fm0 Do Q\n")

    _assert_color(_center_pixel(image), (128, 128, 128), "group at half alpha")


def test_group_without_alpha_paints_opaque():
    """The control: the same group at full alpha covers the page."""
    image = _render("q /GSn gs /Fm0 Do Q\n")

    _assert_color(_center_pixel(image), (0, 0, 0), "group at full alpha")
