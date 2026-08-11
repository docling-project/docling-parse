#!/usr/bin/env python
"""Report text extraction similarity between docling-parse and pdftext."""

from pathlib import Path

import pytest

from tests.test_pypdfium_parse import (
    _collect_text_comparisons,
    _print_text_report,
)


def extract_pdftext_text(pdf_path: Path, page_no: int) -> str:
    from pdftext.extraction import plain_text_output

    return plain_text_output(
        str(pdf_path),
        sort=False,
        hyphens=False,
        page_range=[page_no - 1],
    )


def test_line_text_matches_pdftext() -> None:
    """Report docling-parse line text similarity against pdftext."""
    pytest.importorskip(
        "pdftext",
        reason="pdftext is required for the pdftext comparison",
    )

    comparisons, skipped = _collect_text_comparisons(
        reference_label="pdftext",
        extract_reference=extract_pdftext_text,
    )

    _print_text_report(comparisons, skipped, reference_label="pdftext")
    assert len(comparisons) > 0, "no page could be compared against pdftext"
