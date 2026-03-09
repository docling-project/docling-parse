#!/usr/bin/env python
"""Tests for the threaded PDF parser."""

import glob
import os
from pathlib import Path

from docling_core.types.doc.page import PdfPageBoundaryType, SegmentedPdfPage

from docling_parse.pdf_parser import (
    DecodePageConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    PdfDocument,
    ThreadedPdfParserConfig,
)
from tests.test_parse import (
    GROUNDTRUTH_FOLDER,
    REGRESSION_FOLDER,
    verify_SegmentedPdfPage,
)


def _build_segmented_page_from_decoder(
    page_decoder, boundary_type=PdfPageBoundaryType.CROP_BOX
):
    """Build a SegmentedPdfPage from a page decoder, reusing PdfDocument's conversion logic."""
    # Create a minimal PdfDocument just for its conversion methods
    dummy_doc = PdfDocument.__new__(PdfDocument)
    dummy_doc._boundary_type = boundary_type
    config = DecodePageConfig()
    config.page_boundary = boundary_type.value
    config.do_sanitization = False
    config.keep_qpdf_warnings = False
    return dummy_doc._to_segmented_page_from_decoder(
        page_decoder=page_decoder, config=config
    )


def test_threaded_reference_documents_from_filenames():
    """Load all regression PDFs, decode all pages in parallel, and verify against groundtruth."""

    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    parser_config = ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=4,
        max_concurrent_results=32,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=parser_config,
        decode_config=decode_config,
    )

    # Load all documents
    for pdf_doc_path in pdf_docs:
        parser.load(pdf_doc_path)

    # Page restrictions (same as sequential test)
    page_restrictions = {"deep-mediabox-inheritance.pdf": [2]}

    # Collect all results
    results = {}
    while parser.has_tasks():
        task = parser.get_task()

        assert task.doc_key != "", "doc_key should not be empty"

        if task.success:
            page_decoder, timings = task.get()
            page_number = task.page_number  # 0-indexed
            doc_key = task.doc_key

            pred_page = _build_segmented_page_from_decoder(page_decoder)

            if doc_key not in results:
                results[doc_key] = {}
            results[doc_key][page_number] = pred_page
        else:
            error_msg = task.error()
            # Some pages may fail, log but don't assert
            print(
                f"Warning: task failed for {task.doc_key} page {task.page_number}: {error_msg}"
            )

    # Verify results against groundtruth (same logic as test_reference_documents_from_filenames)
    for pdf_doc_path in pdf_docs:
        key = f"key={str(Path(pdf_doc_path))}"

        assert key in results, f"No results found for {pdf_doc_path}"

        for page_number, pred_page in sorted(results[key].items()):
            page_no = page_number + 1  # convert to 1-indexed for groundtruth filenames

            rname = os.path.basename(pdf_doc_path)

            # Skip pages not in restrictions
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
    filename = "tests/data/regression/table_of_contents_01.pdf"

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=2, max_concurrent_results=4
        ),
        decode_config=decode_config,
    )

    key = parser.load(filename)

    count = 0
    while parser.has_tasks():
        task = parser.get_task()
        assert task.success, f"Failed to decode page {task.page_number}: {task.error()}"
        assert task.doc_key == key

        page_decoder, timings = task.get()
        assert isinstance(timings, dict)
        assert len(timings) > 0

        count += 1

    # Should have processed all pages
    assert count > 0, "Should have processed at least one page"


def test_threaded_results_match_sequential():
    """Verify threaded results match sequential results for the same documents."""

    """
    filenames = [
        "tests/data/regression/font_01.pdf",
        "tests/data/regression/ligatures_01.pdf",
    ]
    """
    filenames = glob.glob("tests/data/regression/*.pdf")

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    # Sequential parsing
    seq_parser = DoclingPdfParser(loglevel="fatal")
    sequential_pages = {}
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
            # print(f"seq: {key}, {page_no}")

    # Threaded parsing
    threaded_parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=2, max_concurrent_results=4
        ),
        decode_config=decode_config,
    )
    for filename in filenames:
        threaded_parser.load(filename)

    threaded_pages = {}
    while threaded_parser.has_tasks():
        task = threaded_parser.get_task()
        assert task.success, f"Failed: {task.error()}"

        page_decoder, timings = task.get()
        pred_page = _build_segmented_page_from_decoder(page_decoder)

        if task.doc_key not in threaded_pages:
            threaded_pages[task.doc_key] = {}
        threaded_pages[task.doc_key][task.page_number + 1] = pred_page  # 1-indexed
        # print(f"threaded: {task.doc_key}, {task.page_number + 1}")

    # Compare
    for key in sequential_pages:
        assert key in threaded_pages, f"Missing key {key} in threaded results"
        for page_no in sequential_pages[key]:
            assert page_no in threaded_pages[key], f"Missing page {page_no} for {key}"

            seq_page = sequential_pages[key][page_no]
            thr_page = threaded_pages[key][page_no]

            eps = max(
                seq_page.dimension.width / 100.0, seq_page.dimension.height / 100.0
            )

            """
            print(f"** Page {page_no} for {key} **")
            print(f" -> char-cells count for {key} page {page_no}: {len(seq_page.char_cells)} versus {len(thr_page.char_cells)}")
            print(f" -> word-cells count for {key} page {page_no}: {len(seq_page.word_cells)} versus {len(thr_page.word_cells)}")
            print(f" -> line-cells count for {key} page {page_no}: {len(seq_page.textline_cells)} versus {len(thr_page.textline_cells)}")
            print(f" -> shapes count for {key} page {page_no}: {len(seq_page.shapes)} versus {len(thr_page.shapes)}")
            """

            # Verify key fields match
            assert len(seq_page.char_cells) == len(
                thr_page.char_cells
            ), f"char_cells count mismatch for {key} page {page_no}"

            """
            if len(seq_page.word_cells)!=len(thr_page.word_cells):
                for i, cell in enumerate(seq_page.word_cells):
                    print(f" === [{i}] === ")
                    print(cell.text)
                    print(thr_page.word_cells[i].text)
                    assert cell.text==thr_page.word_cells[i].text
            """

            assert len(seq_page.word_cells) == len(
                thr_page.word_cells
            ), f"word_cells count mismatch for {key} page {page_no}"
            assert len(seq_page.textline_cells) == len(
                thr_page.textline_cells
            ), f"textline_cells count mismatch for {key} page {page_no}"
            assert len(seq_page.shapes) == len(
                thr_page.shapes
            ), f"shapes count mismatch for {key} page {page_no}"


def test_threaded_backpressure():
    """Test that backpressure works with max_concurrent_results=1."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=1,  # Very tight backpressure
        ),
        decode_config=decode_config,
    )

    parser.load(filename)

    count = 0
    while parser.has_tasks():
        task = parser.get_task()
        assert task.success, f"Failed: {task.error()}"
        count += 1

    assert count > 0


def test_threaded_single_thread():
    """Test threaded parsing with a single thread (sequential baseline)."""
    filename = "tests/data/regression/font_01.pdf"

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=32,
        ),
        decode_config=decode_config,
    )

    parser.load(filename)

    count = 0
    while parser.has_tasks():
        task = parser.get_task()
        assert task.success, f"Failed: {task.error()}"
        count += 1

    assert count > 0
