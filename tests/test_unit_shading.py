#!/usr/bin/env python
"""The `sh` shading-paint operator (ISO 32000-1, 8.7.4.5).

`sh` fills the *current clipping region* with a shading resolved from the
resource dictionary's /Shading entry. Its colours come from the shading's own
colour space and function, never from the current fill colour, and its
geometry lives in the user space in force at the operator -- so the CTM has to
reach the gradient, not just the clip.

These tests build the PDFs in memory: a coloured page background, a clip, and
one axial or radial shading over it. They assert on rendered pixels rather than
on the instruction stream, because the whole point of the operator is that
pixels change.
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

# The colour ramp used by every shading below: pure red at t=0, pure blue at
# t=1, through an exponential (type 2) function.
RED = (255, 0, 0)
BLUE = (0, 0, 255)

# Background painted before the shading, so "nothing was painted" is
# distinguishable from "painted white".
GREEN = (0, 255, 0)

# Blend2D and the reference ramp differ by rounding and by the resampling of
# the colour ramp into gradient stops.
COLOR_TOLERANCE = 12


def _build_pdf(content: str, shading_objects: str) -> bytes:
    """One-page PDF whose /Shading resources are `shading_objects` (objects 5+)."""
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Resources << /Shading << /Sh0 5 0 R >> >> /Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]
    objects.extend(shading_objects)

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


def _axial_shading(
    coords: str = "[0 0 200 0]", extend: str = "[true true]"
) -> List[str]:
    return [
        "<< /ShadingType 2 /ColorSpace /DeviceRGB "
        f"/Coords {coords} /Domain [0 1] /Extend {extend} "
        "/Function 6 0 R >>",
        "<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
    ]


def _radial_shading() -> List[str]:
    return [
        "<< /ShadingType 3 /ColorSpace /DeviceRGB "
        "/Coords [100 50 0 100 50 80] /Domain [0 1] /Extend [true true] "
        "/Function 6 0 R >>",
        "<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
    ]


def _background() -> str:
    return f"0 1 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f\n"


def _render(pdf_bytes: bytes, scale: float = 1.0) -> PILImage.Image:
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=1,
            render_config=_render_config(scale),
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


def _render_config(scale: float) -> RenderConfig:
    render_config = RenderConfig()
    render_config.scale = scale
    return render_config


def _pixel(image: PILImage.Image, x: float, y: float) -> Tuple[int, int, int]:
    """Sample at PDF coordinates (y-up) on a page-sized canvas."""
    px = min(image.width - 1, max(0, int(x)))
    py = min(image.height - 1, max(0, int(image.height - y)))

    # getpixel() is a scalar on single-band images; every image here comes from
    # _render(), which converts to RGB, so a triple is the only valid answer
    pixel = image.getpixel((px, py))
    assert isinstance(pixel, tuple) and len(pixel) >= 3, (
        f"expected an RGB pixel at ({px}, {py}), got {pixel!r}"
    )

    red, green, blue = pixel[:3]
    return (red, green, blue)


def _assert_color(
    actual: Tuple[int, int, int],
    expected: Tuple[int, int, int],
    where: str,
    tolerance: int = COLOR_TOLERANCE,
) -> None:
    deltas = [abs(a - e) for a, e in zip(actual, expected)]
    assert max(deltas) <= tolerance, (
        f"{where}: expected ~{expected}, got {actual} (max delta {max(deltas)})"
    )


# ---------------------------------------------------------------------------
# painting and clipping
# ---------------------------------------------------------------------------


def test_axial_shading_is_painted_inside_a_nonzero_clip():
    """`W n` establishes the region; `sh` fills exactly that region."""
    content = _background() + "q\n" + "50 25 100 50 re\nW\nn\n" + "/Sh0 sh\n" + "Q\n"
    image = _render(_build_pdf(content, _axial_shading()))

    # inside the clip: the ramp, red-ish on the left, blue-ish on the right
    inside_left = _pixel(image, 55, 50)
    inside_right = _pixel(image, 145, 50)
    assert inside_left[0] > inside_left[2], (
        f"left of ramp is not red-ish: {inside_left}"
    )
    assert inside_right[2] > inside_right[0], (
        f"right of ramp is not blue-ish: {inside_right}"
    )

    # outside the clip the background survives
    _assert_color(_pixel(image, 10, 50), GREEN, "left of the clip")
    _assert_color(_pixel(image, 190, 50), GREEN, "right of the clip")
    _assert_color(_pixel(image, 100, 10), GREEN, "below the clip")
    _assert_color(_pixel(image, 100, 90), GREEN, "above the clip")


def test_axial_shading_is_painted_inside_an_even_odd_clip():
    """`W*` selects the even-odd rule; a single rectangle clips the same way."""
    content = _background() + "q\n" + "50 25 100 50 re\nW*\nn\n" + "/Sh0 sh\n" + "Q\n"
    image = _render(_build_pdf(content, _axial_shading()))

    inside = _pixel(image, 100, 50)
    _assert_color(_pixel(image, 10, 50), GREEN, "outside the even-odd clip")
    assert inside != GREEN, "the even-odd clipped shading was not painted"


def test_axial_shading_is_painted_inside_a_triangular_clip():
    """A clip is a path, not a rectangle: `sh` fills whatever shape it has."""
    content = (
        _background()
        + "q\n"
        + "20 10 m 180 10 l 100 90 l h\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    image = _render(_build_pdf(content, _axial_shading()))

    # inside the triangle the ramp runs red to blue as everywhere else
    _assert_color(_pixel(image, 30, 12), (216, 0, 39), "left inside the triangle")
    _assert_color(_pixel(image, 170, 12), (38, 0, 218), "right inside the triangle")

    # the corners of the triangle's bounding box are outside the triangle, so
    # a clip reduced to that box would show there
    _assert_color(_pixel(image, 25, 80), GREEN, "above-left of the hypotenuse")
    _assert_color(_pixel(image, 175, 80), GREEN, "above-right of the hypotenuse")
    _assert_color(_pixel(image, 100, 5), GREEN, "below the triangle")


def test_shading_does_not_leak_outside_the_clip_after_Q():
    """The clip is part of the graphics state: after `Q` the page is untouched."""
    content = (
        _background()
        + "q\n"
        + "50 25 100 50 re\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
        + "0 1 0 rg\n"  # no further painting
    )
    image = _render(_build_pdf(content, _axial_shading()))

    for x in (5, 25, 45, 155, 175, 195):
        _assert_color(_pixel(image, x, 50), GREEN, f"x={x} outside the clip")


# ---------------------------------------------------------------------------
# transformations
# ---------------------------------------------------------------------------


def test_flipped_ctm_mirrors_the_shading_axis():
    """`1 0 0 -1 0 h cm` flips y; the axis must follow the CTM, not the clip."""
    # A vertical axis under a y-flip: red ends up at the top instead of the
    # bottom. The flip maps user y to PAGE_HEIGHT - y, so the shading is
    # authored in the flipped frame.
    shading = _axial_shading(coords=f"[0 0 0 {PAGE_HEIGHT}]")

    flipped = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + f"1 0 0 -1 0 {PAGE_HEIGHT} cm\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    upright = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )

    flipped_image = _render(_build_pdf(flipped, shading))
    upright_image = _render(_build_pdf(upright, shading))

    # upright: red at the bottom of the page, blue at the top
    bottom = _pixel(upright_image, 100, 5)
    top = _pixel(upright_image, 100, 95)
    assert bottom[0] > bottom[2], f"upright bottom is not red-ish: {bottom}"
    assert top[2] > top[0], f"upright top is not blue-ish: {top}"

    # flipped: the same ramp, mirrored
    bottom = _pixel(flipped_image, 100, 5)
    top = _pixel(flipped_image, 100, 95)
    assert bottom[2] > bottom[0], f"flipped bottom is not blue-ish: {bottom}"
    assert top[0] > top[2], f"flipped top is not red-ish: {top}"


def test_scaled_ctm_moves_the_shading_axis():
    """A `cm` scale applies to the shading coordinates, not only to the clip."""
    # The axis spans x in [0, 200] in shading space; under `0.5 0 0 1 0 0 cm`
    # it covers x in [0, 100] on the page, so the ramp finishes at mid-page.
    content = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + "0.5 0 0 1 0 0 cm\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    image = _render(_build_pdf(content, _axial_shading()))

    # at page x=100 the shading parameter is already 1.0 -> blue, and beyond
    # that /Extend keeps it blue
    _assert_color(_pixel(image, 150, 50), BLUE, "beyond the scaled axis")
    _assert_color(_pixel(image, 1, 50), RED, "at the start of the scaled axis")


# ---------------------------------------------------------------------------
# shading variants
# ---------------------------------------------------------------------------


def test_radial_shading_is_painted():
    """/ShadingType 3 paints from the inner circle outwards."""
    content = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    image = _render(_build_pdf(content, _radial_shading()))

    center = _pixel(image, 100, 50)
    assert center[0] > center[2], f"radial center is not red-ish: {center}"

    # beyond the outer circle /Extend keeps the end colour
    _assert_color(_pixel(image, 195, 50), BLUE, "outside the radial circle")


def test_extend_false_leaves_the_area_beyond_the_axis_unpainted():
    """/Extend [false false] paints only between the two axis endpoints."""
    shading = _axial_shading(coords="[80 0 120 0]", extend="[false false]")
    content = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    image = _render(_build_pdf(content, shading))

    _assert_color(_pixel(image, 20, 50), GREEN, "before the un-extended axis")
    _assert_color(_pixel(image, 180, 50), GREEN, "after the un-extended axis")

    middle = _pixel(image, 100, 50)
    assert middle != GREEN, f"the shading interior was not painted: {middle}"


def test_shading_resources_are_inherited_from_the_page_tree():
    """A /Shading defined on an ancestor /Pages node resolves on the page."""
    content = (
        _background()
        + "q\n"
        + f"0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re\nW\nn\n"
        + "/Sh0 sh\n"
        + "Q\n"
    )
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 "
        "/Resources << /Shading << /Sh0 5 0 R >> >> >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]
    objects.extend(_axial_shading())

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

    image = _render(out)
    _assert_color(_pixel(image, 1, 50), RED, "start of the inherited shading")
    _assert_color(_pixel(image, 198, 50), BLUE, "end of the inherited shading")


# ---------------------------------------------------------------------------
# failure modes: rendering continues, the page is not corrupted
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "shading_objects, description",
    [
        (
            [
                "<< /ShadingType 4 /ColorSpace /DeviceRGB /BitsPerCoordinate 16 "
                "/BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 200 0 100 0 1 0 1 0 1] >>",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
            ],
            "unsupported /ShadingType 4",
        ),
        (
            [
                "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] "
                "/Domain [0 1] /Extend [true true] /Function 6 0 R >>",
                "<< /FunctionType 9 /Domain [0 1] /Range [0 1 0 1 0 1] >>",
            ],
            "unsupported /FunctionType",
        ),
        (
            [
                "<< /ShadingType 2 /ColorSpace /DeviceRGB "
                "/Domain [0 1] /Extend [true true] /Function 6 0 R >>",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
            ],
            "missing /Coords",
        ),
    ],
)
def test_unpaintable_shadings_leave_the_page_intact(shading_objects, description):
    """An undecodable shading is skipped; the rest of the page still renders."""
    content = _background() + "q\n" + "50 25 100 50 re\nW\nn\n" + "/Sh0 sh\n" + "Q\n"
    image = _render(_build_pdf(content, shading_objects))

    _assert_color(_pixel(image, 100, 50), GREEN, f"inside the clip ({description})")
    _assert_color(_pixel(image, 10, 50), GREEN, f"outside the clip ({description})")


def test_missing_shading_resource_is_skipped():
    """A name that no /Shading entry defines paints nothing and does not throw."""
    content = (
        _background()
        + "q\n"
        + "50 25 100 50 re\nW\nn\n"
        + "/MissingShading sh\n"
        + "Q\n"
    )
    image = _render(_build_pdf(content, _axial_shading()))

    _assert_color(_pixel(image, 100, 50), GREEN, "inside the clip")
    _assert_color(_pixel(image, 10, 50), GREEN, "outside the clip")


def test_unclipped_shading_covers_the_whole_page():
    """Without a clip, `sh` paints everywhere -- the default clip is the page."""
    content = _background() + "/Sh0 sh\n"
    image = _render(_build_pdf(content, _axial_shading()))

    _assert_color(_pixel(image, 1, 50), RED, "left page edge")
    _assert_color(_pixel(image, 198, 50), BLUE, "right page edge")
    _assert_color(_pixel(image, 1, 95), RED, "top-left page corner")
