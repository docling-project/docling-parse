#!/usr/bin/env python
"""Rotation-invariant character adjacency and contraction."""

from io import BytesIO
from itertools import pairwise
from math import hypot, sqrt

import pytest

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import build_pdf


def _rotated_text_pdf() -> bytes:
    # Helvetica at 12 pt: A/B advance 8.004 pt and C advances 8.664 pt.
    # Each glyph is emitted separately. AB and CD touch, while the 3.336 pt
    # jump before C is one Helvetica space. The text matrix rotates everything
    # by 45 degrees without changing these text-space distances.
    content = " ".join(
        [
            "BT /F1 12 Tf",
            ".70710678 .70710678 -.70710678 .70710678 100 50 Tm",
            "(A) Tj 8.004 0 Td",
            "(B) Tj 11.340 0 Td",
            "(C) Tj 8.664 0 Td",
            "(D) Tj ET",
        ]
    )
    font = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Name /F1 >>"
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            font,
            f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        ]
    )


def _explicit_space_with_irregular_inner_gap_pdf() -> bytes:
    # A and B have a 3.336 pt gap even though they belong to one word. This
    # models the varying side bearings exposed by tight Type 3 ink boxes. The
    # same line contains a genuine PDF space between B and C, which is the
    # authoritative word-boundary signal.
    content = " ".join(
        [
            "BT /F1 12 Tf 100 50 Td",
            "(A) Tj 11.340 0 Td",
            "(B) Tj ( ) Tj (C) Tj (D) Tj ET",
        ]
    )
    font = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Name /F1 >>"
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 100] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            font,
            f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        ]
    )


def _facing_edge_geometry(first, second) -> tuple[float, float]:
    """Return transverse overlap and forward gap of two LTR cell edges."""
    a = first.rect
    b = second.rect
    ux = a.r_x1 - a.r_x0
    uy = a.r_y1 - a.r_y0
    norm = hypot(ux, uy)
    ux, uy = ux / norm, uy / norm
    nx, ny = -uy, ux

    trailing = ((a.r_x1, a.r_y1), (a.r_x2, a.r_y2))
    leading = ((b.r_x0, b.r_y0), (b.r_x3, b.r_y3))
    trailing_n = [x * nx + y * ny for x, y in trailing]
    leading_n = [x * nx + y * ny for x, y in leading]
    overlap = min(max(trailing_n), max(leading_n)) - max(
        min(trailing_n), min(leading_n)
    )

    trailing_mid = tuple(sum(p[i] for p in trailing) / 2 for i in (0, 1))
    leading_mid = tuple(sum(p[i] for p in leading) / 2 for i in (0, 1))
    gap = (leading_mid[0] - trailing_mid[0]) * ux + (
        leading_mid[1] - trailing_mid[1]
    ) * uy
    return overlap, gap


def _projection_bounds(
    cells, ux: float, uy: float
) -> tuple[float, float, float, float]:
    nx, ny = -uy, ux
    points = []
    for cell in cells:
        rect = cell.rect
        points.extend(
            [
                (rect.r_x0, rect.r_y0),
                (rect.r_x1, rect.r_y1),
                (rect.r_x2, rect.r_y2),
                (rect.r_x3, rect.r_y3),
            ]
        )
    along = [x * ux + y * uy for x, y in points]
    normal = [x * nx + y * ny for x, y in points]
    return min(along), max(along), min(normal), max(normal)


def test_facing_edges_contract_at_45_degrees():
    document = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=BytesIO(_rotated_text_pdf())
    )
    try:
        _, page = next(document.iterate_pages())
    finally:
        document.unload()

    chars = [cell for cell in page.char_cells if cell.text in "ABCD"]
    assert [cell.text for cell in chars] == list("ABCD")

    ux = chars[0].rect.r_x1 - chars[0].rect.r_x0
    uy = chars[0].rect.r_y1 - chars[0].rect.r_y0
    norm = hypot(ux, uy)
    assert ux / norm == pytest.approx(1 / sqrt(2), abs=1e-5)
    assert uy / norm == pytest.approx(1 / sqrt(2), abs=1e-5)

    geometry = [_facing_edge_geometry(a, b) for a, b in pairwise(chars)]
    assert all(overlap > 0 for overlap, _ in geometry)
    assert geometry[0][1] == pytest.approx(0.0, abs=0.02)
    assert geometry[1][1] == pytest.approx(3.336, abs=0.02)
    assert geometry[2][1] == pytest.approx(0.0, abs=0.02)

    assert [cell.text for cell in page.word_cells] == ["AB", "CD"]
    assert [cell.text for cell in page.textline_cells] == ["AB CD"]

    # The aggregate uses the maximum rotation-aligned envelope of all member
    # characters, including any intermediate ascent/descent extrema.
    char_bounds = _projection_bounds(chars[:2], ux / norm, uy / norm)
    word_bounds = _projection_bounds(page.word_cells[:1], ux / norm, uy / norm)
    assert word_bounds == pytest.approx(char_bounds, abs=1e-6)


def test_explicit_spaces_override_irregular_inner_word_gaps():
    document = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=BytesIO(_explicit_space_with_irregular_inner_gap_pdf())
    )
    try:
        _, page = next(document.iterate_pages())
    finally:
        document.unload()

    assert [cell.text for cell in page.word_cells] == ["AB", "CD"]
    assert [cell.text for cell in page.textline_cells] == ["AB CD"]
