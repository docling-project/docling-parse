# SPDX-FileCopyrightText: The Docling Contributors
# SPDX-License-Identifier: MIT

"""ClassMap attributes in an unmodified document from the regression corpus."""

from pathlib import Path

from docling_parse.pdf_parser import DoclingPdfParser
from tests.constants import REGRESSION_DIR


def test_adobe_pdf_library_class_attributes_are_preserved() -> None:
    # This existing eight-page Adobe PDF Library document declares these
    # attributes through /C /Pa0 and /C /A0, with no direct /A on the elements.
    path = Path(REGRESSION_DIR) / "10458169359009391401-5.pdf"
    doc = DoclingPdfParser(loglevel="fatal").load(path)
    try:
        structure = doc.get_structure()
        assert structure is not None
        elements = {element.id: element for element in structure.iter_elements()}

        paragraph = elements["24012 0"]
        assert paragraph.attributes["/Layout"] == {
            "/LineHeight": 12,
            "/TextAlign": "/Center",
        }
        first_page_span = elements["24019 0"]
        assert first_page_span.page_no == 1
        assert first_page_span.attributes["/Layout"] == {"/LineHeight": 43}
    finally:
        doc.unload()
