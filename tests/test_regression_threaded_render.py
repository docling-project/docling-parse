#!/usr/bin/env python
"""Tests for threaded parse-and-render mode."""

import glob
import math
import os
from io import BytesIO
from pathlib import Path
from types import TracebackType

import pytest
from docling_core.types.doc.base import BoundingBox, CoordOrigin
from docling_core.types.doc.page import SegmentedPdfPage
from PIL import Image as PILImage

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)
from tests.constants import (
    LARGE_SAMPLE_PDF,
    PARSER_PAGE_RESTRICTIONS,
    SAMPLE_PDF,
)
from tests.groundtruth_io import groundtruth_exists, load_segmented_page
from tests.rendering_regression import (
    ImageTolerance,
    compare_bitmap_artifacts,
    compare_images,
    compare_render_instructions,
    format_image_comparison_table,
    image_comparison_failed,
    measure_image_comparison,
    renderer_groundtruth_exists,
    write_renderer_groundtruth,
)
from tests.test_regression_parse import (
    GROUNDTRUTH_FOLDER,
    REGRESSION_FOLDER,
    verify_SegmentedPdfPage,
)

RENDERER_IMAGE_TOLERANCE = ImageTolerance(
    pixel_threshold=12,
    mean_abs_error=10.0,  # cut-off that works on the CI. would be better ~2-3
    changed_pixels_ratio=0.15,  # cut-off that works on the CI, would be better ~0.02
)

# Documents whose CJK text is drawn with a face that is not in the PDF.
#
# They name fonts nobody outside their country of origin has -- /Batang,
# /Gulim, /宋体, /楷体_GB2312, /HGMarugothicMPRO -- and embed none of them, so
# the renderer must substitute. What it substitutes is a property of the host:
# macOS, where this groundtruth was written, has Hiragino / AppleGothic /
# PingFang; Linux has Noto CJK. Two different typefaces drawing the same page
# differ far more than a rendering change ever would, and no amount of work on
# our side converges them -- see .plans/cjk_system_font_fallback_freetype.md.
#
# They are still compared, at a tolerance wide enough for the substitution and
# no wider, so a page that stops rendering CJK at all (which is what a host
# with no loadable CJK face produces: a grid of .notdef boxes) still fails.
# Everything else keeps the tight tolerance; loosening it globally would have
# retired the signal on the ~200 pages that do not substitute anything.
FONT_SUBSTITUTED_DOCUMENTS = frozenset(
    {
        "1019970077588-3.pdf",
        "1020000079773-1.pdf",
        "1020000086635-2.pdf",
        "1020010076157-4.pdf",
        "11273518440839632455_002.pdf",
        "15523818099946337472-5.pdf",
        "2020020019307-7.pdf",
        "4d0b77de7cfa5295_0005.pdf",
        "7829021aca2be7b4_0002.pdf",
    }
)

SUBSTITUTED_FONT_IMAGE_TOLERANCE = ImageTolerance(
    pixel_threshold=12,
    mean_abs_error=20.0,
    changed_pixels_ratio=0.30,
)


def _image_tolerance_for(document: str) -> ImageTolerance:
    if document in FONT_SUBSTITUTED_DOCUMENTS:
        return SUBSTITUTED_FONT_IMAGE_TOLERANCE
    return RENDERER_IMAGE_TOLERANCE


def _make_decode_config() -> DecodeConfig:
    return DecodeConfig(
        do_sanitization=True,
        keep_glyphs=True,
        keep_qpdf_warnings=False,
    )


def _make_render_config() -> RenderConfig:
    return RenderConfig()


def _make_groundtruth_render_config() -> RenderConfig:
    render_config = RenderConfig()
    render_config.scale = 2.0
    return render_config


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


def _write_matplotlib_table_repro_pdf(path: Path, linewidth: float = 0.4) -> None:
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(8.27, 11.69))
    ax.axis("off")
    ax.text(
        0.5,
        0.95,
        "Contacts available for customer meetings",
        ha="center",
        fontsize=14,
    )
    rows = [
        ["Area of expertise", "Product Management", "Product Marketing"],
        ["Document Cloud", "Vamsi Vutukuru", "Nora Yau"],
        ["Acrobat", "Alex Chen", "Maria Lopez"],
        ["Sign", "Sam Patel", "Lena Frei"],
    ]
    table = ax.table(
        cellText=rows[1:],
        colLabels=rows[0],
        loc="center",
        cellLoc="center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table.scale(1, 2.2)
    for cell in table.get_celld().values():
        cell.set_linewidth(linewidth)
    fig.savefig(path)
    plt.close(fig)


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
        assert result.timings.total_s > 0
        assert result.timings.render_page_s >= 0

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
        len_results = len(results_by_key[key])
        len_pages = parser.page_count(key)
        assert len_results == len_pages


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
        math.ceil(result.page_width * 2.0),
        math.ceil(result.page_height * 2.0),
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
        math.ceil(result.page_width * 2.0),
        math.ceil(result.page_height * 2.0),
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
        math.ceil(result.page_width * 2.0),
        math.ceil(result.page_height * 2.0),
    )
    assert semantic_image.size == (
        math.ceil(result.page_width),
        math.ceil(result.page_height),
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


def test_render_config_exposes_min_stroke_width():
    """RenderConfig exposes the stroke visibility floor."""
    render_config = RenderConfig()
    assert render_config.min_stroke_width == 1.0

    render_config.min_stroke_width = 0.25
    assert render_config.min_stroke_width == 0.25


def test_matplotlib_table_repro_renders_subpixel_ruling_lines(tmp_path: Path):
    pdf_path = tmp_path / "table_repro.pdf"
    _write_matplotlib_table_repro_pdf(pdf_path, linewidth=0.4)

    render_config = RenderConfig()
    render_config.scale = 1.0
    parser = _make_parser(threads=1, max_concurrent=2, render_config=render_config)
    parser.load(str(pdf_path), page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message
    assert result.has_image

    page = result.get_page()
    subpixel_shapes = [
        shape
        for shape in page.shapes
        if 0.0 < shape.line_width < 1.0 and len(shape.points) >= 4
    ]
    assert len(subpixel_shapes) >= 12

    image = result.get_image().convert("RGB")
    dark_pixels = 0
    for y in range(image.height // 3, image.height * 2 // 3):
        for x in range(image.width // 8, image.width * 7 // 8):
            pixel = image.getpixel((x, y))
            assert isinstance(pixel, tuple)
            r, g, b = pixel[:3]
            if max(r, g, b) < 160:
                dark_pixels += 1

    assert dark_pixels > 500


def test_inline_image_mask_renders_dotted_blue_separator():
    pdf_path = (
        Path("tests/data/regression")
        / "dln_5db56eedc88aada588e42fcd8795b61cef1b384b57f66e86a11803530534b02c.pdf"
    )

    render_config = RenderConfig()
    render_config.scale = 2.0
    parser = _make_parser(threads=1, max_concurrent=2, render_config=render_config)
    parser.load(str(pdf_path), page_numbers=[1])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    page = result.get_page()
    separator_masks = [
        bitmap
        for bitmap in page.bitmap_resources
        if abs(bitmap.rect.r_y0 - 610.5) < 0.01 and abs(bitmap.rect.r_y2 - 611.5) < 0.01
    ]
    assert len(separator_masks) == 2
    assert sorted(mask.image.size.width for mask in separator_masks) == [509, 1992]

    image = result.get_image().convert("RGB")
    blue_pixels = 0
    dark_pixels = 0
    for y in range(360, 363):
        for x in range(140, 1150):
            pixel = image.getpixel((x, y))
            assert isinstance(pixel, tuple)
            r, g, b = pixel[:3]
            if b > 120 and r < 100 and g > 40:
                blue_pixels += 1
            if (r + g + b) / 3 < 245:
                dark_pixels += 1

    assert blue_pixels > 300
    assert dark_pixels > 500


def test_render_reference_documents_from_filenames():
    """Render all regression PDFs and verify parse output against groundtruth."""
    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = _make_parser(threads=4, max_concurrent=32)

    test_results: list[tuple[str, str, str, bool, str]] = []
    first_failure: tuple[BaseException, TracebackType | None] | None = None
    doc_keys: dict[str, str] = {}
    key_to_path: dict[str, str] = {}

    for pdf_doc_path in pdf_docs:
        rname = os.path.basename(pdf_doc_path)
        try:
            # page_numbers=None renders the whole document; restricted documents
            # never render the pages that are not verified
            key = parser.load(
                pdf_doc_path, page_numbers=PARSER_PAGE_RESTRICTIONS.get(rname)
            )
        except Exception as exc:
            if first_failure is None:
                first_failure = (exc, exc.__traceback__)
            test_results.append((rname, "N/A", "parser", False, str(exc)))
            continue
        doc_keys[pdf_doc_path] = key
        key_to_path[key] = pdf_doc_path

    results: dict[str, dict[int, SegmentedPdfPage]] = {}
    for result in parser.iterate_results():
        assert result.doc_key != "", "doc_key should not be empty"
        if result.success:
            results.setdefault(result.doc_key, {})[result.page_number] = (
                result.get_page()
            )
            assert result.get_image().mode == "RGBA"
        else:
            pdf_doc_path = key_to_path.get(result.doc_key, result.doc_key)
            err = AssertionError(result.error_message)
            if first_failure is None:
                first_failure = (err, err.__traceback__)
            test_results.append(
                (
                    os.path.basename(pdf_doc_path),
                    str(result.page_number),
                    "render",
                    False,
                    result.error_message,
                )
            )

    for pdf_doc_path in pdf_docs:
        if pdf_doc_path not in doc_keys:
            continue

        key = doc_keys[pdf_doc_path]
        if key not in results:
            err = AssertionError(f"No results found for {pdf_doc_path}")
            if first_failure is None:
                first_failure = (err, err.__traceback__)
            test_results.append(
                (os.path.basename(pdf_doc_path), "N/A", "render", False, str(err))
            )
            continue

        rname = os.path.basename(pdf_doc_path)

        for page_no, pred_page in sorted(results[key].items()):
            if (
                rname in PARSER_PAGE_RESTRICTIONS
                and page_no not in PARSER_PAGE_RESTRICTIONS[rname]
            ):
                continue

            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            if not groundtruth_exists(fname):
                err = AssertionError(f"missing groundtruth file: {fname}")
                if first_failure is None:
                    first_failure = (err, err.__traceback__)
                test_results.append((rname, str(page_no), "all", False, str(err)))
                continue

            try:
                true_page = load_segmented_page(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)
            except Exception as exc:
                if first_failure is None:
                    first_failure = (exc, exc.__traceback__)
                test_results.append((rname, str(page_no), "all", False, str(exc)))
            else:
                test_results.append((rname, str(page_no), "all", True, ""))

    failed = [
        (doc, page, mode, err) for doc, page, mode, ok, err in test_results if not ok
    ]
    if first_failure is not None:
        failure, tb = first_failure
        raise failure.with_traceback(tb)

    # assert on the count: `assert not failed` would inline every error message
    # of every failing page into the pytest report
    num_failed = len(failed)
    assert num_failed == 0, f"{num_failed} page(s) failed: " + ", ".join(
        f"{doc}@{page}[{mode}]" for doc, page, mode, _ in failed
    )


@pytest.mark.groundtruth
def test_rendered_pages_match_groundtruth(update_groundtruth: bool):
    """Compare Blend2D threaded-render output with renderer groundtruth artifacts."""
    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = _make_parser(
        threads=4,
        max_concurrent=32,
        render_config=_make_groundtruth_render_config(),
    )

    key_to_path: dict[str, str] = {}
    for pdf_doc_path in pdf_docs:
        rname = os.path.basename(pdf_doc_path)
        key = parser.load(
            pdf_doc_path,
            page_numbers=PARSER_PAGE_RESTRICTIONS.get(rname),
        )
        key_to_path[key] = pdf_doc_path

    checked_pages = 0
    first_failure: tuple[BaseException, TracebackType | None] | None = None
    failures: list[str] = []
    image_comparisons = []

    for result in parser.iterate_results():
        pdf_doc_path = key_to_path[result.doc_key]
        rname = os.path.basename(pdf_doc_path)

        if not result.success:
            err = AssertionError(result.error_message)
            if first_failure is None:
                first_failure = (err, err.__traceback__)
            failures.append(f"{rname}@{result.page_number}[render]")
            continue

        try:
            if update_groundtruth or not renderer_groundtruth_exists(
                rname, result.page_number
            ):
                write_renderer_groundtruth(rname, result.page_number, result)
            else:
                compare_render_instructions(rname, result.page_number, result)
                compare_bitmap_artifacts(rname, result.page_number, result)
                tolerance = _image_tolerance_for(rname)
                comparison = measure_image_comparison(
                    rname,
                    result.page_number,
                    result.get_image(),
                    tolerance=tolerance,
                )
                image_comparisons.append(comparison)
                if image_comparison_failed(comparison, tolerance=tolerance):
                    compare_images(
                        rname,
                        result.page_number,
                        result.get_image(),
                        tolerance=tolerance,
                    )
        except Exception as exc:
            if first_failure is None:
                first_failure = (exc, exc.__traceback__)
            failures.append(f"{rname}@{result.page_number}[groundtruth]")
        else:
            checked_pages += 1

    parser.unload_all()

    if image_comparisons:
        print(format_image_comparison_table(image_comparisons))

    if first_failure is not None:
        failure, tb = first_failure
        raise failure.with_traceback(tb)

    num_failures = len(failures)
    assert num_failures == 0, f"{num_failures} rendered page(s) failed: " + ", ".join(
        failures
    )
    assert checked_pages > 0
