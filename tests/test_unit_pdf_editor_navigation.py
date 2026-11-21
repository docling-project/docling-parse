"""Navigation must never retain a target outside the document's page tree."""

from io import BytesIO

import pypdf
import pytest
from pypdf.generic import Fit, NameObject, NullObject

from docling_parse import PdfEditor, PdfWriter
from docling_parse.pdf_errors import PdfEditError
from tests.pdf_builder import build_pdf
from tests.test_unit_pdf_compat import writer_bytes
from tests.test_unit_pdf_editor import THREE, make_pdf


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


def test_named_targets_are_resolved_for_inspection_import_and_removal():
    source = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /Dests << /legacy [4 0 R /FitH 140] >> "
            "/Names << /Dests << /Names [(named) << /D [4 0 R /XYZ null 110 1.25] >>] >> >> "
            "/OpenAction << /S /GoTo /D (named) >> /Outlines 5 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 200] >>",
            "<< /Type /Page /Parent 2 0 R /Annots ["
            "<< /Type /Annot /Subtype /Link /Rect [10 10 20 20] /Dest /legacy >> "
            "<< /Type /Annot /Subtype /Link /Rect [30 10 40 20] "
            "/A << /S /GoTo /D (named) >> >>] >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Outlines /First 6 0 R /Last 6 0 R /Count 1 >>",
            "<< /Title (Named target) /Parent 5 0 R /Dest /legacy >>",
        ]
    )
    with PdfEditor(source=source) as editor, PdfEditor() as assembled:
        assert (
            editor.open_destination
            == editor.named_destinations["named"]
            == {
                "page": 2,
                "kind": "/XYZ",
                "arguments": [None, 110, 1.25],
            }
        )
        assert [a["page"] for a in editor.get_page(page_no=1).annotations] == [2, 2]
        assert editor.outline[0]["page"] == 2

        assembled.append(source=editor, page_nos=[2])
        reader = pypdf.PdfReader(BytesIO(assembled.to_bytes()))
        assert reader.outline[0].typ == "/FitH"
        assert reader.outline[0].top == 140
        assert reader.get_destination_page_number(reader.outline[0]) == 0

        editor.remove_page(page_no=2)
        assert editor.open_destination is None
        assert editor.named_destinations == {}
        assert editor.outline[0]["page"] is None
        assert all("page" not in a for a in editor.get_page(page_no=1).annotations)
        links = pypdf.PdfReader(BytesIO(editor.to_bytes())).pages[0]["/Annots"]
        for reference in links:
            link = reference.get_object()
            assert "/Dest" not in link
            assert "/A" not in link


def test_outline_import_remaps_targets_and_nests_under_writer_heading():
    author = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    group = author.add_outline_item("Group", 1, bold=True, is_open=False)
    first = author.add_outline_item(
        "Detail",
        0,
        parent=group,
        color=(0.25, 0.5, 0.75),
        italic=True,
        fit=Fit.xyz(left=None, top=150, zoom=1.25),
    )
    # Use a direct destination so both writers exercise destination remapping.
    item = first.get_object()
    item[NameObject("/Dest")] = item.pop("/A").get_object()["/D"]
    author.add_outline_item("Last", 2, fit=Fit.fit_rectangle(5, 10, 45, 50))
    author.add_outline_item("Omitted", 1)
    source = writer_bytes(author)

    for writer in (PdfWriter(THREE), pypdf.PdfWriter(clone_from=BytesIO(THREE))):
        writer.merge(1, BytesIO(source), outline_item="Imported", pages=[2, 0])
        reader = pypdf.PdfReader(BytesIO(writer_bytes(writer)))
        heading, [parent, [detail], last] = reader.outline
        assert [heading.title, parent.title, detail.title, last.title] == [
            "Imported",
            "Group",
            "Detail",
            "Last",
        ]
        assert [
            reader.get_destination_page_number(item) for item in (heading, detail, last)
        ] == [
            1,
            2,
            1,
        ]
        assert parent["/F"] == 2
        assert detail["/F"] == 1
        assert detail["/C"] == [0.25, 0.5, 0.75]
        assert detail.typ == "/XYZ"
        assert isinstance(detail.left, NullObject)
        assert (detail.top, detail.zoom) == (150, 1.25)
        assert last.typ == "/FitR"
        assert (last.left, last.bottom, last.right, last.top) == (5, 10, 45, 50)

    with PdfWriter(THREE) as writer:
        writer.append(BytesIO(source), outline_item="Imported", pages=[2, 0])
        children = writer.outline[0]["children"]
        assert children[0]["page"] is None
        assert children[0]["open"] is False
        raw_group = pypdf.PdfReader(BytesIO(writer_bytes(writer))).root_object[
            "/Outlines"
        ]["/First"]["/First"]
        assert "/Dest" not in raw_group
        assert raw_group["/Count"] == -1


def test_outline_items_resolve_nonzero_generation_and_reject_detached_ids():
    source = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Outlines /First 5 7 R /Last 5 7 R /Count 1 >>",
            "<< /Title (Existing) /Parent 4 0 R /Dest [3 0 R /Fit] >>",
        ]
    )
    body, xref = source.split(b"xref\n", 1)
    entries = xref.splitlines(keepends=True)
    entries[6] = entries[6].replace(b" 00000 n ", b" 00007 n ")
    source = body.replace(b"5 0 obj\n", b"5 7 obj\n") + b"xref\n" + b"".join(entries)
    with PdfEditor(source=source) as editor:
        parent = editor.outline[0]["id"]
        assert parent == 5
        editor.update_outline_item(item_id=parent, title="Updated")
        child = editor.add_outline_item(title="Child", page_no=1, parent=parent)
        assert editor.outline[0]["title"] == "Updated"

        editor.remove_outline_item(item_id=parent)
        for detached in (parent, child):
            with pytest.raises(PdfEditError, match="does not exist"):
                editor.update_outline_item(item_id=detached, title="Detached")
            with pytest.raises(PdfEditError, match="does not exist"):
                editor.add_outline_item(
                    title="Detached parent", page_no=1, parent=detached
                )
        assert editor.outline == []


def test_closed_leaf_remembers_state_for_its_first_child_after_reopening():
    with PdfEditor(source=THREE) as before:
        before.add_outline_item(title="Closed", page_no=1, is_open=False)
        assert before.outline[0]["open"] is False
        data = before.to_bytes()

    with PdfEditor(source=data) as editor:
        parent = editor.outline[0]["id"]
        editor.add_outline_item(title="Child", page_no=2, parent=parent)
        root = pypdf.PdfReader(BytesIO(editor.to_bytes())).root_object["/Outlines"]
        assert root["/Count"] == 1
        assert root["/First"]["/Count"] == -1
        editor.update_outline_item(item_id=parent, is_open=True)
        root = pypdf.PdfReader(BytesIO(editor.to_bytes())).root_object["/Outlines"]
        assert root["/Count"] == 2
        assert root["/First"]["/Count"] == 1


def test_editor_preserves_explicit_label_ranges_through_page_splices():
    with PdfEditor(source=make_pdf(["one", "two", "three", "four"])) as editor:
        editor.set_page_label(first_page_no=1, last_page_no=2, style="/r")
        editor.set_page_label(
            first_page_no=3, last_page_no=4, style="/D", prefix="B-", start=7
        )
        editor.append(source=THREE, page_nos=[1, 2], before_page_no=2)
        assert editor.page_labels == ["i", "2", "3", "ii", "B-7", "B-8"]
        editor.remove_page(page_no=4)
        editor.remove_page(page_no=1)
        editor.duplicate_page(page_no=2, before_page_no=3)
        assert editor.page_labels == ["2", "3", "3", "B-7", "B-8"]
        assert (
            pypdf.PdfReader(BytesIO(editor.to_bytes())).page_labels
            == editor.page_labels
        )


def test_toc_uses_retained_roman_and_decimal_target_labels():
    with PdfEditor(source=THREE) as editor:
        editor.set_page_label(first_page_no=1, last_page_no=2, style="/r")
        editor.set_page_label(first_page_no=3, last_page_no=3, style="/D", start=7)
        editor.add_outline_item(title="Front", page_no=1)
        editor.add_outline_item(title="Body", page_no=3)

        [toc] = editor.add_table_of_contents(page_size="A4")
        assert editor.page_labels == ["1", "i", "ii", "7"]
        reader = pypdf.PdfReader(BytesIO(editor.to_bytes()))
        assert set(reader.pages[0].extract_text().splitlines()) >= {"Front i", "Body 7"}
        assert [link["page"] for link in toc.annotations] == [2, 4]


def test_writer_page_labels_remain_position_based_like_pypdf():
    with PdfEditor(source=THREE) as editor:
        editor.set_page_label(first_page_no=1, last_page_no=3, style="/r")
        source = editor.to_bytes()
    for writer in (PdfWriter(source), pypdf.PdfWriter(clone_from=BytesIO(source))):
        writer.insert_blank_page(width=200, height=200, index=0)
        assert writer.page_labels == ["i", "ii", "iii", "iv"]
        writer.remove_page(1)
        assert writer.page_labels == ["i", "ii", "iii"]
