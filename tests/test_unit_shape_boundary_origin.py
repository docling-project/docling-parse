#!/usr/bin/env python
"""Shape geometry must be relative to the page boundary, like the text cells.

Text cells come out of the decoder relative to the page boundary (the crop box
by default), and `page_width`/`page_height` are the boundary size, but
`get_shape_lines()` and `get_connected_shape_bounding_boxes()` returned raw PDF
user-space boxes. On a page whose crop box does not start at the origin the two
disagreed by exactly the boundary origin: a table frame no longer matched its
text, and every drawn shape sat 12 points off on a real document whose crop
box starts at (-12, 12).
"""

from __future__ import annotations

from io import BytesIO

from docling_core.types.doc.page import TextCellUnit

from docling_parse.pdf_parser import DoclingThreadedPdfParser, ThreadedPdfParserConfig
from tests.pdf_builder import build_pdf, content_stream

# Media box 400x300 at the origin, crop box 300x200 at (50, 40). The word
# ``Hello`` is drawn at user-space (100, 120); a filled 40x1 point underline
# sits directly below it and a stroked 40 point line 6 points lower.
SHIFTED_CROP_BOX_PAGE = build_pdf(
    [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300]"
        " /CropBox [50 40 350 240] /Resources << /Font << /F1 5 0 R >> >>"
        " /Contents 4 0 R >>",
        content_stream(
            "BT /F1 12 Tf 100 120 Td (Hello) Tj ET\n"
            "0 0 0 rg 100 116 40 1 re f\n"
            "0 0 0 RG 1 w 100 110 m 140 110 l S\n"
        ),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
)


def test_shapes_share_the_text_cells_boundary_frame() -> None:
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=1)
    )
    parser.load(BytesIO(SHIFTED_CROP_BOX_PAGE))
    try:
        (result,) = list(parser.iterate_results())
        assert result.success, result.error_message
        assert (result.page_width, result.page_height) == (300.0, 200.0)

        page = result.get_page()
        word = next(
            cell
            for cell in page.iterate_cells(TextCellUnit.WORD)
            if cell.text == "Hello"
        )
        # Bottom-left origin, relative to the crop box: user-space x=100 -> 50.
        word_box = word.rect.to_bounding_box()
        assert abs(word_box.l - 50.0) < 1.0

        # Both drawn shapes must be in the same frame as the word.
        shapes = result.get_connected_shape_bounding_boxes()
        assert len(shapes) == 2
        underline = max(shapes, key=lambda box: box.t)  # the upper of the two
        assert abs(underline.l - word_box.l) < 0.5
        assert abs(underline.r - (word_box.l + 40.0)) < 0.5
        assert word_box.b - 2.0 <= underline.t <= word_box.b  # directly under it

        (line,) = result.get_shape_lines(horizontal=True, vertical=False)
        assert abs(line.l - word_box.l) < 0.5
        assert abs(line.t - (underline.b - 6.0)) < 1.0
    finally:
        parser.unload_all()
