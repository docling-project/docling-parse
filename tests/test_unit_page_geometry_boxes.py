#!/usr/bin/env python
"""The page geometry reports the page's own /BleedBox, /TrimBox and /ArtBox.

Each of the three boxes defaults to the crop box only when the page does not
declare it (ISO 32000-1, 14.11.2). A page that does declare them must report
its own values in `SegmentedPdfPage.dimension`, through both the
single-threaded and the threaded parser.
"""

from io import BytesIO

from docling_core.types.doc.page import SegmentedPdfPage

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream

MEDIA_BOX = (0.0, 0.0, 300.0, 400.0)
CROP_BOX = (10.0, 10.0, 290.0, 390.0)
BLEED_BOX = (15.0, 15.0, 285.0, 385.0)
TRIM_BOX = (20.0, 20.0, 280.0, 380.0)
ART_BOX = (30.0, 40.0, 270.0, 360.0)


def _pdf_array(box: tuple[float, float, float, float]) -> str:
    return "[" + " ".join(f"{v:g}" for v in box) + "]"


def _page_pdf(*, with_print_boxes: bool) -> bytes:
    boxes = f"/MediaBox {_pdf_array(MEDIA_BOX)} /CropBox {_pdf_array(CROP_BOX)}"
    if with_print_boxes:
        boxes += (
            f" /BleedBox {_pdf_array(BLEED_BOX)}"
            f" /TrimBox {_pdf_array(TRIM_BOX)}"
            f" /ArtBox {_pdf_array(ART_BOX)}"
        )
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            f"<< /Type /Page /Parent 2 0 R {boxes} /Contents 4 0 R >>",
            content_stream("0 0 1 rg 50 50 100 100 re f"),
        ]
    )


def _box(bbox) -> tuple[float, float, float, float]:
    return (bbox.l, bbox.b, bbox.r, bbox.t)


def _parse(pdf_bytes: bytes) -> SegmentedPdfPage:
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(BytesIO(pdf_bytes))
    try:
        return doc.get_page(1)
    finally:
        doc.unload()


def _parse_threaded(pdf_bytes: bytes) -> SegmentedPdfPage:
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=1)
    )
    key = parser.load(BytesIO(pdf_bytes))
    try:
        results = list(parser.iterate_results())
        assert len(results) == 1 and results[0].success
        return results[0].get_page()
    finally:
        parser.unload(key)


def test_declared_print_boxes_are_reported():
    dimension = _parse(_page_pdf(with_print_boxes=True)).dimension

    assert _box(dimension.media_bbox) == MEDIA_BOX
    assert _box(dimension.crop_bbox) == CROP_BOX
    assert _box(dimension.bleed_bbox) == BLEED_BOX
    assert _box(dimension.trim_bbox) == TRIM_BOX
    assert _box(dimension.art_bbox) == ART_BOX


def test_declared_print_boxes_are_reported_by_threaded_parser():
    dimension = _parse_threaded(_page_pdf(with_print_boxes=True)).dimension

    assert _box(dimension.bleed_bbox) == BLEED_BOX
    assert _box(dimension.trim_bbox) == TRIM_BOX
    assert _box(dimension.art_bbox) == ART_BOX


def test_missing_print_boxes_default_to_crop_box():
    dimension = _parse(_page_pdf(with_print_boxes=False)).dimension

    assert _box(dimension.crop_bbox) == CROP_BOX
    assert _box(dimension.bleed_bbox) == CROP_BOX
    assert _box(dimension.trim_bbox) == CROP_BOX
    assert _box(dimension.art_bbox) == CROP_BOX
