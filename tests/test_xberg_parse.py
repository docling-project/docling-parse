#!/usr/bin/env python
"""Report text extraction similarity between docling-parse and xberg."""

from pathlib import Path

import pytest

from tests.test_pypdfium_parse import (
    _collect_text_comparisons,
    _print_text_report,
)


def _make_xberg_config():
    return {
        "disable_ocr": True,
        "output_format": "plain",
        "pages": {"extract_pages": True},
    }


def extract_xberg_text(pdf_path: Path, page_no: int) -> str:
    import asyncio

    import xberg

    result = asyncio.run(
        xberg.extract(
            xberg.ExtractInput(kind="uri", uri=str(pdf_path)),
            _make_xberg_config(),
        )
    )
    documents = getattr(result, "results", None) or [result]
    if not documents:
        return ""

    document = documents[0]
    for page in getattr(document, "pages", []) or []:
        if getattr(page, "page_number", None) == page_no:
            return getattr(page, "content", "") or ""
    return getattr(document, "content", "") or ""


def test_line_text_matches_xberg() -> None:
    """Report docling-parse line text similarity against xberg."""
    pytest.importorskip("xberg", reason="xberg is required for the xberg comparison")

    comparisons, skipped = _collect_text_comparisons(
        reference_label="xberg",
        extract_reference=extract_xberg_text,
    )

    _print_text_report(comparisons, skipped, reference_label="xberg")
    assert len(comparisons) > 0, "no page could be compared against xberg"
