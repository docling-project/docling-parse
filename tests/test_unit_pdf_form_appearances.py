"""Round-trip and rendering checks for form values and appearances."""

from io import BytesIO

import pypdf
import pytest
from pypdf.generic import (
    DecodedStreamObject,
    DictionaryObject,
    NameObject,
    RectangleObject,
)

from docling_parse import PdfEditor
from docling_parse.pdf_editor import PdfBox
from docling_parse.pdf_errors import PdfEditError, PdfUnsupportedError
from tests.pdf_builder import build_pdf, stream_object
from tests.test_unit_pdf_editor import BLUE, FORM, THREE, color_at, rendered
from tests.test_unit_pdf_editor_safety import nested_form_pdf


def read(editor):
    return pypdf.PdfReader(BytesIO(editor.to_bytes()))


def acroform(pdf):
    return pdf.trailer["/Root"]["/AcroForm"]


def first_field(pdf):
    return acroform(pdf)["/Fields"][0].get_object()


def shared_checkbox(second_state):
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 200] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [6 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [7 0 R] >>",
            "<< /FT /Btn /T (shared) /V /Off /Kids [6 0 R 7 0 R] >>",
            "<< /Subtype /Widget /Parent 5 0 R /P 3 0 R /Rect [20 30 40 50] "
            "/AS /Off /AP << /N << /Yes 8 0 R /Off 9 0 R >> >> >>",
            "<< /Subtype /Widget /Parent 5 0 R /P 4 0 R /Rect [20 30 40 50] "
            f"/AS /Off /AP << /N << /{second_state} 8 0 R /Off 9 0 R >> >> >>",
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [0 0 20 20]", b"0 g 0 0 20 20 re f"
            ),
            stream_object("/Type /XObject /Subtype /Form /BBox [0 0 20 20]", b""),
        ]
    )


def test_form_presence_and_widget_bboxes():
    with PdfEditor(source=shared_checkbox("Yes")) as editor:
        editor.add_blank_page(width=200, height=200)
        assert editor.has_form()
        assert not editor.has_xfa()
        assert editor.has_form_on_page(page_no=1)
        assert editor.has_form_on_page(page_no=2)
        assert not editor.has_form_on_page(page_no=3)
        with pytest.raises(IndexError, match="page_no 0 out of range"):
            editor.has_form_on_page(page_no=0)

        editor.add_radio_group(
            page_no=3,
            name="plan",
            buttons={"basic": (20, 30, 40, 50), "premium": (60, 30, 80, 50)},
            selected="basic",
        )
        assert editor.has_form_on_page(page_no=3)
        fields = editor.get_fields()
        assert fields["shared"]["pages"] == [1, 2]
        assert fields["shared"]["bboxes"] == [PdfBox(20, 30, 40, 50)] * 2
        assert fields["plan"]["pages"] == [3]
        assert fields["plan"]["bboxes"] == [
            PdfBox(20, 30, 40, 50),
            PdfBox(60, 30, 80, 50),
        ]
        assert fields["plan"]["bboxes"][1].left == 60


@pytest.mark.parametrize("second_state", ["Yes", "YesLocal"])
def test_shared_checkbox_updates_all_widgets(second_state):
    pdfium = pytest.importorskip("pypdfium2")
    with PdfEditor(source=shared_checkbox(second_state)) as editor:
        assert editor.update_form_field_values(fields={"shared": True}) == 1
        data = editor.to_bytes()
        pdf = pypdf.PdfReader(BytesIO(data))
        field = first_field(pdf)
        assert field["/V"] == "/Yes"
        assert [widget.get_object()["/AS"] for widget in field["/Kids"]] == [
            "/Yes",
            "/" + second_state,
        ]
        with pdfium.PdfDocument(data) as document:
            document.init_forms()
            for page in document:
                image = page.render(scale=1).to_pil().convert("RGB")
                assert color_at(image, 30, 40) == (0, 0, 0)

        assert editor.update_form_field_values(fields={"shared": False}) == 1
        field = first_field(read(editor))
        assert field["/V"] == "/Off"
        assert all(widget.get_object()["/AS"] == "/Off" for widget in field["/Kids"])


def choice_form(flags, value, indices):
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /FT /Ch /T (choice) /P 3 0 R "
            f"/Ff {flags} /V {value} /I {indices} /DV [(a) (c)] "
            "/Opt [[(a) (Alpha)] [(b) (Beta)] [(c) (Gamma)]] /Rect [20 60 180 100] >>",
        ]
    )


def test_list_selection_indices_follow_replacement_options():
    with PdfEditor(source=choice_form(0, "(a)", "[0]")) as editor:
        original = editor.to_bytes()
        with pytest.raises(PdfEditError, match="not one of the options"):
            editor.update_form_field_values(fields={"choice": "Beta"})
        assert editor.to_bytes() == original

        editor.update_form_field_values(fields={"choice": "b"})
        assert first_field(read(editor))["/I"] == [1]
        assert editor.get_fields()["choice"]["value"] == "b"

        editor.update_form_field(name="choice", options=["b", "a"])
        assert first_field(read(editor))["/I"] == [0]
        editor.update_form_field(name="choice", options=["c"])
        field = first_field(read(editor))
        assert "/V" not in field
        assert "/I" not in field


def test_combo_uses_export_value_and_displays_label():
    with PdfEditor(source=choice_form(1 << 17, "(a)", "[0]")) as editor:
        with pytest.raises(PdfEditError, match="not one of the options"):
            editor.update_form_field_values(fields={"choice": "unlisted"})
        editor.update_form_field_values(fields={"choice": "b"})
        field = first_field(read(editor))
        assert field["/V"] == "b"
        assert "/I" not in field
        editor.update_form_field_values(fields={}, flatten=True)
        assert "Beta" in editor.extract_text(page_no=1)


def test_multi_select_values_survive_read_and_rejected_writes():
    with PdfEditor(source=choice_form(1 << 21, "[(a) (c)]", "[0 2]")) as editor:
        field = editor.get_fields()["choice"]
        assert field["value"] == field["default_value"] == ["a", "c"]
        before = editor.to_bytes()
        with pytest.raises(PdfUnsupportedError, match="multi-select"):
            editor.update_form_field_values(fields={"choice": "b"})
        with pytest.raises(PdfUnsupportedError, match="multi-select"):
            editor.update_form_field(name="choice", options=["a", "b"], read_only=True)
        assert editor.to_bytes() == before
        assert first_field(read(editor))["/I"] == [0, 2]


def test_unencodable_value_is_kept_for_viewer_regeneration():
    with PdfEditor(source=FORM) as editor:
        editor.update_form_field_values(fields={"name": "李", "agree": True})
        pdf = read(editor)
        assert first_field(pdf)["/V"] == "李"
        assert "/AP" not in first_field(pdf)
        assert acroform(pdf)["/NeedAppearances"]
        assert pdf.pages[0]["/Annots"][1].get_object()["/AS"] == "/Yes"
        before = editor.to_bytes()
        with pytest.raises(PdfUnsupportedError, match="usable appearance"):
            editor.update_form_field_values(fields={"agree": False}, flatten=True)
        assert editor.to_bytes() == before
        assert editor.get_fields()["agree"]["value"] == "/Yes"


def test_authored_unicode_field_is_left_for_viewer_regeneration():
    with PdfEditor(source=THREE) as editor:
        editor.add_text_field(
            page_no=1, name="name", value="李", rect=(20, 20, 120, 40)
        )
        pdf = read(editor)
        assert first_field(pdf)["/V"] == "李"
        assert acroform(pdf)["/NeedAppearances"]
        assert b"(?)" not in first_field(pdf)["/AP"]["/N"].get_data()


def form_with_square():
    writer = pypdf.PdfWriter(clone_from=BytesIO(FORM))
    writer.add_blank_page(width=200, height=200)
    appearance = DecodedStreamObject()
    appearance.set_data(b"0 0 1 rg 0 0 30 30 re f")
    appearance.update(
        {
            NameObject("/Type"): NameObject("/XObject"),
            NameObject("/Subtype"): NameObject("/Form"),
            NameObject("/BBox"): RectangleObject((0, 0, 30, 30)),
        }
    )
    writer.add_annotation(
        1,
        DictionaryObject(
            {
                NameObject("/Subtype"): NameObject("/Square"),
                NameObject("/Rect"): RectangleObject((20, 20, 50, 50)),
                NameObject("/AP"): DictionaryObject(
                    {NameObject("/N"): writer._add_object(appearance)}
                ),
            }
        ),
    )
    output = BytesIO()
    writer.write(output)
    return output.getvalue()


def test_form_flatten_generates_missing_prefilled_appearance_and_retains_square():
    with PdfEditor(source=form_with_square()) as editor:
        assert "/AP" not in first_field(read(editor))
        assert "/NeedAppearances" not in acroform(read(editor))
        assert editor.update_form_field_values(fields={}, flatten=True) == 0
        pdf = read(editor)
        assert "/AcroForm" not in pdf.trailer["/Root"]
        assert "/Annots" not in pdf.pages[0]
        assert "Ada" in pdf.pages[0].extract_text()
        (square,) = pdf.pages[1]["/Annots"]
        assert square.get_object()["/Subtype"] == "/Square"
        assert color_at(rendered(editor.to_bytes(), 1), 30, 30) == BLUE

        editor.flatten_annotations()
        assert "/Annots" not in read(editor).pages[1]
        assert color_at(rendered(editor.to_bytes(), 1), 30, 30) == BLUE


def test_form_flatten_preserves_nested_shared_widget_appearances():
    with PdfEditor(source=nested_form_pdf()) as editor:
        editor.update_form_field_values(fields={}, flatten=True)
        pdf = read(editor)
        assert "/AcroForm" not in pdf.trailer["/Root"]
        for page_index, page in enumerate(pdf.pages):
            assert "/Annots" not in page
            assert color_at(rendered(editor.to_bytes(), page_index), 40, 40) == BLUE


@pytest.mark.parametrize("unsupported", ["xfa", "empty-rect"])
def test_form_flatten_failure_leaves_values_and_widgets_intact(unsupported):
    writer = pypdf.PdfWriter(clone_from=BytesIO(FORM))
    if unsupported == "xfa":
        template = DecodedStreamObject()
        template.set_data(b"<template/>")
        writer.root_object["/AcroForm"][NameObject("/XFA")] = writer._add_object(
            template
        )
    else:
        annotations = writer.pages[0]["/Annots"]
        widget = annotations[0].get_object()
        widget[NameObject("/Rect")] = RectangleObject((0, 0, 0, 0))
        appearance = annotations[1].get_object()["/AP"]["/N"]["/Yes"]
        appearance.pop("/Subtype")
        # Prepare a paintable widget before reaching the unusable one.
        annotations[0], annotations[1] = annotations[1], annotations[0]
    output = BytesIO()
    writer.write(output)

    with PdfEditor(source=output.getvalue()) as editor:
        assert editor.has_xfa() == (unsupported == "xfa")
        before = editor.to_bytes()
        with pytest.raises(PdfUnsupportedError, match=r"XFA|usable appearance"):
            editor.update_form_field_values(fields={"agree": True}, flatten=True)
        assert editor.to_bytes() == before
        assert editor.get_fields()["agree"]["value"] == "/Off"
        assert len(read(editor).pages[0]["/Annots"]) == 2
        if unsupported == "empty-rect":
            # Force a fresh write so a cached snapshot cannot hide a live edit.
            editor.add_metadata(infos={"Subject": "After rejected flatten"})
            checkbox = read(editor).pages[0]["/Annots"][0].get_object()
            assert "/Subtype" not in checkbox["/AP"]["/N"]["/Yes"]
