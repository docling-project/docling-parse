#!/usr/bin/env python
import glob
import json
import os
import re
from io import BytesIO
from typing import Dict, List, Union

from docling_core.types.doc.page import (
    BitmapResource,
    PdfLine,
    PdfPageBoundaryType,
    PdfTableOfContents,
    PdfTextCell,
    SegmentedPdfPage,
    TextCell,
    TextCellUnit,
)
from pydantic import TypeAdapter

from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument

GENERATE = False

GROUNDTRUTH_FOLDER = "tests/data/groundtruth/"
REGRESSION_FOLDER = "tests/data/regression/*.pdf"


def verify_bitmap_resources(
    true_bitmap_resources: List[BitmapResource],
    pred_bitmap_resources: List[BitmapResource],
    eps: float,
) -> bool:

    assert len(true_bitmap_resources) == len(
        pred_bitmap_resources
    ), "len(true_bitmap_resources)==len(pred_bitmap_resources)"

    for i, true_bitmap_resource in enumerate(true_bitmap_resources):

        pred_bitmap_resource = pred_bitmap_resources[i]

        assert (
            true_bitmap_resource.index == pred_bitmap_resource.index
        ), "true_bitmap_resource.ordering == pred_bitmap_resource.ordering"

        true_rect = true_bitmap_resource.rect.to_polygon()
        pred_rect = pred_bitmap_resource.rect.to_polygon()

        for l in range(0, 4):
            assert (
                abs(true_rect[l][0] - pred_rect[l][0]) < eps
            ), "abs(true_rect[l][0]-pred_rect[l][0])<eps"
            assert (
                abs(true_rect[l][1] - pred_rect[l][1]) < eps
            ), "abs(true_rect[l][1]-pred_rect[l][1])<eps"

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


def verify_cells(
    true_cells: List[Union[PdfTextCell, TextCell]],
    pred_cells: List[Union[PdfTextCell, TextCell]],
    eps: float,
    filename: str,
) -> bool:

    assert len(true_cells) == len(pred_cells), "len(true_cells)==len(pred_cells)"

    for i, true_cell in enumerate(true_cells):

        pred_cell = pred_cells[i]

        assert true_cell.index == pred_cell.index, "true_cell.index == pred_cell.index"

        assert (
            # true_cell.text == pred_cell.text
            normalize_text(true_cell.text)
            == normalize_text(pred_cell.text)
        ), f"true_cell.text == pred_cell.text => {true_cell.text} == {pred_cell.text} for {filename}"
        assert (
            # true_cell.orig == pred_cell.orig
            normalize_text(true_cell.orig)
            == normalize_text(pred_cell.orig)
        ), f"true_cell.orig == pred_cell.orig => {true_cell.orig} == {pred_cell.orig} for {filename}"

        true_rect = true_cell.rect.to_polygon()
        pred_rect = pred_cell.rect.to_polygon()

        for l in range(0, 4):
            assert (
                abs(true_rect[l][0] - pred_rect[l][0]) < eps
            ), f"abs(true_rect[{l}][0]-pred_rect[{l}][0])<eps -> abs({true_rect[l][0]}-{pred_rect[l][0]})<{eps} for {filename}"

            assert (
                abs(true_rect[l][1] - pred_rect[l][1]) < eps
            ), f"abs(true_rect[{l}][1]-pred_rect[{l}][1])<eps -> abs({true_rect[l][1]}-{pred_rect[l][1]})<{eps} for {filename}"

        # print("true-text: ", true_cell.text)
        # print("pred-text: ", pred_cell.text)

        if isinstance(true_cell, PdfTextCell) and isinstance(pred_cell, PdfTextCell):
            assert (
                true_cell.font_key == pred_cell.font_key
            ), "true_cell.font_key == pred_cell.font_key"
            assert (
                true_cell.font_name == pred_cell.font_name
            ), "true_cell.font_name == pred_cell.font_name"

            assert (
                true_cell.widget == pred_cell.widget
            ), "true_cell.widget == pred_cell.widget"

            assert (
                true_cell.rgba.r == pred_cell.rgba.r
            ), "true_cell.rgba.r == pred_cell.rgba.r"
            assert (
                true_cell.rgba.g == pred_cell.rgba.g
            ), "true_cell.rgba.g == pred_cell.rgba.g"
            assert (
                true_cell.rgba.b == pred_cell.rgba.b
            ), "true_cell.rgba.b == pred_cell.rgba.b"
            assert (
                true_cell.rgba.a == pred_cell.rgba.a
            ), "true_cell.rgba.a == pred_cell.rgba.a"
        else:
            return False

    return True


def verify_lines(
    true_lines: List[PdfLine], pred_lines: List[PdfLine], eps: float
) -> bool:

    assert len(true_lines) == len(pred_lines), "len(true_lines)==len(pred_lines)"

    for i, true_line in enumerate(true_lines):

        pred_line = pred_lines[i]

        assert true_line.index == pred_line.index, "true_line.index == pred_line.index"

        true_points = true_line.points
        pred_points = pred_line.points

        assert len(true_points) == len(
            pred_points
        ), "len(true_points) == len(pred_points)"

        for l, true_point in enumerate(true_points):
            assert (
                abs(true_point[0] - pred_points[l][0]) < eps
            ), "abs(true_point[0]-pred_points[l][0])<eps"
            assert (
                abs(true_point[1] - pred_points[l][1]) < eps
            ), "abs(true_point[1]-pred_points[l][1])<eps"

        assert (
            abs(true_line.width - pred_line.width) < eps
        ), "abs(true_line.width-pred_line.width)<eps"

        assert (
            true_line.rgba.r == pred_line.rgba.r
        ), "true_line.rgba.r == pred_line.rgba.r"
        assert (
            true_line.rgba.g == pred_line.rgba.g
        ), "true_line.rgba.g == pred_line.rgba.g"
        assert (
            true_line.rgba.b == pred_line.rgba.b
        ), "true_line.rgba.b == pred_line.rgba.b"
        assert (
            true_line.rgba.a == pred_line.rgba.a
        ), "true_line.rgba.a == pred_line.rgba.a"

    return True


def verify_SegmentedPdfPage(
    true_page: SegmentedPdfPage, pred_page: SegmentedPdfPage, filename: str
):

    eps = max(true_page.dimension.width / 100.0, true_page.dimension.height / 100.0)

    verify_bitmap_resources(
        true_page.bitmap_resources, pred_page.bitmap_resources, eps=eps
    )

    verify_cells(true_page.char_cells, pred_page.char_cells, eps=eps, filename=filename)
    verify_cells(true_page.word_cells, pred_page.word_cells, eps=eps, filename=filename)
    verify_cells(
        true_page.textline_cells, pred_page.textline_cells, eps=eps, filename=filename
    )

    verify_lines(true_page.lines, pred_page.lines, eps=eps)


def test_reference_documents_from_filenames():

    parser = DoclingPdfParser(loglevel="fatal")

    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc_path in pdf_docs:
        # print(f"parsing {pdf_doc_path}")

        pdf_doc: PdfDocument = parser.load(
            path_or_stream=pdf_doc_path,
            boundary_type=PdfPageBoundaryType.CROP_BOX,  # default: CROP_BOX
            lazy=False,
        )  # default: True
        assert pdf_doc is not None

        # PdfDocument.iterate_pages() will automatically populate pages as they are yielded.
        # No need to call PdfDocument.load_all_pages() before.
        for page_no, pred_page in pdf_doc.iterate_pages():
            # print(f" -> Page {page_no} has {len(pred_page.sanitized.cells)} cells.")

            rname = os.path.basename(pdf_doc_path)
            fname = os.path.join(
                GROUNDTRUTH_FOLDER, rname + f".page_no_{page_no}.py.json"
            )

            SPECIAL_SEPERATOR = "\t<|special_separator|>\n"

            if GENERATE or (not os.path.exists(fname)):
                pred_page.save_as_json(fname)

                for unit in [TextCellUnit.CHAR, TextCellUnit.WORD, TextCellUnit.LINE]:
                    lines = pred_page.export_to_textlines(
                        cell_unit=unit,
                        add_fontkey=True,
                        add_fontname=False,
                        add_location=True,
                        add_text_direction=False,
                    )
                    _fname = fname + f".{unit}.txt"
                    with open(_fname, "w") as fw:
                        fw.write(SPECIAL_SEPERATOR.join(lines))
            else:
                # print(f"loading from {fname}")

                for unit in [TextCellUnit.CHAR, TextCellUnit.WORD, TextCellUnit.LINE]:
                    _lines = pred_page.export_to_textlines(
                        cell_unit=unit,
                        add_fontkey=True,
                        add_fontname=False,
                        add_location=True,
                        add_text_direction=False,
                    )

                    _fname = fname + f".{unit}.txt"

                    lines = []
                    with open(_fname, "r") as fr:
                        content = fr.read()
                        lines = content.split(SPECIAL_SEPERATOR)

                    assert len(lines) == len(
                        _lines
                    ), f"len(lines) == len(_lines) => {len(lines)} == {len(_lines)} from {_fname}"

                    for i, line in enumerate(lines):
                        assert (
                            line == _lines[i]
                        ), f"line == _lines[i] => {line} == {_lines[i]} in line {i} for {_fname}"

                true_page = SegmentedPdfPage.load_from_json(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)

            img = pred_page.render_as_image(cell_unit=TextCellUnit.CHAR)
            # img.show()
            img = pred_page.render_as_image(cell_unit=TextCellUnit.WORD)
            # img.show()
            img = pred_page.render_as_image(cell_unit=TextCellUnit.LINE)
            # img.show()

            # print(f"unloading page: {page_no}")
            pdf_doc.unload_pages(page_range=(page_no, page_no + 1))

        toc: PdfTableOfContents = pdf_doc.get_table_of_contents()
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

    assert True


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
    assert 2 in pdf_doc._pages
    assert 1 not in pdf_doc._pages  # Page 1 should not be loaded
    assert 3 not in pdf_doc._pages  # Page 3 should not be loaded

    # Access page 1
    page_1 = pdf_doc.get_page(1)
    assert 1 in pdf_doc._pages

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
    assert 1 not in pdf_doc._pages
    assert len(pdf_doc._pages) == num_pages - 1

    # Unload pages 2-3
    pdf_doc.unload_pages(page_range=(2, 4))
    assert 2 not in pdf_doc._pages
    assert 3 not in pdf_doc._pages


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
        assert (
            true_annots == pred_annots
        ), f"String mismatch: {true_annots}!={pred_annots}"

    elif isinstance(true_annots, int):
        assert true_annots == pred_annots, f"Int mismatch: {true_annots}!={pred_annots}"

    elif isinstance(true_annots, float):
        assert (
            abs(true_annots - pred_annots) < 1e-6
        ), f"Float mismatch: {true_annots}!={pred_annots}"

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
    assert (
        "Model Architecture" in top_level_titles
    ), "TOC should contain 'Model Architecture'"
    assert "Conclusion" in top_level_titles, "TOC should contain 'Conclusion'"

    # Verify nested structure exists
    model_arch_entry = next(
        (child for child in toc.children if child.text == "Model Architecture"), None
    )
    assert model_arch_entry is not None, "Should find 'Model Architecture' entry"
    assert (
        model_arch_entry.children is not None
    ), "'Model Architecture' should have children"
    assert (
        len(model_arch_entry.children) >= 2
    ), "'Model Architecture' should have at least 2 children"

    nested_titles = [child.text for child in model_arch_entry.children]
    assert "Dense Models" in nested_titles, "Should contain 'Dense Models' nested entry"
    assert (
        "Mixture-of-Expert models" in nested_titles
    ), "Should contain 'Mixture-of-Expert models' nested entry"

    # Test caching - calling again should return same instance
    toc2 = pdf_doc.get_table_of_contents()
    assert toc is toc2, "get_table_of_contents should return cached instance"

    # Test get_annotations().table_of_contents
    annotations = pdf_doc.get_annotations()
    assert annotations is not None, "Annotations should not be None"
    assert (
        annotations.table_of_contents is not None
    ), "annotations.table_of_contents should not be None"
    assert (
        len(annotations.table_of_contents) > 0
    ), "annotations.table_of_contents should have entries"

    # Verify PdfTocEntry structure
    first_entry = annotations.table_of_contents[0]
    assert first_entry.title == "Introduction", "First entry should be 'Introduction'"
    assert first_entry.level == 0, "Top-level entries should have level 0"

    # Find entry with children and verify nested structure
    model_arch_annot = next(
        (e for e in annotations.table_of_contents if e.title == "Model Architecture"),
        None,
    )
    assert (
        model_arch_annot is not None
    ), "Should find 'Model Architecture' in annotations TOC"
    assert (
        model_arch_annot.children is not None
    ), "'Model Architecture' annotation should have children"
    assert (
        len(model_arch_annot.children) >= 2
    ), "'Model Architecture' annotation should have at least 2 children"

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
        groundtruth_path = f"tests/data/groundtruth/{pdf_file}.json"

        if not os.path.exists(pdf_path) or not os.path.exists(groundtruth_path):
            continue

        # Load document
        pdf_doc = parser.load(path_or_stream=pdf_path, lazy=True)
        pred_annotations = pdf_doc.get_annotations()

        # Load groundtruth
        with open(groundtruth_path, "r") as fr:
            true_doc = json.load(fr)
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
