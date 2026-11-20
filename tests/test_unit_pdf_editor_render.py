"""Single-page rendering uses the current editor state and one-based page numbers."""

import pytest

from docling_parse.pdf_editor import PdfEditor
from docling_parse.pdf_errors import PdfEditError
from tests.pdf_builder import simple_page_pdf, stream_object
from tests.test_unit_pdf_editor import BLUE, RED, THREE, WHITE, color_at, rendered


def test_render_page_uses_one_based_page_numbers():
    with PdfEditor(source=THREE) as editor:
        assert editor.render_page(page_no=1, scale=2.0).size == (400, 600)
        assert editor.render_page(page_no=3).size == (220, 300)
        assert editor.is_modified is False
        with pytest.raises(PdfEditError):
            editor.render_page(page_no=4)


def test_render_page_includes_unsaved_changes_and_owns_its_pixels():
    source = simple_page_pdf("1 0 0 rg 20 20 40 40 re f")
    with PdfEditor(source=source) as editor:
        before = editor.render_page(page_no=1)
        assert before.getpixel((30, 170)) == (255, 0, 0, 255)
        editor.rotate_page(page_no=1, angle=90)
        after = editor.render_page(page_no=1)
        assert after.getpixel((30, 30)) == (255, 0, 0, 255)
        assert after.getpixel((30, 170)) == (255, 255, 255, 255)
    assert before.getpixel((30, 170)) == (255, 0, 0, 255)


def test_render_form_clips_to_bbox_in_transformed_form_space():
    source = simple_page_pdf(
        "q 1 0 0 1 10 20 cm /Fm Do Q 1 0 0 rg 125 130 20 20 re f",
        resources="/XObject << /Fm 5 0 R >>",
        extra_objects=[
            stream_object(
                "/Type /XObject /Subtype /Form /BBox [10 10 50 50] "
                "/Matrix [1 0 0 1 40 25]",
                b"0 0 1 rg 0 0 200 200 re f",
            )
        ],
    )
    with PdfEditor(source=source) as editor:
        image = editor.render_page(page_no=1).convert("RGB")
    for result in (image, rendered(source)):
        assert color_at(result, 62, 62) == BLUE
        assert color_at(result, 55, 62) == WHITE
        assert color_at(result, 105, 62) == WHITE
        assert color_at(result, 62, 50) == WHITE
        assert color_at(result, 130, 135) == RED


@pytest.mark.parametrize("page_no", [0, -1])
def test_render_page_rejects_invalid_page_numbers(page_no):
    with PdfEditor(source=THREE) as editor, pytest.raises(ValueError):
        editor.render_page(page_no=page_no)


@pytest.mark.parametrize("scale", [0, -1, float("inf"), float("nan")])
def test_render_page_rejects_invalid_scales(scale):
    with PdfEditor(source=THREE) as editor, pytest.raises(ValueError):
        editor.render_page(page_no=1, scale=scale)


def test_render_page_requires_named_arguments_and_integer_page_number():
    with PdfEditor(source=THREE) as editor:
        with pytest.raises(TypeError):
            getattr(editor, "render_page")(1)
        for page_no in (True, 1.5, "1"):
            with pytest.raises(TypeError):
                getattr(editor, "render_page")(page_no=page_no)
