#!/usr/bin/env python
"""Tests for the threaded PDF renderer."""

import glob
import os
from io import BytesIO
from pathlib import Path

from docling_core.types.doc.page import SegmentedPdfPage
from PIL import Image as PILImage

from docling_parse.pdf_parser import (
    DecodePageConfig,
    DoclingThreadedPdfRenderer,
    RenderConfig,
    ThreadedPdfRendererConfig,
)
from tests.test_parse import (
    GROUNDTRUTH_FOLDER,
    REGRESSION_FOLDER,
    verify_SegmentedPdfPage,
)
from tests.test_threaded_parse import _build_segmented_page_from_decoder


def _make_renderer(
    threads: int = 2, max_concurrent: int = 1
) -> DoclingThreadedPdfRenderer:
    return DoclingThreadedPdfRenderer(
        renderer_config=ThreadedPdfRendererConfig(
            loglevel="fatal",
            threads=threads,
            max_concurrent_results=max_concurrent,
        ),
        decode_config=DecodePageConfig(),
        render_config=RenderConfig(),
    )


def test_render_single_document():
    """Render all pages of one document and verify each result is a valid RGBA image."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    renderer = _make_renderer()
    key = renderer.load(filename)

    count = 0
    while renderer.has_tasks():
        result = renderer.get_task()

        assert result.doc_key == key
        assert result.page_number >= 0
        assert result.success, (
            f"Render failed page {result.page_number}: {result.error()}"
        )

        image = result.get_image()
        assert image is not None, "get_image() returned None on success"
        assert isinstance(image, PILImage.Image)
        assert image.mode == "RGBA"
        assert image.width > 0
        assert image.height > 0

        count += 1

    assert count > 0, "Should have rendered at least one page"


def test_render_image_dimensions_are_consistent():
    """Verify image_shape matches the actual PIL image dimensions."""
    filename = "tests/data/regression/font_01.pdf"

    renderer = _make_renderer()
    renderer.load(filename)

    while renderer.has_tasks():
        result = renderer.get_task()
        assert result.success, result.error()

        h, w, channels = result._raw.image_shape
        assert channels == 4, "Expected 4-channel RGBA"

        image = result.get_image()
        assert image.width == w
        assert image.height == h


def test_render_multiple_documents():
    """Load multiple PDFs and verify all pages are rendered."""
    filenames = sorted(glob.glob(REGRESSION_FOLDER))  # limit to first 5 for speed
    assert len(filenames) > 0

    renderer = _make_renderer(threads=4, max_concurrent=16)
    keys = {renderer.load(f) for f in filenames}

    cnt = 0

    results_by_key = {}
    while renderer.has_tasks():
        result = renderer.get_task()
        cnt += 1

        assert result.success, (
            f"Render failed doc-key: {result.doc_key}, page: {result.page_number}: {result.error()}"
        )
        print(
            f"Render success ({cnt}): doc-key={result.doc_key}, page={result.page_number}"
        )

        results_by_key.setdefault(result.doc_key, []).append(result.page_number)

        image = result.get_image()
        assert image is not None, "image is None"

        # img.show()

        assert isinstance(image, PILImage.Image)
        assert image.mode == "RGBA"
        assert image.width > 0
        assert image.height > 0

    # Every loaded key must have at least one result
    for key in keys:
        assert key in results_by_key, f"No results for {key}"


def test_render_from_bytesio():
    """Render a document loaded from a BytesIO object."""
    filename = "tests/data/regression/font_01.pdf"

    with open(filename, "rb") as f:
        data = BytesIO(f.read())

    renderer = _make_renderer()
    key = renderer.load(data)

    count = 0
    while renderer.has_tasks():
        result = renderer.get_task()
        assert result.doc_key == key
        assert result.success, result.error()

        image = result.get_image()
        assert image is not None
        assert image.mode == "RGBA"

        count += 1

    assert count > 0


def test_render_backpressure():
    """Verify rendering completes correctly with max_concurrent_results=1."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    renderer = DoclingThreadedPdfRenderer(
        renderer_config=ThreadedPdfRendererConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=1,  # tight backpressure
        ),
        decode_config=DecodePageConfig(),
        render_config=RenderConfig(),
    )
    renderer.load(filename)

    count = 0
    while renderer.has_tasks():
        result = renderer.get_task()
        assert result.success, result.error()
        count += 1

    assert count > 0


def test_render_single_thread():
    """Render with a single thread as a sequential baseline."""
    filename = "tests/data/regression/font_01.pdf"

    renderer = DoclingThreadedPdfRenderer(
        renderer_config=ThreadedPdfRendererConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=32,
        ),
        decode_config=DecodePageConfig(),
        render_config=RenderConfig(),
    )
    renderer.load(filename)

    count = 0
    while renderer.has_tasks():
        result = renderer.get_task()
        assert result.success, result.error()

        image = result.get_image()
        assert image is not None
        assert image.mode == "RGBA"

        count += 1

    assert count > 0


def test_render_get_image_returns_none_on_failure():
    """get_image() must return None when success is False."""
    from docling_parse.pdf_parser import PdfPageRenderResult

    class _FakeRaw:
        doc_key = "k"
        page_number = 0
        success = False
        error_message = "simulated failure"
        image_shape = [0, 0, 4]

        def get_image(self):
            return b""

    result = PdfPageRenderResult(_FakeRaw())
    assert not result.success
    assert result.get_image() is None
    assert "simulated failure" in result.error()


def test_render_custom_render_config():
    """Renderer accepts a non-default RenderConfig without error."""
    filename = "tests/data/regression/font_01.pdf"

    render_config = RenderConfig()
    render_config.render_text = True
    render_config.draw_text_bbox = False
    render_config.fit_glyph_bbox_to_target = True
    render_config.resolve_fonts = True

    renderer = DoclingThreadedPdfRenderer(
        renderer_config=ThreadedPdfRendererConfig(loglevel="fatal", threads=2),
        decode_config=DecodePageConfig(),
        render_config=render_config,
    )
    renderer.load(filename)

    while renderer.has_tasks():
        result = renderer.get_task()
        assert result.success, result.error()
        image = result.get_image()
        assert image is not None


def test_render_config_exposes_bbox_fit_flag():
    """RenderConfig exposes the opt-in glyph bbox fit flag."""
    render_config = RenderConfig()
    assert render_config.fit_glyph_bbox_to_target is False

    render_config.fit_glyph_bbox_to_target = True
    assert render_config.fit_glyph_bbox_to_target is True


def test_render_reference_documents_from_filenames():
    """Render all regression PDFs and verify parse output against groundtruth."""
    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    decode_config = DecodePageConfig()
    decode_config.page_boundary = "crop_box"
    decode_config.do_sanitization = False
    decode_config.keep_glyphs = True
    decode_config.keep_qpdf_warnings = False

    renderer = DoclingThreadedPdfRenderer(
        renderer_config=ThreadedPdfRendererConfig(
            loglevel="fatal",
            threads=4,
            max_concurrent_results=32,
        ),
        decode_config=decode_config,
        render_config=RenderConfig(),
    )

    for pdf_doc_path in pdf_docs:
        renderer.load(pdf_doc_path)

    # Page restrictions (same as sequential test)
    page_restrictions = {
        "deep-mediabox-inheritance.pdf": [2],
        "font_06.pdf": [1],
        "font_07.pdf": [1],
        "font_08.pdf": [1],
        "font_09.pdf": [1],
        "font_10.pdf": [1],
    }

    results = {}
    while renderer.has_tasks():
        result = renderer.get_task()

        assert result.doc_key != "", "doc_key should not be empty"

        if result.success:
            page_decoder, _timings = result.get()
            pred_page = _build_segmented_page_from_decoder(page_decoder)

            if result.doc_key not in results:
                results[result.doc_key] = {}
            results[result.doc_key][result.page_number] = pred_page
        else:
            print(
                f"Warning: render failed for {result.doc_key} page {result.page_number}: {result.error()}"
            )

    for pdf_doc_path in pdf_docs:
        key = f"key={Path(pdf_doc_path)!s}"

        assert key in results, f"No results found for {pdf_doc_path}"

        for page_number, pred_page in sorted(results[key].items()):
            page_no = page_number + 1  # convert to 1-indexed for groundtruth filenames

            rname = os.path.basename(pdf_doc_path)

            if rname in page_restrictions and page_no not in page_restrictions[rname]:
                continue

            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            if os.path.exists(fname):
                true_page = SegmentedPdfPage.load_from_json(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)
