"""Navigation must never retain a target outside the document's page tree."""

from io import BytesIO

import pypdf

from docling_parse import PdfEditor
from tests.test_unit_pdf_editor import THREE


def test_remove_page_clears_explicit_and_named_destinations():
    document = PdfEditor(source=THREE)
    document.add_named_destination(title="gone", page_no=2)
    document.add_named_destination(title="keep", page_no=3)
    document.add_outline_item(title="Gone", page_no=2)
    document.set_open_destination(page_no=2)
    document.add_link(
        page_no=1,
        rect=(10, 10, 20, 20),
        target_page_no=2,
    )
    document.add_annotation(
        page_no=1,
        annotation={"/Subtype": "/Link", "/Rect": [30, 10, 40, 20], "/Dest": "gone"},
    )
    document.add_annotation(
        page_no=1,
        annotation={
            "/Subtype": "/Link",
            "/Rect": [50, 10, 60, 20],
            "/A": {"/S": "/GoTo", "/D": "gone"},
        },
    )
    document.add_link(
        page_no=1,
        rect=(70, 10, 80, 20),
        target_page_no=3,
    )
    document.remove_page(page_no=2)
    reader = pypdf.PdfReader(BytesIO(document.to_bytes()))
    assert set(reader.named_destinations) == {"keep"}
    assert reader.get_destination_page_number(reader.named_destinations["keep"]) == 1
    assert "/OpenAction" not in reader.trailer["/Root"]
    assert document.outline[0]["page"] is None
    links = [reference.get_object() for reference in reader.pages[0]["/Annots"]]
    assert len(links) == 4
    for link in links[:3]:
        assert "/Dest" not in link
        assert "/A" not in link
    assert reader.get_page_number(links[3]["/Dest"][0].get_object()) == 1
