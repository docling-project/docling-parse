"""Tagged-PDF logical structure: the structure tree and the marked-content
linkage of text cells (ISO 32000-2, 14.7 and 14.8; Well-Tagged PDF 1.0).

The fixture is a one-page tagged PDF built in memory: a role-mapped heading,
a paragraph with a nested /Span carrying /ActualText, a figure with /Alt and a
layout attribute, a link annotation referenced through /OBJR, and a pagination
artifact. Every construct the reader has to resolve appears once.
"""

from io import BytesIO

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    PdfMarkedContentRef,
    PdfObjectRef,
    PdfStructureElement,
)
from tests.pdf_builder import build_pdf, content_stream

CONTENT = (
    "/Heading <</MCID 0>> BDC BT /F1 14 Tf 20 170 Td (Title) Tj ET EMC\n"
    "/P <</MCID 1>> BDC BT /F1 10 Tf 20 150 Td (Body) Tj "
    "/Span <</MCID 2 /ActualText (fi)>> BDC (x) Tj EMC (text) Tj ET EMC\n"
    "/Artifact <</Type /Pagination /Subtype /Footer>> BDC "
    "BT /F1 8 Tf 20 20 Td (Page1) Tj ET EMC\n"
    "/Figure <</MCID 3>> BDC 0.5 g 20 60 50 50 re f EMC\n"
)


def _tagged_pdf() -> bytes:
    objects = [
        # 1 catalog
        "<< /Type /Catalog /Pages 2 0 R /Lang (en-US) "
        "/MarkInfo << /Marked true >> /StructTreeRoot 7 0 R >>",
        # 2 pages
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        # 3 page
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R "
        "/StructParents 0 /Annots [6 0 R] >>",
        # 4 content
        content_stream(CONTENT),
        # 5 font
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
        # 6 link annotation
        "<< /Type /Annot /Subtype /Link /Rect [20 145 80 160] "
        "/A << /S /URI /URI (https://example.org) >> /StructParent 1 >>",
        # 7 structure tree root
        "<< /Type /StructTreeRoot /K [8 0 R] /RoleMap << /Heading /H1 >> >>",
        # 8 Document
        "<< /Type /StructElem /S /Document /P 7 0 R /K [9 0 R 10 0 R 12 0 R 13 0 R] >>",
        # 9 heading (role-mapped, with /Lang)
        "<< /Type /StructElem /S /Heading /P 8 0 R /Pg 3 0 R /K [0] /Lang (en) >>",
        # 10 paragraph with a nested span
        "<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K [1 11 0 R] >>",
        # 11 span with /ActualText
        "<< /Type /StructElem /S /Span /P 10 0 R /Pg 3 0 R /K [2] /ActualText (fi) >>",
        # 12 figure with /Alt and a Layout attribute
        "<< /Type /StructElem /S /Figure /P 8 0 R /Pg 3 0 R /K [3] "
        "/Alt (A grey square) /A << /O /Layout /Placement /Block >> >>",
        # 13 link element referencing the annotation and the paragraph
        "<< /Type /StructElem /S /Link /P 8 0 R /Pg 3 0 R "
        "/K [<< /Type /OBJR /Obj 6 0 R /Pg 3 0 R >>] /Ref [10 0 R] >>",
    ]
    return build_pdf(objects)


def _load():
    parser = DoclingPdfParser(loglevel="fatal")
    return parser.load(path_or_stream=BytesIO(_tagged_pdf()))


def test_structure_tree_is_read_with_attributes_and_kids():
    doc = _load()
    structure = doc.get_structure()
    assert structure is not None
    assert structure.marked is True
    assert structure.role_map == {"/Heading": "/H1"}

    assert len(structure.elements) == 1
    document = structure.elements[0]
    assert document.type == "/Document"
    children = [kid for kid in document.kids if isinstance(kid, PdfStructureElement)]
    assert [kid.type for kid in children] == ["/Heading", "/P", "/Figure", "/Link"]
    heading, paragraph, figure, link = children

    # role map resolves the custom heading type; the raw type is preserved
    assert heading.resolved_type(structure.role_map) == "/H1"
    assert heading.lang == "en"
    assert heading.page == 0
    assert heading.kids == [PdfMarkedContentRef(page=0, mcid=0)]

    # nested elements keep their order and their own marked content
    assert paragraph.kids[0] == PdfMarkedContentRef(page=0, mcid=1)
    span = paragraph.kids[1]
    assert isinstance(span, PdfStructureElement)
    assert span.type == "/Span"
    assert span.actual_text == "fi"
    assert span.kids == [PdfMarkedContentRef(page=0, mcid=2)]

    assert figure.alt == "A grey square"
    assert figure.attributes == {"/Layout": {"/Placement": "/Block"}}

    # object references resolve to the annotation, /Ref to element ids
    assert link.kids == [PdfObjectRef(page=0, obj="6 0", subtype="/Link")]
    assert link.ref == [paragraph.id]

    # depth-first order is the logical content order
    orders = [element.order for element in structure.iter_elements()]
    assert orders == sorted(orders) == list(range(len(orders)))


def test_cells_carry_innermost_mcid_and_artifact_type():
    doc = _load()
    page = doc.get_page(1)
    tags = {tag.index: tag for tag in doc.get_page_marked_content(1)}

    by_mcid: dict[int, str] = {}
    artifacts: dict[str, str] = {}
    for cell in page.char_cells:
        tag = tags.get(cell.index)
        if tag is None:
            continue
        if tag.mcid >= 0:
            by_mcid[tag.mcid] = by_mcid.get(tag.mcid, "") + cell.text
        if tag.artifact_type:
            artifacts[tag.artifact_type] = (
                artifacts.get(tag.artifact_type, "") + cell.text
            )

    assert by_mcid[0] == "Title"
    # the span's glyph was substituted by /ActualText and keeps the inner MCID
    assert by_mcid[2] == "fi"
    assert by_mcid[1] == "Bodytext"
    assert artifacts == {"/Pagination": "Page1"}
    assert {tag.artifact_subtype for tag in tags.values() if tag.artifact_type} == {
        "/Footer"
    }

    # artifact cells are not real content: no MCID
    assert all(tag.mcid < 0 for tag in tags.values() if tag.artifact_type)


def test_untagged_document_has_no_structure():
    parser = DoclingPdfParser(loglevel="fatal")
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream("BT /F1 10 Tf 20 150 Td (Plain) Tj ET"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    doc = parser.load(path_or_stream=BytesIO(build_pdf(objects)))
    assert doc.get_structure() is None
    assert doc.get_page_marked_content(1) == []
