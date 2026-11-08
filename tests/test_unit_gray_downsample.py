"""Opaque grayscale box averaging agrees with the generic alpha path."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object


def _gray_page(
    source: tuple[int, int], target: tuple[int, int], pattern: str, masked: bool
) -> bytes:
    width, height = source
    draw_width, draw_height = target
    if pattern == "black":
        samples = bytes(width * height)
    elif pattern == "white":
        samples = bytes([255]) * (width * height)
    else:
        samples = bytes(
            (x * 37 + y * 53 + x * y) % 256 for y in range(height) for x in range(width)
        )
    dictionary = (
        "/Type /XObject /Subtype /Image "
        f"/Width {width} /Height {height} "
        "/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode"
    )
    image = stream_object(
        dictionary + (" /SMask 6 0 R" if masked else ""), zlib.compress(samples)
    )
    objects: list[str | bytes] = [image]
    if masked:
        # Fully opaque alpha forces the generic path without changing pixels.
        objects.append(
            stream_object(dictionary, zlib.compress(bytes([255]) * (width * height)))
        )
    return simple_page_pdf(
        f"q\n{draw_width} 0 0 {draw_height} 2 2 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        media_box=f"[0 0 {draw_width + 4} {draw_height + 4}]",
        extra_objects=objects,
    )


@pytest.mark.parametrize("pattern", ["black", "white", "gradient"])
@pytest.mark.parametrize(
    "source, target",
    [
        ((5, 3), (2, 1)),
        ((17, 19), (5, 7)),
        ((64, 65), (16, 13)),
        ((127, 95), (17, 11)),
        ((31, 17), (8, 17)),
        ((31, 17), (31, 5)),
        ((31, 17), (31, 17)),
        ((1, 17), (1, 5)),
        ((17, 1), (5, 1)),
        ((7, 5), (1, 1)),
    ],
)
def test_gray_downsample_matches_opaque_alpha_reference(
    source: tuple[int, int], target: tuple[int, int], pattern: str
) -> None:
    actual = render_page(
        _gray_page(source, target, pattern, False), scale=1
    ).get_image()
    expected = render_page(
        _gray_page(source, target, pattern, True), scale=1
    ).get_image()
    assert actual.size == expected.size
    assert actual.mode == expected.mode
    assert actual.tobytes() == expected.tobytes()
