"""The public migration API implements a tested subset of pypdf 6.21.1."""

import inspect
import re
from io import BytesIO

import pypdf
import pytest

from docling_parse import PdfEditor, PdfExtractionMode, PdfReader, PdfWriter
from docling_parse._pdf_document import PdfEditablePage, PdfPage
from docling_parse.pdf_errors import PdfLoadError, PdfUnsupportedError
from tests.pdf_builder import simple_page_pdf
from tests.test_unit_pdf_editor import (
    BLUE,
    BLUE_SQUARE,
    FORM,
    RED,
    RED_SQUARE,
    THREE,
    TWO,
    WHITE,
    color_at,
    rendered,
)

READER_API = """
    attachments close get_fields get_form_text_fields get_num_pages get_page
    get_page_number get_pages_showing_field is_encrypted metadata
    named_destinations open_destination outline page_labels page_layout
    page_mode pages pdf_header viewer_preferences xmp_metadata
""".split()
WRITER_API = (
    READER_API
    + """
    add_annotation add_attachment add_blank_page add_js add_metadata
    add_named_destination add_outline_item add_page add_uri append encrypt
    insert_blank_page insert_page merge remove_annotations remove_links
    remove_page set_need_appearances_writer set_page_label set_page_layout write
""".split()
)
PAGE_API = """
    annotations artbox bleedbox cropbox extract_text images mediabox
    page_number rotation trimbox user_unit
""".split()
EDITABLE_PAGE_API = (
    PAGE_API
    + """
    add_transformation merge_page merge_rotated_page merge_scaled_page
    merge_transformed_page merge_translated_page rotate scale scale_by
    scale_to transfer_rotation_to_content
""".split()
)


def writer_bytes(writer):
    stream = BytesIO()
    writer.write(stream)
    return stream.getvalue()


@pytest.mark.parametrize(
    ("ours", "reference", "names", "constructor"),
    [
        (PdfReader, pypdf.PdfReader, READER_API, True),
        (PdfWriter, pypdf.PdfWriter, WRITER_API, True),
        (PdfPage, pypdf.PageObject, PAGE_API, False),
        (PdfEditablePage, pypdf.PageObject, EDITABLE_PAGE_API, False),
    ],
)
def test_compatibility_method_signatures(ours, reference, names, constructor):
    assert set(names) == {
        name
        for name in dir(ours)
        if not name.startswith("_") and hasattr(reference, name)
    }
    if constructor:
        names = ["__init__", *names]
    for name in names:
        actual_member = getattr(ours, name)
        expected_member = getattr(reference, name)
        if isinstance(expected_member, property):
            assert isinstance(actual_member, property), name
            continue
        assert callable(actual_member) and callable(expected_member), name
        actual = inspect.signature(actual_member).parameters
        expected = inspect.signature(expected_member).parameters
        assert list(actual) == list(expected), name
        for key, parameter in expected.items():
            other = actual[key]
            assert other.kind == parameter.kind, (name, key)
            if parameter.default is inspect.Parameter.empty:
                assert other.default is inspect.Parameter.empty, (name, key)
            elif parameter.default is None or isinstance(
                parameter.default, (str, int, float, bool, tuple)
            ):
                assert other.default == parameter.default, (name, key)
            elif hasattr(parameter.default, "fit_type"):
                assert other.default.fit_type == parameter.default.fit_type
                assert tuple(other.default.fit_args) == tuple(
                    parameter.default.fit_args
                )
            else:
                assert type(other.default) is type(parameter.default), (name, key)


def test_all_editor_parameters_are_keyword_only():
    for name, method in inspect.getmembers(PdfEditor, callable):
        if name.startswith("_") and name != "__init__":
            continue
        for parameter in inspect.signature(method).parameters.values():
            if parameter.name not in ("self", "cls"):
                assert parameter.kind == inspect.Parameter.KEYWORD_ONLY, (
                    name,
                    parameter,
                )


def test_editor_rejects_positional_and_zero_page_arguments():
    with pytest.raises(TypeError):
        getattr(PdfEditor, "__call__")(THREE)
    with PdfEditor(source=THREE) as editor:
        first = editor.get_page(page_no=1)
        assert not hasattr(editor, "pages")
        assert not hasattr(first, "rotate")
        assert first.page_no == editor.get_page_no(page=first) == 1
        assert editor.page_count() == 3
        assert editor.extract_text(page_no=1) == "alpha"
        assert editor.extract_text(page_no=3) == "gamma"
        for page_no in (0, -1, 4):
            with pytest.raises(IndexError):
                editor.get_page(page_no=page_no)
        with pytest.raises(TypeError):
            getattr(editor, "remove_page")(1)


def test_compatibility_page_sequence():
    with PdfReader(THREE) as reader, PdfWriter(THREE) as writer:
        for document in (reader, writer):
            assert document.get_num_pages() == len(document.pages) == 3
            assert document.get_page_number(document.get_page(1)) == 1
            assert document.pages[-1].page_number == 2
            assert [page.page_number for page in document.pages[:2]] == [0, 1]
            assert document.pages[0].extract_text() == "alpha"
            assert document.pages[0].extract_text(extraction_mode="layout") == "alpha"


def test_editor_extraction_uses_enum_and_includes_edits():
    with PdfEditor(source=THREE) as editor:
        assert editor.extract_text(page_no=1) == editor.extract_text(
            page_no=1, extraction_mode=PdfExtractionMode.PLAIN
        )
        with pytest.raises(TypeError, match="PdfExtractionMode"):
            editor.extract_text(page_no=1, extraction_mode="layout")
        editor.draw_text(page_no=1, text="unsaved", x=50, y=90)
        for mode in PdfExtractionMode:
            assert "unsaved" in editor.extract_text(page_no=1, extraction_mode=mode)


def test_editor_inspection_uses_one_based_targets():
    with PdfEditor(source=FORM) as editor:
        parent = editor.add_outline_item(title="Form", page_no=1)
        editor.add_outline_item(
            title="External", page_no=None, parent=parent, url="https://example.org"
        )
        editor.add_named_destination(title="First", page_no=1)
        editor.set_open_destination(page_no=1)
        editor.add_link(page_no=1, rect=(20, 10, 50, 20), target_page_no=1)
        assert editor.outline[0]["page"] == 1
        assert editor.outline[0]["children"][0]["page"] is None
        assert editor.named_destinations["First"]["page"] == 1
        assert editor.open_destination["page"] == 1
        assert editor.get_fields()["agree"]["pages"] == [1]
        assert [
            page.page_no for page in editor.get_pages_showing_field(name="agree")
        ] == [1]
        assert editor.get_page(page_no=1).annotations[-1]["page"] == 1
        assert editor.root_object["/OpenAction"][1] == "/Fit"

        with PdfReader(editor.to_bytes()) as reader:
            assert reader.outline[0]["page"] == 0
            assert reader.named_destinations["First"]["page"] == 0
            assert reader.open_destination["page"] == 0
            assert reader.get_fields()["agree"]["pages"] == [0]
            assert reader.pages[0].annotations[-1]["page"] == 0


@pytest.mark.parametrize("over, expected", [(True, BLUE), (False, RED)])
def test_public_writer_page_merge_positional_arguments(over, expected):
    args = (False,) if over else (False, False)
    with PdfReader(BLUE_SQUARE) as stamp, PdfWriter(RED_SQUARE) as writer:
        assert writer.pages[0].merge_page(stamp.pages[0], *args) is None
        assert color_at(rendered(writer_bytes(writer)), 125, 125) == expected
    writer = pypdf.PdfWriter(clone_from=BytesIO(RED_SQUARE))
    stamp = pypdf.PdfReader(BytesIO(BLUE_SQUARE))
    writer.pages[0].merge_page(stamp.pages[0], *args)
    assert color_at(rendered(writer_bytes(writer)), 125, 125) == expected


def test_writer_merge_expands_transformed_media_box():
    base = simple_page_pdf("")
    matrix = (0, 1, -1, 0, 100, 80)
    with PdfReader(BLUE_SQUARE) as stamp, PdfWriter(base) as writer:
        writer.pages[0].merge_transformed_page(stamp.pages[0], matrix, False, True)
        ours = writer.pages[0].mediabox
        output = writer_bytes(writer)

    reference = pypdf.PdfWriter(clone_from=BytesIO(base))
    stamp = pypdf.PdfReader(BytesIO(BLUE_SQUARE))
    reference.pages[0].merge_transformed_page(stamp.pages[0], matrix, False, True)
    assert ours == tuple(reference.pages[0].mediabox) == (-100, 0, 200, 280)
    assert rendered(output).size == rendered(writer_bytes(reference)).size == (300, 280)

    with PdfReader(BLUE_SQUARE) as stamp, PdfWriter(base) as writer:
        writer.pages[0].merge_translated_page(stamp.pages[0], 35, -5, True, True)
        assert writer.pages[0].mediabox == (0, -5, 235, 200)


def test_writer_merge_clips_to_transformed_crop_box():
    base = simple_page_pdf("")
    with PdfEditor(source=simple_page_pdf("0 0 1 rg 0 0 200 200 re f")) as stamp:
        stamp.set_page_box(page_no=1, box="crop", bounds=(60, 60, 150, 150))
        stamp.set_page_box(page_no=1, box="trim", bounds=(100, 100, 125, 125))
        source = stamp.to_bytes()

    for reader_class, writer_class in (
        (PdfReader, PdfWriter),
        (pypdf.PdfReader, pypdf.PdfWriter),
    ):
        stamp = reader_class(BytesIO(source))
        writer = writer_class(clone_from=BytesIO(base))
        writer.pages[0].merge_translated_page(stamp.pages[0], 35, -5)
        image = rendered(writer_bytes(writer))
        assert color_at(image, 105, 70) == BLUE
        assert color_at(image, 80, 65) == WHITE
        assert color_at(image, 192, 80) == WHITE


def test_migration_options_fail_explicitly():
    with PdfReader(THREE) as reader, PdfWriter(TWO) as writer:
        with pytest.raises(PdfUnsupportedError, match="extraction_mode"):
            reader.pages[0].extract_text(visitor_text=lambda *_: None)
        with pytest.raises(PdfUnsupportedError, match="extraction_mode"):
            reader.pages[0].extract_text(layout_mode_space_vertically=False)
        with pytest.raises(PdfUnsupportedError, match="expanding"):
            writer.pages[0].add_transformation((1, 0, 0, 1, 0, 0), expand=True)
        with pytest.raises(PdfUnsupportedError, match="excluding"):
            writer.insert_page(reader.pages[0], excluded_keys=("/Annots",))
        assert writer.get_num_pages() == 2


@pytest.mark.parametrize(
    "kwargs",
    [
        {"incremental": True},
        {"full": True},
        {"keep_initial_header": True},
        {"incremental_clone_object_count_limit": 10},
    ],
)
def test_unsupported_writer_constructor_options(kwargs):
    with pytest.raises(PdfUnsupportedError):
        PdfWriter(**kwargs)


@pytest.mark.parametrize(
    "reader_class, writer_class",
    [(PdfReader, PdfWriter), (pypdf.PdfReader, pypdf.PdfWriter)],
)
def test_public_writer_copies_and_edits_pages(reader_class, writer_class):
    reader = reader_class(BytesIO(THREE))
    writer = writer_class()
    added = writer.add_page(reader.pages[0])
    assert added.rotate(90) is added
    assert writer.insert_page(reader.pages[2], 0).mediabox.width == 220
    assert writer.pages[1].rotation == 90
    writer.remove_page(0)
    output = pypdf.PdfReader(BytesIO(writer_bytes(writer)))
    assert len(output.pages) == 1
    assert output.pages[0].rotation == 90
    assert output.pages[0].extract_text() == "alpha"


@pytest.mark.parametrize("writer_class", [PdfWriter, pypdf.PdfWriter])
def test_positional_pypdf_workflow(writer_class):
    writer = writer_class()
    writer.append(BytesIO(THREE), "Part", (0, 2))
    writer.merge(1, BytesIO(TWO), None, [1])
    writer.insert_blank_page(80, 90, 1)
    writer.add_blank_page()
    assert [float(page.mediabox.width) for page in writer.pages] == [
        200,
        80,
        210,
        210,
        210,
    ]
    stream = BytesIO()
    opened, output = writer.write(stream)
    assert opened is False
    assert output is stream and not output.closed
    assert len(pypdf.PdfReader(BytesIO(stream.getvalue())).pages) == 5


def test_writer_write_path_result(tmp_path):
    writer = PdfWriter(THREE)
    path = tmp_path / "out.pdf"
    opened, stream = writer.write(path)
    assert opened is True
    assert stream.closed is True
    assert str(stream.name) == str(path)
    assert len(PdfReader(path).pages) == 3


@pytest.mark.parametrize(
    ("algorithm", "revision"),
    [("RC4-40", 2), ("RC4-128", 3), ("AES-128", 4), ("AES-256-R5", 5), ("AES-256", 6)],
)
def test_compatibility_encryption_algorithms(algorithm, revision):
    writer = PdfWriter(THREE)
    writer.encrypt("secret", algorithm=algorithm)
    data = writer_bytes(writer)
    reader = pypdf.PdfReader(BytesIO(data), password="secret")
    assert reader.trailer["/Encrypt"]["/R"] == revision
    assert len(reader.pages) == 3


def test_encryption_defaults_differ_at_the_api_boundary():
    writer = PdfWriter(THREE)
    writer.encrypt("secret")
    with PdfReader(writer_bytes(writer), password="secret") as reader:
        assert reader.encryption["revision"] == 3
    with PdfEditor(source=THREE) as editor:
        editor.encrypt(user_password="secret")
        with PdfReader(editor.to_bytes(), password="secret") as reader:
            assert reader.encryption["revision"] == 6


def test_strict_reader_rejects_repaired_input():
    damaged = re.sub(rb"startxref\n[0-9]+", b"startxref\n0", THREE)
    assert len(PdfReader(damaged).pages) == 3
    with pytest.raises(PdfLoadError):
        PdfReader(damaged, strict=True)


def test_get_pages_showing_field_accepts_returned_field_dictionary():
    with PdfReader(FORM) as reader:
        field = reader.get_fields()["agree"]
        assert reader.get_pages_showing_field(field)[0].index == 0


@pytest.mark.parametrize("clean", [False, True])
def test_remove_page_clean_controls_stray_references(clean):
    source = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    source.root_object[pypdf.generic.NameObject("/CustomPage")] = source.pages[
        1
    ].indirect_reference
    data = BytesIO()
    source.write(data)
    writer = PdfWriter(data)
    writer.remove_page(1, clean=clean)
    reader = pypdf.PdfReader(BytesIO(writer_bytes(writer)))
    target = (
        reader.trailer["/Root"]
        .get("/CustomPage", pypdf.generic.NullObject())
        .get_object()
    )
    if clean:
        assert isinstance(target, pypdf.generic.NullObject)
    else:
        assert target["/Type"] == "/Page"
