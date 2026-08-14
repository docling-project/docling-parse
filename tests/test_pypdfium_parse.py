#!/usr/bin/env python
"""Report text extraction similarity between docling-parse and pypdfium2.

This mirrors the non-binding style of `test_pypdfium_render.py`: the per-page
scores are printed as a report and no quality threshold is enforced. The test
only fails when there is no page that could be compared at all.
"""

import glob
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import pytest

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.constants import PARSER_PAGE_RESTRICTIONS
from tests.test_parse import REGRESSION_FOLDER

TEXT_OUTPUT_DIR = Path("tests/data/texts")
DOCLING_ENGINE_LABEL = "docling-parse"


@dataclass(frozen=True)
class TextComparison:
    document: str
    page_no: int
    f1: float | None
    precision: float | None
    recall: float | None
    edit_distance: float | None
    docling_chars: int
    reference_chars: int


ReferenceExtractor = Callable[[Path, int], str]
TextByDocument = dict[str, list[tuple[int, str]]]


def _make_text_parser() -> DoclingThreadedPdfParser:
    return DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=4,
            max_concurrent_results=32,
            page_content_config=ContentConfig(
                char_cells_content_level=ContentLevel.COMPUTE,
                word_cells_content_level=ContentLevel.COMPUTE,
                line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
                shapes_content_level=ContentLevel.SKIP,
                bitmaps_content_level=ContentLevel.SKIP,
                include_bitmap_bytes=False,
            ),
        ),
        decode_config=DecodeConfig(keep_glyphs=True),
    )


def _normalize_line_text(text: str | None) -> str:
    if not text:
        return ""
    lines = [" ".join(line.split()) for line in text.replace("\r", "\n").split("\n")]
    return "\n".join(line for line in lines if line)


def _metric_value(evaluation, *names: str) -> float | None:
    for name in names:
        value = getattr(evaluation, name, None)
        if isinstance(value, int | float):
            return float(value)

    dump = None
    if hasattr(evaluation, "model_dump"):
        dump = evaluation.model_dump()
    elif hasattr(evaluation, "dict"):
        dump = evaluation.dict()

    if isinstance(dump, dict):
        for name in names:
            value = dump.get(name)
            if isinstance(value, int | float):
                return float(value)

    return None


def _evaluate_text_pair(
    *,
    sample_id: str,
    docling_text: str,
    reference_text: str,
) -> tuple[float | None, float | None, float | None, float | None]:
    from docling_metrics_text import TextMetrics, TextPairSample

    evaluation = TextMetrics().evaluate_sample(
        TextPairSample(
            id=sample_id,
            text_a=docling_text,
            text_b=reference_text,
        )
    )
    return (
        _metric_value(evaluation, "f1", "f1_score", "F1"),
        _metric_value(evaluation, "precision", "Precision"),
        _metric_value(evaluation, "recall", "Recall"),
        _metric_value(
            evaluation,
            "edit_distance",
            "editdistance",
            "editDistance",
            "levenshtein_distance",
        ),
    )


def _format_float(value: float | None) -> str:
    return "" if value is None else f"{value:.4f}"


def _format_text_comparison_table(
    comparisons: list[TextComparison],
    *,
    reference_label: str,
) -> str:
    if not comparisons:
        return "No comparable pages."

    rows = [
        [
            comparison.document,
            str(comparison.page_no),
            _format_float(comparison.f1),
            _format_float(comparison.precision),
            _format_float(comparison.recall),
            _format_float(comparison.edit_distance),
            str(comparison.docling_chars),
            str(comparison.reference_chars),
        ]
        for comparison in sorted(
            comparisons,
            key=lambda item: (float("inf") if item.f1 is None else item.f1),
        )
    ]
    headers = [
        "document",
        "page",
        "f1",
        "precision",
        "recall",
        "edit_distance",
        "docling chars",
        f"{reference_label} chars",
    ]

    widths = [
        max(len(row[col]) for row in [headers, *rows])
        for col in range(len(headers))
    ]
    lines = [
        " | ".join(value.ljust(widths[col]) for col, value in enumerate(headers)),
        "-+-".join("-" * width for width in widths),
    ]
    lines.extend(
        " | ".join(value.ljust(widths[col]) for col, value in enumerate(row))
        for row in rows
    )
    return "\n".join(lines)


def _safe_engine_label(engine: str) -> str:
    return engine.replace("/", "_").replace(os.sep, "_")


def _record_page_text(
    texts: TextByDocument,
    document: str,
    page_no: int,
    text: str,
) -> None:
    texts.setdefault(document, []).append((page_no, text))


def _write_text_outputs(texts: TextByDocument, *, engine: str) -> None:
    TEXT_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    safe_engine = _safe_engine_label(engine)

    for document, pages in texts.items():
        output_path = TEXT_OUTPUT_DIR / f"{document}.{safe_engine}.txt"
        parts = [
            f"--- page {page_no} ---\n{text}"
            for page_no, text in sorted(pages, key=lambda item: item[0])
        ]
        output_path.write_text("\n\n".join(parts) + "\n", encoding="utf-8")


def _write_text_report(
    comparisons: list[TextComparison],
    skipped: list[str],
    *,
    reference_label: str,
) -> str:
    table = _format_text_comparison_table(comparisons, reference_label=reference_label)
    report = table
    if skipped:
        report += "\n\n"
        report += f"{len(skipped)} page(s) could not be compared: " + ", ".join(skipped)

    output_path = Path(f"tests/data/texts_{_safe_engine_label(reference_label)}.txt")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(report + "\n", encoding="utf-8")
    return report


def _collect_text_comparisons(
    *,
    reference_label: str,
    extract_reference: ReferenceExtractor,
) -> tuple[list[TextComparison], list[str]]:
    pytest.importorskip(
        "docling_metrics_text",
        reason="docling_metrics_text is required for text similarity reporting",
    )

    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = _make_text_parser()
    key_to_path: dict[str, Path] = {}
    for pdf_doc_path in pdf_docs:
        rname = os.path.basename(pdf_doc_path)
        key = parser.load(
            pdf_doc_path,
            page_numbers=PARSER_PAGE_RESTRICTIONS.get(rname),
        )
        key_to_path[key] = Path(pdf_doc_path)

    comparisons: list[TextComparison] = []
    skipped: list[str] = []
    docling_texts: TextByDocument = {}
    reference_texts: TextByDocument = {}

    for result in parser.iterate_results():
        pdf_path = key_to_path[result.doc_key]
        rname = pdf_path.name

        if not result.success:
            skipped.append(
                f"{rname}@{result.page_number}[docling: {result.error_message}]"
            )
            continue

        docling_text = _normalize_line_text(
            "\n".join(cell.text for cell in result.get_page().textline_cells)
        )
        _record_page_text(docling_texts, rname, result.page_number, docling_text)
        try:
            reference_text = _normalize_line_text(
                extract_reference(pdf_path, result.page_number)
            )
        except Exception as exc:
            skipped.append(f"{rname}@{result.page_number}[{reference_label}: {exc}]")
            continue
        _record_page_text(reference_texts, rname, result.page_number, reference_text)

        f1, precision, recall, edit_distance = _evaluate_text_pair(
            sample_id=f"{rname}@{result.page_number}",
            docling_text=docling_text,
            reference_text=reference_text,
        )
        comparisons.append(
            TextComparison(
                document=rname,
                page_no=result.page_number,
                f1=f1,
                precision=precision,
                recall=recall,
                edit_distance=edit_distance,
                docling_chars=len(docling_text),
                reference_chars=len(reference_text),
            )
        )

    parser.unload_all()
    _write_text_outputs(docling_texts, engine=DOCLING_ENGINE_LABEL)
    _write_text_outputs(reference_texts, engine=reference_label)
    return comparisons, skipped


def _print_text_report(
    comparisons: list[TextComparison],
    skipped: list[str],
    *,
    reference_label: str,
) -> None:
    print(
        _write_text_report(
            comparisons,
            skipped,
            reference_label=reference_label,
        )
    )


def extract_pypdfium_text(pdf_path: Path, page_no: int) -> str:
    import pypdfium2 as pdfium

    pdf = pdfium.PdfDocument(str(pdf_path))
    try:
        page = pdf[page_no - 1]
        text_page = page.get_textpage()
        try:
            return text_page.get_text_range()
        finally:
            text_page.close()
            page.close()
    finally:
        pdf.close()


@pytest.mark.pypdfium
def test_line_text_matches_pypdfium() -> None:
    """Report docling-parse line text similarity against pypdfium2."""
    pytest.importorskip(
        "pypdfium2",
        reason="pypdfium2 is required for the pypdfium text comparison",
    )

    comparisons, skipped = _collect_text_comparisons(
        reference_label="pypdfium2",
        extract_reference=extract_pypdfium_text,
    )

    _print_text_report(comparisons, skipped, reference_label="pypdfium2")
    assert len(comparisons) > 0, "no page could be compared against pypdfium2"
