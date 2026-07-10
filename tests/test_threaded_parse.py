#!/usr/bin/env python
"""Tests for the threaded PDF parser."""

import glob
import os
from pathlib import Path

import pytest
from docling_core.types.doc.base import CoordOrigin
from docling_core.types.doc.page import PdfPageBoundaryType, SegmentedPdfPage

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)
from tests.constants import PARSER_PAGE_RESTRICTIONS
from tests.test_parse import (
    GROUNDTRUTH_FOLDER,
    REGRESSION_FOLDER,
    verify_SegmentedPdfPage,
)

SAMPLE_PDF = "docs/dln-v1.pdf"
LARGE_SAMPLE_PDF = "docs/PDF32000_2008.pdf"


def _write_shape_geometry_pdf(path: Path) -> None:
    content = b"""
q
1 w
10 10 m 110 10 l S
20 20 m 20 120 l S
30 30 m 45 50 55 50 70 30 c S
q
0 0 50 50 re W n
10 60 m 110 60 l S
Q
q
0 0 80 80 re W n
10 70 m 110 70 l S
Q
120 120 30 30 re f
140 140 30 30 re f
10 150 10 10 re f
Q
"""
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        b"/CropBox [0 0 200 200] /Contents 4 0 R >>",
        b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content),
    ]

    data = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for idx, obj in enumerate(objects, start=1):
        offsets.append(len(data))
        data.extend(f"{idx} 0 obj\n".encode("ascii"))
        data.extend(obj)
        data.extend(b"\nendobj\n")

    xref_offset = len(data)
    data.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    data.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        data.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    data.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref_offset}\n%%EOF\n".encode("ascii")
    )
    path.write_bytes(data)


def _shape_geometry_result(tmp_path: Path):
    pdf_path = tmp_path / "shape_geometry.pdf"
    _write_shape_geometry_pdf(pdf_path)

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=2,
            boundary_type=PdfPageBoundaryType.CROP_BOX,
        ),
        decode_config=_make_decode_config(),
    )
    parser.load(str(pdf_path), page_numbers=[1])
    result = next(parser.iterate_results())
    assert result.success, result.error_message
    return result


def _bbox_tuple(bbox):
    return (round(bbox.l, 3), round(bbox.b, 3), round(bbox.r, 3), round(bbox.t, 3))


def _make_decode_config() -> DecodeConfig:
    return DecodeConfig(
        do_sanitization=True,
        keep_glyphs=True,
        keep_qpdf_warnings=False,
    )


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

    # Each entry: (doc_name, page_no_str, mode, success, error_msg)
    test_results: list[tuple[str, str, str, bool, str]] = []
    first_failure: tuple[BaseException, object] | None = None
    doc_keys: dict[str, str] = {}
    key_to_path: dict[str, str] = {}

    for pdf_doc_path in pdf_docs:
        print(f"parsing {pdf_doc_path}")
        rname = os.path.basename(pdf_doc_path)
        try:
            key = parser.load(pdf_doc_path)
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
        else:
            pdf_doc_path = key_to_path.get(result.doc_key, result.doc_key)
            err = AssertionError(result.error_message)
            if first_failure is None:
                first_failure = (err, err.__traceback__)
            test_results.append(
                (
                    os.path.basename(pdf_doc_path),
                    str(result.page_number),
                    "page",
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
                (os.path.basename(pdf_doc_path), "N/A", "page", False, str(err))
            )
            continue

        rname = os.path.basename(pdf_doc_path)

        for page_no, pred_page in sorted(results.get(key, {}).items()):
            print(f" -> Page {page_no} has {len(pred_page.textline_cells)} cells.")

            if (
                rname in PARSER_PAGE_RESTRICTIONS
                and page_no not in PARSER_PAGE_RESTRICTIONS[rname]
            ):
                continue

            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            if not os.path.exists(fname):
                err = AssertionError(f"missing groundtruth file: {fname}")
                if first_failure is None:
                    first_failure = (err, err.__traceback__)
                test_results.append((rname, str(page_no), "all", False, str(err)))
                continue

            try:
                true_page = SegmentedPdfPage.load_from_json(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)
            except Exception as exc:
                if first_failure is None:
                    first_failure = (exc, exc.__traceback__)
                test_results.append((rname, str(page_no), "all", False, str(exc)))
            else:
                test_results.append((rname, str(page_no), "all", True, ""))

    # --- results table ---
    from tabulate import tabulate

    def _trunc(v, n=128):
        s = str(v)
        return s if len(s) <= n else s[: n - 3] + "..."

    table = [
        (_trunc(doc), page, mode, "PASS" if ok else "FAIL", _trunc(err))
        for doc, page, mode, ok, err in test_results
    ]
    print(
        "\n"
        + tabulate(
            table,
            headers=["document", "page", "mode", "status", "error"],
            tablefmt="grid",
        )
        + "\n"
    )

    failed = [
        (doc, page, mode, err) for doc, page, mode, ok, err in test_results if not ok
    ]
    if first_failure is not None:
        failure, tb = first_failure
        raise failure.with_traceback(tb)

    assert not failed, f"{len(failed)} page(s) failed: " + ", ".join(
        f"{doc}@{page}[{mode}]" for doc, page, mode, _ in failed
    )


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
        assert result.timings.total_s > 0
        assert result.timings.decode_page_s > 0
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
            decode_config=decode_config,
        )
        key = f"key={filename}"
        sequential_pages[key] = {}
        for page_no, page in pdf_doc.iterate_pages():
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


def _make_bitmap_config() -> DecodeConfig:
    return DecodeConfig(do_sanitization=False)


def test_threaded_bitmap_no_materialization_preserves_geometry():
    """Threaded path: geometry matches between full and placeholder-only modes."""
    from docling_core.types.doc.base import ImageRefMode

    config = _make_bitmap_config()
    materialize_full = ContentConfig(include_bitmap_bytes=True)
    materialize_geo = ContentConfig(include_bitmap_bytes=False)

    def _get_page1(
        content_config: ContentConfig,
    ) -> "SegmentedPdfPage":
        parser = DoclingThreadedPdfParser(
            parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=2),
            decode_config=config,
        )
        parser.load(BITMAP_PDF)
        return next(
            r.get_page(content_config)
            for r in parser.iterate_results()
            if r.success and r.page_number == 1
        )

    page_full = _get_page1(materialize_full)
    page_geo = _get_page1(materialize_geo)

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


def _first_successful_result(parser):
    return next(r for r in parser.iterate_results() if r.success)


def test_threaded_result_upgrade_compute_to_materialize():
    """A batch decoded at COMPUTE can surface those cells per result."""
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            page_content_config=ContentConfig(
                word_cells_content_level=ContentLevel.COMPUTE
            ),
        ),
        decode_config=_make_decode_config(),
    )
    parser.load(SAMPLE_PDF)
    result = _first_successful_result(parser)

    # Batch default emit is COMPUTE -> not surfaced.
    assert len(result.get_page().word_cells) == 0
    # Upgrade COMPUTE -> COMPUTE_AND_MATERIALIZE: cells were computed in C++, now surfaced.
    upgraded = result.get_page(
        ContentConfig(word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE)
    )
    assert len(upgraded.word_cells) > 0


def test_threaded_result_rejects_skipped_entity():
    """Requesting an entity the batch skipped raises instead of yielding empty."""
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            page_content_config=ContentConfig(
                word_cells_content_level=ContentLevel.SKIP
            ),
        ),
        decode_config=_make_decode_config(),
    )
    parser.load(SAMPLE_PDF)
    result = _first_successful_result(parser)

    with pytest.raises(ValueError, match="batch skipped"):
        result.get_page(
            ContentConfig(word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE)
        )


def test_threaded_result_rejects_skipped_entity_after_config_mutation():
    """The rejection uses the compiled batch mask, not the caller's mutable config."""
    content_config = ContentConfig(word_cells_content_level=ContentLevel.SKIP)
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            page_content_config=content_config,
        ),
        decode_config=_make_decode_config(),
    )
    content_config.word_cells_content_level = ContentLevel.COMPUTE_AND_MATERIALIZE

    parser.load(SAMPLE_PDF)
    result = _first_successful_result(parser)

    with pytest.raises(ValueError, match="batch skipped"):
        result.get_page(
            ContentConfig(word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE)
        )


def test_threaded_result_get_shape_lines_returns_only_visible_straight_lines(
    tmp_path: Path,
):
    result = _shape_geometry_result(tmp_path)

    lines = result.get_shape_lines(horizontal=True, vertical=True)
    assert all(line.coord_origin == CoordOrigin.BOTTOMLEFT for line in lines)

    line_boxes = {_bbox_tuple(line) for line in lines}
    assert (10.0, 10.0, 110.0, 10.0) in line_boxes
    assert (20.0, 20.0, 20.0, 120.0) in line_boxes
    assert (10.0, 70.0, 80.0, 70.0) in line_boxes

    assert (10.0, 60.0, 110.0, 60.0) not in line_boxes
    assert not any(box[0] == 30.0 and box[2] == 70.0 for box in line_boxes)

    horizontal = {_bbox_tuple(line) for line in result.get_shape_lines(vertical=False)}
    vertical = {_bbox_tuple(line) for line in result.get_shape_lines(horizontal=False)}
    assert (10.0, 10.0, 110.0, 10.0) in horizontal
    assert (20.0, 20.0, 20.0, 120.0) not in horizontal
    assert (20.0, 20.0, 20.0, 120.0) in vertical
    assert (10.0, 10.0, 110.0, 10.0) not in vertical


def test_threaded_result_get_connected_shape_bounding_boxes(tmp_path: Path):
    result = _shape_geometry_result(tmp_path)

    boxes = result.get_connected_shape_bounding_boxes()
    assert all(box.coord_origin == CoordOrigin.BOTTOMLEFT for box in boxes)

    box_tuples = {_bbox_tuple(box) for box in boxes}
    assert (120.0, 120.0, 170.0, 170.0) in box_tuples
    assert (10.0, 150.0, 20.0, 160.0) in box_tuples
