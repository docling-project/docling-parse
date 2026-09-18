#!/usr/bin/env python
"""Fallback behavior for invalid vertical metrics in a font descriptor."""

import pytest
from docling_core.types.doc.page import TextCellUnit

from tests.pdf_builder import parse_page, simple_page_pdf


def test_positive_descent_falls_back_to_font_bbox() -> None:
    """A positive descent must not collapse a tall glyph above its baseline."""
    content = "BT /F1 10 Tf 50 100 Td (!) Tj ET\n"
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /BrokenSymbol "
        "/FirstChar 33 /LastChar 33 /Widths [572] "
        "/Encoding /WinAnsiEncoding /FontDescriptor 6 0 R >>"
    )
    descriptor = (
        "<< /Type /FontDescriptor /FontName /BrokenSymbol /Flags 4 "
        "/FontBBox [-15 -2959 1387 40] /ItalicAngle 0 "
        "/Ascent 772 /Descent 623 /CapHeight 0 /StemV 0 >>"
    )
    page = parse_page(
        simple_page_pdf(
            content,
            resources="/Font << /F1 5 0 R >>",
            extra_objects=[font, descriptor],
        )
    )

    cells = list(page.iterate_cells(TextCellUnit.CHAR))
    assert len(cells) == 1
    box = cells[0].rect.to_bounding_box()

    assert box.b == pytest.approx(70.41)
    assert box.t == pytest.approx(100.40)
    assert box.height == pytest.approx(29.99)
