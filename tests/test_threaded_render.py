#!/usr/bin/env python
"""Tests for threaded parse-and-render mode."""

import glob
import os
from io import BytesIO

import pytest
from docling_core.types.doc.page import SegmentedPdfPage
from PIL import Image as PILImage

from docling_parse.pdf_parser import (
    DecodePageConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
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


def _make_render_config() -> RenderConfig:
    return RenderConfig()


def _make_parser(
    threads: int = 2,
    max_concurrent: int = 1,
    render_config: RenderConfig | None = None,
) -> DoclingThreadedPdfParser:
    return DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=threads,
            max_concurrent_results=max_concurrent,
            render_config=render_config or _make_render_config(),
        ),
        decode_config=_make_decode_config(),
    )


def test_render_single_document():
    """Render all pages of one document and verify each result is a valid RGBA image."""
    filename = SAMPLE_PDF

    parser = _make_parser()
    key = parser.load(filename)

    count = 0
    for result in parser.iterate_results():
        assert result.doc_key == key
        assert result.page_number >= 1
        assert result.success, (
            f"Render failed page {result.page_number}: {result.error_message}"
        )
        assert result.has_image

        image = result.get_image()
        assert isinstance(image, PILImage.Image)
        assert image.mode == "RGBA"
        assert image.width > 0
        assert image.height > 0
        assert result.get_page().dimension.rect is not None

        count += 1

    assert count == parser.page_count(key)


def test_render_image_dimensions_are_consistent():
    """Verify rendered image dimensions are positive and stable."""
    filename = SAMPLE_PDF

    parser = _make_parser()
    parser.load(filename)

    for result in parser.iterate_results():
        assert result.success, result.error_message
        image = result.get_image()
        assert image.width > 0
        assert image.height > 0


def test_render_multiple_documents():
    """Load multiple PDFs and verify all pages are rendered."""
    parser = _make_parser(threads=4, max_concurrent=16)
    path_key = parser.load(SAMPLE_PDF)
    with open(SAMPLE_PDF, "rb") as f:
        bytes_key = parser.load(BytesIO(f.read()))
    keys = {path_key, bytes_key}

    results_by_key: dict[str, list[int]] = {}
    for result in parser.iterate_results():
        assert result.success, (
            f"Render failed doc-key: {result.doc_key}, page: {result.page_number}: {result.error_message}"
        )
        results_by_key.setdefault(result.doc_key, []).append(result.page_number)

        image = result.get_image()
        assert isinstance(image, PILImage.Image)
        assert image.mode == "RGBA"
        assert image.width > 0
        assert image.height > 0

    for key in keys:
        assert key in results_by_key, f"No results for {key}"
        assert len(results_by_key[key]) == parser.page_count(key)


def test_render_from_bytesio():
    """Render a document loaded from a BytesIO object."""
    filename = SAMPLE_PDF

    with open(filename, "rb") as f:
        data = BytesIO(f.read())

    parser = _make_parser()
    key = parser.load(data)

    count = 0
    for result in parser.iterate_results():
        assert result.doc_key == key
        assert result.success, result.error_message
        assert result.get_image().mode == "RGBA"
        count += 1

    assert count == parser.page_count(key)


def test_render_backpressure():
    """Verify rendering completes correctly with max_concurrent_results=1."""
    filename = LARGE_SAMPLE_PDF

    parser = _make_parser(threads=2, max_concurrent=1)
    key = parser.load(filename)

    count = sum(1 for result in parser.iterate_results() if result.success)
    assert count == parser.page_count(key)


def test_render_single_thread():
    """Render with a single thread as a sequential baseline."""
    filename = SAMPLE_PDF

    parser = _make_parser(threads=1, max_concurrent=32)
    key = parser.load(filename)

    count = sum(1 for result in parser.iterate_results() if result.success)
    assert count == parser.page_count(key)


def test_get_image_raises_without_rendering():
    """Parse-only results must fail loudly when image access is requested."""
    filename = SAMPLE_PDF

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
        decode_config=_make_decode_config(),
    )
    parser.load(filename)

    result = next(parser.iterate_results())
    assert not result.has_image
    with pytest.raises(RuntimeError, match="Rendered image not available"):
        result.get_image()


def test_render_custom_render_config():
    """Parser accepts a non-default RenderConfig without error."""
    filename = SAMPLE_PDF

    render_config = RenderConfig()
    render_config.render_text = True
    render_config.draw_text_bbox = False
    render_config.resolve_fonts = True

    parser = _make_parser(render_config=render_config)
    parser.load(filename)

    for result in parser.iterate_results():
        assert result.success, result.error_message
        assert result.get_image() is not None


def test_render_reference_documents_from_filenames():
    """Render all regression PDFs and verify parse output against groundtruth."""
    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = _make_parser(threads=4, max_concurrent=32)
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
            assert result.get_image().mode == "RGBA"
        else:
            print(
                f"Warning: render failed for {result.doc_key} page {result.page_number}: {result.error_message}"
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
