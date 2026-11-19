"""Regressions for page identity, atomic edits and retained PDF references."""

from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from threading import Event

import pypdf
import pytest

from docling_parse import PdfEditor, PdfReader, PdfWriter, _pdf_images
from docling_parse.pdf_errors import PdfEditError, PdfStaleHandleError
from tests.pdf_builder import build_pdf, content_stream, stream_object
from tests.test_unit_pdf_editor import (
    BLUE,
    FORM,
    THREE,
    TWO,
    WHITE,
    color_at,
    image_bytes,
)

NOTE = {"/Subtype": "/Text", "/Rect": [10, 10, 30, 30]}


@pytest.mark.parametrize(
    "operation",
    [
        lambda writer, page: writer.remove_page(page),
        lambda writer, page: writer.add_annotation(page, NOTE),
        lambda writer, page: writer.add_outline_item("Bookmark", page),
    ],
    ids=["remove-page", "add-annotation", "add-outline-item"],
)
def test_writer_rejects_stale_and_foreign_page_handles(operation):
    with PdfWriter(THREE) as writer, PdfWriter(TWO) as other:
        stale = writer.pages[0]
        writer.insert_blank_page(90, 100, 0)

        for handle in (stale, other.pages[0]):
            with pytest.raises(PdfStaleHandleError):
                operation(writer, handle)

        assert [page.mediabox.width for page in writer.pages] == [90, 200, 210, 220]
        assert not writer.outline
        assert all(not page.annotations for page in writer.pages)
        for read in (lambda: stale.index, lambda: stale.page_number):
            with pytest.raises(PdfStaleHandleError):
                read()


def test_writer_append_checks_handle_owner_and_generation():
    with PdfWriter(THREE) as source, PdfWriter(TWO) as other, PdfWriter() as writer:
        stale = source.pages[2]
        source.remove_page(0)
        for handle in (stale, other.pages[0]):
            with pytest.raises(PdfStaleHandleError):
                writer.append(source, pages=[handle])
        assert not writer.pages

        writer.append(source, pages=[source.pages[1], 0])
        assert [page.extract_text() for page in writer.pages] == ["gamma", "beta"]


def test_writer_rechecks_the_source_after_reading_the_selection():
    with PdfWriter(THREE) as source, PdfWriter() as writer:

        def pages():
            yield source.pages[0]
            source.remove_page(0)

        with pytest.raises(PdfStaleHandleError):
            writer.append(source, pages=pages())
        assert not writer.pages


def test_editor_page_number_fails_after_reopen():
    with PdfEditor(source=THREE) as editor:
        handle = editor.get_page(page_no=1)
        editor.open(source=TWO)
        with pytest.raises(PdfStaleHandleError):
            editor.get_page_no(page=handle)
        with pytest.raises(PdfStaleHandleError):
            _ = handle.page_no


def test_insert_handle_is_stale_when_page_moves_before_python_receives_it():
    with PdfEditor(source=THREE) as editor, ThreadPoolExecutor(1) as executor:
        native = editor._native
        inserted = Event()
        moved = Event()

        class PauseAfterInsert:
            def __getattr__(self, name):
                return getattr(native, name)

            def insert_blank_page(self, *args):
                result = native.insert_blank_page(*args)
                inserted.set()
                assert moved.wait(5)
                return result

        editor._native = PauseAfterInsert()
        result = executor.submit(editor.add_blank_page, width=77, height=100)
        try:
            assert inserted.wait(5)
            editor.move_page(page_no=4, to_page_no=1)
        finally:
            moved.set()

        handle = result.result(timeout=5)
        with pytest.raises(PdfStaleHandleError):
            _ = handle.mediabox
        assert editor.get_page(page_no=1).mediabox.width == 77
        assert editor.get_page(page_no=4).mediabox.width == 220


def test_failed_form_batch_does_not_set_an_earlier_field_or_modify_source():
    with PdfEditor(source=FORM) as editor:
        fields = editor.get_fields()
        page = editor.get_page(page_no=1)
        with pytest.raises(PdfEditError, match="zz_typo"):
            editor.update_form_field_values(fields={"agree": True, "zz_typo": "bad"})
        assert not editor.is_modified
        assert editor.get_fields() == fields
        assert page.page_no == 1

        original = editor.to_bytes()
        with pytest.raises(PdfEditError, match="zz_typo"):
            editor.update_form_field_values(fields={"agree": True, "zz_typo": "bad"})
        assert editor.to_bytes() == original
        assert editor.get_fields() == fields


def test_failed_outline_update_preserves_title_and_destination():
    with PdfEditor(source=THREE) as editor:
        item = editor.add_outline_item(title="Original", page_no=1)
        page = editor.get_page(page_no=1)
        original = editor.to_bytes()
        outline = editor.outline

        with pytest.raises(PdfEditError, match="top"):
            editor.update_outline_item(
                item_id=item, title="Changed", page_no=2, top="bad"
            )

        assert editor.outline == outline
        assert editor.to_bytes() == original
        assert page.page_no == 1


def test_writer_checks_bookmark_sibling_before_linking_new_bookmark():
    with PdfWriter(THREE) as writer:
        parent = writer.add_outline_item("Parent", 0)
        child = writer.add_outline_item("Child", 1, parent)
        original = writer.outline
        with pytest.raises(PdfEditError, match="same parent"):
            writer.add_outline_item("Unlinked", 1, before=child)
        assert writer.outline == original
        writer.add_outline_item("First", 1, before=parent)
        assert [item["title"] for item in writer.outline] == ["First", "Parent"]


def test_rejected_scale_leaves_boxes_and_content_unchanged():
    with PdfEditor(source=THREE) as editor:
        page = editor.get_page(page_no=1)
        box = page.mediabox
        with pytest.raises(PdfEditError, match="invertible"):
            editor.scale_page(page_no=1, sx=5e-7, sy=5e-7)
        assert not editor.is_modified
        assert page.mediabox == box

        original = editor.to_bytes()
        with pytest.raises(PdfEditError, match="invertible"):
            editor.scale_page(page_no=1, sx=5e-7, sy=5e-7)
        assert page.mediabox == box and page.page_no == 1
        assert editor.extract_text(page_no=1) == "alpha"
        assert editor.to_bytes() == original


@pytest.mark.parametrize("invalid", ["font", "label"])
def test_rejected_contents_page_leaves_page_tree_and_handles_unchanged(invalid):
    with PdfEditor(source=THREE) as editor:
        editor.add_outline_item(title="Original", page_no=1)
        font = "Helvetica"
        if invalid == "font":
            font = "Invalid font"
        else:
            editor.set_page_label(
                first_page_no=1, last_page_no=3, style="/D", prefix="李"
            )
        page = editor.get_page(page_no=1)
        original = editor.to_bytes()
        outline = editor.outline

        with pytest.raises(PdfEditError):
            editor.add_table_of_contents(font=font, page_size="A4")

        assert editor.page_count() == 3
        assert editor.outline == outline
        assert editor.to_bytes() == original
        assert page.mediabox.width == 200


@pytest.mark.parametrize("invalid", ["decoder", "samples"])
def test_rejected_second_image_frame_leaves_no_page(monkeypatch, invalid):
    first = next(_pdf_images.pictures(image_bytes("RGB", (10, 10), (0, 0, 0), "PNG")))

    def pictures(_):
        yield first
        if invalid == "decoder":
            raise OSError("second frame cannot be decoded")
        yield first._replace(data=b"x")

    monkeypatch.setattr(_pdf_images, "pictures", pictures)
    with PdfEditor(source=THREE) as editor:
        page = editor.get_page(page_no=1)
        original = editor.to_bytes()
        with pytest.raises(OSError if invalid == "decoder" else PdfEditError):
            editor.add_image(image=b"two frames", before_page_no=1)
        assert editor.to_bytes() == original
        assert page.mediabox.width == 200 and editor.page_count() == 3


def nested_form_pdf() -> bytes:
    """One shared field, one field on page one, and a shared custom appearance."""
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm 9 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 300] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [5 0 R 10 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [6 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /Parent 7 0 R /P 3 0 R "
            "/Rect [20 30 110 50] /AP << /N 11 0 R >> >>",
            "<< /Type /Annot /Subtype /Widget /Parent 7 0 R /P 4 0 R "
            "/Rect [20 30 110 50] /AP << /N 11 0 R >> >>",
            "<< /FT /Tx /T (shared) /V (survivor) /Kids [5 0 R 6 0 R] /Parent 8 0 R >>",
            "<< /T (group) /Kids [7 0 R 10 0 R] >>",
            "<< /Fields [8 0 R] /CO [10 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /FT /Tx /T (exclusive) /V (deleted-value) "
            "/Parent 8 0 R /P 3 0 R /Rect [30 130 120 150] /AP << /N 11 0 R >> >>",
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [0 0 90 20]",
                b"0 0 1 rg 0 0 90 20 re f",
            ),
        ]
    )


def pdf_reader(editor):
    return pypdf.PdfReader(BytesIO(editor.to_bytes()))


def test_page_removal_prunes_nested_widgets_and_preserves_the_shared_field():
    with PdfEditor(source=nested_form_pdf()) as editor:
        assert editor.get_fields()["group.shared"]["pages"] == [1, 2]
        editor.remove_page(page_no=1)
        fields = editor.get_fields()
        assert list(fields) == ["group.shared"]
        assert fields["group.shared"]["pages"] == [1]
        assert fields["group.shared"]["value"] == "survivor"

        saved = pdf_reader(editor)
        form = saved.trailer["/Root"]["/AcroForm"]
        assert not form["/CO"]
        group = form["/Fields"][0].get_object()
        assert len(group["/Kids"]) == 1
        widget = saved.pages[0]["/Annots"][0].get_object()
        assert widget.raw_get("/Parent") == group["/Kids"][0]
        assert widget.raw_get("/P") == saved.pages[0].indirect_reference
        assert b"deleted-value" not in editor.to_bytes()


def test_removing_nested_widget_annotations_prunes_the_fields():
    with PdfEditor(source=nested_form_pdf()) as editor:
        assert editor.remove_annotations(subtypes="/Widget") == 3
        assert editor.get_fields() == {}
        saved = pdf_reader(editor)
        assert all("/Annots" not in page for page in saved.pages)
        assert not saved.trailer["/Root"]["/AcroForm"]["/Fields"]


@pytest.mark.parametrize("clean", [False, True])
def test_writer_clean_controls_widget_cleanup(clean):
    with PdfWriter(nested_form_pdf()) as writer:
        writer.remove_page(0, clean=clean)
        fields = set(writer.get_fields())
        if clean:
            assert fields == {"group.shared"}
        else:
            assert fields == {"group.shared", "group.exclusive"}


def tagged_pdf_with_page_children() -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R /MarkInfo << /Marked true >> >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 300] "
            "/Resources << /Font << /F1 7 0 R >> >> >>",
            "<< /Type /Page /Parent 2 0 R /StructParents 0 /Contents 5 0 R >>",
            "<< /Type /Page /Parent 2 0 R /StructParents 1 /Contents 6 0 R >>",
            content_stream(
                "/P <</MCID 0>> BDC BT /F1 12 Tf 20 20 Td (removed) Tj ET EMC"
            ),
            content_stream(
                "/P <</MCID 0>> BDC BT /F1 12 Tf 20 20 Td (survives) Tj ET EMC"
            ),
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            "<< /Type /StructTreeRoot /K 9 0 R /ParentTree 12 0 R /IDTree 13 0 R /ParentTreeNextKey 2 >>",
            "<< /Type /StructElem /S /Document /P 8 0 R /Pg 3 0 R /K [10 0 R 11 0 R] >>",
            "<< /Type /StructElem /S /P /P 9 0 R /ID (deleted) /K 0 >>",
            "<< /Type /StructElem /S /P /P 9 0 R /ID (kept) /Pg 4 0 R "
            "/K << /Type /MCR /Pg 4 0 R /MCID 0 >> >>",
            "<< /Nums [0 [10 0 R] 1 [11 0 R]] >>",
            "<< /Names [(deleted) 10 0 R (kept) 11 0 R] >>",
        ]
    )


def test_page_removal_prunes_tagged_tree_and_preserves_sibling_references():
    with PdfEditor(source=tagged_pdf_with_page_children()) as editor:
        editor.remove_page(page_no=1)
        saved = pdf_reader(editor)
        assert len(saved.pages) == 1
        assert saved.pages[0].extract_text() == "survives"

        tree = saved.trailer["/Root"]["/StructTreeRoot"]
        document = tree["/K"]
        assert "/Pg" not in document
        (remaining,) = document["/K"]
        node = remaining.get_object()
        assert node.raw_get("/Pg") == saved.pages[0].indirect_reference
        assert node["/K"].raw_get("/Pg") == saved.pages[0].indirect_reference
        assert tree["/ParentTree"]["/Nums"] == [1, [remaining]]
        assert tree["/IDTree"]["/Names"] == ["kept", remaining]
        page_objects = [
            saved.get_object(number) for number in saved.xref.get(0, {}) if number != 0
        ]
        assert (
            sum(
                isinstance(obj, dict) and obj.get("/Type") == "/Page"
                for obj in page_objects
            )
            == 1
        )


@pytest.mark.parametrize(
    "operation, expected",
    [
        ("translate", [30, 45, 120, 65]),
        ("scale", [40, 15, 220, 25]),
        ("rotation", [30, 90, 50, 180]),
    ],
)
def test_transform_preserves_fields_shared_between_pages(operation, expected):
    with PdfEditor(source=nested_form_pdf()) as editor:
        if operation == "translate":
            editor.transform_page(page_no=1, ctm=(1, 0, 0, 1, 10, 15))
        elif operation == "scale":
            editor.scale_page(page_no=1, sx=2, sy=0.5)
        else:
            editor.rotate_page(page_no=1, angle=90)
            editor.transfer_page_rotation_to_content(page_no=1)

        assert editor.get_fields()["group.shared"]["pages"] == [1, 2]
        saved = pdf_reader(editor)
        first, second = (page["/Annots"][0].get_object() for page in saved.pages)
        assert first.raw_get("/Parent") == second.raw_get("/Parent")
        assert first["/Rect"] == expected
        assert second["/Rect"] == [20, 30, 110, 50]
        assert "/Matrix" in first["/AP"]["/N"]
        assert "/Matrix" not in second["/AP"]["/N"]


def test_widget_rectangle_edit_preserves_custom_appearance():
    pdfium = pytest.importorskip("pypdfium2")
    with PdfEditor(source=nested_form_pdf()) as editor:
        original = pdf_reader(editor).pages[0]["/Annots"][1].get_object()["/AP"]["/N"]
        editor.update_form_field(name="group.exclusive", rect=(35, 125, 125, 145))
        widget = pdf_reader(editor).pages[0]["/Annots"][1].get_object()
        assert widget["/Rect"] == [35, 125, 125, 145]
        assert widget["/AP"]["/N"].get_data() == original.get_data()
        assert widget["/AP"]["/N"]["/BBox"] == original["/BBox"]
        with pdfium.PdfDocument(editor.to_bytes()) as document:
            document.init_forms()
            image = document[0].render(scale=1).to_pil().convert("RGB")
        assert color_at(image, 40, 128) == BLUE
        assert color_at(image, 40, 148) == WHITE


def test_annotation_transform_moves_quadpoints_with_rectangle():
    with PdfEditor(source=THREE) as editor:
        editor.add_annotation(
            page_no=1,
            annotation={
                "/Subtype": "/Highlight",
                "/Rect": [10, 30, 90, 50],
                "/QuadPoints": [10, 50, 90, 50, 10, 30, 90, 30],
            },
        )
        editor.transform_page(page_no=1, ctm=(1, 0, 0, 1, 20, 40))
        (highlight,) = pdf_reader(editor).pages[0]["/Annots"]
        assert highlight.get_object()["/Rect"] == [30, 70, 110, 90]
        assert highlight.get_object()["/QuadPoints"] == [
            30,
            90,
            110,
            90,
            30,
            70,
            110,
            70,
        ]


def test_nonfinite_dimensions_and_rectangles_are_rejected():
    with PdfEditor(source=FORM) as editor:
        infinity = float("inf")
        with pytest.raises(PdfEditError, match="finite"):
            editor.add_blank_page(width=infinity, height=100)
        with pytest.raises(PdfEditError, match="finite"):
            editor.set_page_box(page_no=1, box="crop", bounds=(0, 0, infinity, 100))
        with pytest.raises(PdfEditError, match="rectangle"):
            editor.add_text_field(page_no=1, name="bad", rect=(0, 0, 90, infinity))
        with pytest.raises(ValueError, match="finite"):
            editor.add_image(image=b"", page_size=(100, infinity))
        assert editor.page_count() == 1
        assert set(editor.get_fields()) == {"agree", "name"}
