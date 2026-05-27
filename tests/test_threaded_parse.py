#!/usr/bin/env python
"""Tests for the threaded PDF parser."""

import glob
import os

import pytest
from docling_core.types.doc.page import PdfPageBoundaryType, SegmentedPdfPage

from docling_parse import pdf_parsers
from docling_parse.pdf_parser import (
    DecodePageConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.test_parse import (
    GROUNDTRUTH_FOLDER,
    REGRESSION_FOLDER,
    verify_SegmentedPdfPage,
)

SAMPLE_PDF = "docs/dln-v1.pdf"
LARGE_SAMPLE_PDF = "docs/PDF32000_2008.pdf"


def _make_decode_config() -> DecodePageConfig:
    config = DecodePageConfig()
    config.page_boundary = "crop_box"
    config.do_sanitization = False
    config.keep_glyphs = True
    config.keep_qpdf_warnings = False
    return config


def test_threaded_raw_pybind_types_are_internal():
    assert not hasattr(pdf_parsers, "PageDecodeResult")
    assert not hasattr(pdf_parsers, "threaded_pdf_parser")
    assert not hasattr(pdf_parsers, "PageRenderResult")
    assert not hasattr(pdf_parsers, "threaded_pdf_renderer")


def test_threaded_reference_documents_from_filenames():
    """Load all regression PDFs, decode all pages in parallel, and verify against groundtruth."""
    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=4,
            max_concurrent_results=32,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    doc_keys = {pdf_doc_path: parser.load(pdf_doc_path) for pdf_doc_path in pdf_docs}

    page_restrictions = {
        "deep-mediabox-inheritance.pdf": [2],
        "font_06.pdf": [1],
        "font_07.pdf": [1],
        "font_08.pdf": [1],
        "font_09.pdf": [1],
        "font_10.pdf": [1],
    }

    results: dict[str, dict[int, SegmentedPdfPage]] = {}
    for result in parser.iterate_results():
        assert result.doc_key != "", "doc_key should not be empty"
        if result.success:
            results.setdefault(result.doc_key, {})[result.page_number] = (
                result.get_page()
            )
        else:
            print(
                f"Warning: task failed for {result.doc_key} page {result.page_number}: {result.error_message}"
            )

    for pdf_doc_path in pdf_docs:
        key = doc_keys[pdf_doc_path]
        assert key in results, f"No results found for {pdf_doc_path}"

        rname = os.path.basename(pdf_doc_path)

        for page_no, pred_page in sorted(results[key].items()):
            if rname in page_restrictions and page_no not in page_restrictions[rname]:
                continue

            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            if os.path.exists(fname):
                true_page = SegmentedPdfPage.load_from_json(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)


def test_threaded_single_document():
    """Test threaded parsing with a single document."""
    filename = SAMPLE_PDF

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=4,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    key = parser.load(filename)
    assert parser.page_count(key) > 0

    count = 0
    for result in parser.iterate_results():
        assert result.success, (
            f"Failed to decode page {result.page_number}: {result.error_message}"
        )
        assert result.doc_key == key
        assert result.page_width > 0
        assert result.page_height > 0
        assert result.get_timings().total() > 0
        count += 1

    assert count == parser.page_count(key)


def test_threaded_results_match_sequential():
    """Verify threaded results match sequential results for the same documents."""
    filenames = [SAMPLE_PDF]
    decode_config = _make_decode_config()

    seq_parser = DoclingPdfParser(loglevel="fatal")
    sequential_pages: dict[str, dict[int, SegmentedPdfPage]] = {}
    for filename in filenames:
        pdf_doc = seq_parser.load(
            path_or_stream=filename,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
            lazy=True,
        )
        key = f"key={filename}"
        sequential_pages[key] = {}
        for page_no, page in pdf_doc.iterate_pages(config=decode_config):
            sequential_pages[key][page_no] = page

    threaded_parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=4,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=decode_config,
    )
    for filename in filenames:
        threaded_parser.load(filename)

    threaded_pages: dict[str, dict[int, SegmentedPdfPage]] = {}
    for result in threaded_parser.iterate_results():
        assert result.success, f"Failed: {result.error_message}"
        threaded_pages.setdefault(result.doc_key, {})[result.page_number] = (
            result.get_page()
        )

    for key in sequential_pages:
        assert key in threaded_pages, f"Missing key {key} in threaded results"
        for page_no in sequential_pages[key]:
            assert page_no in threaded_pages[key], f"Missing page {page_no} for {key}"

            seq_page = sequential_pages[key][page_no]
            thr_page = threaded_pages[key][page_no]

            assert len(seq_page.char_cells) == len(thr_page.char_cells), (
                f"char_cells count mismatch for {key} page {page_no}"
            )
            assert len(seq_page.word_cells) == len(thr_page.word_cells), (
                f"word_cells count mismatch for {key} page {page_no}"
            )
            assert len(seq_page.textline_cells) == len(thr_page.textline_cells), (
                f"textline_cells count mismatch for {key} page {page_no}"
            )
            assert len(seq_page.shapes) == len(thr_page.shapes), (
                f"shapes count mismatch for {key} page {page_no}"
            )


def test_threaded_backpressure():
    """Test that backpressure works with max_concurrent_results=1."""
    filename = LARGE_SAMPLE_PDF

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=1,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    key = parser.load(filename)
    count = sum(1 for result in parser.iterate_results() if result.success)
    assert count == parser.page_count(key)


def test_threaded_single_thread():
    """Test threaded parsing with a single thread (sequential baseline)."""
    filename = SAMPLE_PDF

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=32,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    key = parser.load(filename)
    count = sum(1 for result in parser.iterate_results() if result.success)
    assert count == parser.page_count(key)


def test_threaded_selected_pages_schedule_subset():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=4,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    key = parser.load(LARGE_SAMPLE_PDF, page_numbers=[2, 1, 2])

    assert parser.page_count(key) >= 2
    assert parser.scheduled_page_count(key) == 2

    emitted_pages = sorted(
        result.page_number for result in parser.iterate_results() if result.success
    )
    assert emitted_pages == [1, 2]


def test_threaded_selected_pages_invalid_page_number():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
        decode_config=_make_decode_config(),
    )

    with pytest.raises(RuntimeError, match="Invalid page number"):
        parser.load(SAMPLE_PDF, page_numbers=[9999])


def test_threaded_multiple_documents_with_different_subsets():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=4,
            max_concurrent_results=8,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )

    path_key = parser.load(LARGE_SAMPLE_PDF, page_numbers=[1, 2])
    bytes_key = parser.load(SAMPLE_PDF, page_numbers=[1])

    results_by_key: dict[str, list[int]] = {}
    for result in parser.iterate_results():
        assert result.success, result.error_message
        results_by_key.setdefault(result.doc_key, []).append(result.page_number)

    assert sorted(results_by_key[path_key]) == [1, 2]
    assert sorted(results_by_key[bytes_key]) == [1]
    assert parser.scheduled_page_count(path_key) == 2
    assert parser.scheduled_page_count(bytes_key) == 1


def test_threaded_unload_after_consumption_is_idempotent():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
        decode_config=_make_decode_config(),
    )

    key = parser.load(SAMPLE_PDF, page_numbers=[1])
    list(parser.iterate_results())

    assert parser.unload(key) is True
    assert parser.unload(key) is False

    with pytest.raises(ValueError):
        parser.page_count(key)


def test_threaded_unload_during_active_iteration_raises():
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
        decode_config=_make_decode_config(),
    )

    key = parser.load(SAMPLE_PDF)
    assert parser.has_tasks()

    with pytest.raises(RuntimeError, match="threaded iteration is active"):
        parser.unload(key)


BITMAP_PDF = "tests/data/regression/annots_01.pdf"


def _make_bitmap_config() -> DecodePageConfig:
    config = DecodePageConfig()
    config.keep_bitmaps = True
    config.do_sanitization = False
    return config


def test_threaded_bitmap_no_materialization_preserves_geometry():
    """Threaded path: geometry matches between full and placeholder-only modes."""
    from docling_core.types.doc.base import ImageRefMode

    config_full = _make_bitmap_config()
    config_full.materialize_bitmap_bytes = True

    config_geo = _make_bitmap_config()
    config_geo.materialize_bitmap_bytes = False

    def _get_page1(decode_config: DecodePageConfig) -> "SegmentedPdfPage":
        parser = DoclingThreadedPdfParser(
            parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
            decode_config=decode_config,
        )
        parser.load(BITMAP_PDF)
        return next(
            r.get_page()
            for r in parser.iterate_results()
            if r.success and r.page_number == 1
        )

    page_full = _get_page1(config_full)
    page_geo = _get_page1(config_geo)

    assert len(page_full.bitmap_resources) > 0, "test PDF must contain bitmaps"
    assert len(page_full.bitmap_resources) == len(page_geo.bitmap_resources)

    eps = 1e-3
    for i, (full, geo) in enumerate(
        zip(page_full.bitmap_resources, page_geo.bitmap_resources)
    ):
        full_poly = full.rect.to_polygon()
        geo_poly = geo.rect.to_polygon()
        for pt in range(4):
            assert abs(full_poly[pt][0] - geo_poly[pt][0]) < eps
            assert abs(full_poly[pt][1] - geo_poly[pt][1]) < eps

    for bm in page_geo.bitmap_resources:
        assert bm.image is None
        assert bm.mode == ImageRefMode.PLACEHOLDER

    assert any(bm.image is not None for bm in page_full.bitmap_resources)
