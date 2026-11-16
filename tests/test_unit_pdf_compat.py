"""The compatibility adapters preserve the pinned pypdf call signatures."""

import inspect
import re
from io import BytesIO

import pypdf
import pytest

from docling_parse import PdfEditor, PdfReader, PdfWriter
from docling_parse.pdf_errors import PdfLoadError
from tests.test_unit_pdf_editor import FORM, THREE, TWO


@pytest.mark.parametrize(
    ("ours", "reference"), [(PdfReader, pypdf.PdfReader), (PdfWriter, pypdf.PdfWriter)]
)
def test_compatibility_method_signatures(ours, reference):
    names = ["__init__"] + [
        name
        for name, method in inspect.getmembers(ours, inspect.isfunction)
        if not name.startswith("_") and callable(getattr(reference, name, None))
    ]
    for name in names:
        actual = inspect.signature(getattr(ours, name)).parameters
        expected = inspect.signature(getattr(reference, name)).parameters
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
        assert editor.get_page(page_no=1).extract_text() == "alpha"
        assert editor.get_page_no(page=editor.pages[0]) == 1
        assert editor.pages[0].extract_text() == "alpha"
        assert editor.get_page(page_no=3).extract_text() == "gamma"
        for page_no in (0, -1, 4):
            with pytest.raises(IndexError):
                editor.get_page(page_no=page_no)
        with pytest.raises(TypeError):
            getattr(editor, "remove_page")(1)


def test_adapters_forward_named_editor_calls(monkeypatch):
    with PdfReader(THREE) as reader, PdfWriter(THREE) as writer:
        calls = []
        sentinel = object()

        def get_page(self, *, page_no):
            calls.append((self._native, page_no))
            return sentinel

        monkeypatch.setattr(PdfEditor, "get_page", get_page)
        assert reader.get_page(1) is sentinel
        assert writer.get_page(0) is sentinel
        assert calls == [(reader._native, 2), (writer._native, 1)]


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
    data = writer.to_bytes()
    reader = pypdf.PdfReader(BytesIO(data), password="secret")
    assert reader.trailer["/Encrypt"]["/R"] == revision
    assert len(reader.pages) == 3


def test_encryption_defaults_differ_at_the_api_boundary():
    writer = PdfWriter(THREE)
    writer.encrypt("secret")
    with PdfReader(writer.to_bytes(), password="secret") as reader:
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
    reader = pypdf.PdfReader(BytesIO(writer.to_bytes()))
    target = (
        reader.trailer["/Root"]
        .get("/CustomPage", pypdf.generic.NullObject())
        .get_object()
    )
    if clean:
        assert isinstance(target, pypdf.generic.NullObject)
    else:
        assert target["/Type"] == "/Page"
