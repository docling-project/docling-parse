#!/usr/bin/env python
import copy
import glob
import json
import os
import re
from collections import Counter
from io import BytesIO
from typing import Dict, List, Union

from docling_core.types.doc.page import (
    BitmapResource,
    PdfHyperlink,
    PdfPageBoundaryType,
    PdfShape,
    PdfTableOfContents,
    PdfTextCell,
    PdfWidget,
    SegmentedPdfPage,
    TextCell,
    TextCellUnit,
)
from pydantic import TypeAdapter

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingPdfParser,
    PdfDocument,
)
from tests.constants import PARSER_PAGE_RESTRICTIONS, SAMPLE_PDF
from tests.data_utils import PARSER_GROUNDTRUTH_DIR
from tests.groundtruth_io import (
    dump_groundtruth_json,
    groundtruth_exists,
    load_groundtruth_json,
    load_segmented_page,
    read_groundtruth_text,
    resolve_groundtruth_path,
    write_groundtruth_text,
)


def write_textline_delta(lines: List[str], filename: str, separator: str) -> None:
    with open(filename, "w", encoding="utf-8") as fw:
        fw.write(separator.join(lines))


def _round_floats(obj, ndigits=3):
    """Recursively round all floats in a JSON-serializable structure."""
    if isinstance(obj, float):
        return round(obj, ndigits)
    if isinstance(obj, dict):
        return {k: _round_floats(v, ndigits) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_round_floats(v, ndigits) for v in obj]
    return obj


def save_as_json_rounded(page: SegmentedPdfPage, filename, ndigits=3):
    """Save SegmentedPdfPage as JSON with floats rounded to ndigits."""
    out = _round_floats(page.export_to_dict(), ndigits=ndigits)
    dump_groundtruth_json(filename, out)


GROUNDTRUTH_FOLDER = os.fspath(PARSER_GROUNDTRUTH_DIR)
REGRESSION_FOLDER = "tests/data/regression/*.pdf"


def _truncate_data_uri(uri, max_chars: int = 64):
    if uri is None:
        return uri

    value = str(uri)
    marker = "base64,"
    if not value.startswith("data:") or marker not in value:
        return uri

    prefix, payload = value.split(marker, 1)
    return f"{prefix}{marker}{payload[:max_chars]}..."


def _set_attr(obj, name: str, value) -> None:
    try:
        setattr(obj, name, value)
    except Exception:
        object.__setattr__(obj, name, value)


def _sanitize_bitmap_resources(
    bitmap_resources: List[BitmapResource],
) -> List[BitmapResource]:
    sanitized = copy.deepcopy(bitmap_resources)

    for bitmap_resource in sanitized:
        if bitmap_resource.uri is not None:
            _set_attr(
                bitmap_resource,
                "uri",
                _truncate_data_uri(bitmap_resource.uri),
            )

        if bitmap_resource.image is not None and bitmap_resource.image.uri is not None:
            _set_attr(
                bitmap_resource.image,
                "uri",
                _truncate_data_uri(bitmap_resource.image.uri),
            )

    return sanitized


def _rounds_to_zero_pixels(bitmap_resource: BitmapResource, page_height: float) -> bool:
    """Whether docling-core would resize this bitmap to a zero-sized image."""
    bbox = bitmap_resource.rect.to_top_left_origin(
        page_height=page_height
    ).to_bounding_box()

    return round(bbox.r) - round(bbox.l) <= 0 or round(bbox.b) - round(bbox.t) <= 0


def render_page_smoke(page: SegmentedPdfPage) -> None:
    """Render the page for every cell unit, purely to smoke-test the page model.

    docling-core's SegmentedPdfPage.render_as_image() pastes each embedded
    bitmap at its rounded on-page pixel size, so any image whose placement is
    thinner than a pixel -- perfectly legal, e.g. `.24 0 0 .48 x y cm /Im Do`
    -- makes PIL raise "height and width must be > 0". That is a limitation of
    the visualizer in the dependency, not a parser regression, so it is
    tolerated here, but only for pages that really do carry such a bitmap; any
    other render failure still fails the test.
    """
    page_height = page.dimension.crop_bbox.height

    for cell_unit in [TextCellUnit.CHAR, TextCellUnit.WORD, TextCellUnit.LINE]:
        try:
            page.render_as_image(cell_unit=cell_unit)
        except ValueError as exc:
            if "height and width must be > 0" not in str(exc):
                raise
            if not any(
                bitmap_resource.image is not None
                and _rounds_to_zero_pixels(bitmap_resource, page_height)
                for bitmap_resource in page.bitmap_resources
            ):
                raise
            return


def verify_bitmap_resources(
    true_bitmap_resources: List[BitmapResource],
    pred_bitmap_resources: List[BitmapResource],
    eps: float,
) -> bool:
    # true_bitmap_resources = _sanitize_bitmap_resources(true_bitmap_resources)
    # pred_bitmap_resources = _sanitize_bitmap_resources(pred_bitmap_resources)

    return _verify_bitmap_resources(
        true_bitmap_resources=true_bitmap_resources,
        pred_bitmap_resources=pred_bitmap_resources,
        eps=eps,
    )


def _verify_bitmap_resources(
    true_bitmap_resources: List[BitmapResource],
    pred_bitmap_resources: List[BitmapResource],
    eps: float,
) -> bool:

    # compare the lengths through local variables: a bare `len(x) == len(y)` makes
    # pytest inline both collections into the assertion failure report
    len_true_bitmaps = len(true_bitmap_resources)
    len_pred_bitmaps = len(pred_bitmap_resources)
    assert len_true_bitmaps == len_pred_bitmaps, (
        "bitmap resource count mismatch: "
        f"expected {len_true_bitmaps}, got {len_pred_bitmaps}"
    )

    for i, true_bitmap_resource in enumerate(true_bitmap_resources):
        pred_bitmap_resource = pred_bitmap_resources[i]

        assert true_bitmap_resource.index == pred_bitmap_resource.index, (
            "true_bitmap_resource.ordering == pred_bitmap_resource.ordering"
        )

        true_rect = true_bitmap_resource.rect.to_polygon()
        pred_rect = pred_bitmap_resource.rect.to_polygon()

        for point_idx in range(4):
            assert abs(true_rect[point_idx][0] - pred_rect[point_idx][0]) < eps, (
                "abs(true_rect[l][0]-pred_rect[l][0])<eps"
            )
            assert abs(true_rect[point_idx][1] - pred_rect[point_idx][1]) < eps, (
                "abs(true_rect[l][1]-pred_rect[l][1])<eps"
            )

    return True


def normalize_text(text: str) -> str:
    """
    Removes multiple consecutive spaces from the given text and replaces them with a single space.

    Args:
        text (str): The input string.

    Returns:
        str: The processed string with multiple spaces replaced by a single space.
    """
    return re.sub(r"\s+", " ", text).strip()


def _describe_cell_count_mismatch(
    true_cells: List[Union[PdfTextCell, TextCell]],
    pred_cells: List[Union[PdfTextCell, TextCell]],
    filename: str,
) -> str:
    """Dump a cell-count mismatch and return a one-line summary for the assert.

    A count mismatch says nothing about *which* cells appeared or vanished, and
    the interesting cases so far have been intermittent, so the run that hits
    one is the only chance to see it. Write the full comparison next to the
    groundtruth and keep the assertion message short.
    """
    delta_path = filename + ".cells.delta.txt"

    def key(cell) -> tuple:
        rect = cell.rect
        return (round(rect.r_x0, 2), round(rect.r_y0, 2), cell.text)

    true_keys = Counter(key(cell) for cell in true_cells)
    pred_keys = Counter(key(cell) for cell in pred_cells)

    only_pred = pred_keys - true_keys
    only_true = true_keys - pred_keys

    def fonts(cells) -> Counter:
        return Counter(getattr(cell, "font_key", "?") for cell in cells)

    lines = [
        f"file      : {filename}",
        f"true cells: {len(true_cells)}",
        f"pred cells: {len(pred_cells)}",
        "",
        f"fonts (true): {dict(fonts(true_cells))}",
        f"fonts (pred): {dict(fonts(pred_cells))}",
        "",
        f"only in pred ({sum(only_pred.values())} cells, by (x0, y0, text)):",
    ]
    lines += [f"  {count} x {item!r}" for item, count in only_pred.most_common(200)]
    lines += ["", f"only in true ({sum(only_true.values())} cells):"]
    lines += [f"  {count} x {item!r}" for item, count in only_true.most_common(200)]

    first_divergence = next(
        (i for i, (a, b) in enumerate(zip(true_cells, pred_cells)) if key(a) != key(b)),
        min(len(true_cells), len(pred_cells)),
    )
    lines += ["", f"first index where the sequences differ: {first_divergence}"]

    with open(delta_path, "w", encoding="utf-8") as fw:
        fw.write("\n".join(lines) + "\n")

    duplicated = sum(count for item, count in only_pred.items() if item in true_keys)

    return (
        f"{sum(only_pred.values())} cell(s) only in pred "
        f"({duplicated} of them duplicates of an existing cell), "
        f"{sum(only_true.values())} only in true, "
        f"first divergence at index {first_divergence}; see {delta_path}"
    )


def verify_cells(
    true_cells: List[Union[PdfTextCell, TextCell]],
    pred_cells: List[Union[PdfTextCell, TextCell]],
    eps: float,
    filename: str,
) -> bool:

    len_true_cells = len(true_cells)
    len_pred_cells = len(pred_cells)
    if len_true_cells != len_pred_cells:
        summary = _describe_cell_count_mismatch(true_cells, pred_cells, filename)
        raise AssertionError(
            f"len(true_cells)==len(pred_cells) => {len_true_cells} == "
            f"{len_pred_cells} for {filename}: {summary}"
        )

    # print(f"===================== {filename}")

    for i, true_cell in enumerate(true_cells):
        pred_cell = pred_cells[i]

        assert true_cell.index == pred_cell.index, "true_cell.index == pred_cell.index"

        assert (
            # true_cell.text == pred_cell.text
            normalize_text(true_cell.text) == normalize_text(pred_cell.text)
        ), (
            f"true_cell.text == pred_cell.text => {true_cell.text} == {pred_cell.text} for {filename}"
        )
        assert (
            # true_cell.orig == pred_cell.orig
            normalize_text(true_cell.orig) == normalize_text(pred_cell.orig)
        ), (
            f"true_cell.orig == pred_cell.orig => {true_cell.orig} == {pred_cell.orig} for {filename}"
        )

        true_rect = true_cell.rect.to_polygon()
        pred_rect = pred_cell.rect.to_polygon()

        # print(f"[{i}] true-text: ", true_cell.text, " => ", true_rect)
        # print(f"[{i}] pred-text: ", pred_cell.text, " => ", pred_rect)

        # if(true_rect[0][1]-pred_rect[0][1])>eps:
        #     input("continue")

        for point_idx in range(4):
            assert abs(true_rect[point_idx][0] - pred_rect[point_idx][0]) < eps, (
                f"abs(true_rect[{point_idx}][0]-pred_rect[{point_idx}][0])<eps -> abs({true_rect[point_idx][0]}-{pred_rect[point_idx][0]})<{eps} for {filename}"
            )

            assert abs(true_rect[point_idx][1] - pred_rect[point_idx][1]) < eps, (
                f"abs(true_rect[{point_idx}][1]-pred_rect[{point_idx}][1])<eps -> abs({true_rect[point_idx][1]}-{pred_rect[point_idx][1]})<{eps} for {filename}"
            )

        if isinstance(true_cell, PdfTextCell) and isinstance(pred_cell, PdfTextCell):
            assert true_cell.font_key == pred_cell.font_key, (
                "true_cell.font_key == pred_cell.font_key"
            )
            assert true_cell.font_name == pred_cell.font_name, (
                "true_cell.font_name == pred_cell.font_name"
            )

            assert true_cell.widget == pred_cell.widget, (
                "true_cell.widget == pred_cell.widget"
            )

            assert true_cell.rgba.r == pred_cell.rgba.r, (
                "true_cell.rgba.r == pred_cell.rgba.r"
            )
            assert true_cell.rgba.g == pred_cell.rgba.g, (
                "true_cell.rgba.g == pred_cell.rgba.g"
            )
            assert true_cell.rgba.b == pred_cell.rgba.b, (
                "true_cell.rgba.b == pred_cell.rgba.b"
            )
            assert true_cell.rgba.a == pred_cell.rgba.a, (
                "true_cell.rgba.a == pred_cell.rgba.a"
            )
        else:
            return False

    return True


def verify_shapes(
    true_shapes: List[PdfShape], pred_shapes: List[PdfShape], eps: float
) -> bool:

    len_true_shapes = len(true_shapes)
    len_pred_shapes = len(pred_shapes)
    assert len_true_shapes == len_pred_shapes, (
        f"len(true_shapes)==len(pred_shapes) => {len_true_shapes} == {len_pred_shapes}"
    )

    for i, true_shape in enumerate(true_shapes):
        pred_shape = pred_shapes[i]

        assert true_shape.index == pred_shape.index, (
            "true_shape.index == pred_shape.index"
        )

        assert true_shape.parent_id == pred_shape.parent_id, (
            "true_shape.parent_id == pred_shape.parent_id"
        )

        true_points = true_shape.points
        pred_points = pred_shape.points

        len_true_points = len(true_points)
        len_pred_points = len(pred_points)
        assert len_true_points == len_pred_points, (
            f"len(true_points) == len(pred_points) => {len_true_points} == {len_pred_points}"
        )

        for point_idx, true_point in enumerate(true_points):
            assert abs(true_point[0] - pred_points[point_idx][0]) < eps, (
                "abs(true_point[0]-pred_points[l][0])<eps"
            )
            assert abs(true_point[1] - pred_points[point_idx][1]) < eps, (
                "abs(true_point[1]-pred_points[l][1])<eps"
            )

        assert true_shape.has_graphics_state == pred_shape.has_graphics_state, (
            "true_shape.has_graphics_state == pred_shape.has_graphics_state"
        )

        assert abs(true_shape.line_width - pred_shape.line_width) < eps, (
            "abs(true_shape.line_width - pred_shape.line_width) < eps"
        )
        assert abs(true_shape.miter_limit - pred_shape.miter_limit) < eps, (
            "abs(true_shape.miter_limit - pred_shape.miter_limit) < eps"
        )
        assert true_shape.line_cap == pred_shape.line_cap, (
            "true_shape.line_cap == pred_shape.line_cap"
        )
        assert true_shape.line_join == pred_shape.line_join, (
            "true_shape.line_join == pred_shape.line_join"
        )
        assert abs(true_shape.dash_phase - pred_shape.dash_phase) < eps, (
            "abs(true_shape.dash_phase - pred_shape.dash_phase) < eps"
        )
        len_true_dashes = len(true_shape.dash_array)
        len_pred_dashes = len(pred_shape.dash_array)
        assert len_true_dashes == len_pred_dashes, (
            f"len(true_shape.dash_array) == len(pred_shape.dash_array) => "
            f"{len_true_dashes} == {len_pred_dashes}"
        )
        for j, true_dash in enumerate(true_shape.dash_array):
            assert abs(true_dash - pred_shape.dash_array[j]) < eps, (
                "abs(true_dash - pred_shape.dash_array[j]) < eps"
            )
        assert abs(true_shape.flatness - pred_shape.flatness) < eps, (
            "abs(true_shape.flatness - pred_shape.flatness) < eps"
        )

        assert true_shape.rgb_stroking.r == pred_shape.rgb_stroking.r, (
            "true_shape.rgb_stroking.r == pred_shape.rgb_stroking.r"
        )
        assert true_shape.rgb_stroking.g == pred_shape.rgb_stroking.g, (
            "true_shape.rgb_stroking.g == pred_shape.rgb_stroking.g"
        )
        assert true_shape.rgb_stroking.b == pred_shape.rgb_stroking.b, (
            "true_shape.rgb_stroking.b == pred_shape.rgb_stroking.b"
        )

        assert true_shape.rgb_filling.r == pred_shape.rgb_filling.r, (
            "true_shape.rgb_filling.r == pred_shape.rgb_filling.r"
        )
        assert true_shape.rgb_filling.g == pred_shape.rgb_filling.g, (
            "true_shape.rgb_filling.g == pred_shape.rgb_filling.g"
        )
        assert true_shape.rgb_filling.b == pred_shape.rgb_filling.b, (
            "true_shape.rgb_filling.b == pred_shape.rgb_filling.b"
        )

    return True


def verify_widgets(
    true_widgets: List[PdfWidget], pred_widgets: List[PdfWidget], eps: float
) -> bool:

    len_true_widgets = len(true_widgets)
    len_pred_widgets = len(pred_widgets)
    assert len_true_widgets == len_pred_widgets, (
        f"len(true_widgets)==len(pred_widgets) => {len_true_widgets} == {len_pred_widgets}"
    )

    for i, true_widget in enumerate(true_widgets):
        pred_widget = pred_widgets[i]

        assert true_widget.index == pred_widget.index, (
            "true_widget.index == pred_widget.index"
        )

        true_rect = true_widget.rect.to_polygon()
        pred_rect = pred_widget.rect.to_polygon()

        for point_idx in range(4):
            assert abs(true_rect[point_idx][0] - pred_rect[point_idx][0]) < eps, (
                "abs(true_rect[l][0]-pred_rect[l][0])<eps"
            )
            assert abs(true_rect[point_idx][1] - pred_rect[point_idx][1]) < eps, (
                "abs(true_rect[l][1]-pred_rect[l][1])<eps"
            )

        assert true_widget.widget_text == pred_widget.widget_text, (
            "true_widget.widget_text == pred_widget.widget_text"
        )
        assert true_widget.widget_description == pred_widget.widget_description, (
            "true_widget.widget_description == pred_widget.widget_description"
        )
        assert true_widget.widget_field_name == pred_widget.widget_field_name, (
            "true_widget.widget_field_name == pred_widget.widget_field_name"
        )
        assert true_widget.widget_field_type == pred_widget.widget_field_type, (
            "true_widget.widget_field_type == pred_widget.widget_field_type"
        )

    return True


def verify_hyperlinks(
    true_hyperlinks: List[PdfHyperlink],
    pred_hyperlinks: List[PdfHyperlink],
    eps: float,
) -> bool:

    len_true_hyperlinks = len(true_hyperlinks)
    len_pred_hyperlinks = len(pred_hyperlinks)
    assert len_true_hyperlinks == len_pred_hyperlinks, (
        f"len(true_hyperlinks)==len(pred_hyperlinks) => "
        f"{len_true_hyperlinks} == {len_pred_hyperlinks}"
    )

    for i, true_hyperlink in enumerate(true_hyperlinks):
        pred_hyperlink = pred_hyperlinks[i]

        assert true_hyperlink.index == pred_hyperlink.index, (
            "true_hyperlink.index == pred_hyperlink.index"
        )

        true_rect = true_hyperlink.rect.to_polygon()
        pred_rect = pred_hyperlink.rect.to_polygon()

        for point_idx in range(4):
            assert abs(true_rect[point_idx][0] - pred_rect[point_idx][0]) < eps, (
                "abs(true_rect[l][0]-pred_rect[l][0])<eps"
            )
            assert abs(true_rect[point_idx][1] - pred_rect[point_idx][1]) < eps, (
                "abs(true_rect[l][1]-pred_rect[l][1])<eps"
            )

        assert str(true_hyperlink.uri) == str(pred_hyperlink.uri), (
            "true_hyperlink.uri == pred_hyperlink.uri"
        )

    return True


def verify_SegmentedPdfPage(
    true_page: SegmentedPdfPage,
    pred_page: SegmentedPdfPage,
    filename: str,
    cell_unit: TextCellUnit | None = None,
):

    eps = max(true_page.dimension.width / 100.0, true_page.dimension.height / 100.0)

    verify_bitmap_resources(
        true_page.bitmap_resources, pred_page.bitmap_resources, eps=eps
    )

    if cell_unit in (None, TextCellUnit.CHAR):
        verify_cells(
            true_page.char_cells, pred_page.char_cells, eps=eps, filename=filename
        )
    if cell_unit in (None, TextCellUnit.WORD):
        verify_cells(
            true_page.word_cells, pred_page.word_cells, eps=eps, filename=filename
        )
    if cell_unit in (None, TextCellUnit.LINE):
        verify_cells(
            true_page.textline_cells,
            pred_page.textline_cells,
            eps=eps,
            filename=filename,
        )

    verify_shapes(true_page.shapes, pred_page.shapes, eps=eps)
    verify_widgets(true_page.widgets, pred_page.widgets, eps=eps)
    verify_hyperlinks(true_page.hyperlinks, pred_page.hyperlinks, eps=eps)


def test_reference_documents_from_filenames(update_groundtruth: bool):

    parser = DoclingPdfParser(loglevel="fatal")
    # parser = DoclingPdfParser(loglevel="info")

    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    config = DecodeConfig(
        keep_glyphs=True, keep_qpdf_warnings=False, do_sanitization=True
    )

    # Each entry: (doc_name, page_no_str, success, error_msg)
    results: List[tuple] = []

    for pdf_doc_path in pdf_docs:
        rname = os.path.basename(pdf_doc_path)
        print(f"parsing {pdf_doc_path}")

        try:
            pdf_doc: PdfDocument = parser.load(
                path_or_stream=pdf_doc_path,
                boundary_type=PdfPageBoundaryType.CROP_BOX,  # default: CROP_BOX
                lazy=True,
                decode_config=config,
            )
            assert pdf_doc is not None
        except Exception as exc:
            results.append((rname, "N/A", "parser", False, str(exc)))
            continue

        # don't do all pages of big pdf's. pdf_doc is lazy, so decoding only the
        # pages that are actually verified means the rest is never touched.
        n_pages = pdf_doc.number_of_pages()
        requested = PARSER_PAGE_RESTRICTIONS.get(rname)

        if requested is None:
            page_numbers = list(range(1, n_pages + 1))
        else:
            page_numbers = sorted(p for p in requested if 1 <= p <= n_pages)
            for page_no in sorted(set(requested) - set(page_numbers)):
                results.append(
                    (
                        rname,
                        str(page_no),
                        "page",
                        False,
                        f"restricted page {page_no} does not exist, "
                        f"the document has {n_pages} page(s)",
                    )
                )

        for page_no in page_numbers:
            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            SPECIAL_SEPERATOR = "\t<|special_separator|>\n"

            page_failed = False

            try:
                # PdfDocument.get_page() populates this page only
                pred_page = pdf_doc.get_page(page_no)
                print(f" -> Page {page_no} has {len(pred_page.textline_cells)} cells.")

                if update_groundtruth or (not groundtruth_exists(fname)):
                    save_as_json_rounded(pred_page, fname)

                    for unit in [
                        TextCellUnit.CHAR,
                        TextCellUnit.WORD,
                        TextCellUnit.LINE,
                    ]:
                        lines = pred_page.export_to_textlines(
                            cell_unit=unit,
                            add_fontkey=True,
                            add_fontname=False,
                            add_location=True,
                            add_text_direction=False,
                        )
                        _fname = fname + f".{unit}.txt"
                        write_groundtruth_text(_fname, SPECIAL_SEPERATOR.join(lines))
                else:
                    # print(f"loading from {fname}")

                    for unit in [
                        TextCellUnit.CHAR,
                        TextCellUnit.WORD,
                        TextCellUnit.LINE,
                    ]:
                        _lines = pred_page.export_to_textlines(
                            cell_unit=unit,
                            add_fontkey=True,
                            add_fontname=False,
                            add_location=True,
                            add_text_direction=False,
                        )

                        _fname = fname + f".{unit}.txt"
                        delta_fname = fname + f".{unit}.delta.txt"

                        content = read_groundtruth_text(_fname)
                        lines = content.split(SPECIAL_SEPERATOR) if content else []

                        len_true_lines = len(lines)
                        len_pred_lines = len(_lines)

                        try:
                            assert len_true_lines == len_pred_lines, (
                                f"len(true_lines) == len(pred_lines) => {len_true_lines} == {len_pred_lines} in {unit} from {os.path.basename(_fname)} for {os.path.basename(pdf_doc_path)}"
                            )

                            # this is a bit dangerous due to rounding errors ...
                            """
                            for i, line in enumerate(lines):
                                assert (
                                    line == _lines[i]
                                ), f"line == _lines[i] => {line} == {_lines[i]} in line {i} for {_fname}"
                            """
                        except AssertionError as exc:
                            write_textline_delta(_lines, delta_fname, SPECIAL_SEPERATOR)
                            results.append(
                                (rname, str(page_no), str(unit), False, str(exc))
                            )
                            page_failed = True
                        else:
                            if os.path.exists(delta_fname):
                                os.remove(delta_fname)

                    true_page = load_segmented_page(fname)
                    for unit in [
                        TextCellUnit.CHAR,
                        TextCellUnit.WORD,
                        TextCellUnit.LINE,
                    ]:
                        try:
                            verify_SegmentedPdfPage(
                                true_page, pred_page, filename=fname, cell_unit=unit
                            )
                        except Exception as exc:
                            results.append(
                                (rname, str(page_no), str(unit), False, str(exc))
                            )
                            page_failed = True

                render_page_smoke(pred_page)

                if not page_failed:
                    results.append((rname, str(page_no), "all", True, ""))
            except Exception as exc:
                results.append((rname, str(page_no), "page", False, str(exc)))

            # print(f"unloading page: {page_no}")
            pdf_doc.unload_pages(page_range=(page_no, page_no + 1))

        _toc: PdfTableOfContents | None = pdf_doc.get_table_of_contents()
        """
        if toc is not None:
            data = toc.export_to_dict()
            print("data: \n", json.dumps(data, indent=2))
        else:
            print(f"toc: {toc}")
        """

        pdf_doc.get_meta()
        """
        if meta is not None:
            for key, val in meta.data.items():
                print(f" => {key}: {val}")
        else:
            print(f"meta: {meta}")
        """

    # --- results table ---
    from tabulate import tabulate

    def _trunc(v, n=128):
        s = str(v)
        return s if len(s) <= n else s[: n - 3] + "..."

    table = [
        (_trunc(doc), page, mode, "PASS" if ok else "FAIL", _trunc(err))
        for doc, page, mode, ok, err in results
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

    failed = [(doc, page, mode, err) for doc, page, mode, ok, err in results if not ok]
    # the failures are already printed in the table above, so assert on the count:
    # `assert not failed` would repeat every error message in the pytest report
    num_failed = len(failed)
    assert num_failed == 0, f"{num_failed} page(s) failed: " + ", ".join(
        f"{doc}@{page}[{mode}]" for doc, page, mode, _ in failed
    )


def test_load_lazy_or_eager():
    filename = "tests/data/regression/table_of_contents_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    pdf_doc_case1: PdfDocument = parser.load(path_or_stream=filename, lazy=True)

    pdf_doc_case2: PdfDocument = parser.load(path_or_stream=filename, lazy=False)

    # The lazy doc has no pages populated, since they were never iterated so far.
    # The eager doc one has the pages pre-populated before first iteration.
    assert pdf_doc_case1._pages != pdf_doc_case2._pages

    # This method triggers the pre-loading on the lazy document after creation.
    pdf_doc_case1.load_all_pages()

    # After loading the pages of the lazy doc, the two documents are equal.
    assert pdf_doc_case1._pages == pdf_doc_case2._pages


def test_load_two_distinct_docs():
    filename1 = "tests/data/regression/rotated_text_01.pdf"
    filename2 = "tests/data/regression/table_of_contents_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    pdf_doc_case1: PdfDocument = parser.load(path_or_stream=filename1, lazy=True)

    pdf_doc_case2: PdfDocument = parser.load(path_or_stream=filename2, lazy=True)

    assert pdf_doc_case1.number_of_pages() != pdf_doc_case2.number_of_pages()

    pdf_doc_case1.load_all_pages()
    pdf_doc_case2.load_all_pages()

    # The two PdfDocument instances must be non-equal. This confirms
    # that no internal state is overwritten by accident when loading more than
    # one document with the same DoclingPdfParser instance.
    assert pdf_doc_case1._pages != pdf_doc_case2._pages


def test_serialize_and_reload():
    filename = "tests/data/regression/table_of_contents_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    pdf_doc: PdfDocument = parser.load(path_or_stream=filename, lazy=True)

    # We can serialize the pages dict the following way.
    page_adapter = TypeAdapter(Dict[int, SegmentedPdfPage])

    json_pages = page_adapter.dump_json(pdf_doc._pages)
    reloaded_pages: Dict[int, SegmentedPdfPage] = page_adapter.validate_json(json_pages)

    assert reloaded_pages == pdf_doc._pages


def test_load_from_bytesio_lazy():
    """Test loading PDF from BytesIO with lazy=True."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    # Read file into BytesIO
    with open(filename, "rb") as file:
        file_content = file.read()
    bytes_io = BytesIO(file_content)

    parser = DoclingPdfParser(loglevel="fatal")

    # Load from BytesIO
    pdf_doc_bytesio = parser.load(path_or_stream=bytes_io, lazy=True)

    # Load from path for comparison
    pdf_doc_path = parser.load(path_or_stream=filename, lazy=True)

    # Both should have same number of pages
    assert pdf_doc_bytesio.number_of_pages() == pdf_doc_path.number_of_pages()

    # Load all pages and compare
    pdf_doc_bytesio.load_all_pages()
    pdf_doc_path.load_all_pages()

    # Pages should be identical
    assert pdf_doc_bytesio._pages == pdf_doc_path._pages


def test_load_from_bytesio_eager():
    """Test loading PDF from BytesIO with lazy=False."""
    filename = "tests/data/regression/rotated_text_01.pdf"

    # Read file into BytesIO
    with open(filename, "rb") as file:
        file_content = file.read()
    bytes_io = BytesIO(file_content)

    parser = DoclingPdfParser(loglevel="fatal")

    # Load from BytesIO (eager)
    pdf_doc_bytesio = parser.load(path_or_stream=bytes_io, lazy=False)

    # Load from path (eager)
    pdf_doc_path = parser.load(path_or_stream=filename, lazy=False)

    # Pages should already be loaded
    assert len(pdf_doc_bytesio._pages) > 0
    assert len(pdf_doc_path._pages) > 0

    # Pages should be identical
    assert pdf_doc_bytesio._pages == pdf_doc_path._pages


def test_list_loaded_keys_lifecycle():
    """Test document key management through load/unload lifecycle."""
    filename1 = "tests/data/regression/font_01.pdf"
    filename2 = "tests/data/regression/ligatures_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    # Initially no keys
    keys = parser.list_loaded_keys()
    assert len(keys) == 0, "Should start with no loaded documents"

    # Load first document
    pdf_doc1 = parser.load(path_or_stream=filename1, lazy=True)
    keys = parser.list_loaded_keys()
    assert len(keys) == 1, "Should have one loaded document"

    # Load second document
    pdf_doc2 = parser.load(path_or_stream=filename2, lazy=True)
    keys = parser.list_loaded_keys()
    assert len(keys) == 2, "Should have two loaded documents"

    # Unload first document
    pdf_doc1.unload()
    keys = parser.list_loaded_keys()
    assert len(keys) == 1, "Should have one loaded document after unload"

    # Unload second document
    pdf_doc2.unload()
    keys = parser.list_loaded_keys()
    assert len(keys) == 0, "Should have no loaded documents after unload all"


def test_get_page_individually():
    """Test accessing individual pages without iterating all pages."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(path_or_stream=filename, lazy=True)

    num_pages = pdf_doc.number_of_pages()
    assert num_pages > 2, "Test needs PDF with multiple pages"

    # Access page 2 directly (should not load other pages)
    page_2 = pdf_doc.get_page(2)
    assert any(k[0] == 2 for k in pdf_doc._pages)
    assert not any(k[0] == 1 for k in pdf_doc._pages)  # Page 1 should not be loaded
    assert not any(k[0] == 3 for k in pdf_doc._pages)  # Page 3 should not be loaded

    # Access page 1
    page_1 = pdf_doc.get_page(1)
    assert any(k[0] == 1 for k in pdf_doc._pages)

    # Verify pages are different
    assert page_1 != page_2


def test_unload_individual_pages():
    """Test unloading specific page ranges."""
    filename = "tests/data/regression/table_of_contents_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(path_or_stream=filename, lazy=False)

    num_pages = pdf_doc.number_of_pages()
    assert len(pdf_doc._pages) == num_pages, "All pages should be loaded (eager)"

    # Unload page 1
    pdf_doc.unload_pages(page_range=(1, 2))
    assert not any(k[0] == 1 for k in pdf_doc._pages)
    assert len(pdf_doc._pages) == num_pages - 1

    # Unload pages 2-3
    pdf_doc.unload_pages(page_range=(2, 4))
    assert not any(k[0] == 2 for k in pdf_doc._pages)
    assert not any(k[0] == 3 for k in pdf_doc._pages)


def test_boundary_types():
    """Test loading PDF with different boundary types."""
    filename = "tests/data/regression/cropbox_versus_mediabox_01.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    # Load with different boundary types
    boundary_types = [
        PdfPageBoundaryType.CROP_BOX,
        PdfPageBoundaryType.MEDIA_BOX,
    ]

    pages_by_boundary = {}

    for boundary_type in boundary_types:
        pdf_doc = parser.load(
            path_or_stream=filename, lazy=False, boundary_type=boundary_type
        )

        page = pdf_doc.get_page(1)
        pages_by_boundary[boundary_type.value] = page

        # Verify page was loaded with correct boundary
        assert pdf_doc._boundary_type == boundary_type

        pdf_doc.unload()

    # Different boundary types may produce different dimensions
    # (This test verifies the boundary type parameter is respected)
    assert len(pages_by_boundary) == 2


def test_lazy_vs_eager_pages_identical():
    """Verify that lazy and eager loading produce identical pages."""
    filename = "tests/data/regression/font_04.pdf"

    parser = DoclingPdfParser(loglevel="fatal")

    # Load lazy
    pdf_doc_lazy = parser.load(path_or_stream=filename, lazy=True)
    pdf_doc_lazy.load_all_pages()

    # Load eager
    pdf_doc_eager = parser.load(path_or_stream=filename, lazy=False)

    # Pages should be identical
    assert pdf_doc_lazy._pages == pdf_doc_eager._pages

    # Verify each page individually
    for page_no in pdf_doc_lazy._pages.keys():
        lazy_page = pdf_doc_lazy._pages[page_no]
        eager_page = pdf_doc_eager._pages[page_no]

        # Compare page content
        assert lazy_page.char_cells == eager_page.char_cells
        assert lazy_page.word_cells == eager_page.word_cells
        assert lazy_page.textline_cells == eager_page.textline_cells
        assert lazy_page.dimension == eager_page.dimension


def test_get_annotations():
    """Test accessing document annotations."""
    parser = DoclingPdfParser(loglevel="fatal")

    # Test with form_fields.pdf which has annotations
    pdf_doc = parser.load(
        path_or_stream="tests/data/regression/form_fields.pdf", lazy=True
    )

    annotations = pdf_doc.get_annotations()

    assert annotations is not None
    assert annotations.form is not None or annotations.form is None

    # form_fields.pdf has form data
    if annotations.form is not None:
        assert isinstance(annotations.form, dict)

    # Test caching
    annotations2 = pdf_doc.get_annotations()
    assert annotations is annotations2  # Should return cached instance

    pdf_doc.unload()


def verify_annotations_recursive(true_annots, pred_annots):
    """Recursively verify annotations match expected structure."""
    if isinstance(true_annots, dict):
        for k, v in true_annots.items():
            assert k in pred_annots, f"Missing key: {k}"
            verify_annotations_recursive(true_annots[k], pred_annots[k])

    elif isinstance(true_annots, list):
        assert len(true_annots) == len(pred_annots), "List length mismatch"
        for i, _ in enumerate(true_annots):
            verify_annotations_recursive(true_annots[i], pred_annots[i])

    elif isinstance(true_annots, str):
        assert true_annots == pred_annots, (
            f"String mismatch: {true_annots}!={pred_annots}"
        )

    elif isinstance(true_annots, int):
        assert true_annots == pred_annots, f"Int mismatch: {true_annots}!={pred_annots}"

    elif isinstance(true_annots, float):
        assert abs(true_annots - pred_annots) < 1e-6, (
            f"Float mismatch: {true_annots}!={pred_annots}"
        )

    elif true_annots is None:
        assert pred_annots is None, "Expected None"

    else:
        assert True  # Other types pass


def test_table_of_contents():
    """Test table of contents extraction from PDF documents."""
    parser = DoclingPdfParser(loglevel="fatal")

    # Test with a PDF that has a TOC
    pdf_doc = parser.load(
        path_or_stream="tests/data/regression/table_of_contents_01.pdf", lazy=True
    )

    # Test get_table_of_contents() method
    toc = pdf_doc.get_table_of_contents()
    assert toc is not None, "TOC should not be None for table_of_contents_01.pdf"
    assert toc.text == "<root>", "Root TOC entry should have text '<root>'"
    assert toc.children is not None, "Root TOC should have children"
    assert len(toc.children) > 0, "Root TOC should have at least one child"

    # Verify expected top-level entries exist
    top_level_titles = [child.text for child in toc.children]
    assert "Introduction" in top_level_titles, "TOC should contain 'Introduction'"
    assert "Model Architecture" in top_level_titles, (
        "TOC should contain 'Model Architecture'"
    )
    assert "Conclusion" in top_level_titles, "TOC should contain 'Conclusion'"

    # Verify nested structure exists
    model_arch_entry = next(
        (child for child in toc.children if child.text == "Model Architecture"), None
    )
    assert model_arch_entry is not None, "Should find 'Model Architecture' entry"
    assert model_arch_entry.children is not None, (
        "'Model Architecture' should have children"
    )
    assert len(model_arch_entry.children) >= 2, (
        "'Model Architecture' should have at least 2 children"
    )

    nested_titles = [child.text for child in model_arch_entry.children]
    assert "Dense Models" in nested_titles, "Should contain 'Dense Models' nested entry"
    assert "Mixture-of-Expert models" in nested_titles, (
        "Should contain 'Mixture-of-Expert models' nested entry"
    )

    # Test caching - calling again should return same instance
    toc2 = pdf_doc.get_table_of_contents()
    assert toc is toc2, "get_table_of_contents should return cached instance"

    # Test get_annotations().table_of_contents
    annotations = pdf_doc.get_annotations()
    assert annotations is not None, "Annotations should not be None"
    assert annotations.table_of_contents is not None, (
        "annotations.table_of_contents should not be None"
    )
    assert len(annotations.table_of_contents) > 0, (
        "annotations.table_of_contents should have entries"
    )

    # Verify PdfTocEntry structure
    first_entry = annotations.table_of_contents[0]
    assert first_entry.title == "Introduction", "First entry should be 'Introduction'"
    assert first_entry.level == 0, "Top-level entries should have level 0"

    # Find entry with children and verify nested structure
    model_arch_annot = next(
        (e for e in annotations.table_of_contents if e.title == "Model Architecture"),
        None,
    )
    assert model_arch_annot is not None, (
        "Should find 'Model Architecture' in annotations TOC"
    )
    assert model_arch_annot.children is not None, (
        "'Model Architecture' annotation should have children"
    )
    assert len(model_arch_annot.children) >= 2, (
        "'Model Architecture' annotation should have at least 2 children"
    )

    for child in model_arch_annot.children:
        assert child.level == 1, "Children of top-level entry should have level 1"

    pdf_doc.unload()


def test_table_of_contents_none_for_pdf_without_toc():
    """Test that TOC is None for PDFs without table of contents."""
    parser = DoclingPdfParser(loglevel="fatal")

    # font_01.pdf is a simple PDF without TOC
    pdf_doc = parser.load(path_or_stream="tests/data/regression/font_01.pdf", lazy=True)

    toc = pdf_doc.get_table_of_contents()
    assert toc is None, "TOC should be None for PDF without table of contents"

    annotations = pdf_doc.get_annotations()
    assert annotations is not None, "Annotations should not be None even without TOC"
    assert (
        annotations.table_of_contents is None or len(annotations.table_of_contents) == 0
    ), "table_of_contents should be None or empty for PDF without TOC"

    pdf_doc.unload()


def test_annotations_match_groundtruth():
    """Test that annotations match parser groundtruth."""
    parser = DoclingPdfParser(loglevel="fatal")

    # Test a few PDFs that have groundtruth with annotations
    test_files = [
        "form_fields.pdf",
        "table_of_contents_01.pdf",
    ]

    for pdf_file in test_files:
        pdf_path = f"tests/data/regression/{pdf_file}"
        groundtruth_path = PARSER_GROUNDTRUTH_DIR / f"{pdf_file}.json"

        if not os.path.exists(pdf_path) or not groundtruth_exists(groundtruth_path):
            continue

        # Load document
        pdf_doc = parser.load(path_or_stream=pdf_path, lazy=True)
        pred_annotations = pdf_doc.get_annotations()

        # Load groundtruth
        true_doc = load_groundtruth_json(groundtruth_path)
        true_annotations = true_doc["annotations"]

        # Convert PdfAnnotations to dict for comparison
        pred_dict = {
            "form": pred_annotations.form,
            "language": pred_annotations.language,
            "meta_xml": pred_annotations.meta_xml,
            "table_of_contents": (
                None
                if pred_annotations.table_of_contents is None
                else [
                    entry.model_dump(exclude_none=True)
                    for entry in pred_annotations.table_of_contents
                ]
            ),
        }

        # Verify match
        verify_annotations_recursive(true_annotations, pred_dict)

        pdf_doc.unload()


BITMAP_PDF = "tests/data/regression/annots_01.pdf"


def _make_bitmap_config() -> DecodeConfig:
    return DecodeConfig(do_sanitization=False)


def test_bitmap_no_materialization_preserves_geometry():
    """bitmap_resources count and rects match regardless of bitmap bytes."""
    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(
        path_or_stream=BITMAP_PDF, lazy=True, decode_config=_make_bitmap_config()
    )

    materialize_full = ContentConfig(include_bitmap_bytes=True)
    materialize_geo = ContentConfig(include_bitmap_bytes=False)

    page_full = pdf_doc.get_page(1, content_config=materialize_full)
    page_geo = pdf_doc.get_page(1, content_config=materialize_geo)

    assert len(page_full.bitmap_resources) == len(page_geo.bitmap_resources), (
        "bitmap count must match between full and geometry-only modes"
    )
    assert len(page_full.bitmap_resources) > 0, "test PDF must contain bitmaps"

    eps = 1e-3
    for i, (full, geo) in enumerate(
        zip(page_full.bitmap_resources, page_geo.bitmap_resources)
    ):
        full_poly = full.rect.to_polygon()
        geo_poly = geo.rect.to_polygon()
        for pt in range(4):
            assert abs(full_poly[pt][0] - geo_poly[pt][0]) < eps, (
                f"bitmap {i} point {pt} x mismatch"
            )
            assert abs(full_poly[pt][1] - geo_poly[pt][1]) < eps, (
                f"bitmap {i} point {pt} y mismatch"
            )

    pdf_doc.unload()


def test_bitmap_no_materialization_has_no_image():
    """include_bitmap_bytes=False produces placeholders with image=None."""
    from docling_core.types.doc.base import ImageRefMode

    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(
        path_or_stream=BITMAP_PDF, lazy=True, decode_config=_make_bitmap_config()
    )

    page = pdf_doc.get_page(1, content_config=ContentConfig(include_bitmap_bytes=False))
    assert len(page.bitmap_resources) > 0, "test PDF must contain bitmaps"

    for bm in page.bitmap_resources:
        assert bm.image is None, (
            "image must be None when bitmap bytes are not materialized"
        )
        assert bm.mode == ImageRefMode.PLACEHOLDER, (
            "mode must be PLACEHOLDER when bitmap bytes are not materialized"
        )


def test_bitmap_materialization_cache_false_then_true():
    """Requesting False then True for the same page returns the correct result each time."""
    from docling_core.types.doc.base import ImageRefMode

    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(
        path_or_stream=BITMAP_PDF, lazy=True, decode_config=_make_bitmap_config()
    )

    page_geo = pdf_doc.get_page(
        1, content_config=ContentConfig(include_bitmap_bytes=False)
    )
    page_full = pdf_doc.get_page(
        1, content_config=ContentConfig(include_bitmap_bytes=True)
    )

    for bm in page_geo.bitmap_resources:
        assert bm.image is None
        assert bm.mode == ImageRefMode.PLACEHOLDER

    assert any(bm.image is not None for bm in page_full.bitmap_resources), (
        "at least one bitmap should be embedded when bitmap bytes are materialized"
    )

    pdf_doc.unload()


def test_bitmap_materialization_cache_true_then_false():
    """Requesting True then False for the same page returns the correct result each time."""
    from docling_core.types.doc.base import ImageRefMode

    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(
        path_or_stream=BITMAP_PDF, lazy=True, decode_config=_make_bitmap_config()
    )

    page_full = pdf_doc.get_page(
        1, content_config=ContentConfig(include_bitmap_bytes=True)
    )
    page_geo = pdf_doc.get_page(
        1, content_config=ContentConfig(include_bitmap_bytes=False)
    )

    assert any(bm.image is not None for bm in page_full.bitmap_resources), (
        "at least one bitmap should be embedded when bitmap bytes are materialized"
    )

    for bm in page_geo.bitmap_resources:
        assert bm.image is None
        assert bm.mode == ImageRefMode.PLACEHOLDER

    pdf_doc.unload()


# --- ContentConfig redesign ---------------------------------------------

# TODO: move to the HF regression dataset, like the rest of the test corpus.
TEXT_PDF = SAMPLE_PDF


def test_word_cells_materialize_without_char_cells():
    """Word cells can be produced without surfacing character cells."""
    parser = DoclingPdfParser(loglevel="fatal")

    skip = ContentLevel.SKIP
    mat = ContentLevel.COMPUTE_AND_MATERIALIZE

    # word_cells COMPUTE_AND_MATERIALIZE, char_cells SKIP -> words present, no char cells.
    pdf_doc = parser.load(
        path_or_stream=TEXT_PDF,
        lazy=True,
        content_config=ContentConfig(
            char_cells_content_level=skip, word_cells_content_level=mat
        ),
    )
    page = pdf_doc.get_page(1)
    assert len(page.word_cells) > 0, (
        "words must be present when word_cells_content_level=COMPUTE_AND_MATERIALIZE"
    )
    assert len(page.char_cells) == 0, (
        "char cells must be absent when char_cells_content_level=SKIP"
    )

    # char_cells SKIP -> empty; word_cells SKIP -> empty.
    page2 = pdf_doc.get_page(
        1,
        content_config=ContentConfig(
            char_cells_content_level=skip, word_cells_content_level=skip
        ),
    )
    assert len(page2.char_cells) == 0
    assert len(page2.word_cells) == 0
    pdf_doc.unload()


def test_content_escalation_redecodes_page():
    """Opening without word cells, then requesting them, re-decodes the page."""
    parser = DoclingPdfParser(loglevel="fatal")
    pdf_doc = parser.load(
        path_or_stream=TEXT_PDF,
        lazy=True,
        content_config=ContentConfig(word_cells_content_level=ContentLevel.SKIP),
    )

    page_no_words = pdf_doc.get_page(1)
    assert len(page_no_words.word_cells) == 0, "words skipped at document default"

    page_words = pdf_doc.get_page(
        1,
        content_config=ContentConfig(
            word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE
        ),
    )
    assert len(page_words.word_cells) > 0, (
        "escalation must re-decode the page and surface word cells"
    )
    pdf_doc.unload()
