#!/usr/bin/env python
"""ExtGState soft masks (ISO 32000-1, 11.6.5).

An ExtGState `/SMask` names a transparency group whose luminosity (or alpha)
becomes the coverage of everything painted while the mask is in force. The
mask's coordinate system is the CTM at the `gs` that installed it, its `/BBox`
bounds it (outside, the `/BC` backdrop shows), and `/TR` remaps the result.

InDesign builds drop shadows and feathered photo edges from exactly this, so a
reader that ignores the mask paints the effect as an opaque box over the
photo. These tests assert on rendered pixels, since compositing has no other
output.
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

# Blend2D composites in 8-bit: 0.5 of 255 lands on 127 or 128 depending on
# the rounding of each step, and a gradient sample is a pixel wide.
COLOR_TOLERANCE = 3

RED = (255, 0, 0)
BLUE = (0, 0, 255)
HALF_BLUE_ON_RED = (128, 0, 128)

# a red page with a blue box in the middle, painted under /G1
BOX = f"0 0 1 rg 50 25 {PAGE_WIDTH // 2} {PAGE_HEIGHT // 2} re f"
MASKED_BOX = f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs {BOX}"

FULL_BBOX = f"[0 0 {PAGE_WIDTH} {PAGE_HEIGHT}]"


def _stream(entries: str, data: str) -> str:
    return f"<< {entries} /Length {len(data)} >>\nstream\n{data}\nendstream"


def _group(content: str, bbox: str = FULL_BBOX, extra: str = "") -> str:
    """A transparency-group form XObject, the /G of a soft mask."""
    return _stream(
        "/Type /XObject /Subtype /Form "
        f"/BBox {bbox} /Group << /S /Transparency /CS /DeviceGray >> {extra}",
        content,
    )


def _build_pdf(content: str, resources: str, objects: List[str]) -> bytes:
    """One page. Objects 1-4 are catalog, pages, page and content; `objects`
    follow from 5 on, which is how `resources` refers to them."""
    all_objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        f"/Resources << {resources} >> /Contents 4 0 R >>",
        _stream("", content),
        *objects,
    ]

    out = b"%PDF-1.7\n"
    offsets = []
    for index, obj in enumerate(all_objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(all_objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(all_objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


def _render(pdf: bytes, render_soft_masks: bool = True) -> PILImage.Image:
    render_config = RenderConfig()
    render_config.scale = 1.0
    render_config.render_soft_masks = render_soft_masks

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=1,
            render_config=render_config,
        ),
        decode_config=DecodeConfig(),
    )
    key = parser.load(BytesIO(pdf))
    try:
        results = list(parser.iterate_results())
        assert len(results) == 1
        return results[0].get_image().convert("RGB")
    finally:
        parser.unload(key)


def _pixel(image: PILImage.Image, x: int, y: int) -> Tuple[int, int, int]:
    """The pixel at PDF user-space coordinates (x, y), y up."""
    pixel = image.getpixel((x, PAGE_HEIGHT - 1 - y))
    assert isinstance(pixel, tuple) and len(pixel) >= 3, (
        f"expected an RGB pixel, got {pixel!r}"
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


def _smask(group_ref: str, extra: str = "") -> str:
    return f"<< /SMask << /S /Luminosity /G {group_ref} {extra} >> >>"


# ---------------------------------------------------------------------------
# luminosity is coverage
# ---------------------------------------------------------------------------


def test_luminosity_mask_halves_the_paint():
    """The page of docling-parse issue #384: a 50% grey mask lets half of the
    blue through, as pdfium and MuPDF draw it."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [_smask("6 0 R"), _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f")],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "inside the box")
    _assert_color(_pixel(image, 10, 10), RED, "outside the box")


def test_mask_is_read_where_it_is_painted():
    """A mask that is black on the left and white on the right hides the left
    half of the box and leaves the right half opaque."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R"),
            _group(
                f"0 g 0 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f "
                f"1 g {PAGE_WIDTH // 2} 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f"
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 75, 50), RED, "under the black half")
    _assert_color(_pixel(image, 125, 50), BLUE, "under the white half")


def test_gradient_mask_feathers_the_edge():
    """An axial shading in the group fades the box in from left to right."""
    shading = (
        "<< /Sh0 << /ShadingType 2 /ColorSpace /DeviceGray /Coords [50 0 150 0] "
        "/Function << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >> "
        "/Extend [true true] >> >>"
    )
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R"),
            _group("/Sh0 sh", extra=f"/Resources << /Shading {shading} >>"),
        ],
    )
    image = _render(pdf)

    blues = [_pixel(image, x, 50)[2] for x in (60, 100, 140)]
    assert blues[0] < blues[1] < blues[2], f"blue should rise left to right: {blues}"
    assert blues[2] - blues[0] > 150, f"the fade should span most of the range: {blues}"
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "mid-fade", tolerance=8)


def test_strokes_are_masked_too():
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs "
        "0 0 1 RG 20 w 50 50 m 150 50 l S",
        "/ExtGState << /G1 5 0 R >>",
        [_smask("6 0 R"), _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f")],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "on the stroke")
    _assert_color(_pixel(image, 100, 80), RED, "beside the stroke")


def test_images_are_masked_too():
    """A photo under a mask is the InDesign case: a 1x1 blue image scaled to
    the box."""
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs "
        "q 100 0 0 50 50 25 cm /Im1 Do Q",
        "/ExtGState << /G1 5 0 R >> /XObject << /Im1 7 0 R >>",
        [
            _smask("6 0 R"),
            _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
            _stream(
                "/Type /XObject /Subtype /Image /Width 1 /Height 1 "
                "/ColorSpace /DeviceRGB /BitsPerComponent 8",
                "\x00\x00\xff",
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "inside the image")


def _circle_path(cx: float, cy: float, r: float) -> str:
    """Four-bezier circle, as a PDF path without a painting operator."""
    k = 0.5523 * r
    return (
        f"{cx + r} {cy} m "
        f"{cx + r} {cy + k} {cx + k} {cy + r} {cx} {cy + r} c "
        f"{cx - k} {cy + r} {cx - r} {cy + k} {cx - r} {cy} c "
        f"{cx - r} {cy - k} {cx - k} {cy - r} {cx} {cy - r} c "
        f"{cx + k} {cy - r} {cx + r} {cy - k} {cx + r} {cy} c h"
    )


def test_mask_combines_with_a_curved_clip():
    """Inside a circular clip the mask applies; outside it nothing paints.
    The curved clip takes the renderer's coverage-mask route, so this is the
    mask layer inside the soft-mask layer."""
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f "
        f"{_circle_path(100, 50, 20)} W n /G1 gs {BOX}",
        "/ExtGState << /G1 5 0 R >>",
        [_smask("6 0 R"), _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f")],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "inside the clip")
    _assert_color(_pixel(image, 60, 35), RED, "in the box but outside the clip")


# ---------------------------------------------------------------------------
# /BBox, /BC, /TR and /S
# ---------------------------------------------------------------------------


def test_outside_the_bbox_the_backdrop_shows():
    """The group covers the left half only. Beyond its /BBox the mask is the
    backdrop, black by default, so the right half of the box vanishes."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R"),
            _group(
                f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f",
                bbox=f"[0 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT}]",
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 75, 50), HALF_BLUE_ON_RED, "inside the bbox")
    _assert_color(_pixel(image, 125, 50), RED, "outside the bbox")


def test_bc_sets_the_backdrop():
    """With /BC white, beyond the /BBox the mask is fully opaque."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R", extra="/BC [1]"),
            _group(
                f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f",
                bbox=f"[0 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT}]",
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 75, 50), HALF_BLUE_ON_RED, "inside the bbox")
    _assert_color(_pixel(image, 125, 50), BLUE, "outside the bbox")


def test_tr_remaps_the_luminosity():
    """An inverting /TR turns a 25% grey group into 75% coverage."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask(
                "6 0 R",
                extra="/TR << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>",
            ),
            _group(f"0.25 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), (64, 0, 191), "inside the box", tolerance=4)


def test_alpha_mask_reads_the_group_alpha():
    """/S /Alpha takes the group's alpha: a fill at /ca 0.5 is 50% coverage."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            "<< /SMask << /S /Alpha /G 6 0 R >> >>",
            _group(
                f"/GA gs 0 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f",
                extra="/Resources << /ExtGState << /GA << /ca 0.5 >> >> >>",
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "inside the box")


# ---------------------------------------------------------------------------
# the graphics state
# ---------------------------------------------------------------------------


def test_smask_none_clears_the_mask():
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs /G2 gs {BOX}",
        "/ExtGState << /G1 5 0 R /G2 7 0 R >>",
        [
            _smask("6 0 R"),
            _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
            "<< /SMask /None >>",
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), BLUE, "after /SMask /None")


def test_mask_is_part_of_the_saved_state():
    objects = [
        _smask("6 0 R"),
        _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
    ]
    resources = "/ExtGState << /G1 5 0 R >>"
    red_page = f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f "

    # installed inside q/Q: gone after the Q
    image = _render(_build_pdf(red_page + f"q /G1 gs Q {BOX}", resources, objects))
    _assert_color(_pixel(image, 100, 50), BLUE, "after Q restored the state")

    # installed before a q/Q pair: still there after the Q
    image = _render(
        _build_pdf(red_page + f"/G1 gs q 1 0 0 1 0 0 cm Q {BOX}", resources, objects)
    )
    _assert_color(
        _pixel(image, 100, 50), HALF_BLUE_ON_RED, "mask survives a nested q/Q"
    )


def test_mask_space_is_the_ctm_at_gs():
    """The mask is black left of x=100 and white right of it in its own
    space. Installed under a CTM shifted right by 50, its edge sits at page
    x=150, so the whole box is under black even though the box is painted
    after the shift is undone. Reading the mask at paint time would put the
    edge at x=100 and show the right half."""
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f "
        f"1 0 0 1 50 0 cm /G1 gs 1 0 0 1 -50 0 cm {BOX}",
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R"),
            _group(
                f"0 g 0 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f "
                f"1 g {PAGE_WIDTH // 2} 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f"
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 75, 50), RED, "left of the box")
    _assert_color(_pixel(image, 125, 50), RED, "right of the box, still under black")


def test_mask_applies_inside_a_form_xobject():
    """A mask installed on the page masks what a form paints."""
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs /Fm1 Do",
        "/ExtGState << /G1 5 0 R >> /XObject << /Fm1 7 0 R >>",
        [
            _smask("6 0 R"),
            _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
            _stream(f"/Type /XObject /Subtype /Form /BBox {FULL_BBOX}", BOX),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "inside the form's box")


def test_mask_inside_the_mask_group_is_not_applied():
    """The group's own ExtGState sets a mask that would hide everything; it
    is reported and skipped, and the outer mask still applies."""
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [
            _smask("6 0 R"),
            _group(
                f"/GN gs 0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f",
                extra="/Resources << /ExtGState << /GN 7 0 R >> >>",
            ),
            _smask("8 0 R"),
            _group(f"0 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), HALF_BLUE_ON_RED, "outer mask only")


@pytest.mark.parametrize(
    ("inner_gs", "expected", "description"),
    [
        ("", HALF_BLUE_ON_RED, "no mask of its own"),
        ("/GN gs", HALF_BLUE_ON_RED, "/SMask /None inside"),
        ("/G1 gs", (191, 0, 64), "the same 50% mask again"),
    ],
)
def test_mask_at_the_do_masks_a_transparency_group(
    inner_gs: str, expected: Tuple[int, int, int], description: str
):
    """A transparency group painted under a mask is masked as a whole (11.6.6).
    A mask its content installs, or clears, acts inside the group only: a
    50% mask inside a group drawn under a 50% mask leaves a quarter of the
    blue, which is what pdfium shows."""
    pdf = _build_pdf(
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f /G1 gs /Fm1 Do",
        "/ExtGState << /G1 5 0 R /GN << /SMask /None >> >> /XObject << /Fm1 7 0 R >>",
        [
            _smask("6 0 R"),
            _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"),
            _stream(
                f"/Type /XObject /Subtype /Form /BBox {FULL_BBOX} "
                "/Group << /S /Transparency /CS /DeviceRGB >>",
                f"{inner_gs} {BOX}",
            ),
        ],
    )
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), expected, description)


# ---------------------------------------------------------------------------
# the switch
# ---------------------------------------------------------------------------


def test_render_soft_masks_off_paints_unmasked():
    pdf = _build_pdf(
        MASKED_BOX,
        "/ExtGState << /G1 5 0 R >>",
        [_smask("6 0 R"), _group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f")],
    )
    image = _render(pdf, render_soft_masks=False)
    _assert_color(_pixel(image, 100, 50), BLUE, "masks switched off")


@pytest.mark.parametrize(
    "smask, description",
    [
        ("<< /SMask << /S /Luminosity >> >>", "no /G"),
        ("<< /SMask 42 >>", "not a dictionary"),
    ],
)
def test_unreadable_mask_paints_unmasked(smask: str, description: str):
    pdf = _build_pdf(MASKED_BOX, "/ExtGState << /G1 5 0 R >>", [smask])
    image = _render(pdf)
    _assert_color(_pixel(image, 100, 50), BLUE, description)


def test_decodes_per_page_are_capped():
    """A page can install a new mask with every `gs` for next to nothing.
    Past 512 distinct masks the rest of the page paints unmasked: the box
    under the first mask is half-blended, the one under the 600th is opaque."""
    count = 600
    objects: List[str] = []
    resources = []
    for index in range(1, count + 1):
        smask_number = 5 + 2 * (index - 1)
        objects.append(_smask(f"{smask_number + 1} 0 R"))
        objects.append(_group(f"0.5 g 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f"))
        resources.append(f"/G{index} {smask_number} 0 R")

    content = (
        f"1 0 0 rg 0 0 {PAGE_WIDTH} {PAGE_HEIGHT} re f "
        f"/G1 gs 0 0 1 rg 0 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f "
        + " ".join(f"/G{index} gs" for index in range(2, count))
        + f" /G{count} gs 0 0 1 rg {PAGE_WIDTH // 2} 0 {PAGE_WIDTH // 2} {PAGE_HEIGHT} re f"
    )
    pdf = _build_pdf(content, f"/ExtGState << {' '.join(resources)} >>", objects)
    image = _render(pdf)
    _assert_color(_pixel(image, 50, 50), HALF_BLUE_ON_RED, "under the first mask")
    _assert_color(_pixel(image, 150, 50), BLUE, "past the per-page cap")
