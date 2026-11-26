#!/usr/bin/env python
"""Self-contained regressions for significant PDF TJ positioning jumps."""

from tests.pdf_builder import parse_page, simple_page_pdf


def _page_for_tj(adjustment: str):
    content = (
        "BT /F1 12 Tf 20 100 Td "
        f"[(123.4){adjustment}(567.8)] TJ ET"
    )
    return parse_page(
        simple_page_pdf(
            content,
            resources="/Font << /F1 5 0 R >>",
            extra_objects=[
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            ],
        )
    ).get_page(1)


def test_significant_tj_jump_preserves_text_field_boundary():
    """Regression for docling-project/docling#4341.

    The G.19 PDF uses negative TJ values such as -166.487 between table
    fields. Those values move the next fragment forward, but the resulting
    geometric gap is close to ordinary character spacing. The parser must
    retain the source-level TJ signal so contraction does not glue the fields.
    """
    page = _page_for_tj("-166.487")

    assert [cell.text for cell in page.word_cells] == ["123.4", "567.8"]
    assert [cell.text for cell in page.textline_cells] == ["123.4", "567.8"]


def test_small_tj_kerning_adjustment_still_contracts():
    """Ordinary small TJ positioning must remain inside one word."""
    page = _page_for_tj("80")

    assert [cell.text for cell in page.word_cells] == ["123.4567.8"]
