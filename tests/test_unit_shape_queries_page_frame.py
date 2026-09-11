#!/usr/bin/env python
"""Text, shapes and bitmaps of a page share one frame, and so do the queries.

The render instructions stay in raw, unrotated user space so the renderer can
orient its own canvas. The text cells, shapes and images of the page are
instead rotated by /Rotate and moved to the page boundary (the crop box by
default), and `page_width` / `page_height` are the boundary size.
`get_shape_lines()`, `get_connected_shape_bounding_boxes()` and
`intersects_with()` walked the instructions and answered in raw space: on a
page whose crop box does not start at the origin they were off by exactly the
boundary origin, and on a rotated page by the rotation as well, so a table
frame no longer matched its text and `has_content_in()` looked for shapes in
the wrong place (docling-project/docling-parse#343).

The page is built in memory: media box 400x300 at the origin, crop box
300x200 at (50, 40), the word `Hello`, a filled underline directly below it,
a stroked line 6 points lower and a small RGB image to the right of the word.
For each /Rotate value the expected frame is computed here from the raw
user-space geometry, and the word, the sanitized shapes, the bitmap and the
three queries must all agree with it.
"""

from __future__ import annotations

from io import BytesIO

import pytest
from docling_core.types.doc.base import BoundingBox, CoordOrigin
from docling_core.types.doc.page import TextCellUnit

from docling_parse.pdf_parser import DoclingThreadedPdfParser, ThreadedPdfParserConfig
from tests.pdf_builder import build_pdf, content_stream, stream_object

MEDIA_BOX = (0.0, 0.0, 400.0, 300.0)
CROP_BOX = (50.0, 40.0, 350.0, 240.0)

# raw user-space geometry of the page content
WORD_X, WORD_Y = 100.0, 120.0
UNDERLINE = (100.0, 116.0, 140.0, 117.0)  # `re f`
LINE = (100.0, 110.0, 140.0, 110.0)  # `m l S`, no area
LINE_WIDTH = 1.0
IMAGE = (150.0, 116.0, 170.0, 132.0)  # `cm Do`

Box = tuple[float, float, float, float]


def _page(rotate: int) -> bytes:
    rotate_entry = f" /Rotate {rotate}" if rotate else ""
    ix0, iy0, ix1, iy1 = IMAGE
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R"
            f" /MediaBox [{' '.join(f'{v:g}' for v in MEDIA_BOX)}]"
            f" /CropBox [{' '.join(f'{v:g}' for v in CROP_BOX)}]{rotate_entry}"
            " /Resources << /Font << /F1 5 0 R >> /XObject << /Im1 6 0 R >> >>"
            " /Contents 4 0 R >>",
            content_stream(
                f"BT /F1 12 Tf {WORD_X:g} {WORD_Y:g} Td (Hello) Tj ET\n"
                f"0 0 0 rg {UNDERLINE[0]:g} {UNDERLINE[1]:g}"
                f" {UNDERLINE[2] - UNDERLINE[0]:g} {UNDERLINE[3] - UNDERLINE[1]:g} re f\n"
                f"0 0 0 RG {LINE_WIDTH:g} w {LINE[0]:g} {LINE[1]:g} m"
                f" {LINE[2]:g} {LINE[3]:g} l S\n"
                f"q {ix1 - ix0:g} 0 0 {iy1 - iy0:g} {ix0:g} {iy0:g} cm /Im1 Do Q\n"
            ),
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            stream_object(
                "/Type /XObject /Subtype /Image /Width 4 /Height 4"
                " /ColorSpace /DeviceRGB /BitsPerComponent 8",
                bytes([255, 0, 0] * 16),
            ),
        ]
    )


def _expected(rotate: int, box: Box) -> Box:
    """Raw user-space box -> page frame: /Rotate clockwise, then crop origin.

    Mirrors `page_item<PAGE_DIMENSION>::rotate()` (the rotated page is moved
    back into the first quadrant by the media box size) and the dimension
    sanitator (the boundary origin is subtracted).
    """
    _, _, media_w, media_h = MEDIA_BOX

    def point(x: float, y: float) -> tuple[float, float]:
        if rotate == 0:
            return x, y
        if rotate == 90:
            return y, media_w - x
        if rotate == 180:
            return media_w - x, media_h - y
        if rotate == 270:
            return media_h - y, x
        raise ValueError(rotate)

    (ax, ay), (bx, by) = point(box[0], box[1]), point(box[2], box[3])
    (cx0, cy0), (cx1, cy1) = (
        point(CROP_BOX[0], CROP_BOX[1]),
        point(CROP_BOX[2], CROP_BOX[3]),
    )
    ox, oy = min(cx0, cx1), min(cy0, cy1)
    return (min(ax, bx) - ox, min(ay, by) - oy, max(ax, bx) - ox, max(ay, by) - oy)


def _box(b: BoundingBox) -> Box:
    return (b.l, b.b, b.r, b.t)


def _close(a: Box, b: Box, tol: float) -> bool:
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def _assert_one_close(boxes: list[Box], want: Box, tol: float, what: str) -> None:
    assert any(_close(got, want, tol) for got in boxes), (what, want, boxes)


def _probe(box: Box, pad: float = 1.0) -> BoundingBox:
    return BoundingBox(
        l=box[0] - pad,
        b=box[1] - pad,
        r=box[2] + pad,
        t=box[3] + pad,
        coord_origin=CoordOrigin.BOTTOMLEFT,
    )


@pytest.mark.parametrize("rotate", [0, 90, 180, 270])
def test_text_shapes_and_bitmaps_share_one_frame(rotate: int) -> None:
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=1)
    )
    parser.load(BytesIO(_page(rotate)))
    try:
        (result,) = list(parser.iterate_results())
        assert result.success, result.error_message

        expected_size = (300.0, 200.0) if rotate % 180 == 0 else (200.0, 300.0)
        assert _close((result.page_width, result.page_height), expected_size, 1e-6)

        page = result.get_page()

        # the text cell contains the start of its baseline and shares an edge
        # with the underline, which starts at the same raw x
        word = next(
            cell
            for cell in page.iterate_cells(TextCellUnit.WORD)
            if cell.text == "Hello"
        )
        word_box = _box(word.rect.to_bounding_box())
        bx, by, _, _ = _expected(rotate, (WORD_X, WORD_Y, WORD_X, WORD_Y))
        assert word_box[0] - 1 <= bx <= word_box[2] + 1, (rotate, word_box, bx)
        assert word_box[1] - 1 <= by <= word_box[3] + 1, (rotate, word_box, by)
        underline = _expected(rotate, UNDERLINE)
        assert min(abs(w - u) for w in word_box for u in underline) < 0.5, (
            rotate,
            word_box,
        )

        # the sanitized shapes of the segmented page
        sanitized = [
            (
                min(p.x for p in shape.points),
                min(p.y for p in shape.points),
                max(p.x for p in shape.points),
                max(p.y for p in shape.points),
            )
            for shape in page.shapes
        ]
        assert len(sanitized) == 2
        _assert_one_close(sanitized, _expected(rotate, UNDERLINE), 1e-6, "underline")
        _assert_one_close(sanitized, _expected(rotate, LINE), 1e-6, "line")

        # the bitmap of the segmented page
        bitmaps = [_box(b.rect.to_bounding_box()) for b in page.bitmap_resources]
        assert len(bitmaps) == 1
        _assert_one_close(bitmaps, _expected(rotate, IMAGE), 1e-6, "bitmap")

        # the shape queries; the stroked line is padded by half its width
        connected = [_box(b) for b in result.get_connected_shape_bounding_boxes()]
        assert len(connected) == 2
        _assert_one_close(
            connected, _expected(rotate, UNDERLINE), 1e-6, "connected underline"
        )
        _assert_one_close(
            connected, _expected(rotate, LINE), LINE_WIDTH / 2 + 1e-6, "connected line"
        )

        (line,) = result.get_shape_lines(
            horizontal=(rotate % 180 == 0), vertical=(rotate % 180 != 0)
        )
        assert _close(_box(line), _expected(rotate, LINE), 1e-6), (rotate, _box(line))

        # has_content_in(): the frame of the query is the frame of the page
        for raw, kind in ((UNDERLINE, "shapes"), (LINE, "shapes"), (IMAGE, "bitmaps")):
            flags = {
                "chars": False,
                "shapes": kind == "shapes",
                "bitmaps": kind == "bitmaps",
            }
            assert result.intersects_with(
                bbox=_probe(_expected(rotate, raw)), **flags
            ), (
                rotate,
                kind,
            )
            # ... and nothing of that kind is left at the raw user-space location
            assert not result.intersects_with(bbox=_probe(raw, pad=0.0), **flags), (
                rotate,
                kind,
            )
    finally:
        parser.unload_all()


def test_underline_sits_under_its_word() -> None:
    """The readable version of the check above, for the unrotated page."""
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=1)
    )
    parser.load(BytesIO(_page(0)))
    try:
        (result,) = list(parser.iterate_results())
        assert result.success, result.error_message

        page = result.get_page()
        word = next(
            cell
            for cell in page.iterate_cells(TextCellUnit.WORD)
            if cell.text == "Hello"
        )
        word_box = word.rect.to_bounding_box()
        # user-space x=100 relative to the crop box at x=50
        assert abs(word_box.l - 50.0) < 1.0

        shapes = result.get_connected_shape_bounding_boxes()
        assert len(shapes) == 2
        underline = max(shapes, key=lambda box: box.t)
        assert abs(underline.l - word_box.l) < 0.5
        assert abs(underline.r - (word_box.l + 40.0)) < 0.5
        assert word_box.b - 2.0 <= underline.t <= word_box.b

        (line,) = result.get_shape_lines(horizontal=True, vertical=False)
        assert abs(line.l - word_box.l) < 0.5
        assert abs(line.t - (underline.b - 6.0)) < 1.0

        (bitmap,) = page.bitmap_resources
        image = bitmap.rect.to_bounding_box()
        assert abs(image.l - (word_box.l + 50.0)) < 0.5  # 50 pt right of the word
        assert abs(image.b - underline.b) < 0.5  # on the underline's baseline
    finally:
        parser.unload_all()
