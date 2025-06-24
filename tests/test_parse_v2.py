#!/usr/bin/env python

GENERATE = False
VISUALISE_TESTS = False

GROUNDTRUTH_FOLDER = "tests/data/groundtruth/"
REGRESSION_FOLDER = "tests/data/regression/*.pdf"

MAX_PAGES = 2

import glob
import io
import json
import os

from docling_parse.pdf_parsers import pdf_parser_v2  # type: ignore[import]

# from docling_parse.utils import create_pil_image_of_page_v2
# from docling_core.types.doc.page import TextCellUnit, ParsedPdfDocument


def verify_annots(true_annots, pred_annots):

    if isinstance(true_annots, dict):
        for k, v in true_annots.items():
            verify_annots(true_annots[k], pred_annots[k])

    elif isinstance(true_annots, list):
        for i, _ in enumerate(true_annots):
            verify_annots(true_annots[i], pred_annots[i])

    elif isinstance(true_annots, str):
        assert (
            true_annots == pred_annots
        ), f"true_annots!=pred_annots -> {true_annots}!={pred_annots}"

    elif isinstance(true_annots, int):
        assert (
            true_annots == pred_annots
        ), f"true_annots!=pred_annots -> {true_annots}!={pred_annots}"

    elif isinstance(true_annots, float):
        assert (
            true_annots == pred_annots
        ), f"true_annots!=pred_annots -> {round(true_annots)}!={round(pred_annots)}"

    else:
        assert True


def verify_dimensions(true_dimension, pred_dimension):

    for _ in ["width", "height", "angle"]:
        assert abs(true_dimension[_] - pred_dimension[_]) < 1.0e-2, f"_ are different"

    for i in range(0, 3):
        assert (
            abs(true_dimension["bbox"][i] - pred_dimension["bbox"][i]) < 1.0e-2
        ), "bbox are different"

    return True


def verify_cells(true_cells, true_header, pred_cells, pred_header, pdf_doc):

    assert true_header == pred_header, f"true_header==pred_header for {pdf_doc}"

    assert len(pred_cells) == len(
        true_cells
    ), f"len(pred_cells)==len(true_cells) for {pdf_doc}"

    for row_i, row_j in zip(pred_cells, true_cells):
        assert len(row_i) == len(
            row_j
        ), f"len(pred_cells)==len(true_cells) for {pdf_doc}"

        for i, _ in enumerate(pred_header):

            if isinstance(row_i[i], float):
                assert (
                    abs(row_i[i] - row_j[i]) <= 1.0e-2
                ), f"{_}: {row_i[i]}=={row_j[i]} for {pdf_doc} with \npred: {row_i} and \ntrue: {row_j}"
            else:
                assert row_i[i] == row_j[i], f"{_}: {row_i[i]}=={row_j[i]}"

    return True


def verify_images(true_images, true_header, pred_images, pred_header, pdf_doc):

    assert true_header == pred_header, "true_header==pred_header"

    assert len(pred_images) == len(true_images), "len(pred_images)==len(true_images)"

    for row_i, row_j in zip(pred_images, true_images):
        assert len(row_i) == len(row_j), "len(pred_images)==len(true_images)"

        for i, _ in enumerate(pred_header):

            if isinstance(row_i[i], float):
                assert abs(row_i[i] - row_j[i]) <= 1.0e-2, "row_i[i]==row_j[i]"
            else:
                assert row_i[i] == row_j[i], "row_i[i]==row_j[i]"

    return True


def verify_lines(true_lines, pred_lines):

    assert len(true_lines) == len(pred_lines), "len(true_lines)==len(pred_lines)"

    for tline, pline in zip(true_lines, pred_lines):

        for key, true_val in tline.items():

            pred_val = pline[key]
            assert len(true_val) == len(pred_val), "len(true_val)==len(pred_val)"

            for tv, pv in zip(true_val, pred_val):
                if isinstance(tv, float):
                    assert abs(tv - pv) <= 1.0e-2, "row_i[i]==row_j[i]"
                else:
                    assert tv == pv, "row_i[i]==row_j[i]"

    return True


def verify_reference_output(true_doc, pred_doc, pdf_doc):

    num_true_pages = len(true_doc["pages"])
    num_pred_pages = len(pred_doc["pages"])

    message = f'len(pred_doc["pages"])!=len(true_doc["pages"]) => {num_pred_pages}!={num_true_pages}'
    assert num_pred_pages == num_true_pages, message

    verify_annots(true_doc["annotations"], pred_doc["annotations"])

    for pred_page, true_page in zip(pred_doc["pages"], true_doc["pages"]):
        # print(pred_page.keys())
        # print(pred_page["original"].keys())
        # print(pred_page["original"]["lines"])

        for _ in ["original", "sanitized"]:
            true_dimension = pred_page[_]["dimension"]
            pred_dimension = true_page[_]["dimension"]

            assert verify_dimensions(
                true_dimension, pred_dimension
            ), f"verify {_} dimension"

            pred_cells = pred_page[_]["cells"]["data"]
            true_cells = true_page[_]["cells"]["data"]

            pred_header = pred_page[_]["cells"]["header"]
            true_header = true_page[_]["cells"]["header"]

            assert verify_cells(
                true_cells, true_header, pred_cells, pred_header, pdf_doc
            ), f"verify {_} cells for {pdf_doc}"

            pred_images = pred_page[_]["images"]["data"]
            true_images = true_page[_]["images"]["data"]

            pred_header = pred_page[_]["images"]["header"]
            true_header = true_page[_]["images"]["header"]

            assert verify_images(
                true_images, true_header, pred_images, pred_header, pdf_doc
            ), f"verify {_} images for {pdf_doc}"

            pred_lines = pred_page[_]["lines"]
            true_lines = true_page[_]["lines"]

            verify_lines(true_lines, pred_lines)

    return True


def test_reference_documents_from_filenames_with_keys():

    parser = pdf_parser_v2(level="fatal")
    # parser = docling_parse.pdf_parser_v2("fatal")
    # parser.set_loglevel_with_label("fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:
        doc_key = f"key={pdf_doc}"
        # print("testing: ", pdf_doc)

        # print(" => load_document ...")
        parser.load_document(doc_key, pdf_doc)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        pred_doc = parser.parse_pdf_from_key(doc_key)
        # pred_doc = parser.parse_pdf_from_key_on_page(doc_key, 1)

        num_pages = parser.number_of_pages(doc_key)
        if num_pages > 10:  # skip large files
            parser.unload_document(doc_key)
            continue

        # print(" => unload_document ...")
        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"

        rname = os.path.basename(pdf_doc)
        fname = os.path.join(GROUNDTRUTH_FOLDER, rname + ".v2.json")

        if GENERATE or (not os.path.exists(fname)):
            with open(fname, "w") as fw:
                fw.write(json.dumps(pred_doc, indent=2))

            assert True
        else:
            with open(fname, "r") as fr:
                true_doc = json.load(fr)

            assert verify_reference_output(
                true_doc, pred_doc, pdf_doc
            ), "verify_reference_output(true_doc, pred_doc)"


def test_reference_documents_from_filenames_with_keys_page_by_page():

    parser = pdf_parser_v2(level="fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:
        # print(f"testing {pdf_doc}")

        doc_key = f"key={pdf_doc}"
        # print("testing: ", pdf_doc)

        # print(" => load_document ...")
        parser.load_document(doc_key, pdf_doc)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        num_pages = parser.number_of_pages(doc_key)

        for page in range(0, min(MAX_PAGES, num_pages)):

            rname = os.path.basename(pdf_doc)
            fname = os.path.join(GROUNDTRUTH_FOLDER, f"{rname}.v2.p={page}.json")

            pred_doc = parser.parse_pdf_from_key_on_page(doc_key, page)

            if GENERATE or (not os.path.exists(fname)):
                with open(fname, "w") as fw:
                    fw.write(json.dumps(pred_doc, indent=2))

                assert True
            else:
                with open(fname, "r") as fr:
                    true_doc = json.load(fr)

                assert verify_reference_output(
                    true_doc, pred_doc, pdf_doc
                ), "verify_reference_output(true_doc, pred_doc)"

        # print(" => unload_document ...")
        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"


def test_reference_documents_from_bytesio_with_keys():

    parser = pdf_parser_v2(level="fatal")
    # parser = docling_parse.pdf_parser_v2("fatal")
    # parser.set_loglevel_with_label("fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:

        # Open the file in binary mode and read its contents
        with open(pdf_doc, "rb") as file:
            file_content = file.read()

        # Create a BytesIO object and write the file contents to it
        bytes_io = io.BytesIO(file_content)

        doc_key = f"key={pdf_doc}"
        parser.load_document_from_bytesio(doc_key, bytes_io)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        pred_doc = parser.parse_pdf_from_key(doc_key)

        num_pages = parser.number_of_pages(doc_key)
        if num_pages > 10:  # skip large files
            parser.unload_document(doc_key)
            continue

        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"

        rname = os.path.basename(pdf_doc)
        fname = os.path.join(GROUNDTRUTH_FOLDER, rname + ".v2.json")

        if GENERATE or (not os.path.exists(fname)):
            with open(fname, "w") as fw:
                fw.write(json.dumps(pred_doc, indent=2))

            assert True
        else:
            with open(fname, "r") as fr:
                true_doc = json.load(fr)

            assert verify_reference_output(
                true_doc, pred_doc, pdf_doc
            ), "verify_reference_output(true_doc, pred_doc)"


def test_reference_documents_from_bytesio_with_keys_page_by_page():

    parser = pdf_parser_v2(level="fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:

        # Open the file in binary mode and read its contents
        with open(pdf_doc, "rb") as file:
            file_content = file.read()

        # Create a BytesIO object and write the file contents to it
        bytes_io = io.BytesIO(file_content)

        doc_key = f"key={pdf_doc}"
        parser.load_document_from_bytesio(doc_key, bytes_io)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        num_pages = parser.number_of_pages(doc_key)

        # for page in range(0, num_pages):
        for page in range(0, min(MAX_PAGES, num_pages)):

            rname = os.path.basename(pdf_doc)
            fname = os.path.join(GROUNDTRUTH_FOLDER, f"{rname}.v2.p={page}.json")

            pred_doc = parser.parse_pdf_from_key_on_page(doc_key, page)

            if GENERATE or (not os.path.exists(fname)):
                with open(fname, "w") as fw:
                    fw.write(json.dumps(pred_doc, indent=2))

                assert True
            else:
                with open(fname, "r") as fr:
                    true_doc = json.load(fr)

                assert verify_reference_output(
                    true_doc, pred_doc, pdf_doc
                ), "verify_reference_output(true_doc, pred_doc)"

        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"


def test_visualisation():

    parser = pdf_parser_v2(level="fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:

        doc_key = f"key={pdf_doc}"

        parser.load_document(doc_key, pdf_doc)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        num_pages = parser.number_of_pages(doc_key)

        for page in range(0, min(MAX_PAGES, num_pages)):

            rname = os.path.basename(pdf_doc)
            fname = os.path.join(GROUNDTRUTH_FOLDER, f"{rname}.v2.p={page}.json")

            parser.parse_pdf_from_key_on_page(doc_key, page)

            # img = create_pil_image_of_page_v2(pred_doc["pages"][0])
            # if VISUALISE_TESTS:
            #     img.show()

        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"


def test_sanitize_cells_in_bbox():

    parser = pdf_parser_v2(level="fatal")

    pdf_docs = glob.glob(REGRESSION_FOLDER)
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    for pdf_doc in pdf_docs:

        doc_key = f"key={pdf_doc}"

        parser.load_document(doc_key, pdf_doc)

        keys = parser.list_loaded_keys()
        assert len(keys) == 1, "len(keys)==1"

        num_pages = parser.number_of_pages(doc_key)

        for page in range(0, min(MAX_PAGES, num_pages)):

            rname = os.path.basename(pdf_doc)
            fname = os.path.join(GROUNDTRUTH_FOLDER, f"{rname}.v2.p={page}.json")

            doc = parser.parse_pdf_from_key_on_page(doc_key, page)

            sanitized_cells = doc["pages"][0]["sanitized"]["cells"]

            for sanitized_cell in sanitized_cells["data"]:
                # print("=============================")
                # print(sanitized_cell)

                bbox = [
                    sanitized_cell[sanitized_cells["header"].index("x0")],
                    sanitized_cell[sanitized_cells["header"].index("y0")],
                    sanitized_cell[sanitized_cells["header"].index("x1")],
                    sanitized_cell[sanitized_cells["header"].index("y1")],
                ]

                out = parser.sanitize_cells_in_bbox(
                    page=doc["pages"][0],
                    bbox=bbox,
                    cell_overlap=0.9,
                    horizontal_cell_tolerance=1.0,
                    enforce_same_font=False,
                    space_width_factor_for_merge=1.5,
                    space_width_factor_for_merge_with_space=0.33,
                )
                # print(out)

            if VISUALISE_TESTS:
                img.show()

        parser.unload_document(doc_key)

        keys = parser.list_loaded_keys()
        assert len(keys) == 0, "len(keys)==0"
