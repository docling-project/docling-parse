#!/usr/bin/env python
"""Pattern colour spaces (PDF 32000-1, 8.7.3).

A tiling pattern is a content stream replayed on a lattice. Nothing replayed it
before, so a banner painted with one came out empty; and a `scn` naming a
pattern the parser could not resolve left the fill colour at its initial value,
which painted the region solid black -- far worse than leaving it alone.

The lattice is the part worth pinning. A first version derived the cell index
range in one direction only, so the row at index -1 was never emitted and the
banner was tiled across but not down: coverage that looks plausible in total
while half the lattice is missing. These tests compare the halves of the filled
area against each other, which is what that bug breaks.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import parse_page, render_page, simple_page_pdf, stream_object
from tests.rendering_regression import (
    assert_color_near,
    center_color,
    coverage_ratio,
    region_image,
)

CELL = 20.0
INK = 10.0

# the painted area, inset from the page edge, in top-left page coordinates
FILL_BOX = (20.0, 20.0, 180.0, 180.0)

# each cell paints an INK x INK square out of a CELL x CELL step
CELL_COVERAGE = (INK * INK) / (CELL * CELL)


def _tiling_pattern_object(paint: str = "1 0 0 rg") -> bytes:
    return stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"{paint}\n0 0 {INK} {INK} re f\n".encode("latin-1"),
    )


def _pattern_filled_page(pattern_object: bytes) -> bytes:
    content = (
        "/Pattern cs /P0 scn\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
    )
    return simple_page_pdf(
        content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[pattern_object],
    )


def test_tiling_pattern_is_painted():
    """A fill in a tiling pattern space paints the pattern cell, repeatedly."""
    image = region_image(
        render_page(_pattern_filled_page(_tiling_pattern_object())), FILL_BOX
    )

    coverage = coverage_ratio(image)
    assert coverage > 0.05, (
        f"the tiling pattern painted almost nothing ({coverage:.3f} coverage); "
        "the fill region is empty"
    )
    assert coverage == pytest.approx(CELL_COVERAGE, abs=0.08), (
        f"tiled coverage {coverage:.3f} does not match the {CELL_COVERAGE:.3f} "
        "the pattern cell paints out of its step"
    )


def test_tiling_lattice_covers_both_directions():
    """The lattice repeats across *and* down the filled region.

    Comparing the halves catches a lattice that only advances one way: total
    coverage stays believable while an entire direction is missing.
    """
    image = region_image(
        render_page(_pattern_filled_page(_tiling_pattern_object())), FILL_BOX
    )
    width, height = image.size

    left = coverage_ratio(image.crop((0, 0, width // 2, height)))
    right = coverage_ratio(image.crop((width // 2, 0, width, height)))
    top = coverage_ratio(image.crop((0, 0, width, height // 2)))
    bottom = coverage_ratio(image.crop((0, height // 2, width, height)))

    assert left == pytest.approx(right, abs=0.06), (
        f"the lattice does not repeat horizontally: left {left:.3f} vs "
        f"right {right:.3f}"
    )
    assert top == pytest.approx(bottom, abs=0.06), (
        f"the lattice does not repeat vertically: top {top:.3f} vs bottom {bottom:.3f}"
    )


PAGE = 200.0

RED = (255, 0, 0)
WHITE = (255, 255, 255)


def _sample(result, x: float, y: float, *, half: float = 2.0):
    """The colour at one point of the page, given in PDF coordinates."""
    return center_color(
        region_image(result, (x - half, PAGE - y - half, x + half, PAGE - y + half))
    )


def _assert_lattice(
    result,
    ink: list[tuple[float, float]],
    gaps: list[tuple[float, float]],
    *,
    what: str,
) -> None:
    """Every `ink` point carries a cell, every `gaps` point falls between them.

    Coverage alone cannot say this: a lattice translated by any amount covers
    exactly as much of the page as one in the right place, which is why a
    displaced lattice went unnoticed.
    """
    for x, y in ink:
        assert_color_near(
            _sample(result, x, y),
            RED,
            tolerance=40,
            what=f"{what}: ({x}, {y}) should be inside a cell's ink",
        )
    for x, y in gaps:
        assert_color_near(
            _sample(result, x, y),
            WHITE,
            tolerance=40,
            what=f"{what}: ({x}, {y}) should fall between cells",
        )


def test_pattern_matrix_is_relative_to_the_page():
    """/Matrix maps pattern space to the page's default space, not the CTM.

    The pattern is anchored to the page, so a `cm` in force when the pattern is
    used must not move the lattice. Painting the same pattern through a
    translated CTM has to leave the same tiles in the same places -- the same
    places, not merely the same number of them.
    """
    plain = _pattern_filled_page(_tiling_pattern_object())
    shifted_content = (
        "q\n1 0 0 1 7 11 cm\n"
        "/Pattern cs /P0 scn\n"
        f"{FILL_BOX[0] - 7} {FILL_BOX[0] - 11} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\nQ\n"
    )
    shifted = simple_page_pdf(
        shifted_content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[_tiling_pattern_object()],
    )

    plain_result = render_page(plain)
    shifted_result = render_page(shifted)

    # /Matrix is the identity here, so a cell's ink covers [20k, 20k + 10] in
    # both directions and the rest of each step is bare page.
    ink = [(25.0, 25.0), (65.0, 45.0)]
    gaps = [(35.0, 35.0), (75.0, 55.0)]
    _assert_lattice(plain_result, ink, gaps, what="pattern filled without a CTM")
    _assert_lattice(shifted_result, ink, gaps, what="pattern filled through a CTM")

    plain_image = region_image(plain_result, FILL_BOX)
    shifted_image = region_image(shifted_result, FILL_BOX)

    assert coverage_ratio(plain_image) == pytest.approx(
        coverage_ratio(shifted_image), abs=0.05
    ), "a CTM in force when the pattern is used moved the lattice"


def test_lattice_is_anchored_where_the_pattern_matrix_puts_it():
    """The cells land where /Matrix says, whatever the CTM at the fill.

    Pattern space is anchored to the space the content stream started in
    (8.7.3.1). Anchoring it to the CTM at the painting operation instead moves
    every cell by that operation's own translation, reduced modulo the step:
    the tiling stays the right size and covers the right area, and is out of
    phase with the artwork it fills. Figure L.8 on page 745 of the PDF
    specification is a page of card suits sitting in the wrong places.
    """
    origin_x, origin_y = 5.0, 7.0
    fill_dx, fill_dy = 13.0, 3.0  # not a multiple of the step, so a wrong
    #                               anchor cannot coincide with the right one

    pattern = stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        f"/Resources << >> /Matrix [1 0 0 1 {origin_x} {origin_y}]",
        f"1 0 0 rg\n0 0 {INK} {INK} re f\n".encode("latin-1"),
    )

    content = (
        "q\n"
        f"1 0 0 1 {fill_dx} {fill_dy} cm\n"
        "/Pattern cs /P0 scn\n"
        f"{-fill_dx} {-fill_dy} {PAGE} {PAGE} re f\n"
        "Q\n"
    )
    result = render_page(
        simple_page_pdf(
            content,
            resources="/Pattern << /P0 5 0 R >>",
            extra_objects=[pattern],
        )
    )

    # ink covers [5 + 20k, 15 + 20k] across and [7 + 20k, 17 + 20k] down.
    # Anchoring to the fill's CTM instead would put it at [18 + 20k] across and
    # [10 + 20k] down, which is what these points are chosen to separate.
    _assert_lattice(
        result,
        ink=[(50.0, 52.0), (110.0, 112.0)],
        gaps=[(60.0, 55.0), (120.0, 115.0)],
        what="a pattern painted through a translated CTM",
    )


def _solid_pattern_object() -> bytes:
    """A cell that paints its whole step, so any leak outside the fill shows."""
    return stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"1 0 0 rg\n0 0 {CELL} {CELL} re f\n".encode("latin-1"),
    )


def test_pattern_is_bounded_by_the_filled_path():
    """The lattice paints only inside the path being filled (8.7.3.1).

    The cells are laid out to cover the page box, so without the filled path
    bounding them the pattern tiles the entire page: a document whose pattern
    fill was one small shape came out with a block of cells in the corner that
    no other renderer draws.
    """
    content = "/Pattern cs /P0 scn\n80 80 40 40 re f\n"
    pdf = simple_page_pdf(
        content,
        resources="/Pattern << /P0 5 0 R >>",
        extra_objects=[_solid_pattern_object()],
    )
    page = render_page(pdf)

    inside = coverage_ratio(region_image(page, (85.0, 85.0, 115.0, 115.0)))
    assert inside > 0.9, (
        f"the pattern did not paint inside the filled path ({inside:.3f} coverage)"
    )

    for name, box in (
        ("top-left", (0.0, 0.0, 60.0, 60.0)),
        ("top-right", (140.0, 0.0, 200.0, 60.0)),
        ("bottom-left", (0.0, 140.0, 60.0, 200.0)),
        ("bottom-right", (140.0, 140.0, 200.0, 200.0)),
    ):
        outside = coverage_ratio(region_image(page, box))
        assert outside < 0.02, (
            f"the pattern painted outside the filled path: the {name} corner "
            f"has {outside:.3f} coverage and the fill never reached it"
        )


def test_pattern_bound_honours_the_even_odd_rule():
    """`f*` bounds the pattern by the even-odd rule, so a hole stays a hole.

    Two nested rectangles wound the same way: under even-odd the inner one is a
    hole, under nonzero it is filled. Bounding the pattern with the wrong rule
    fills the hole with cells.
    """
    rects = "40 40 120 120 re\n70 70 60 60 re\n"
    hole = (90.0, 90.0, 110.0, 110.0)
    ring = (50.0, 50.0, 65.0, 65.0)

    even_odd = render_page(
        simple_page_pdf(
            f"/Pattern cs /P0 scn\n{rects}f*\n",
            resources="/Pattern << /P0 5 0 R >>",
            extra_objects=[_solid_pattern_object()],
        )
    )
    nonzero = render_page(
        simple_page_pdf(
            f"/Pattern cs /P0 scn\n{rects}f\n",
            resources="/Pattern << /P0 5 0 R >>",
            extra_objects=[_solid_pattern_object()],
        )
    )

    assert coverage_ratio(region_image(even_odd, ring)) > 0.9, (
        "the pattern did not paint the ring between the two rectangles"
    )
    assert coverage_ratio(region_image(even_odd, hole)) < 0.02, (
        "f* left the inner rectangle filled; the pattern was bounded by the "
        "nonzero rule instead of even-odd"
    )
    assert coverage_ratio(region_image(nonzero, hole)) > 0.9, (
        "f left the inner rectangle empty; the same-wound subpath is not a hole "
        "under the nonzero rule"
    )


def test_uncolored_tiling_pattern_uses_scn_color_operands():
    """PaintType 2 cells inherit the colour operands from the selecting scn."""
    pattern = stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"0 0 {CELL} {CELL} re f\n".encode("latin-1"),
    )
    content = "1 1 0 rg\n/CS1 cs 1 0 0 /P0 scn\n40 40 80 80 re f\n"
    pdf = simple_page_pdf(
        content,
        resources="/ColorSpace << /CS1 [/Pattern /DeviceRGB] >> "
        "/Pattern << /P0 5 0 R >>",
        extra_objects=[pattern],
    )

    image = region_image(render_page(pdf), (60.0, 60.0, 100.0, 100.0))
    assert_color_near(
        center_color(image),
        (255, 0, 0),
        tolerance=8,
        what="uncolored pattern selected colour",
    )


def test_pattern_fill_stroke_operator_uses_pattern_for_fill_only():
    """`B` paints the selected pattern fill, then strokes the original path."""
    pattern = stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << >> /Matrix [1 0 0 1 0 0]",
        f"0 0 {INK} {INK} re f\n".encode("latin-1"),
    )
    content = "0 0 1 RG\n2 w\n/CS1 cs 1 0 0 /P0 scn\n40 40 80 80 re B\n"
    pdf = simple_page_pdf(
        content,
        resources="/ColorSpace << /CS1 [/Pattern /DeviceRGB] >> "
        "/Pattern << /P0 5 0 R >>",
        extra_objects=[pattern],
    )

    image = region_image(render_page(pdf), (45.0, 45.0, 115.0, 115.0))
    coverage = coverage_ratio(image)
    assert 0.1 < coverage < 0.45, (
        f"`B` used a solid fill instead of the pattern stencil: {coverage:.3f}"
    )


def test_unresolved_pattern_paints_nothing():
    """A fill naming a pattern that cannot be resolved is skipped, not blackened.

    `scn` with an unknown pattern name leaves the fill colour where it was --
    initially black -- so painting the fill anyway floods the region. Skipping
    the fill loses the artwork; painting it black loses the page.
    """
    content = (
        "/Pattern cs /DoesNotExist scn\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
    )
    pdf = simple_page_pdf(content, resources="/Pattern << >>")

    image = region_image(render_page(pdf), FILL_BOX)
    assert coverage_ratio(image) < 0.02, (
        "an unresolved pattern fill painted the region; it must paint nothing"
    )


def test_unresolved_pattern_keeps_the_stroke():
    """Only the fill is dropped: a stroke in the same operator still paints."""
    content = (
        "/Pattern cs /DoesNotExist scn\n"
        "0 0 1 RG\n2 w\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re B\n"
    )
    pdf = simple_page_pdf(content, resources="/Pattern << >>")

    image = region_image(render_page(pdf), FILL_BOX)
    coverage = coverage_ratio(image)
    assert 0.0 < coverage < 0.3, (
        f"expected only the outline to paint, got {coverage:.3f} coverage"
    )


# longer than the small-string buffer, so the name lives on the heap
SHADING_PATTERN_NAME = "AxialShadingPatternWithALongName"


def _shading_pattern_page(depth: int) -> bytes:
    """An axial shading-pattern fill inside `depth` nested `q` ... `Q`."""
    content = (
        "q\n" * depth
        + f"/Pattern cs /{SHADING_PATTERN_NAME} scn\n"
        + f"{FILL_BOX[0]} {FILL_BOX[0]} "
        + f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
        + "Q\n" * depth
    )
    pattern = (
        "<< /Type /Pattern /PatternType 2 /Shading << /ShadingType 2 "
        "/ColorSpace /DeviceRGB /Coords [20 0 180 0] "
        "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> "
        ">> >>"
    )
    return simple_page_pdf(
        content,
        resources=f"/Pattern << /{SHADING_PATTERN_NAME} 5 0 R >>",
        extra_objects=[pattern],
    )


@pytest.mark.parametrize("depth", range(18))
def test_shading_pattern_fill_survives_graphics_state_growth(depth):
    """A shading-pattern fill parses at any `q` nesting depth.

    The pattern name was passed by reference into the current graphics state,
    and painting the pattern pushes a new state. When that push reallocated the
    state stack, the name dangled, and copying it read freed memory: a garbage
    length that threw std::bad_alloc ("Page N failed to parse"), or corrupted
    the heap. Which depths reallocate depends on the stack's growth, so every
    depth up to a few doublings is tried.
    """
    page = parse_page(_shading_pattern_page(depth))
    assert page is not None


def _text_cell_pattern_page() -> bytes:
    """A tiling pattern whose cell draws a glyph, filled under a page label."""
    pattern = stream_object(
        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
        f"/BBox [0 0 {CELL} {CELL}] /XStep {CELL} /YStep {CELL} "
        "/Resources << /Font << /F0 6 0 R >> >> /Matrix [1 0 0 1 0 0]",
        b"BT /F0 8 Tf 2 2 Td (x) Tj ET\n",
    )
    font = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    content = (
        "BT /F0 12 Tf 20 190 Td (label) Tj ET\n"
        "/Pattern cs /P0 scn\n"
        f"{FILL_BOX[0]} {FILL_BOX[0]} "
        f"{FILL_BOX[2] - FILL_BOX[0]} {FILL_BOX[3] - FILL_BOX[1]} re f\n"
    )
    return simple_page_pdf(
        content,
        resources="/Font << /F0 6 0 R >> /Pattern << /P0 5 0 R >>",
        extra_objects=[pattern, font],
    )


def _textline_texts(pdf_bytes: bytes, *, keep_shapes: bool) -> list[str]:
    from io import BytesIO

    from docling_parse.pdf_parser import ContentConfig, ContentLevel, DoclingPdfParser

    config = ContentConfig(
        char_cells_content_level=ContentLevel.COMPUTE,
        word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        shapes_content_level=(
            ContentLevel.COMPUTE if keep_shapes else ContentLevel.SKIP
        ),
        bitmaps_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        include_bitmap_bytes=False,
    )
    doc = DoclingPdfParser(loglevel="fatal").load(BytesIO(pdf_bytes), lazy=True)
    page = doc.get_page(1, content_config=config)
    return [cell.text for cell in page.textline_cells]


def test_tiling_lattice_is_not_replayed_when_shapes_are_skipped():
    """Skipping shapes skips the tiling replay, not just its output.

    Each tiling fill replays its cell on a lattice of up to 17x17 nested
    stream decoders. Without shape tracking the path being filled is never
    recorded, so the lattice fell back to the page box and ran in full for
    every fill, and every stroke it produced was then dropped by the same
    shape guards. A CAD sheet hatched with a few thousand pattern fills took
    minutes to decode for text alone (docling-parse#375). The replay is tied
    to shape tracking now, the same way shading patterns already were.
    """
    pdf = _text_cell_pattern_page()

    assert _textline_texts(pdf, keep_shapes=False) == ["label"], (
        "a skipped-shapes decode replayed the pattern cell"
    )

    with_shapes = _textline_texts(pdf, keep_shapes=True)
    assert "label" in with_shapes
    replayed = [line for line in with_shapes if line != "label"]
    assert replayed and all(set(line) <= {"x", " "} for line in replayed), (
        "keeping shapes must still replay the cell across the lattice"
    )
