#!/usr/bin/env python
"""Fallback behavior for invalid vertical metrics in a font descriptor."""

from base64 import b64decode

import pytest
from docling_core.types.doc.page import TextCellUnit

from tests.pdf_builder import parse_page, simple_page_pdf, stream_object

# An original, synthetic OpenType/CFF font with two rectangular outlines:
# summation spans y=-400..0, while parenleft spans y=-900..100. Keeping the
# fixture inline makes the exact malformed-font construct reviewable without
# adding an opaque binary test file.
_SYNTHETIC_MATH_OTF = b64decode(
    "T1RUTwAJAIAAAwAQQ0ZGIDf5/7AAAAL0AAAAnE9TLzJFSGWhAAABAAAAAGBjbWFwRAzemwAAApgA"
    "AAA8aGVhZC94MLoAAACcAAAANmhoZWEFFgH2AAAA1AAAACRobXR4ArwAAAAAA5AAAAAIbWF4cAAD"
    "UAAAAAD4AAAABm5hbWXfdpiwAAABYAAAAThwb3N0AAMAAAAAAtQAAAAgAAEAAAABAAA2rx1LXw88"
    "9QADA+gAAAAA5tJ5fQAAAADm0nl9ADL8fAKKAGQAAAADAAIAAAAAAAAAAQAAArz+1AAAArwAAABk"
    "AlgAAQAAAAAAAAAAAAAAAAAAAAEAAFAAAAMAAAADArwBkAAFAAQAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAEAAABAAAAAAAAAAAA/Pz8/AAAAKCIRArz+1AAAArwBLAAAAAAAAAAA"
    "AAAAAAAAACAAAAAAAAoAfgABAAAAAAABAA0AAAABAAAAAAACAAcADQABAAAAAAADABUAFAABAAAA"
    "AAAEABUAKQABAAAAAAAGABUAFAADAAEECQABABoAPgADAAEECQACAA4AWAADAAEECQADACoAZgAD"
    "AAEECQAEACoAkAADAAEECQAGACoAZlN5bnRoZXRpY01hdGhSZWd1bGFyU3ludGhldGljTWF0aC1S"
    "ZWd1bGFyU3ludGhldGljTWF0aCBSZWd1bGFyAFMAeQBuAHQAaABlAHQAaQBjAE0AYQB0AGgAUgBl"
    "AGcAdQBsAGEAcgBTAHkAbgB0AGgAZQB0AGkAYwBNAGEAdABoAC0AUgBlAGcAdQBsAGEAcgBTAHkA"
    "bgB0AGgAZQB0AGkAYwBNAGEAdABoACAAUgBlAGcAdQBsAGEAcgAAAAIAAAADAAAAFAADAAEAAAAU"
    "AAQAKAAAAAYABAABAAIAKCIR//8AAAAoIhH////a3fAAAQAAAAAAAAADAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAQAEAQABAQEWU3ludGhldGljTWF0aC1SZWd1bGFyAAEBARv4HAL4HQP4"
    "GAS9/hj5Hu8F9wUPi/cwEvcKEQADAQEKHyxzdW1tYXRpb25TeW50aGV0aWNNYXRoIFJlZ3VsYXJT"
    "eW50aGV0aWNNYXRoAAAAAYcACQADAQEEEiD5UA75UL38JBX47Pgk/OwGDvlQ7/4YFfgk+nz8JAYO"
    "ArwAAAAAAAA="
)


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


def test_invalid_metrics_use_embedded_glyph_vertical_bounds() -> None:
    """A malformed math font uses each outline, not its font-wide height."""
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /SyntheticMath-Regular "
        "/FirstChar 65 /LastChar 66 /Widths [700 700] "
        "/Encoding << /Type /Encoding "
        "/Differences [65 /summation /parenleft] >> "
        "/FontDescriptor 6 0 R >>"
    )
    descriptor = (
        "<< /Type /FontDescriptor /FontName /SyntheticMath-Regular /Flags 4 "
        "/FontBBox [0 -900 700 100] /ItalicAngle 0 "
        "/Ascent 700 /Descent 500 /CapHeight 0 /StemV 0 /FontFile3 7 0 R >>"
    )
    embedded_font = stream_object("/Subtype /OpenType", _SYNTHETIC_MATH_OTF)
    page = parse_page(
        simple_page_pdf(
            "BT /F1 10 Tf 50 100 Td (A) Tj 20 0 Td (B) Tj ET\n",
            resources="/Font << /F1 5 0 R >>",
            extra_objects=[font, descriptor, embedded_font],
        )
    )

    cells = list(page.iterate_cells(TextCellUnit.CHAR))
    assert len(cells) == 2
    summation = cells[0].rect.to_bounding_box()
    parenthesis = cells[1].rect.to_bounding_box()

    # Vertical bounds come from each outline. Horizontal bounds deliberately
    # remain the PDF advance boxes: 700 units at 10 pt equals 7 pt.
    assert (summation.l, summation.r) == pytest.approx((50.0, 57.0))
    assert (summation.b, summation.t) == pytest.approx((96.0, 100.0))
    assert (parenthesis.l, parenthesis.r) == pytest.approx((70.0, 77.0))
    assert (parenthesis.b, parenthesis.t) == pytest.approx((91.0, 101.0))
