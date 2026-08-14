#!/usr/bin/env python
"""Report text extraction similarity between docling-parse and liteparse."""

from pathlib import Path

import pytest

from tests.test_pypdfium_parse import (
    _collect_text_comparisons,
    _print_text_report,
)


def extract_liteparse_text(pdf_path: Path, page_no: int) -> str:
    from liteparse import LiteParse

    parser = LiteParse(
        ocr_enabled=False,
        output_format="text",
        target_pages=str(page_no),
        quiet=True,
    )
    result = parser.parse(str(pdf_path))
    return getattr(result, "text", "") or ""


def test_line_text_matches_liteparse() -> None:
    """Report docling-parse line text similarity against liteparse."""
    pytest.importorskip(
        "liteparse",
        reason="liteparse is required for the liteparse comparison",
    )

    comparisons, skipped = _collect_text_comparisons(
        reference_label="liteparse",
        extract_reference=extract_liteparse_text,
    )

    _print_text_report(comparisons, skipped, reference_label="liteparse")
    assert len(comparisons) > 0, "no page could be compared against liteparse"
