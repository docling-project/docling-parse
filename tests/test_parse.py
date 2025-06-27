#!/usr/bin/env python
"""
Test suite for PDF parsing functionality.

This module includes tests for both synchronous and asynchronous PDF parsing.
The async tests serve two purposes:
1. Verify that the async interface works correctly for sequential operations
2. Demonstrate thread-safety issues in the C-backend when parallel page loading is attempted

The parallel loading test is expected to fail due to lack of thread synchronization
in the underlying C++ implementation, which is the intended behavior to expose this issue.
"""
import asyncio
import glob
import os
import re
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
        print(f"parsing {pdf_doc_path}")

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

            if GENERATE or (not os.path.exists(fname)):
                pred_page.save_as_json(fname)

                for unit in [TextCellUnit.CHAR, TextCellUnit.WORD, TextCellUnit.LINE]:
                    lines = pred_page.export_to_textlines(
                        cell_unit=unit,
                        add_fontkey=True,
                        add_fontname=False,
                    )
                    _fname = fname + f".{unit}.txt"
                    with open(_fname, "w") as fw:
                        fw.write("\n".join(lines))
            else:
                # print(f"loading from {fname}")

                for unit in [TextCellUnit.CHAR, TextCellUnit.WORD, TextCellUnit.LINE]:
                    _lines = pred_page.export_to_textlines(
                        cell_unit=unit,
                        add_fontkey=True,
                        add_fontname=False,
                    )

                    _fname = fname + f".{unit}.txt"
                    with open(_fname, "r") as fr:
                        lines = fr.readlines()

                    olines = "".join(lines)
                    _olines = "\n".join(_lines)

                    assert olines == _olines, "olines==_olines"

                true_page = SegmentedPdfPage.load_from_json(fname)
                verify_SegmentedPdfPage(true_page, pred_page, filename=fname)

            img = pred_page.render_as_image(cell_unit=TextCellUnit.CHAR)
            # img.show()
            img = pred_page.render_as_image(cell_unit=TextCellUnit.WORD)
            # img.show()
            img = pred_page.render_as_image(cell_unit=TextCellUnit.LINE)
            # img.show()

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


async def test_async_parallel_page_loading():
    """Test async interface with parallel page loading to trigger C-backend thread parallelism.
    
    NOTE: This test is expected to crash due to thread-safety issues in the C-backend
    when multiple pages are loaded in parallel. The goal is to expose this problem
    and demonstrate the need for proper thread synchronization in the C++ implementation.
    """
    filename = "tests/data/cases/2206.01062.pdf"
    
    parser = DoclingPdfParser(loglevel="info")
    
    # Load document asynchronously
    pdf_doc: PdfDocument = await parser.load_async(
        path_or_stream=filename, 
        lazy=True,
        boundary_type=PdfPageBoundaryType.CROP_BOX
    )
    
    assert pdf_doc is not None
    assert pdf_doc.number_of_pages() == 9
    
    print(f"Document loaded successfully with {pdf_doc.number_of_pages()} pages")
    print("Attempting parallel page loading (expected to trigger thread-safety issues)...")
    
    # Load all pages in parallel using asyncio.gather
    page_numbers = list(range(1, pdf_doc.number_of_pages() + 1))

    # Create tasks for parallel page loading
    # MAX_TASKS = 2
    page_tasks = [
        pdf_doc.get_page_async(page_no=page_no, create_words=True, create_textlines=True)
        for page_no in page_numbers #[0:MAX_TASKS]
    ]
    
    print(f"Created {len(page_tasks)} parallel tasks for pages {page_numbers}")
    print("Executing parallel page loading (this may crash due to C-backend thread-safety issues)...")

    try:
        # Execute all page loading tasks in parallel
        pages = await asyncio.gather(*page_tasks)
        
        # If we reach here, the parallel loading succeeded (unexpected)
        print("WARNING: Parallel loading succeeded - this may indicate thread-safety has been fixed")
        
        # Verify all pages were loaded correctly
        assert len(pages) == 9 #MAX_TASKS
        
        for i, page in enumerate(pages):
            assert isinstance(page, SegmentedPdfPage)
            assert len(page.char_cells) > 0  # Should have some text content
            
            # Verify the page was cached in the document
            cached_page = pdf_doc._pages[i + 1]
            assert cached_page == page
        
        print("All pages loaded and verified successfully")
        
        # Test async iteration as well
        async_pages = []
        async for page_no, page in pdf_doc.iterate_pages_async():
            async_pages.append((page_no, page))
        
        assert len(async_pages) == 9
        
        # Verify async iteration returns the same pages
        for i, (page_no, page) in enumerate(async_pages):
            assert page_no == i + 1
            assert page == pages[i]
            
        print("Async iteration test completed successfully")
        
    except Exception as e:
        print(f"Parallel loading failed as expected: {type(e).__name__}: {e}")
        print("This failure indicates thread-safety issues in the C-backend")
        print("The C++ implementation needs proper synchronization for concurrent page parsing")
        # Re-raise to make the test fail
        raise

def test_async_parallel_page_loading_sync_wrapper():
    """Synchronous wrapper for the async test to integrate with pytest.
    
    This test is expected to fail due to thread-safety issues in the C-backend
    when multiple pages are parsed concurrently. The failure demonstrates the
    need for proper thread synchronization in the underlying C++ implementation.
    """
    try:
        asyncio.run(test_async_parallel_page_loading())
    except Exception as e:
        print(f"\nTest failed as expected: {type(e).__name__}: {e}")
        print("This confirms thread-safety issues in the C-backend")
        # Re-raise to make the test fail
        raise


async def test_async_sequential_page_loading():
    """Test async interface with sequential page loading to verify async functionality works correctly."""
    filename = "tests/data/cases/2206.01062.pdf"
    
    parser = DoclingPdfParser(loglevel="info")
    
    # Load document asynchronously
    pdf_doc: PdfDocument = await parser.load_async(
        path_or_stream=filename, 
        lazy=True,
        boundary_type=PdfPageBoundaryType.CROP_BOX
    )
    
    assert pdf_doc is not None
    assert pdf_doc.number_of_pages() == 9
    
    # Load pages sequentially using async
    pages = []
    for page_no in range(1, pdf_doc.number_of_pages() + 1):
        page = await pdf_doc.get_page_async(page_no=page_no, create_words=True, create_textlines=True)
        pages.append(page)
    
    # Verify all pages were loaded correctly
    assert len(pages) == 9
    
    for i, page in enumerate(pages):
        assert isinstance(page, SegmentedPdfPage)
        assert len(page.char_cells) > 0  # Should have some text content
        
        # Verify the page was cached in the document
        cached_page = pdf_doc._pages[i + 1]
        assert cached_page == page
    
    # Test async iteration
    async_pages = []
    async for page_no, page in pdf_doc.iterate_pages_async():
        async_pages.append((page_no, page))
    
    assert len(async_pages) == 9
    
    # Verify async iteration returns the same pages
    for i, (page_no, page) in enumerate(async_pages):
        assert page_no == i + 1
        assert page == pages[i]


def test_async_sequential_page_loading_sync_wrapper():
    """Synchronous wrapper for the sequential async test."""
    asyncio.run(test_async_sequential_page_loading())
