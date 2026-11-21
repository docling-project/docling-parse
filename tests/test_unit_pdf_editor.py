#!/usr/bin/env python
"""Exercise the public PdfEditor workflows and read back the written PDFs.

The fixtures identify pages by size and text. Where available, pypdf provides
an independent structural check. The public migration API is also exercised
in test_unit_pdf_compat.py.
"""

from __future__ import annotations

import re
import subprocess
import sys
import threading
from io import BytesIO
from types import ModuleType

import pytest

from docling_parse import PdfEditor, PdfExtractionMode
from docling_parse.pdf_errors import (
    PdfEditError,
    PdfEditErrorCategory,
    PdfLoadError,
    PdfPasswordError,
    PdfStaleHandleError,
    PdfUnsupportedError,
    PdfWriteError,
)
from docling_parse.pdf_reader import PdfReader
from docling_parse.pdf_writer import PdfWriter
from tests.outline_fixtures import build_destination_kinds
from tests.pdf_builder import simple_page_pdf
from tests.test_unit_acroform_widgets import _form_pdf
from tests.test_unit_structure_tree import _tagged_pdf

pypdf: ModuleType | None
try:
    import pypdf
except ImportError:  # pragma: no cover - depends on the installed groups
    pypdf = None

GARBAGE = b"%PDF-1.4\nthis is not a valid pdf at all"


def _source_page_nos(source, selection):
    with PdfReader(source) as reader:
        count = len(reader.pages)
    indices = (
        range(*selection.indices(count)) if isinstance(selection, slice) else selection
    )
    return [index + count + 1 if index < 0 else index + 1 for index in indices]


def make_pdf(
    labels: list[str], *, title: str = "fixture", rotate: int = 0, y: int = 150
) -> bytes:
    """One page per label. Page i is (200 + 10*i) x 300 and shows its label
    at height `y`.

    Objects: 1 catalog, 2 page tree, 3 font, 4 info, then a page and its
    content stream per label.
    """
    first_page = 5
    kids = " ".join(f"{first_page + 2 * i} 0 R" for i in range(len(labels)))
    rotate_entry = f" /Rotate {rotate}" if rotate else ""

    objects: list[str] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        f"<< /Type /Pages /Kids [{kids}] /Count {len(labels)} >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        f"<< /Title ({title}) /Author (docling-parse tests) >>",
    ]
    for i, label in enumerate(labels):
        content = f"BT /F1 24 Tf 20 {y} Td ({label}) Tj ET"
        objects.append(
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {200 + 10 * i} 300]"
            f"{rotate_entry} /Resources << /Font << /F1 3 0 R >> >> "
            f"/Contents {first_page + 2 * i + 1} 0 R >>"
        )
        objects.append(f"<< /Length {len(content)} >>\nstream\n{content}\nendstream")

    out = b"%PDF-1.7\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R /Info 4 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


def widths(document) -> list[float]:
    if isinstance(document, PdfEditor):
        return [
            document.get_page(page_no=page_no).mediabox.width
            for page_no in range(1, document.page_count() + 1)
        ]
    return [page.mediabox.width for page in document.pages]


def written_widths(data: bytes) -> list[float]:
    """Page widths of a written file, as both readers see them."""
    with PdfReader(data) as reader:
        result = widths(reader)
    if pypdf is not None:
        pages = pypdf.PdfReader(BytesIO(data)).pages
        assert [float(page.mediabox.width) for page in pages] == result
    return result


def written_rotations(data: bytes) -> list[int]:
    """Page rotations of a written file, as both readers see them."""
    with PdfReader(data) as reader:
        result = [page.rotation for page in reader.pages]
    if pypdf is not None:
        pages = pypdf.PdfReader(BytesIO(data)).pages
        assert [page.rotation for page in pages] == result
    return result


def test_pypdf_is_available_as_second_reader():
    """Skipped, visibly, when the written files are checked by one reader only."""
    pytest.importorskip("pypdf")


THREE = make_pdf(["alpha", "beta", "gamma"])
TWO = make_pdf(["delta", "epsilon"], title="second")

# Text low on the page, so it shares no line with the labels of THREE.
STAMP = make_pdf(["STAMP"], y=40)

# 2x2 uncompressed RGB samples: red, green / blue, white.
PIXELS = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])


def make_image_pdf() -> bytes:
    """One 200x300 page drawing a 2x2 image into [20 30 120 130]."""
    content = "q 100 0 0 100 20 30 cm /Im1 Do Q"
    image = (
        b"<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
        b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\nstream\n"
        + PIXELS
        + b"\nendstream"
    )
    objects: list[bytes] = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
        b"/Resources << /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream".encode(),
        image,
    ]

    out = b"%PDF-1.7\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n".encode() + obj + b"\nendobj\n"

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode()
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode()
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode()
    return out


IMAGE = make_image_pdf()


def lines(page) -> set[str]:
    """The page's text lines, without relying on their order."""
    return set(page.extract_text().split("\n"))


# ---------------------------------------------------------------- reader


def test_reader_inspects_without_decoding():
    with PdfReader(THREE) as reader:
        assert len(reader.pages) == 3
        assert reader.pdf_version == "1.7"
        assert reader.is_encrypted is False
        assert reader.encryption is None
        assert reader.metadata == {
            "Title": "fixture",
            "Author": "docling-parse tests",
        }
        assert reader.xmp_metadata is None
        assert widths(reader) == [200.0, 210.0, 220.0]


def test_reader_page_geometry():
    with PdfReader(make_pdf(["only"], rotate=90)) as reader:
        page = reader.pages[0]
        assert page.mediabox == (0.0, 0.0, 200.0, 300.0)
        assert page.mediabox.height == 300.0
        # no /CropBox in the file: it defaults to the media box
        assert page.cropbox == page.mediabox
        assert page.trimbox == page.mediabox
        assert page.rotation == 90
        assert page.user_unit == 1.0


def test_reader_page_sequence():
    with PdfReader(THREE) as reader:
        assert reader.pages[-1].index == 2
        assert [page.index for page in reader.pages[1:]] == [1, 2]
        assert [page.index for page in reader.pages] == [0, 1, 2]
        with pytest.raises(IndexError):
            reader.pages[3]


def test_reader_extracts_text_per_page():
    with PdfReader(THREE) as reader:
        assert [page.extract_text() for page in reader.pages] == [
            "alpha",
            "beta",
            "gamma",
        ]


def test_layout_text_keeps_columns_and_spacing():
    content = (
        "BT /F1 12 Tf 20 250 Td (left) Tj ET "
        "BT /F1 12 Tf 150 250 Td (right) Tj ET "
        "BT /F1 12 Tf 100 150 Td (indented) Tj ET"
    )
    pdf = simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 300]",
        extra_objects=["<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"],
    )

    with PdfReader(pdf) as reader:
        page = reader.pages[0]
        plain = page.extract_text()
        layout = page.extract_text(extraction_mode="layout")

        with pytest.raises(ValueError):
            page.extract_text(extraction_mode="fancy")

    with PdfEditor(source=pdf) as editor:
        assert editor.extract_text(page_no=1) == plain
        assert (
            editor.extract_text(page_no=1, extraction_mode=PdfExtractionMode.LAYOUT)
            == layout
        )

    assert {"left", "right", "indented"} <= set(plain.split())

    rows = layout.split("\n")
    first, last = rows[0], rows[-1]

    # one row for the two runs that share a baseline, the gap between them kept
    assert first.startswith("left")
    assert first.rstrip().endswith("right")
    assert first.index("right") - len("left") >= 5

    # the lower run keeps its indentation and its distance from the first row
    assert last.strip() == "indented"
    assert last.startswith("     ")
    assert len(rows) >= 3 and all(row == "" for row in rows[1:-1])


def test_layout_text_preserves_gutters_without_padding_ordinary_rows():
    content = (
        "BT /F1 24 Tf 20 195 Td (Wide title) Tj ET "
        "BT /F1 12 Tf 20 160 Td (Left one) Tj 0 -16 Td (Left two) Tj "
        "0 -16 Td (Left three) Tj ET "
        "BT /F1 12 Tf 140 160 Td (Right one) Tj 0 -16 Td (Right two) Tj "
        "0 -16 Td (Right three) Tj ET "
        "BT /F1 12 Tf 20 80 Td (Later block) Tj ET"
    )
    pdf = simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 240]",
        extra_objects=["<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"],
    )

    with PdfEditor(source=pdf) as editor:
        layout = editor.extract_text(
            page_no=1, extraction_mode=PdfExtractionMode.LAYOUT
        )

    rows = layout.splitlines()
    assert rows[0] == "Wide title"
    first_body_row = next(
        index for index, row in enumerate(rows) if row.startswith("Left")
    )
    assert rows[first_body_row:] == [
        f"{'Left one':20}Right one",
        f"{'Left two':20}Right two",
        f"{'Left three':20}Right three",
        "",
        "",
        "Later block",
    ]


@pytest.mark.parametrize("kind", ["bytes", "stream", "path", "reader"])
def test_reader_sources(kind, tmp_path):
    if kind == "bytes":
        source = THREE
    elif kind == "stream":
        source = BytesIO(THREE)
    elif kind == "path":
        source = tmp_path / "three.pdf"
        source.write_bytes(THREE)
    else:
        source = PdfReader(THREE)

    with PdfReader(source) as reader:
        assert widths(reader) == [200.0, 210.0, 220.0]

    if kind == "stream":
        # the stream is the caller's: read, not closed
        assert not source.closed


def test_reader_reports_load_failure(tmp_path):
    with pytest.raises(PdfLoadError):
        PdfReader(GARBAGE)
    with pytest.raises(PdfLoadError):
        PdfReader(tmp_path / "missing.pdf")


def test_reader_has_no_editing_surface():
    with PdfReader(THREE) as reader:
        for name in ("append", "write", "remove_page", "add_blank_page"):
            assert not hasattr(reader, name)
        assert not hasattr(reader.pages[0], "rotate")
        with pytest.raises(TypeError):
            del reader.pages[0]
        for name in ("write_to_bytes", "remove_page", "append_pages", "create_empty"):
            assert not hasattr(reader._native, name)


# ---------------------------------------------------------------- images


def test_reader_exports_page_images():
    with PdfReader(IMAGE) as reader:
        images = reader.pages[0].images

    assert len(images) == 1
    image = images[0]

    assert "Im1" in image.name
    assert (image.width, image.height) == (2, 2)
    assert image.mode == "RGB"
    assert image.format == "raw"
    assert image.extension == ".bin"
    assert image.data == PIXELS
    assert image.bbox == pytest.approx((20.0, 30.0, 120.0, 130.0))
    assert image.bbox.width == pytest.approx(100.0)

    assert image.to_pil().size == (2, 2)


def test_image_export_saves_bytes(tmp_path):
    with PdfReader(IMAGE) as reader:
        image = reader.pages[0].images[0]

    target = tmp_path / f"image{image.extension}"
    image.save(target)
    assert target.read_bytes() == image.data


def test_text_and_images_of_one_page_do_not_shadow_each_other():
    """Both decode the page, with different settings: asking for one must not
    hand back what was decoded for the other."""
    with PdfReader(IMAGE) as reader:
        page = reader.pages[0]
        assert page.extract_text() == ""
        assert len(page.images) == 1
        assert page.extract_text() == ""
        assert len(page.images) == 1

    with PdfReader(THREE) as reader:
        assert reader.pages[0].images == []
        assert reader.pages[0].extract_text() == "alpha"


# ---------------------------------------------------------------- writing


def test_clone_round_trips_unchanged():
    document = PdfEditor(source=THREE)
    data = document.to_bytes()

    assert written_widths(data) == [200.0, 210.0, 220.0]
    with PdfReader(data) as reader:
        assert reader.metadata["Title"] == "fixture"
        assert [page.extract_text() for page in reader.pages] == [
            "alpha",
            "beta",
            "gamma",
        ]


def test_remove_reorder_rotate():
    document = PdfEditor(source=THREE)

    document.rotate_page(page_no=1, angle=90)
    document.rotate_page(page_no=1, angle=90)
    document.remove_page(page_no=2)
    document.move_page(page_no=1, to_page_no=2)

    assert widths(document) == [220.0, 200.0]
    assert [
        document.get_page(page_no=page_no).rotation
        for page_no in range(1, document.page_count() + 1)
    ] == [0, 180]

    data = document.to_bytes()
    assert written_widths(data) == [220.0, 200.0]
    assert written_rotations(data) == [0, 180]

    # the source bytes are untouched
    with PdfReader(THREE) as reader:
        assert widths(reader) == [200.0, 210.0, 220.0]


def test_page_boxes_and_absolute_rotation():
    document = PdfEditor(source=THREE)
    document.set_page_box(page_no=3, box="crop", bounds=(10, 20, 110, 220))
    document.set_page_rotation(page_no=3, angle=270)

    with PdfReader(document.to_bytes()) as reader:
        written = reader.pages[2]
        assert written.cropbox == (10.0, 20.0, 110.0, 220.0)
        assert written.mediabox == (0.0, 0.0, 220.0, 300.0)
        assert written.rotation == 270

    with pytest.raises(PdfEditError) as excinfo:
        document.rotate_page(page_no=3, angle=45)
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(PdfEditError):
        document.set_page_box(page_no=3, box="crop", bounds=(10, 10, 10, 50))


def test_assemble_from_several_sources():
    document = PdfEditor()
    assert document.page_count() == 0

    document.append(source=THREE)
    document.append(
        source=PdfReader(TWO), page_nos=_source_page_nos(PdfReader(TWO), [1, 0])
    )
    document.append(
        source=BytesIO(THREE),
        page_nos=_source_page_nos(BytesIO(THREE), slice(0, 1)),
        before_page_no=1,
    )
    blank = document.add_blank_page(width=100, height=150)
    assert blank.mediabox == (0.0, 0.0, 100.0, 150.0)
    document.insert_blank_page(before_page_no=2, width=50, height=60)

    expected = [200.0, 50.0, 200.0, 210.0, 220.0, 210.0, 200.0, 100.0]
    assert widths(document) == expected

    data = document.to_bytes()
    assert written_widths(data) == expected
    with PdfReader(data) as reader:
        texts = [page.extract_text() for page in reader.pages]
    assert texts == ["alpha", "", "alpha", "beta", "gamma", "epsilon", "delta", ""]


def test_editor_page_by_page_assembly():
    """The recipes people know: add_page in a loop, insert_page, merge."""
    reader = PdfReader(THREE)

    document = PdfEditor()
    for page in reader.pages:
        added = document.add_page(page=page)
        assert added.page_no == document.page_count()
    document.insert_page(page=PdfReader(TWO).pages[1], before_page_no=1)
    document.merge(
        before_page_no=2,
        fileobj=TWO,
        page_nos=_source_page_nos(TWO, [0]),
    )

    assert document.page_count() == 5
    assert widths(document) == [210.0, 200.0, 200.0, 210.0, 220.0]
    assert document.get_page(page_no=5).mediabox.width == 220.0
    assert reader.pdf_header == "%PDF-1.7"

    other = PdfEditor(source=TWO)
    stale = other.get_page(page_no=2)
    other.remove_page(page_no=1)
    with pytest.raises(PdfStaleHandleError):
        document.add_page(page=stale)

    assert written_widths(document.to_bytes()) == [210.0, 200.0, 200.0, 210.0, 220.0]


def test_scale_to():
    document = PdfEditor(source=THREE)
    document.scale_page_to(page_no=2, width=105, height=600)

    assert document.get_page(page_no=2).mediabox == pytest.approx(
        (0.0, 0.0, 105.0, 600.0)
    )
    assert widths(document) == pytest.approx([200.0, 105.0, 220.0])


def test_append_same_page_twice_gives_independent_pages():
    document = PdfEditor()
    document.append(source=THREE, page_nos=_source_page_nos(THREE, [0, 0]))
    document.rotate_page(page_no=2, angle=90)

    assert [
        document.get_page(page_no=page_no).rotation
        for page_no in range(1, document.page_count() + 1)
    ] == [0, 90]
    data = document.to_bytes()
    assert written_rotations(data) == [0, 90]


def test_append_from_edited_document_and_from_itself():
    other = PdfEditor(source=TWO)
    other.remove_page(page_no=1)

    document = PdfEditor(source=THREE)
    document.append(source=other)
    document.append(source=document, page_nos=_source_page_nos(document, [0]))

    assert widths(document) == [200.0, 210.0, 220.0, 210.0, 200.0]


def test_text_of_edited_document():
    document = PdfEditor(source=THREE)
    document.remove_page(page_no=1)
    document.append(source=TWO, page_nos=_source_page_nos(TWO, [0]))

    assert [
        document.extract_text(page_no=page_no)
        for page_no in range(1, document.page_count() + 1)
    ] == [
        "beta",
        "gamma",
        "delta",
    ]


def test_duplicate_page():
    document = PdfEditor(source=THREE)

    copy = document.duplicate_page(page_no=1)
    assert copy.page_no == 4
    document.rotate_page(page_no=copy.page_no, angle=90)

    front = document.duplicate_page(page_no=3, before_page_no=1)
    assert front.page_no == 1

    assert widths(document) == [220.0, 200.0, 210.0, 220.0, 200.0]
    # the copy was rotated, the page it was copied from was not
    assert [
        document.get_page(page_no=page_no).rotation
        for page_no in range(1, document.page_count() + 1)
    ] == [0, 0, 0, 0, 90]

    data = document.to_bytes()
    assert written_widths(data) == [220.0, 200.0, 210.0, 220.0, 200.0]
    assert written_rotations(data) == [0, 0, 0, 0, 90]
    with PdfReader(data) as reader:
        assert reader.pages[4].extract_text() == "alpha"


@pytest.mark.parametrize("over", [True, False])
def test_merge_page_draws_both_pages(over):
    stamp = PdfReader(STAMP).pages[0]

    document = PdfEditor(source=THREE)
    document.overlay_page(page_no=1, source_page=stamp, over=over)

    assert set(document.extract_text(page_no=1).splitlines()) == {"alpha", "STAMP"}
    assert set(document.extract_text(page_no=2).splitlines()) == {"beta"}
    # merging draws on the page; it does not resize it
    assert widths(document) == [200.0, 210.0, 220.0]

    data = document.to_bytes()
    assert written_widths(data) == [200.0, 210.0, 220.0]
    with PdfReader(data) as reader:
        assert lines(reader.pages[0]) == {"alpha", "STAMP"}


def test_watermark_every_page():
    stamp = PdfReader(STAMP).pages[0]

    document = PdfEditor(source=THREE)
    for page_no in range(1, document.page_count() + 1):
        document.overlay_page(page_no=page_no, source_page=stamp, over=False)

    with PdfReader(document.to_bytes()) as reader:
        assert [lines(page) for page in reader.pages] == [
            {"alpha", "STAMP"},
            {"beta", "STAMP"},
            {"gamma", "STAMP"},
        ]


def test_merge_page_fit_scales_into_the_page():
    """The 2x2 image fills [20 30 120 130] of a 200x300 page. Fitted onto a
    400x600 page, that page is drawn at twice the size."""
    document = PdfEditor()
    document.add_blank_page(width=400, height=600)
    document.overlay_page(page_no=1, source_page=PdfReader(IMAGE).pages[0], fit=True)

    image = document.get_page(page_no=1).images[0]
    assert image.bbox == pytest.approx((40.0, 60.0, 240.0, 260.0))


def test_merge_page_from_own_document_and_twice():
    document = PdfEditor(source=THREE)
    document.overlay_page(page_no=1, source_page=PdfReader(STAMP).pages[0])
    document.overlay_page(page_no=1, source_page=PdfReader(TWO).pages[1])
    document.overlay_page(page_no=3, source_page=document.get_page(page_no=2))

    assert set(document.extract_text(page_no=1).splitlines()) >= {"STAMP"}
    assert len(document.to_bytes()) > 0
    assert written_widths(document.to_bytes()) == [200.0, 210.0, 220.0]


def test_merge_page_rejects_stale_and_closed_sources():
    other = PdfEditor(source=TWO)
    handle = other.get_page(page_no=2)
    other.remove_page(page_no=1)

    document = PdfEditor(source=THREE)
    with pytest.raises(PdfStaleHandleError):
        document.overlay_page(page_no=1, source_page=handle)

    reader = PdfReader(STAMP)
    stamp = reader.pages[0]
    reader.close()
    with pytest.raises(PdfEditError):
        document.overlay_page(page_no=1, source_page=stamp)

    # nothing was drawn by the failed calls
    assert set(document.extract_text(page_no=1).splitlines()) == {"alpha"}


def test_scale_page():
    document = PdfEditor(source=IMAGE)
    page = document.get_page(page_no=1)

    document.scale_page(page_no=1, sx=2, sy=0.5)
    assert page.mediabox == pytest.approx((0.0, 0.0, 400.0, 150.0))
    assert page.cropbox == pytest.approx((0.0, 0.0, 400.0, 150.0))
    assert page.images[0].bbox == pytest.approx((40.0, 15.0, 240.0, 65.0))

    document.scale_page(page_no=1, sx=0.5, sy=0.5)
    assert page.mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))

    with PdfReader(document.to_bytes()) as reader:
        assert reader.pages[0].mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))
        assert reader.pages[0].images[0].bbox == pytest.approx((20.0, 7.5, 120.0, 32.5))

    with pytest.raises(PdfEditError):
        document.scale_page(page_no=1, sx=0, sy=0)
    # the rejected call changed nothing
    assert page.mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))


def test_add_transformation():
    document = PdfEditor(source=IMAGE)
    page = document.get_page(page_no=1)

    document.transform_page(page_no=1, ctm=(1, 0, 0, 1, 10, 5))
    # content moves, the page boundary does not
    assert page.mediabox == (0.0, 0.0, 200.0, 300.0)
    assert page.images[0].bbox == pytest.approx((30.0, 35.0, 130.0, 135.0))

    with pytest.raises(PdfEditError) as excinfo:
        document.transform_page(page_no=1, ctm=(1, 2, 2, 4, 0, 0))
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(ValueError):
        document.transform_page(page_no=1, ctm=(1, 0, 0, 1))


def test_stale_page_handle():
    document = PdfEditor(source=THREE)
    first = document.get_page(page_no=1)
    last = document.get_page(page_no=3)

    # in-place edits and appending at the end keep indices meaningful
    document.rotate_page(page_no=1, angle=90)
    document.add_blank_page(width=10, height=10)
    assert last.mediabox.width == 220.0

    document.remove_page(page_no=2)

    with pytest.raises(PdfStaleHandleError):
        last.mediabox
    with pytest.raises(PdfStaleHandleError):
        first.annotations
    with pytest.raises(PdfStaleHandleError):
        first.images

    assert document.get_page(page_no=1).rotation == 90


def test_write_to_path_and_stream(tmp_path):
    document = PdfEditor(source=THREE)
    document.remove_page(page_no=1)

    target = tmp_path / "out.pdf"
    target.write_bytes(b"previous content")
    document.write(target=target)
    assert written_widths(target.read_bytes()) == [210.0, 220.0]
    assert [p.name for p in tmp_path.iterdir()] == ["out.pdf"]

    stream = BytesIO()
    document.write(target=stream)
    assert not stream.closed
    assert stream.getvalue() == target.read_bytes()

    with pytest.raises(PdfWriteError):
        document.write(target=tmp_path / "no-such-directory" / "out.pdf")


def test_write_over_the_source_path(tmp_path):
    path = tmp_path / "document.pdf"
    path.write_bytes(THREE)

    with PdfEditor(source=path) as editor:
        editor.remove_page(page_no=1)
        editor.write(target=path)

    assert written_widths(path.read_bytes()) == [210.0, 220.0]


# ---------------------------------------------------------------- document level


def reread(document):
    """What a fresh reader sees in the document as written."""
    return PdfReader(document.to_bytes())


def test_write_metadata():
    document = PdfEditor(source=THREE)
    document.add_metadata(
        infos={"/Title": "New title", "Subject": "Ünïcode ✓", "Author": None}
    )

    expected = {"Title": "New title", "Subject": "Ünïcode ✓"}
    assert document.metadata == expected
    with reread(document) as reader:
        assert reader.metadata == expected

    if pypdf is not None:
        info = pypdf.PdfReader(BytesIO(document.to_bytes())).metadata
        assert info.title == "New title"
        assert info.subject == "Ünïcode ✓"
        assert info.author is None

    with pytest.raises(PdfEditError):
        document.add_metadata(infos={"": "nameless"})


def test_write_xmp_metadata():
    packet = '<x:xmpmeta xmlns:x="adobe:ns:meta/">é</x:xmpmeta>'

    document = PdfEditor(source=THREE)
    assert document.xmp_metadata is None

    document.set_xmp_metadata(packet=packet)
    with reread(document) as reader:
        assert reader.xmp_metadata == packet

    document.set_xmp_metadata(packet=None)
    with reread(document) as reader:
        assert reader.xmp_metadata is None


def test_viewer_settings():
    document = PdfEditor(source=THREE)
    assert document.page_layout is None
    assert document.page_mode is None
    assert document.viewer_preferences is None
    assert document.open_destination is None

    document.set_page_layout(layout="/TwoColumnLeft")
    document.set_page_mode(mode="UseOutlines")
    document.set_viewer_preferences(
        preferences={"/HideToolbar": True, "Direction": "/R2L"}
    )
    document.set_open_destination(page_no=3)

    with reread(document) as reader:
        assert reader.page_layout == "/TwoColumnLeft"
        assert reader.page_mode == "/UseOutlines"
        assert reader.viewer_preferences == {"/HideToolbar": True, "/Direction": "/R2L"}
        assert reader.open_destination == {"page": 2, "kind": "/Fit", "arguments": []}

    with pytest.raises(PdfEditError) as excinfo:
        document.set_page_layout(layout="/Sideways")
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    document.set_page_layout(layout=None)
    document.set_page_mode(mode=None)
    document.set_viewer_preferences(preferences=None)
    document.set_open_destination(page_no=None)

    with reread(document) as reader:
        assert reader.page_layout is None
        assert reader.page_mode is None
        assert reader.viewer_preferences is None
        assert reader.open_destination is None


def test_page_labels():
    document = PdfEditor(source=make_pdf([f"p{i}" for i in range(6)]))
    assert document.page_labels == ["1", "2", "3", "4", "5", "6"]

    # front matter in roman numerals; the rest keeps counting 3, 4, ...
    document.set_page_label(
        first_page_no=1,
        last_page_no=2,
        style="/r",
    )
    assert document.page_labels == ["i", "ii", "3", "4", "5", "6"]

    document.set_page_label(
        first_page_no=3,
        last_page_no=4,
        style="/D",
        prefix="A-",
        start=5,
    )
    assert document.page_labels == ["i", "ii", "A-5", "A-6", "5", "6"]

    document.set_page_label(
        first_page_no=6,
        last_page_no=6,
        style="/A",
        start=27,
    )
    expected = ["i", "ii", "A-5", "A-6", "5", "AA"]
    assert document.page_labels == expected

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert reader.page_labels == expected
    if pypdf is not None:
        assert pypdf.PdfReader(BytesIO(data)).page_labels == expected

    with pytest.raises(PdfEditError):
        document.set_page_label(
            first_page_no=1,
            last_page_no=2,
            style="/X",
        )
    with pytest.raises(IndexError):
        document.set_page_label(
            first_page_no=1,
            last_page_no=10,
            style="/D",
        )


def test_named_destinations():
    document = PdfEditor(source=THREE)
    assert document.named_destinations == {}

    document.add_named_destination(title="chapter two", page_no=2)
    document.add_named_destination(title="end", page_no=document.page_count())

    with reread(document) as reader:
        destinations = reader.named_destinations
    assert destinations == {
        "chapter two": {"page": 1, "kind": "/Fit", "arguments": []},
        "end": {"page": 2, "kind": "/Fit", "arguments": []},
    }


def flat_outline(items, depth: int = 0) -> list[tuple[int, str, int | None]]:
    """(depth, title, page) of every outline item, in reading order."""
    result = []
    for item in items:
        result.append((depth, item["title"], item["page"]))
        result.extend(flat_outline(item["children"], depth + 1))
    return result


def pypdf_flat_outline(data: bytes) -> list[tuple[int, str, int | None]]:
    assert pypdf is not None
    reader = pypdf.PdfReader(BytesIO(data))

    def walk(items, depth):
        result = []
        for item in items:
            if isinstance(item, list):
                result.extend(walk(item, depth + 1))
            else:
                result.append(
                    (depth, item.title, reader.get_destination_page_number(item))
                )
        return result

    return walk(reader.outline, 0)


def test_build_an_outline():
    document = PdfEditor(source=THREE)
    assert document.outline == []

    one = document.add_outline_item(title="One", page_no=1)
    document.add_outline_item(title="Sub ö", page_no=2, parent=one)
    document.add_outline_item(title="Sub two", page_no=3, parent=one)
    document.add_outline_item(title="Two", page_no=3)

    expected = [(0, "One", 0), (1, "Sub ö", 1), (1, "Sub two", 2), (0, "Two", 2)]
    assert flat_outline(document.outline) == [
        (0, "One", 1),
        (1, "Sub ö", 2),
        (1, "Sub two", 3),
        (0, "Two", 3),
    ]

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert flat_outline(reader.outline) == expected
    if pypdf is not None:
        assert pypdf_flat_outline(data) == expected

    with pytest.raises(PdfEditError):
        document.add_outline_item(title="orphan", page_no=1, parent=987654)


def test_removing_a_page_unhooks_its_bookmarks():
    document = PdfEditor(source=THREE)
    document.add_outline_item(title="One", page_no=1)
    document.add_outline_item(title="Two", page_no=2)
    document.add_outline_item(title="Three", page_no=3)
    document.set_open_destination(page_no=2)

    document.remove_page(page_no=2)

    # the bookmark stays as a heading; the ones behind it follow their pages
    expected = [(0, "One", 0), (0, "Two", None), (0, "Three", 1)]
    with reread(document) as reader:
        assert flat_outline(reader.outline) == expected
        assert reader.open_destination is None
        assert len(reader.pages) == 2


def test_append_brings_the_outline_of_copied_pages():
    """build_destination_kinds: three pages, twelve bookmarks, eleven of them
    on the first page and one without a destination."""
    source = build_destination_kinds()
    with PdfReader(source) as reader:
        titles = [item["title"] for item in reader.outline]
        pages = [item["page"] for item in reader.outline]
    assert len(titles) == 12
    assert pages == [0] * 11 + [None]

    document = PdfEditor(source=THREE)
    document.append(source=source, page_nos=_source_page_nos(source, [1, 0]))

    # the first page of the source is now page 5 of the document
    assert flat_outline(document.outline) == [(0, title, 5) for title in titles[:11]]

    without = PdfEditor(source=THREE)
    without.append(source=source, page_nos=_source_page_nos(source, [1, 2]))
    assert without.outline == []

    declined = PdfEditor(source=THREE)
    declined.append(source=source, import_outline=False)
    assert declined.outline == []


# ---------------------------------------------------------------- forms


def make_form_pdf() -> bytes:
    """One 200x200 page with a text field "name" (value "Ada") and a
    checkbox "agree" whose on state is /Yes, drawn as a black square."""
    on = "0 g 2 2 16 16 re f"
    objects: list[str] = [
        "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R 6 0 R] "
        "/DA (/Helv 12 Tf 0 g) /DR << /Font << /Helv 3 0 R >> >> >> >>",
        "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        "/Resources << /Font << /Helv 3 0 R >> >> /Contents 9 0 R "
        "/Annots [5 0 R 6 0 R] >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /V (Ada) "
        "/Rect [20 150 180 170] /P 4 0 R /F 4 /DA (/Helv 12 Tf 0 g) >>",
        "<< /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /V /Off /AS /Off "
        "/Rect [20 100 40 120] /P 4 0 R /F 4 "
        "/AP << /N << /Yes 7 0 R /Off 8 0 R >> >> >>",
        f"<< /Type /XObject /Subtype /Form /BBox [0 0 20 20] /Length {len(on)} >>"
        f"\nstream\n{on}\nendstream",
        "<< /Type /XObject /Subtype /Form /BBox [0 0 20 20] /Length 0 >>"
        "\nstream\n\nendstream",
        "<< /Length 0 >>\nstream\n\nendstream",
    ]

    out = b"%PDF-1.7\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


FORM = make_form_pdf()


def test_reader_lists_form_fields():
    with PdfReader(FORM) as reader:
        assert reader.has_form is True
        assert reader.has_xfa is False

        fields = reader.get_fields()
        assert set(fields) == {"name", "agree"}

        name = fields["name"]
        assert (name["kind"], name["type"], name["value"]) == ("text", "/Tx", "Ada")
        assert name["pages"] == [0]
        assert name["read_only"] is False

        agree = fields["agree"]
        assert (agree["kind"], agree["value"]) == ("checkbox", "/Off")
        assert agree["states"] == ["/Yes"]

        assert reader.get_form_text_fields() == {"name": "Ada"}

    with PdfReader(THREE) as reader:
        assert reader.has_form is False
        assert reader.get_fields() == {}


def test_fill_form_fields():
    document = PdfEditor(source=FORM)
    assert (
        document.update_form_field_values(
            fields={"name": "Grace Hopper", "agree": True}
        )
        == 2
    )

    data = document.to_bytes()
    with PdfReader(data) as reader:
        fields = reader.get_fields()
        assert fields["name"]["value"] == "Grace Hopper"
        assert fields["agree"]["value"] == "/Yes"
        # the widget shows the state its field has
        widgets = [a for a in reader.pages[0].annotations if a["/Subtype"] == "/Widget"]
        assert [w.get("/AS") for w in widgets if w.get("/T") == "agree"] == ["/Yes"]

    if pypdf is not None:
        assert pypdf.PdfReader(BytesIO(data)).get_form_text_fields() == {
            "name": "Grace Hopper"
        }

    assert document.update_form_field_values(fields={"agree": False}) == 1
    with reread(document) as reader:
        assert reader.get_fields()["agree"]["value"] == "/Off"


def test_fill_form_rejects_what_it_cannot_set():
    document = PdfEditor(source=FORM)

    with pytest.raises(PdfEditError) as excinfo:
        document.update_form_field_values(fields={"no such field": "x"})
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(PdfEditError):
        document.update_form_field_values(fields={"agree": "/Maybe"})

    with pytest.raises(PdfEditError):
        PdfEditor(source=THREE).update_form_field_values(fields={"name": "x"})

    # nothing was changed by the rejected calls
    with reread(document) as reader:
        assert reader.get_form_text_fields() == {"name": "Ada"}


def test_flatten_form_into_page_content():
    document = PdfEditor(source=FORM)
    document.update_form_field_values(
        fields={"name": "Grace Hopper", "agree": True}, flatten=True
    )

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert reader.has_form is False
        assert reader.pages[0].annotations == []
        # the value is ordinary page text now
        assert "Grace Hopper" in reader.pages[0].extract_text()

    # the checkbox's on appearance fills [22 38] x [102 118] of the page
    image = rendered(data)
    assert color_at(image, 30, 110) == (0, 0, 0)
    assert color_at(image, 100, 60) == WHITE


# ---------------------------------------------------------------- annotations


def test_add_read_and_remove_annotations():
    document = PdfEditor(source=THREE)
    assert document.get_page(page_no=1).annotations == []

    document.add_uri(
        page_no=1,
        uri="https://example.org/a b",
        rect=(10, 10, 60, 30),
    )
    document.add_link(
        page_no=1,
        rect=(10, 40, 60, 60),
        target_page_no=3,
    )
    document.add_annotation(
        page_no=2,
        annotation={
            "/Subtype": "/Text",
            "/Rect": [50, 200, 70, 220],
            "/Contents": "Nöte ✓",
        },
    )

    data = document.to_bytes()
    with PdfReader(data) as reader:
        first, second = reader.pages[0].annotations
        assert first["/Subtype"] == "/Link"
        assert first["/Rect"] == [10, 10, 60, 30]
        assert first["/A"]["/URI"] == "https://example.org/a b"
        assert "page" not in first

        assert second["/Subtype"] == "/Link"
        assert second["page"] == 2

        (note,) = reader.pages[1].annotations
        assert note["/Subtype"] == "/Text"
        assert note["/Contents"] == "Nöte ✓"
        assert note["/Type"] == "/Annot"

        assert reader.pages[2].annotations == []

    if pypdf is not None:
        page = pypdf.PdfReader(BytesIO(data)).pages[0]
        assert [a.get_object()["/Subtype"] for a in page["/Annots"]] == [
            "/Link",
            "/Link",
        ]

    assert document.remove_links() == 2
    assert document.get_page(page_no=1).annotations == []
    assert len(document.get_page(page_no=2).annotations) == 1

    assert document.remove_annotations() == 1
    assert document.get_page(page_no=2).annotations == []


def test_add_annotation_validates_its_input():
    document = PdfEditor(source=THREE)

    with pytest.raises(PdfEditError):
        document.add_annotation(page_no=1, annotation={"/Rect": [0, 0, 10, 10]})
    with pytest.raises(PdfEditError):
        document.add_annotation(page_no=1, annotation={"/Subtype": "/Text"})
    with pytest.raises(IndexError):
        document.add_annotation(
            page_no=8,
            annotation={"/Subtype": "/Text", "/Rect": [0, 0, 1, 1]},
        )
    with pytest.raises(TypeError):
        document.add_annotation(
            page_no=1,
            annotation={"/Subtype": "/Text", "/Rect": object()},
        )

    assert document.get_page(page_no=1).annotations == []


def test_removing_widgets_removes_their_fields():
    document = PdfEditor(source=FORM)

    # form fields are kept unless they are asked for by name
    assert document.remove_annotations() == 0
    assert set(document.get_fields()) == {"name", "agree"}

    assert document.remove_annotations(subtypes="/Widget") == 2
    with reread(document) as reader:
        assert reader.get_fields() == {}
        assert reader.pages[0].annotations == []


# ---------------------------------------------------------------- attachments


def test_attachments():
    document = PdfEditor(source=THREE)
    assert document.attachments == {}

    document.add_attachment(
        filename="notes.txt",
        data=b"hello",
        description="a note",
        mime_type="text/plain",
    )
    document.add_attachment(filename="data ü.bin", data=bytes(range(256)))
    document.add_attachment(filename="notes.txt", data="replaced")

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert reader.attachments == {
            "data ü.bin": [bytes(range(256))],
            "notes.txt": [b"replaced"],
        }
        assert reader.get_attachment("notes.txt") == b"replaced"

        info = {item["name"]: item for item in reader.attachment_info}
        assert info["data ü.bin"]["size"] == 256
        assert info["notes.txt"]["size"] == len(b"replaced")

        with pytest.raises(PdfEditError):
            reader.get_attachment("missing")

    if pypdf is not None:
        assert pypdf.PdfReader(BytesIO(data)).attachments == {
            "data ü.bin": [bytes(range(256))],
            "notes.txt": [b"replaced"],
        }

    assert document.remove_attachment(name="notes.txt") is True
    assert document.remove_attachment(name="notes.txt") is False
    with reread(document) as reader:
        assert set(reader.attachments) == {"data ü.bin"}


def test_attachment_metadata():
    document = PdfEditor(source=THREE)
    document.add_attachment(
        filename="notes.txt",
        data=b"hello",
        description="a note",
        mime_type="text/plain",
    )

    with reread(document) as reader:
        (info,) = reader.attachment_info
    assert info["filename"] == "notes.txt"
    assert info["description"] == "a note"
    assert info["mime_type"] == "text/plain"
    assert info["creation_date"].startswith("D:")


def test_document_scripts_are_stored_and_can_be_stripped():
    document = PdfEditor(source=THREE)
    assert document.javascript == {}

    document.add_js(javascript="app.alert('one');")
    document.add_js(javascript="app.alert('zwei ü');", name="second")

    with reread(document) as reader:
        assert reader.javascript == {
            "script-1": "app.alert('one');",
            "second": "app.alert('zwei ü');",
        }

    assert document.remove_javascript() == 2
    with reread(document) as reader:
        assert reader.javascript == {}


# ---------------------------------------------------------------- output options


def test_encrypt_with_permissions():
    document = PdfEditor(source=THREE)
    document.encrypt(
        user_password="user",
        owner_password="owner",
        allow_print=False,
        allow_extract=False,
        allow_modify=False,
    )
    data = document.to_bytes()

    with PdfReader(data, password="user") as reader:
        permissions = reader.permissions
        assert permissions is not None
        assert permissions["print"] is False
        assert permissions["extract"] is False
        assert permissions["modify"] is False
        assert permissions["annotate"] is True
        assert permissions["fill_forms"] is True
        assert permissions["assemble"] is True
        # reported, not enforced
        assert reader.pages[0].extract_text() == "alpha"

    with PdfReader(THREE) as reader:
        assert reader.permissions is None

    document.encrypt(user_password="user", allow_print="low")
    with PdfReader(document.to_bytes(), password="user") as reader:
        assert reader.permissions["print"] is True
        assert reader.permissions["print_high_quality"] is False

    with pytest.raises(ValueError):
        document.encrypt(user_password="user", allow_print="sometimes")


def test_output_options():
    document = PdfEditor(source=THREE)
    plain = document.to_bytes()
    assert b"/Linearized" not in plain

    document.set_output_options(linearize=True)
    linearized = document.to_bytes()
    assert b"/Linearized" in linearized
    assert written_widths(linearized) == [200.0, 210.0, 220.0]

    document.set_output_options(object_streams="generate")
    packed = document.to_bytes()
    assert b"/ObjStm" in packed
    assert written_widths(packed) == [200.0, 210.0, 220.0]

    document.set_output_options(object_streams="disable", compress_streams=False)
    loose = document.to_bytes()
    assert b"/ObjStm" not in loose
    # the page content is stored as written
    assert b"(alpha) Tj" in loose
    with PdfReader(loose) as reader:
        assert reader.pages[0].extract_text() == "alpha"

    with pytest.raises(PdfEditError):
        document.set_output_options(object_streams="sometimes")


def test_reader_has_no_document_level_editing():
    with PdfReader(FORM) as reader:
        for name in (
            "add_metadata",
            "set_page_label",
            "add_outline_item",
            "update_form_field_values",
            "flatten_annotations",
            "add_annotation",
            "add_attachment",
            "add_js",
            "encrypt",
            "set_output_options",
        ):
            assert not hasattr(reader, name)
        for name in (
            "set_info",
            "set_form_values",
            "add_annotation",
            "add_attachment",
            "set_encryption",
        ):
            assert not hasattr(reader._native, name)


# ---------------------------------------------------------------- rendering

RED = (255, 0, 0)
BLUE = (0, 0, 255)
WHITE = (255, 255, 255)

# A red square over [50 150]^2 and a blue one over [100 180]^2, each on its
# own 200x200 page: they overlap in [100 150]^2.
RED_SQUARE = simple_page_pdf("1 0 0 rg 50 50 100 100 re f")
BLUE_SQUARE = simple_page_pdf("0 0 1 rg 100 100 80 80 re f")


def rendered(data: bytes, page_index: int = 0):
    """The page as pypdfium2 draws it, one pixel per point: a renderer that
    shares no code with docling-parse."""
    pdfium = pytest.importorskip("pypdfium2")

    document = pdfium.PdfDocument(data)
    try:
        return document[page_index].render(scale=1).to_pil().convert("RGB")
    finally:
        document.close()


def color_at(image, x: float, y: float) -> tuple[int, int, int]:
    """The pixel at PDF coordinates (x, y), origin bottom-left."""
    return image.getpixel((int(x), image.height - 1 - int(y)))


@pytest.mark.parametrize(("over", "overlap"), [(True, BLUE), (False, RED)])
def test_merge_page_stacking_order_as_rendered(over, overlap):
    document = PdfEditor(source=RED_SQUARE)
    document.overlay_page(
        page_no=1, source_page=PdfReader(BLUE_SQUARE).pages[0], over=over
    )

    image = rendered(document.to_bytes())

    assert image.size == (200, 200)
    assert color_at(image, 10, 10) == WHITE
    assert color_at(image, 60, 60) == RED
    assert color_at(image, 165, 165) == BLUE
    # the only place where the order of the two pages is visible
    assert color_at(image, 125, 125) == overlap


def test_overlay_does_not_inherit_the_page_s_graphics_state():
    """The page ends with a scaled CTM and a green fill still in force. An
    overlay drawn after it must not be scaled or recolored by them."""
    base = simple_page_pdf("0.5 0 0 0.5 0 0 cm 0 1 0 rg")

    document = PdfEditor(source=base)
    document.overlay_page(page_no=1, source_page=PdfReader(BLUE_SQUARE).pages[0])

    image = rendered(document.to_bytes())
    assert color_at(image, 165, 165) == BLUE
    # where the square would be if the page's 0.5 scale had leaked into it
    assert color_at(image, 60, 60) == WHITE


def test_merge_transformed_pages_as_rendered():
    """The blue square covers [100 180]^2 of its page."""
    blue = PdfReader(BLUE_SQUARE).pages[0]

    document = PdfEditor()
    for _ in range(4):
        document.add_blank_page(width=200, height=200)

    # halved about the origin: [50 90]^2
    document.overlay_page(page_no=1, source_page=blue, ctm=(0.5, 0, 0, 0.5, 0, 0))
    # moved left and down by 100: [0 80]^2
    document.overlay_page(page_no=2, source_page=blue, ctm=(1, 0, 0, 1, -100, -100))
    # a quarter turn counter-clockwise takes (x, y) to (-y, x); moved right
    # by 200 the square lands on x in [20 100], y in [100 180]
    document.overlay_page(page_no=3, source_page=blue, ctm=(0, 1, -1, 0, 0, 0))
    document.transform_page(page_no=3, ctm=(1, 0, 0, 1, 200, 0))
    # the same, as one matrix
    document.overlay_page(page_no=4, source_page=blue, ctm=(0, 1, -1, 0, 200, 0))

    data = document.to_bytes()

    scaled = rendered(data, 0)
    assert color_at(scaled, 70, 70) == BLUE
    assert color_at(scaled, 140, 140) == WHITE

    translated = rendered(data, 1)
    assert color_at(translated, 40, 40) == BLUE
    assert color_at(translated, 140, 140) == WHITE

    for index in (2, 3):
        turned = rendered(data, index)
        assert color_at(turned, 60, 140) == BLUE
        assert color_at(turned, 140, 140) == WHITE
        assert color_at(turned, 60, 60) == WHITE


def test_transfer_rotation_to_content_keeps_the_picture():
    document = PdfEditor(source=RED_SQUARE)
    document.set_page_box(page_no=1, box="crop", bounds=(50, 50, 200, 150))
    document.rotate_page(page_no=1, angle=90)
    before = rendered(document.to_bytes())

    document.transfer_page_rotation_to_content(page_no=1)
    assert document.get_page(page_no=1).rotation == 0

    after = rendered(document.to_bytes())
    assert after.size == before.size == (100, 150)
    # Compared away from the square's edges, which lie on multiples of 50:
    # the two files may antialias an edge differently, not move it.
    points = [(x, y) for x in range(5, 100, 10) for y in range(5, 150, 10)]
    assert [after.getpixel(p) for p in points] == [before.getpixel(p) for p in points]
    assert {before.getpixel(p) for p in points} == {RED, WHITE}


def test_scale_and_transformation_as_rendered():
    document = PdfEditor(source=RED_SQUARE)
    document.scale_page(page_no=1, sx=0.5, sy=0.5)

    image = rendered(document.to_bytes())
    assert image.size == (100, 100)
    assert color_at(image, 50, 50) == RED
    assert color_at(image, 10, 10) == WHITE
    assert color_at(image, 90, 90) == WHITE

    document = PdfEditor(source=RED_SQUARE)
    document.transform_page(page_no=1, ctm=(1, 0, 0, 1, 40, 0))

    image = rendered(document.to_bytes())
    assert image.size == (200, 200)
    assert color_at(image, 60, 100) == WHITE
    assert color_at(image, 180, 100) == RED


def test_rotation_and_crop_as_rendered():
    document = PdfEditor(source=RED_SQUARE)
    document.set_page_box(page_no=1, box="crop", bounds=(50, 50, 200, 150))
    document.rotate_page(page_no=1, angle=90)

    image = rendered(document.to_bytes())
    # a 150x100 crop, turned a quarter: 100 wide, 150 high
    assert image.size == (100, 150)


# ---------------------------------------------------------------- bookmark editing


def pypdf_titles(data: bytes) -> list[str]:
    """The outline titles as pypdf reads them, depth first."""
    assert pypdf is not None

    def walk(items):
        titles = []
        for item in items:
            titles.extend(walk(item) if isinstance(item, list) else [item.title])
        return titles

    return walk(pypdf.PdfReader(BytesIO(data)).outline)


def test_edit_bookmarks():
    document = PdfEditor(source=make_pdf([f"p{i}" for i in range(5)]))

    one = document.add_outline_item(title="One", page_no=1)
    two = document.add_outline_item(title="Two", page_no=2)
    sub = document.add_outline_item(title="Sub", page_no=3, parent=one)
    web = document.add_outline_item(
        title="Web", page_no=None, url="https://example.org"
    )

    document.update_outline_item(item_id=two, title="Second", page_no=4)
    document.move_outline_item(item_id=sub, parent=two)
    document.move_outline_item(item_id=web, index=0)

    expected = [(0, "Web", None), (0, "One", 0), (0, "Second", 3), (1, "Sub", 2)]
    assert flat_outline(document.outline) == [
        (0, "Web", None),
        (0, "One", 1),
        (0, "Second", 4),
        (1, "Sub", 3),
    ]

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert flat_outline(reader.outline) == expected
        assert reader.outline[0]["url"] == "https://example.org"
        assert reader.outline[1]["url"] is None
    if pypdf is not None:
        assert pypdf_titles(data) == ["Web", "One", "Second", "Sub"]

    # an item cannot go below itself or below its own children
    with pytest.raises(PdfEditError):
        document.move_outline_item(item_id=two, parent=sub)
    with pytest.raises(PdfEditError):
        document.move_outline_item(item_id=two, parent=two)

    document.remove_outline_item(item_id=one)
    assert flat_outline(document.outline) == [
        (0, "Web", None),
        (0, "Second", 4),
        (1, "Sub", 3),
    ]

    # removing the target leaves a heading
    document.update_outline_item(item_id=two, page_no=None)
    assert flat_outline(document.outline)[1] == (0, "Second", None)

    with pytest.raises(PdfEditError):
        document.remove_outline_item(item_id=987654)

    document.clear_outline()
    assert document.outline == []
    with reread(document) as reader:
        assert reader.outline == []


def test_bookmark_style_and_position():
    document = PdfEditor(source=THREE)
    item = document.add_outline_item(
        title="Styled",
        page_no=2,
        color="#ff0000",
        bold=True,
        italic=True,
        top=200,
        zoom=2,
    )
    document.add_outline_item(title="Child", page_no=3, parent=item)
    document.update_outline_item(item_id=item, is_open=False)

    with PdfEditor(source=document.to_bytes()) as editor:
        (entry,) = editor.outline
        assert entry["open"] is False
        assert entry["page"] == 2

        raw = editor.get_object(object_id=entry["id"])
        assert raw["/C"] == [1, 0, 0]
        assert raw["/F"] == 3  # italic and bold
        assert raw["/Dest"][1:] == ["/XYZ", None, 200, 2]
        # one child, folded away
        assert raw["/Count"] == -1

    document.update_outline_item(item_id=item, color=None, bold=False, italic=False)
    with PdfEditor(source=document.to_bytes()) as editor:
        raw = editor.get_object(object_id=editor.outline[0]["id"])
        assert "/C" not in raw
        assert "/F" not in raw

    with pytest.raises(ValueError):
        document.add_outline_item(title="both", page_no=1, url="https://example.org")
    with pytest.raises(ValueError):
        document.add_outline_item(title="position without page", page_no=None, top=10)
    with pytest.raises(ValueError):
        document.add_outline_item(title="bad color", page_no=1, color=(2, 0, 0))


# ---------------------------------------------------------------- creating form fields


def test_create_form_fields():
    document = PdfEditor(source=THREE)
    document.add_text_field(
        page_no=1,
        name="name",
        rect=(20, 250, 180, 270),
        value="Ada",
        tooltip="Your name",
    )
    document.add_text_field(
        page_no=1,
        name="notes",
        rect=(20, 180, 180, 240),
        multiline=True,
    )
    document.add_checkbox(
        page_no=1,
        name="agree",
        rect=(20, 150, 36, 166),
        checked=True,
    )
    document.add_choice_field(
        page_no=1,
        name="country",
        rect=(20, 120, 180, 140),
        options=["Belgium", "Switzerland"],
        value="Switzerland",
    )
    document.add_radio_group(
        page_no=2,
        name="plan",
        buttons={"basic": (20, 250, 36, 266), "premium": (60, 250, 76, 266)},
        selected="premium",
    )
    document.add_signature_field(page_no=3, name="signature", rect=(20, 20, 180, 60))

    data = document.to_bytes()
    with PdfReader(data) as reader:
        fields = reader.get_fields()
        assert {name: field["kind"] for name, field in fields.items()} == {
            "name": "text",
            "notes": "text",
            "agree": "checkbox",
            "country": "choice",
            "plan": "radio",
            "signature": "signature",
        }
        assert fields["name"]["value"] == "Ada"
        assert fields["name"]["alternate_name"] == "Your name"
        assert fields["name"]["rects"] == [[20, 250, 180, 270]]
        assert fields["agree"]["value"] == "/Yes"
        assert fields["country"]["value"] == "Switzerland"
        assert fields["country"]["choices"] == ["Belgium", "Switzerland"]
        assert fields["plan"]["value"] == "/premium"
        assert fields["plan"]["states"] == ["/basic", "/premium"]
        assert fields["plan"]["rects"] == [
            [20, 250, 36, 266],
            [60, 250, 76, 266],
        ]
        assert fields["plan"]["pages"] == [1]
        assert fields["signature"]["pages"] == [2]

    if pypdf is not None:
        reader = pypdf.PdfReader(BytesIO(data))
        assert set(reader.get_fields()) == set(fields)
        assert reader.get_form_text_fields()["name"] == "Ada"

    # the new fields are filled like any other
    document.update_form_field_values(
        fields={"name": "Grace", "agree": False, "country": "Belgium", "plan": "/basic"}
    )
    with reread(document) as reader:
        values = {name: field["value"] for name, field in reader.get_fields().items()}
    assert values["name"] == "Grace"
    assert values["agree"] == "/Off"
    assert values["country"] == "Belgium"
    assert values["plan"] == "/basic"


def test_radio_group_update_and_remove():
    document = PdfEditor(source=THREE)
    document.add_radio_group(
        page_no=2,
        name="plan",
        buttons={"basic": (20, 250, 36, 266), "premium": (60, 250, 76, 266)},
    )
    document.add_text_field(
        page_no=2,
        name="keep",
        rect=(20, 200, 180, 220),
        value="Still here",
    )

    for selected in ("premium", "basic", "Off", "premium"):
        assert document.update_form_field_values(fields={"plan": selected}) == 1
        with reread(document) as reader:
            assert reader.get_fields()["plan"]["value"] == f"/{selected}"
            widgets = [
                a
                for a in reader.pages[1].annotations
                if a["/Subtype"] == "/Widget" and "/AS" in a
            ]
            assert [a["/AS"] for a in widgets] == [
                "/basic" if selected == "basic" else "/Off",
                "/premium" if selected == "premium" else "/Off",
            ]

    # A group has two widgets, so a single replacement rectangle is invalid.
    with pytest.raises(PdfEditError):
        document.update_form_field(name="plan", rect=(20, 200, 36, 216))

    document.update_form_field(
        name="plan", new_name="subscription", read_only=True, tooltip="Choose a plan"
    )
    with reread(document) as reader:
        fields = reader.get_fields()
        assert set(fields) == {"subscription", "keep"}
        assert fields["subscription"]["states"] == ["/basic", "/premium"]
        assert fields["subscription"]["read_only"] is True
        assert fields["subscription"]["alternate_name"] == "Choose a plan"

    document.remove_form_field(name="subscription")
    with reread(document) as reader:
        assert set(reader.get_fields()) == {"keep"}
        widgets = [a for a in reader.pages[1].annotations if a["/Subtype"] == "/Widget"]
        assert len(widgets) == 1


def test_created_fields_render_and_flatten():
    document = PdfEditor(source=THREE)
    document.add_text_field(
        page_no=1,
        name="name",
        rect=(20, 250, 180, 270),
        value="Visible value",
    )
    document.add_radio_group(
        page_no=1,
        name="plan",
        buttons={"basic": (20, 200, 36, 216), "premium": (60, 200, 76, 216)},
        selected="premium",
    )
    document.flatten_annotations()

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert reader.has_form is False
        assert "Visible value" in reader.pages[0].extract_text()

    image = rendered(data)
    # the dot of the selected button, and none in the other one
    assert color_at(image, 68, 208) == (0, 0, 0)
    assert color_at(image, 28, 208) == WHITE


def test_create_form_fields_rejects_bad_input():
    document = PdfEditor(source=FORM)

    # the existing form keeps its fields when new ones are added to it
    document.add_text_field(page_no=1, name="city", rect=(20, 60, 180, 80))
    assert {"name", "agree", "city"} <= set(document.get_fields())

    with pytest.raises(PdfEditError):
        document.add_text_field(page_no=1, name="city", rect=(20, 30, 180, 50))  # taken
    with pytest.raises(PdfEditError):
        document.add_text_field(
            page_no=1, name="a.b", rect=(20, 30, 180, 50)
        )  # a period
    with pytest.raises(PdfEditError):
        document.add_text_field(
            page_no=1, name="flat", rect=(20, 30, 20, 50)
        )  # no width
    with pytest.raises(PdfEditError):
        document.add_choice_field(
            page_no=1,
            name="pick",
            rect=(20, 30, 180, 50),
            options=["a", "b"],
            value="c",
        )
    with pytest.raises(PdfEditError):
        document.add_radio_group(
            page_no=1,
            name="group",
            buttons={"a": (0, 0, 10, 10)},
            selected="z",
        )
    with pytest.raises(PdfEditError):
        document.add_radio_group(
            page_no=1,
            name="group",
            buttons={"Off": (0, 0, 10, 10)},
        )
    with pytest.raises(IndexError):
        document.add_checkbox(page_no=10, name="box", rect=(0, 0, 10, 10))

    # nothing half-made was left behind
    assert set(document.get_fields()) == {"name", "agree", "city"}

    # a choice field that can be typed into takes other values
    document.add_choice_field(
        page_no=1,
        name="pick",
        rect=(20, 30, 180, 50),
        options=["a", "b"],
        value="c",
        editable=True,
    )
    assert document.get_fields()["pick"]["value"] == "c"


def test_update_and_remove_form_fields():
    document = PdfEditor(source=THREE)
    document.add_text_field(
        page_no=1,
        name="name",
        rect=(20, 250, 180, 270),
        value="Ada",
    )
    document.add_checkbox(page_no=1, name="agree", rect=(20, 150, 36, 166))
    document.add_choice_field(
        page_no=1,
        name="country",
        rect=(20, 120, 180, 140),
        options=["Belgium"],
    )

    document.update_form_field(
        name="name",
        new_name="full_name",
        rect=(20, 200, 200, 230),
        read_only=True,
        required=True,
        tooltip="Full name",
    )
    document.update_form_field(name="country", options=["Belgium", "Italy"])

    with reread(document) as reader:
        fields = reader.get_fields()
    assert "name" not in fields
    renamed = fields["full_name"]
    assert renamed["value"] == "Ada"
    assert renamed["read_only"] is True
    assert renamed["flags"] & 2  # required
    assert renamed["rects"] == [[20, 200, 200, 230]]
    assert renamed["alternate_name"] == "Full name"
    assert fields["country"]["choices"] == ["Belgium", "Italy"]

    document.update_form_field(name="full_name", tooltip=None, read_only=False)
    renamed = document.get_fields()["full_name"]
    assert renamed["alternate_name"] == "full_name"
    assert renamed["read_only"] is False

    with pytest.raises(PdfEditError):
        document.update_form_field(
            name="full_name", options=["a"]
        )  # not a choice field
    with pytest.raises(PdfEditError):
        document.update_form_field(name="agree", new_name="full_name")  # taken
    with pytest.raises(PdfEditError):
        document.update_form_field(name="missing", read_only=True)

    document.remove_form_field(name="agree")
    with reread(document) as reader:
        assert set(reader.get_fields()) == {"full_name", "country"}
        widgets = [a for a in reader.pages[0].annotations if a["/Subtype"] == "/Widget"]
        assert len(widgets) == 2

    with pytest.raises(PdfEditError):
        document.remove_form_field(name="agree")


# ---------------------------------------------------------------- drawing text


def test_draw_text():
    document = PdfEditor(source=THREE)

    document.draw_text(
        page_no=2, text="Stamped (1) \\ ok", x=20, y=50, size=14, font="Times-Bold"
    )
    document.draw_text(page_no=2, text="Größe € one\nline two", x=20, y=100)

    with reread(document) as reader:
        text = reader.pages[1].extract_text()
    for expected in ("beta", "Stamped (1) \\ ok", "Größe € one", "line two"):
        assert expected in text

    with pytest.raises(PdfUnsupportedError):
        document.draw_text(page_no=2, text="日本語", x=20, y=20)
    with pytest.raises(PdfEditError):
        document.draw_text(page_no=2, text="x", x=20, y=20, font="Comic Sans")
    with pytest.raises(PdfEditError):
        document.draw_text(page_no=2, text="x", x=20, y=20, size=0)

    # the rejected calls left the page as it was
    with reread(document) as reader:
        assert "日本語" not in reader.pages[1].extract_text()


def test_draw_text_color_as_rendered():
    document = PdfEditor()
    document.add_blank_page(width=200, height=200)
    document.draw_text(
        page_no=1,
        text="MM",
        x=10,
        y=40,
        size=120,
        font="Helvetica-Bold",
        color="#0000ff",
    )

    image = rendered(document.to_bytes())
    # get_flattened_data() requires Pillow 12.1.0; we still support Pillow 10.
    # Remove the hasattr check and getdata() fallback when the minimum is >=12.1.0.
    pixels = (
        image.get_flattened_data()
        if hasattr(image, "get_flattened_data")
        else image.getdata()
    )
    blue = [p for p in pixels if p[2] > 200 and p[0] < 60 and p[1] < 60]
    assert len(blue) > 1000


# ---------------------------------------------------------------- contents pages


def test_table_of_contents_page():
    document = PdfEditor(source=make_pdf([f"p{i}" for i in range(4)]))
    one = document.add_outline_item(title="Introduction", page_no=1)
    document.add_outline_item(title="Scope", page_no=2, parent=one)
    document.add_outline_item(title="Results", page_no=4)

    pages = document.add_table_of_contents(page_size="A4")
    assert [page.page_no for page in pages] == [1]
    assert document.page_count() == 5

    # the bookmarks follow their pages behind the new one
    assert flat_outline(document.outline) == [
        (0, "Introduction", 2),
        (1, "Scope", 3),
        (0, "Results", 5),
    ]

    with reread(document) as reader:
        contents = reader.pages[0]
        assert contents.mediabox == pytest.approx((0, 0, 595.28, 841.89))
        text = contents.extract_text()
        for expected in ("Contents", "Introduction", "Scope", "Results"):
            assert expected in text
        assert [a["page"] for a in contents.annotations] == [1, 2, 4]

    assert PdfEditor(source=THREE).add_table_of_contents() == []


def test_table_of_contents_spans_pages_and_depth():
    document = PdfEditor(source=make_pdf([f"p{i}" for i in range(3)]))
    for number in range(60):
        parent = document.add_outline_item(
            title=f"Chapter {number}", page_no=number % 3 + 1
        )
        document.add_outline_item(
            title=f"Section {number}.1",
            page_no=number % 3 + 1,
            parent=parent,
        )

    pages = document.add_table_of_contents(page_size="A5", max_depth=0)
    assert len(pages) > 1

    links = []
    with reread(document) as reader:
        for page in pages:
            links.extend(a["page"] for a in reader.pages[page.page_no - 1].annotations)
    # one link per top-level bookmark, to the pages behind the contents
    assert len(links) == 60
    assert set(links) == {len(pages), len(pages) + 1, len(pages) + 2}


# ---------------------------------------------------------------- pages from images


def image_bytes(mode: str, size, color, fmt: str, **options) -> bytes:
    from PIL import Image

    out = BytesIO()
    Image.new(mode, size, color).save(out, fmt, **options)
    return out.getvalue()


def test_pdf_from_images(tmp_path):
    jpeg = image_bytes("RGB", (300, 200), (200, 30, 30), "JPEG", dpi=(150, 150))
    png = image_bytes("RGBA", (100, 50), (0, 0, 255, 128), "PNG")
    gray = tmp_path / "gray.png"
    gray.write_bytes(image_bytes("L", (40, 40), 128, "PNG"))

    writer = PdfEditor.from_images(images=[jpeg, BytesIO(png), gray])
    data = writer.to_bytes()

    with PdfReader(data) as reader:
        boxes = [page.mediabox for page in reader.pages]
        assert boxes[0] == pytest.approx((0, 0, 144, 96))  # 300 x 200 px at 150 dpi
        assert boxes[1] == pytest.approx((0, 0, 100, 50))  # no resolution: 1 px = 1 pt
        assert boxes[2] == pytest.approx((0, 0, 40, 40))

        (photo,) = reader.pages[0].images
        # the JPEG file is stored as it is
        assert photo.format == "jpeg"
        assert photo.data == jpeg
        assert photo.bbox == pytest.approx((0, 0, 144, 96))

        (shade,) = reader.pages[2].images
        assert (shade.width, shade.height, shade.mode) == (40, 40, "L")

    # half-transparent blue over the white page
    red, green, blue = color_at(rendered(data, 1), 50, 25)
    assert blue > 200 and 100 < red < 160 and 100 < green < 160

    if pypdf is not None:
        assert len(pypdf.PdfReader(BytesIO(data)).pages) == 3


def test_image_on_a_fixed_page_size():
    jpeg = image_bytes("RGB", (300, 200), (30, 200, 30), "JPEG")

    document = PdfEditor(source=THREE)
    (page,) = document.add_image(
        image=jpeg, page_size="A4", margin=36, before_page_no=2
    )
    assert page.page_no == 2
    assert document.page_count() == 4

    assert page.mediabox == pytest.approx((0, 0, 595.28, 841.89))
    (image,) = page.images
    # 300 x 200 scaled to the 523.28 pt between the margins, centered
    height = 523.28 * 200 / 300
    bottom = (841.89 - height) / 2
    assert image.bbox == pytest.approx((36, bottom, 559.28, bottom + height), abs=0.01)

    (landscape,) = document.add_image(image=jpeg, page_size=(842, 595))
    assert landscape.mediabox == pytest.approx((0, 0, 842, 595))

    with pytest.raises(ValueError):
        document.add_image(image=jpeg, page_size="B7")
    with pytest.raises(ValueError):
        document.add_image(image=jpeg, page_size="A4", margin=400)


def test_image_orientation_frames_and_cmyk():
    from PIL import Image

    exif = Image.Exif()
    exif[0x0112] = 6  # stored turned: show it rotated a quarter clockwise
    turned = image_bytes("RGB", (300, 200), (200, 30, 30), "JPEG", exif=exif.tobytes())

    first = Image.new("RGB", (60, 40), (255, 0, 0))
    second = Image.new("RGB", (60, 40), (0, 0, 255))
    frames = BytesIO()
    first.save(frames, "TIFF", save_all=True, append_images=[second])

    cmyk = image_bytes("CMYK", (50, 50), (0, 255, 255, 0), "JPEG")

    writer = PdfEditor()
    assert len(writer.add_image(image=turned)) == 1
    # one page per frame
    assert len(writer.add_image(image=frames.getvalue())) == 2
    writer.add_image(image=cmyk)

    data = writer.to_bytes()
    with PdfReader(data) as reader:
        assert reader.pages[0].rotation == 90
        assert reader.pages[0].mediabox == pytest.approx((0, 0, 300, 200))
        assert len(reader.pages) == 4

    # shown portrait, as the camera meant it
    assert rendered(data, 0).size == (200, 300)
    assert color_at(rendered(data, 1), 30, 20) == pytest.approx(RED, abs=8)
    assert color_at(rendered(data, 2), 30, 20) == pytest.approx(BLUE, abs=8)
    # a CMYK JPEG keeps its colors: no magenta-and-yellow cast inverted to cyan
    red, green, blue = color_at(rendered(data, 3), 25, 25)
    assert red > 180 and green < 90 and blue < 90


def test_image_errors():
    writer = PdfEditor()
    with pytest.raises(OSError):
        writer.add_image(image=b"not an image")
    with pytest.raises(TypeError):
        writer.add_image(image=12345)
    assert writer.page_count() == 0


def test_reader_cannot_create_content():
    with PdfReader(THREE) as reader:
        for name in (
            "add_text_field",
            "add_checkbox",
            "update_form_field",
            "remove_form_field",
            "update_outline_item",
            "remove_outline_item",
            "add_table_of_contents",
            "add_image",
            "from_images",
        ):
            assert not hasattr(reader, name)
        assert not hasattr(reader.pages[0], "draw_text")
        for name in (
            "add_form_field",
            "draw_text",
            "insert_image_page",
            "clear_outline",
        ):
            assert not hasattr(reader._native, name)


# ---------------------------------------------------------------- editor only


def test_editor_reads_until_first_change():
    with PdfEditor(source=THREE) as editor:
        assert editor.is_modified is False
        assert editor.extract_text(page_no=1) == "alpha"
        assert editor.is_modified is False

        editor.rotate_page(page_no=1, angle=90)
        assert editor.is_modified is True
        assert editor.extract_text(page_no=1) == "alpha"

        assert isinstance(editor.annotations, dict)


OBJECT_REFERENCE = re.compile(r"^\d+ \d+( R)?$")


def text_leaves(value) -> list[str]:
    """Every string in a JSON-like value, sorted, without object references.

    A rewrite renumbers the file's objects, so "6 0" before may be "9 0"
    after; everything else a document says about itself must not change.
    """
    found: list[str] = []

    def walk(item) -> None:
        if isinstance(item, str):
            if not OBJECT_REFERENCE.match(item):
                found.append(item)
        elif isinstance(item, dict):
            for key, child in item.items():
                found.append(str(key))
                walk(child)
        elif isinstance(item, (list, tuple)):
            for child in item:
                walk(child)

    walk(value)
    return sorted(found)


@pytest.mark.parametrize(
    ("build", "marker"),
    [
        (_tagged_pdf, "A grey square"),
        (_form_pdf, "Ada Lovelace"),
        (build_destination_kinds, "FitBV"),
    ],
    ids=["structure-tree", "acroform", "outline"],
)
def test_clone_keeps_document_level_content(build, marker):
    """A clone is a second parse of the same bytes, so the structure tree, the
    form and the outline survive an edit and a rewrite without remapping."""
    source = build()

    with PdfEditor(source=source) as editor:
        before = text_leaves(editor.annotations)
        assert marker in before
        assert editor.is_modified is False

        # an edit that changes no geometry: make the default crop box explicit
        page = editor.get_page(page_no=1)
        editor.set_page_box(page_no=1, box="crop", bounds=page.mediabox)
        assert editor.is_modified is True

        assert text_leaves(editor.annotations) == before
        data = editor.to_bytes()

    with PdfEditor(source=data) as reopened:
        assert text_leaves(reopened.annotations) == before


def encrypted_pdf(password: str) -> bytes:
    pypdf = pytest.importorskip("pypdf", reason="pypdf writes the encrypted fixture")

    writer = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    writer.encrypt(password)
    out = BytesIO()
    writer.write(out)
    return out.getvalue()


def test_encrypted_source_needs_its_password():
    data = encrypted_pdf("secret")

    with pytest.raises(PdfPasswordError) as excinfo:
        PdfReader(data)
    assert excinfo.value.password_supplied is False

    with pytest.raises(PdfPasswordError) as excinfo:
        PdfReader(data, password="wrong")
    assert excinfo.value.password_supplied is True

    with PdfReader(data, password="secret") as reader:
        assert reader.is_encrypted is True
        encryption = reader.encryption
        assert encryption is not None
        assert {"revision", "version", "permissions", "stream_method"} <= set(
            encryption
        )
        assert reader.pages[1].extract_text() == "beta"


def test_encrypted_source_is_written_in_the_clear():
    """The documented default: output is unencrypted unless asked otherwise."""
    document = PdfEditor(source=encrypted_pdf("secret"), password="secret")
    document.remove_page(page_no=1)

    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False
        assert [page.extract_text() for page in reader.pages] == ["beta", "gamma"]


def test_pages_of_an_encrypted_document_can_be_imported():
    document = PdfEditor()
    document.append(
        source=encrypted_pdf("secret"),
        page_nos=[3],
        password="secret",
    )
    stamp = PdfReader(STAMP).pages[0]
    document.overlay_page(page_no=1, source_page=stamp)

    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False
        assert lines(reader.pages[0]) == {"gamma", "STAMP"}
        assert widths(reader) == [220.0]


def test_encrypt_output():
    document = PdfEditor(source=THREE)
    document.encrypt(user_password="secret")
    data = document.to_bytes()

    with pytest.raises(PdfPasswordError):
        PdfReader(data)

    with PdfReader(data, password="secret") as reader:
        assert reader.is_encrypted is True
        encryption = reader.encryption
        assert encryption is not None
        assert encryption["revision"] == 6
        assert encryption["stream_method"] == "aesv3"
        assert [page.extract_text() for page in reader.pages] == [
            "alpha",
            "beta",
            "gamma",
        ]

    # the encrypted state is still readable and reusable by its owner
    assert document.extract_text(page_no=3) == "gamma"
    assert document.is_encrypted is True
    other = PdfEditor()
    other.append(source=document, page_nos=_source_page_nos(document, [1]))
    assert other.extract_text(page_no=1) == "beta"
    assert other.is_encrypted is False

    document.remove_encryption()
    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False


def test_encrypt_with_owner_password_only():
    document = PdfEditor(source=THREE)
    document.encrypt(user_password="", owner_password="owner")

    # no user password: anyone can open it, and it is still encrypted
    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is True
        assert reader.pages[0].extract_text() == "alpha"

    with pytest.raises(PdfEditError) as excinfo:
        document.encrypt(user_password="")
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT


def test_editor_shows_raw_objects():
    with PdfEditor(source=THREE) as editor:
        trailer = editor.trailer
        assert trailer["/Size"] > 0
        assert "/Root" in trailer

        catalog = editor.root_object
        assert catalog["/Type"] == "/Catalog"
        assert catalog["/Pages"]["/Count"] == 3

        # object 3 of the fixture is its font
        assert editor.get_object(object_id=3) == {
            "/Type": "/Font",
            "/Subtype": "/Type1",
            "/BaseFont": "/Helvetica",
        }

        with pytest.raises(PdfEditError):
            editor.get_object(object_id=9999)
        with pytest.raises(ValueError):
            editor.get_object(object_id=0)

        # reading objects is not an edit
        assert editor.is_modified is False


def test_page_lookups():
    with PdfReader(FORM) as reader:
        page = reader.pages[0]
        assert reader.get_page_number(page) == 0
        assert [p.index for p in reader.get_pages_showing_field("agree")] == [0]
        with pytest.raises(KeyError):
            reader.get_pages_showing_field("missing")
        with pytest.raises(ValueError):
            reader.get_page_number(PdfReader(THREE).pages[0])


def test_writer_does_not_expose_editor_only_operations():
    writer = PdfWriter(THREE)
    for name in (
        "annotations",
        "add_text_field",
        "add_table_of_contents",
        "add_image",
        "from_images",
        "set_output_options",
        "to_bytes",
    ):
        assert not hasattr(writer, name)
    assert not hasattr(writer.pages[0], "draw_text")
    assert not hasattr(writer._native, "annotations")


# ---------------------------------------------------------------- threading


def test_one_editor_shared_between_threads():
    """Thread safe, not parallel: mixed calls on one instance are serialized
    and leave a document that still writes and reads back."""
    editor = PdfEditor(source=make_pdf([f"page{i}" for i in range(12)]))
    errors: list[BaseException] = []

    def work(seed: int) -> None:
        try:
            for step in range(25):
                kind = (seed + step) % 5
                try:
                    if kind == 0:
                        editor.add_blank_page(width=50 + seed, height=50)
                    elif kind == 1:
                        editor.rotate_page(page_no=1, angle=90)
                    elif kind == 2:
                        editor.extract_text(page_no=1)
                    elif kind == 3:
                        editor.to_bytes()
                    else:
                        editor.append(
                            source=TWO,
                            page_nos=_source_page_nos(TWO, [0]),
                            before_page_no=1,
                        )
                except PdfStaleHandleError:
                    # another thread changed the page list between taking the
                    # handle and using it: the documented outcome
                    pass
        except BaseException as exc:
            errors.append(exc)

    threads = [threading.Thread(target=work, args=(seed,)) for seed in range(6)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert errors == []

    data = editor.to_bytes()
    assert len(written_widths(data)) == editor.page_count()


def test_two_writers_appending_to_each_other():
    left = PdfWriter(THREE)
    right = PdfWriter(TWO)
    errors: list[BaseException] = []

    def work(target: PdfWriter, source: PdfWriter) -> None:
        try:
            for _ in range(10):
                target.append(source, pages=[0])
        except BaseException as exc:
            errors.append(exc)

    threads = [
        threading.Thread(target=work, args=(left, right)),
        threading.Thread(target=work, args=(right, left)),
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert errors == []
    assert len(left.pages) == 13
    assert len(right.pages) == 12


# ---------------------------------------------------------------- packaging


def test_imports_stay_free_of_heavy_dependencies():
    """The reader, writer and editor must not pull in docling-core, pydantic
    or PIL: they are importable in a process that has none of them loaded."""
    script = (
        "import sys\n"
        "import docling_parse\n"
        "from docling_parse import PdfReader, PdfWriter, PdfEditor\n"
        "heavy = [m for m in ('docling_core', 'pydantic', 'PIL', "
        "'docling_parse.pdf_parser') if m in sys.modules]\n"
        "print(','.join(heavy))\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", script], capture_output=True, text=True, check=True
    )
    assert result.stdout.strip() == ""


def test_load_errors_are_the_parser_s_classes():
    from docling_parse import pdf_parser

    assert pdf_parser.PdfLoadError is PdfLoadError
