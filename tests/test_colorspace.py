#!/usr/bin/env python
"""/Separation and /DeviceN colour spaces (ISO 32000-1, 8.6.6.4 and 8.6.6.5).

Both are subtractive spaces: their operands are *tints*, not colours. What
gets painted is the alternate colour space the tint transform maps those tints
onto, so a renderer that does not evaluate the transform paints the wrong
colour -- not a slightly-off colour, an unrelated one.

These tests build the PDFs in memory and assert on rendered pixels, because
the tint transform only shows up as colour.
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

# Blend2D rounds; the colours below are exact so the margin only has to absorb
# the 8-bit quantisation of the tint transform's output.
COLOR_TOLERANCE = 2


def _build_pdf(content: str, colorspace_objects: List[str]) -> bytes:
    """One-page PDF whose /ColorSpace resource /CS0 is object 5."""
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Resources << /ColorSpace << /CS0 5 0 R >> >> /Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]
    objects.extend(colorspace_objects)

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


def _build_image_pdf(image_objects: List[str]) -> bytes:
    """One-page PDF whose /XObject /Im0 is object 5, stretched over the page.

    The images below are all two pixels wide and one tall, so the left half of
    the page carries the first sample and the right half the second.
    """
    content = f"q {PAGE_WIDTH} 0 0 {PAGE_HEIGHT} 0 0 cm /Im0 Do Q\n"
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]
    objects.extend(image_objects)

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


def _image_xobject(colorspace: str, samples: str) -> str:
    return (
        "<< /Type /XObject /Subtype /Image /Width 2 /Height 1 "
        f"/BitsPerComponent 8 /ColorSpace {colorspace} /Length {len(samples)} >>\n"
        f"stream\n{samples}\nendstream"
    )


def _fill(operands: str) -> str:
    """Select /CS0, set a colour with `scn`, and cover the page."""
    return f"/CS0 cs {operands} scn 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f\n"


def _cmyk_fill(c: float, m: float, y: float, k: float) -> str:
    """Set a DeviceCMYK colour with `k` and cover the page."""
    return f"{c} {m} {y} {k} k 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f\n"


def _render(pdf_bytes: bytes) -> PILImage.Image:
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
    key = parser.load(BytesIO(pdf_bytes))
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


# ---------------------------------------------------------------------------
# the tint transform decides the colour
# ---------------------------------------------------------------------------


def test_separation_tint_goes_through_an_exponential_transform():
    """A type 2 transform at tint 1 yields /C1, here a mid blue."""
    image = _render(
        _build_pdf(
            _fill("1"),
            [
                "[/Separation /Spot /DeviceRGB 6 0 R]",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] "
                "/C1 [0.2 0.4 0.8] /N 1 >>",
            ],
        )
    )

    _assert_color(_center_pixel(image), (51, 102, 204), "full tint")


def test_separation_half_tint_interpolates_along_the_transform():
    """Half a tint is half way along the type 2 ramp, not half the ink."""
    image = _render(
        _build_pdf(
            _fill("0.5"),
            [
                "[/Separation /Spot /DeviceRGB 6 0 R]",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] "
                "/C1 [0.2 0.4 0.8] /N 1 >>",
            ],
        )
    )

    # halfway between white and (0.2, 0.4, 0.8)
    _assert_color(_center_pixel(image), (153, 179, 230), "half tint")


def test_separation_tint_goes_through_a_sampled_transform():
    """A type 0 transform interpolates between its two samples."""
    samples = "\x00\xff"
    image = _render(
        _build_pdf(
            _fill("0.5"),
            [
                "[/Separation /Spot /DeviceGray 6 0 R]",
                "<< /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] "
                f"/BitsPerSample 8 /Length {len(samples)} >>\n"
                f"stream\n{samples}\nendstream",
            ],
        )
    )

    _assert_color(_center_pixel(image), (128, 128, 128), "midpoint of the table")


def test_devicen_passes_every_colorant_to_the_transform():
    """A /DeviceN transform takes one input per colorant, not just the first.

    The type 4 program pushes a literal 0 onto the stack the two tints already
    sit on, so the three outputs are (tint_a, tint_b, 0) -- an answer that is
    only reachable if both tints arrive.
    """
    program = "{ 0 }"
    image = _render(
        _build_pdf(
            _fill("1 0.5"),
            [
                "[/DeviceN [/A /B] /DeviceRGB 6 0 R]",
                "<< /FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1] "
                f"/Length {len(program)} >>\nstream\n{program}\nendstream",
            ],
        )
    )

    _assert_color(_center_pixel(image), (255, 128, 0), "two-colorant tint")


# ---------------------------------------------------------------------------
# colorants that make no marks, and transforms that cannot be used
# ---------------------------------------------------------------------------


def test_separation_none_makes_no_marks():
    """8.6.6.4: a /None colorant paints nothing, whatever its transform says."""
    image = _render(
        _build_pdf(
            _fill("1"),
            [
                "[/Separation /None /DeviceRGB 6 0 R]",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] /C1 [1 0 0] /N 1 >>",
            ],
        )
    )

    _assert_color(_center_pixel(image), (255, 255, 255), "a /None separation")


@pytest.mark.parametrize(
    "colorspace_objects, description",
    [
        (
            [
                "[/Separation /Spot /DeviceRGB 6 0 R]",
                "<< /FunctionType 9 /Domain [0 1] >>",
            ],
            "unsupported /FunctionType",
        ),
        (
            [
                "[/Separation /Spot /DeviceRGB 6 0 R]",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 1] /C1 [0 0] /N 1 >>",
            ],
            "transform output count does not match the alternate space",
        ),
        (
            ["[/Separation /Spot /DeviceRGB]", "<< >>"],
            "no tint transform at all",
        ),
    ],
)
def test_unusable_tint_transform_falls_back_to_darkening(
    colorspace_objects, description
):
    """Without a usable transform a tint can only be approximated as ink.

    Tint 0.25 becomes a light grey rather than a colour: wrong, but monotone in
    the tint and never darker than the ink it stands for.
    """
    image = _render(_build_pdf(_fill("0.25"), colorspace_objects))

    _assert_color(_center_pixel(image), (191, 191, 191), description)


# ---------------------------------------------------------------------------
# DeviceCMYK renders like ink, not like a subtractive filter
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "cmyk, expected, subtractive, description",
    [
        ((0, 0, 0, 0), (255, 255, 255), (255, 255, 255), "paper"),
        ((0, 0, 0, 1), (35, 31, 32), (0, 0, 0), "solid black ink"),
        ((0, 0, 0, 0.5), (147, 149, 152), (128, 128, 128), "half black"),
        ((1, 0, 0, 0), (0, 173, 240), (0, 255, 255), "solid cyan"),
        ((0, 1, 0, 0), (240, 0, 142), (255, 0, 255), "solid magenta"),
        ((0, 0, 1, 0), (255, 238, 20), (255, 255, 0), "solid yellow"),
        ((1, 1, 0, 0), (56, 42, 144), (0, 0, 255), "cyan over magenta"),
        ((1, 1, 1, 0), (59, 49, 51), (0, 0, 0), "three-colour black"),
    ],
)
def test_device_cmyk_renders_as_process_ink(cmyk, expected, subtractive, description):
    """8.6.4.4 leaves DeviceCMYK to the reader; this one renders a press.

    `subtractive` is what the textbook R = (1-C)(1-K) formula would give. It is
    carried here so the test states what the conversion is *not*: solid cyan is
    #00AEEF on paper, not the #00FFFF of a perfect filter.
    """
    image = _render(_build_pdf(_cmyk_fill(*cmyk), ["<< >>"]))
    actual = _center_pixel(image)

    _assert_color(actual, expected, description)

    if expected != subtractive:
        assert actual != subtractive, (
            f"{description}: rendered the subtractive approximation {subtractive}"
        )


def test_device_cmyk_black_is_monotone():
    """More black ink is never lighter, all the way down the K ramp."""
    previous = 256
    for step in range(9):
        image = _render(_build_pdf(_cmyk_fill(0, 0, 0, step / 8), ["<< >>"]))
        value = _center_pixel(image)[0]

        assert value <= previous, f"K = {step}/8 is lighter than the step before"
        previous = value


# ---------------------------------------------------------------------------
# images are tints too
# ---------------------------------------------------------------------------

# Scaling a two-pixel image over the page costs a code value or two at the
# edges of the resampling filter, which the flat-fill tolerance does not allow.
IMAGE_COLOR_TOLERANCE = 4


def _pixel_at(image: PILImage.Image, x: int, y: int) -> Tuple[int, int, int]:
    pixel = image.getpixel((x, y))
    assert isinstance(pixel, tuple) and len(pixel) >= 3, (
        f"expected an RGB pixel at ({x}, {y}), got {pixel!r}"
    )
    red, green, blue = pixel[:3]
    return (red, green, blue)


def _image_halves(
    image: PILImage.Image,
) -> Tuple[Tuple[int, int, int], Tuple[int, int, int]]:
    """The colour of the left and right halves of a two-sample image."""
    y = image.height // 2
    return (
        _pixel_at(image, image.width // 10, y),
        _pixel_at(image, image.width * 9 // 10, y),
    )


def _assert_image_color(
    actual: Tuple[int, int, int],
    expected: Tuple[int, int, int],
    where: str,
) -> None:
    deltas = [abs(a - e) for a, e in zip(actual, expected)]
    assert max(deltas) <= IMAGE_COLOR_TOLERANCE, (
        f"{where}: expected ~{expected}, got {actual} (max delta {max(deltas)})"
    )


def test_separation_image_samples_go_through_the_tint_transform():
    """An image in a /Separation space stores tints, not grey levels.

    Two samples, 0 and 255, are the ends of the tint range: they have to come
    out as the two ends of the transform, white and /C1.
    """
    image = _render(
        _build_image_pdf(
            [
                _image_xobject("[/Separation /Spot /DeviceRGB 6 0 R]", "\x00\xff"),
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] "
                "/C1 [0.2 0.4 0.8] /N 1 >>",
            ]
        )
    )

    left, right = _image_halves(image)
    _assert_image_color(left, (255, 255, 255), "tint 0")
    _assert_image_color(right, (51, 102, 204), "tint 1")


def test_devicen_image_passes_every_colorant_per_pixel():
    """A /DeviceN image feeds all of a pixel's tints to the transform.

    The type 4 program turns tints (a, b) into (a, b, 0), so the two pixels --
    (1, 0) and (0, 1) -- can only come out red and green if both colorants of
    each pixel reach the transform together.
    """
    program = "{ 0 }"
    image = _render(
        _build_image_pdf(
            [
                _image_xobject(
                    "[/DeviceN [/A /B] /DeviceRGB 6 0 R]", "\xff\x00\x00\xff"
                ),
                "<< /FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1] "
                f"/Length {len(program)} >>\nstream\n{program}\nendstream",
            ]
        )
    )

    left, right = _image_halves(image)
    _assert_image_color(left, (255, 0, 0), "tints (1, 0)")
    _assert_image_color(right, (0, 255, 0), "tints (0, 1)")


def test_separation_image_without_a_transform_still_draws():
    """An unusable transform must not cost the artwork.

    The tints are then read as ink coverage -- 0 is bare paper and 255 is solid
    -- which is wrong in colour but still shows the image, where refusing the
    colour space altogether shows nothing.
    """
    image = _render(
        _build_image_pdf(
            [
                _image_xobject("[/Separation /Black /DeviceCMYK 6 0 R]", "\x00\xff"),
                "<< /FunctionType 9 /Domain [0 1] >>",
            ]
        )
    )

    left, right = _image_halves(image)
    _assert_image_color(left, (255, 255, 255), "no ink")
    _assert_image_color(right, (0, 0, 0), "full ink")
