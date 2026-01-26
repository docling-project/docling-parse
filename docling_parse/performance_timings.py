#!/usr/bin/env python
import glob
from datetime import datetime
from pathlib import Path

from docling_core.types.doc.page import PdfPageBoundaryType
from pydantic import BaseModel

from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument
from docling_parse.pdf_parsers import pdf_parser  # type: ignore[import]


class DocumentTiming(BaseModel):

    filename: str
    num_pages: int = -1
    total_time: float = 0.0
    load_time: float = 0.0
    iterate_time: float = 0.0
    page_times: list[float] = []


def test_performance_pdf_parse(
    ifolder: Path, lazy: bool = True, loglevel: str = "fatal"
) -> list[DocumentTiming]:

    pdf_docs = sorted(glob.glob(str(ifolder)))

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    timings = []

    for pdf_doc_path in pdf_docs:
        print(f"parsing {pdf_doc_path}")

        if not str(pdf_doc_path).endswith(".pdf"):
            continue

        doc_key = str(pdf_doc_path)

        timing = DocumentTiming(filename=pdf_doc_path)

        start_0_time = datetime.now()

        parser = pdf_parser(level="fatal")
        parser.load_document(doc_key, str(pdf_doc_path))

        parser.parse_pdf_from_key(doc_key)

        parser.unload_document(doc_key)

        elapsed = datetime.now() - start_0_time
        timing.total_time = elapsed.total_seconds()

        timings.append(timing)

    return timings


def test_performance_pdf_parse_py(
    ifolder: Path,
    keep_chars: bool = True,
    keep_lines: bool = True,
    keep_bitmaps: bool = True,
    create_words: bool = True,
    create_textlines: bool = True,
    enforce_same_font: bool = True,
    lazy: bool = True,
    loglevel: str = "error",
) -> list[DocumentTiming]:

    pdf_docs = sorted(glob.glob(str(ifolder)))

    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    timings = []

    for pdf_doc_path in pdf_docs:
        print(f"parsing {pdf_doc_path}")

        if not str(pdf_doc_path).endswith(".pdf"):
            continue

        timing = DocumentTiming(filename=pdf_doc_path)

        start_0_time = datetime.now()

        parser = DoclingPdfParser(loglevel=loglevel)

        pdf_doc: PdfDocument = parser.load(
            path_or_stream=pdf_doc_path,
            boundary_type=PdfPageBoundaryType.CROP_BOX,  # default: CROP_BOX
            lazy=lazy,
        )  # default: True
        assert pdf_doc is not None

        elapsed = datetime.now() - start_0_time
        timing.load_time = elapsed.total_seconds()

        start_1_time = datetime.now()
        timing.num_pages = 0
        for page_no, pred_page in pdf_doc.iterate_pages(
            keep_chars=keep_chars,
            keep_lines=keep_lines,
            keep_bitmaps=keep_bitmaps,
            create_words=create_words,
            create_textlines=create_textlines,
            enforce_same_font=enforce_same_font,
        ):
            timing.num_pages += 1

            elapsed = datetime.now() - start_1_time
            timing.page_times.append(elapsed.total_seconds())

            start_1_time = datetime.now()

            break

        elapsed = datetime.now() - start_0_time
        timing.total_time = elapsed.total_seconds()

        timing.iterate_time = timing.total_time - timing.load_time
        timings.append(timing)

    return timings


def main():

    ifolder = Path("./timings/*.pdf")

    timings = test_performance_pdf_parse(ifolder=ifolder)

    for _ in timings:
        print(_)

    # Optimized
    timings = test_performance_pdf_parse_py(
        ifolder=ifolder,
        keep_chars=False,
        keep_lines=False,
        keep_bitmaps=False,
        create_words=True,
        create_textlines=True,
        enforce_same_font=False,
    )

    for _ in timings:
        print(_)

    # Optoimized for time, not memory
    timings = test_performance_pdf_parse_py(
        ifolder=ifolder,
        keep_chars=True,
        keep_lines=True,
        keep_bitmaps=True,
        create_words=True,
        create_textlines=True,
        enforce_same_font=True,
    )

    for _ in timings:
        print(_)

    # Original ...
    timings = test_performance_pdf_parse_py(
        ifolder=ifolder,
        keep_chars=True,
        keep_lines=True,
        keep_bitmaps=True,
        create_words=False,
        create_textlines=False,
        enforce_same_font=True,
    )

    for _ in timings:
        print(_)


if __name__ == "__main__":
    main()
