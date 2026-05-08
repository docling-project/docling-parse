#!/usr/bin/env python
"""Tests for threaded parse-and-render mode."""

import glob
import os
from io import BytesIO
from pathlib import Path

import pytest
from docling_core.types.doc.base import BoundingBox, CoordOrigin
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


def _write_variable_page_size_pdf(path: Path) -> None:
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Contents 4 0 R >>",
        "<< /Length 0 >>\nstream\n\nendstream",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 500] /Contents 6 0 R >>",
        "<< /Length 0 >>\nstream\n\nendstream",
    ]

    chunks = [b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"]
    offsets = [0]

    for object_number, body in enumerate(objects, start=1):
        offsets.append(sum(len(chunk) for chunk in chunks))
        chunks.append(f"{object_number} 0 obj\n{body}\nendobj\n".encode("ascii"))

    xref_offset = sum(len(chunk) for chunk in chunks)
    xref_lines = [
        "xref",
        f"0 {len(objects) + 1}",
        "0000000000 65535 f ",
    ]
    xref_lines.extend(f"{offset:010d} 00000 n " for offset in offsets[1:])
    trailer = [
        "trailer",
        f"<< /Size {len(objects) + 1} /Root 1 0 R >>",
        "startxref",
        str(xref_offset),
        "%%EOF",
    ]
    chunks.append(("\n".join(xref_lines) + "\n").encode("ascii"))
    chunks.append(("\n".join(trailer) + "\n").encode("ascii"))

    path.write_bytes(b"".join(chunks))


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
    render_config.fit_glyph_bbox_to_target = True
    render_config.resolve_fonts = True

    parser = _make_parser(render_config=render_config)
    parser.load(filename)

    for result in parser.iterate_results():
        assert result.success, result.error_message
        assert result.get_image() is not None


def test_get_image_scale_rerenders_for_canvas_config():
    render_config = RenderConfig()
    render_config.canvas_width = 1224
    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    scaled_image = result.get_image(scale=2.0)

    assert scaled_image.size == (
        round(result.page_width * 2.0),
        round(result.page_height * 2.0),
    )


def test_get_image_rerenders_non_default_scale():
    render_config = RenderConfig()
    render_config.scale = 1.0
    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    default_image = result.get_image()
    scaled_image = result.get_image(scale=2.0)

    assert scaled_image.size == (
        round(result.page_width * 2.0),
        round(result.page_height * 2.0),
    )
    assert scaled_image.size != default_image.size


def test_get_image_canvas_size_is_accepted_for_canvas_config():
    render_config = RenderConfig()
    render_config.canvas_width = 1224

    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    default_image = result.get_image()
    same_image = result.get_image(canvas_size=default_image.size)
    custom_image = result.get_image(canvas_size=(600, 800))

    assert same_image.size == default_image.size
    assert custom_image.size == (600, 800)


def test_get_image_canvas_size_is_accepted_for_scale_config():
    render_config = RenderConfig()
    render_config.scale = 2.0

    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    default_image = result.get_image()
    semantic_image = result.get_image(scale=1.0)
    same_image = result.get_image(canvas_size=default_image.size)

    assert default_image.size == (
        round(result.page_width * 2.0),
        round(result.page_height * 2.0),
    )
    assert semantic_image.size == (
        round(result.page_width),
        round(result.page_height),
    )
    assert same_image.size == default_image.size


def test_get_image_rejects_scale_with_canvas_size():
    render_config = RenderConfig()
    render_config.scale = 1.0

    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    with pytest.raises(ValueError):
        result.get_image(scale=1.0, canvas_size=(100, 100))


def test_render_config_rejects_scale_with_canvas_dimensions():
    render_config = RenderConfig()
    render_config.scale = 2.0
    render_config.canvas_width = 1224

    with pytest.raises(ValueError):
        _make_parser(render_config=render_config)


def test_get_image_crops_using_page_coordinates():
    render_config = RenderConfig()
    render_config.scale = 2.0
    parser = _make_parser(render_config=render_config)
    parser.load(SAMPLE_PDF, page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    cropbox = BoundingBox(
        l=10,
        t=20,
        r=60,
        b=90,
        coord_origin=CoordOrigin.TOPLEFT,
    )
    cropped = result.get_image(scale=2.0, cropbox=cropbox)

    assert cropped.size == (
        round((cropbox.r - cropbox.l) * 2.0),
        round((cropbox.b - cropbox.t) * 2.0),
    )


def test_render_scale_config_handles_pages_with_different_sizes(tmp_path: Path):
    pdf_path = tmp_path / "variable_page_sizes.pdf"
    _write_variable_page_size_pdf(pdf_path)

    render_config = RenderConfig()
    render_config.scale = 2.0

    parser = _make_parser(render_config=render_config)
    parser.load(pdf_path)

    sizes_by_page: dict[int, tuple[int, int]] = {}
    for result in parser.iterate_results():
        assert result.success, result.error_message
        image = result.get_image()
        sizes_by_page[result.page_number] = image.size

    assert sizes_by_page[1] == (400, 600)
    assert sizes_by_page[2] == (800, 1000)


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
