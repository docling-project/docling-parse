#!/usr/bin/env python
"""PdfReader, PdfWriter and PdfEditor over the native docling_editor.

The three classes publish different subsets of one native implementation, so
the editing tests run against both PdfWriter and PdfEditor: a difference
between the two is a bug in the binding, not a feature.

Every written document is read back, since "it wrote without raising" says
nothing about the file: always with our own reader and, where pypdf is
installed (it is in the `perf` dependency group, not `dev`), with pypdf as an
independent second opinion. The fixtures are built in memory so each page is
identifiable by its size and its text.
"""

from __future__ import annotations

import re
import subprocess
import sys
import threading
from io import BytesIO

import pytest

from docling_parse.pdf_editor import PdfEditor
from docling_parse.pdf_errors import (
    PdfEditError,
    PdfEditErrorCategory,
    PdfLoadError,
    PdfPasswordError,
    PdfStaleHandleError,
    PdfWriteError,
)
from docling_parse.pdf_reader import PdfReader
from docling_parse.pdf_writer import PdfWriter
from tests.outline_fixtures import build_destination_kinds
from tests.pdf_builder import simple_page_pdf
from tests.test_unit_acroform_widgets import _form_pdf
from tests.test_unit_structure_tree import _tagged_pdf

try:
    import pypdf
except ImportError:  # pragma: no cover - depends on the installed groups
    pypdf = None

GARBAGE = b"%PDF-1.4\nthis is not a valid pdf at all"

EDITING_CLASSES = [PdfWriter, PdfEditor]


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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_clone_round_trips_unchanged(cls):
    document = cls(THREE)
    data = document.to_bytes()

    assert written_widths(data) == [200.0, 210.0, 220.0]
    with PdfReader(data) as reader:
        assert reader.metadata["Title"] == "fixture"
        assert [page.extract_text() for page in reader.pages] == [
            "alpha",
            "beta",
            "gamma",
        ]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_remove_reorder_rotate(cls):
    document = cls(THREE)

    document.pages[0].rotate(90)
    document.pages[0].rotate(90)
    del document.pages[1]
    document.move_page(0, 1)

    assert widths(document) == [220.0, 200.0]
    assert [page.rotation for page in document.pages] == [0, 180]

    data = document.to_bytes()
    assert written_widths(data) == [220.0, 200.0]
    assert written_rotations(data) == [0, 180]

    # the source bytes are untouched
    with PdfReader(THREE) as reader:
        assert widths(reader) == [200.0, 210.0, 220.0]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_page_boxes_and_absolute_rotation(cls):
    document = cls(THREE)
    page = document.pages[2]

    page.cropbox = (10, 20, 110, 220)
    page.rotation = 270

    with PdfReader(document.to_bytes()) as reader:
        written = reader.pages[2]
        assert written.cropbox == (10.0, 20.0, 110.0, 220.0)
        assert written.mediabox == (0.0, 0.0, 220.0, 300.0)
        assert written.rotation == 270

    with pytest.raises(PdfEditError) as excinfo:
        page.rotate(45)
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(PdfEditError):
        page.cropbox = (10, 10, 10, 50)


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_assemble_from_several_sources(cls):
    document = cls()
    assert len(document.pages) == 0

    document.append(THREE)
    document.append(PdfReader(TWO), pages=[1, 0])
    document.append(BytesIO(THREE), pages=slice(0, 1), index=0)
    blank = document.add_blank_page(100, 150)
    assert blank.mediabox == (0.0, 0.0, 100.0, 150.0)
    document.insert_blank_page(1, 50, 60)

    expected = [200.0, 50.0, 200.0, 210.0, 220.0, 210.0, 200.0, 100.0]
    assert widths(document) == expected

    data = document.to_bytes()
    assert written_widths(data) == expected
    with PdfReader(data) as reader:
        texts = [page.extract_text() for page in reader.pages]
    assert texts == ["alpha", "", "alpha", "beta", "gamma", "epsilon", "delta", ""]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_pypdf_style_page_by_page_assembly(cls):
    """The recipes people know: add_page in a loop, insert_page, merge."""
    reader = PdfReader(THREE)

    document = cls()
    for page in reader.pages:
        added = document.add_page(page)
        assert added.page_number == len(document.pages) - 1
    document.insert_page(PdfReader(TWO).pages[1], 0)
    document.merge(1, TWO, pages=[0])

    assert document.get_num_pages() == 5
    assert widths(document) == [210.0, 200.0, 200.0, 210.0, 220.0]
    assert document.get_page(4).mediabox.width == 220.0
    assert reader.pdf_header == "%PDF-1.7"

    other = cls(TWO)
    stale = other.pages[1]
    del other.pages[0]
    with pytest.raises(PdfStaleHandleError):
        document.add_page(stale)

    assert written_widths(document.to_bytes()) == [210.0, 200.0, 200.0, 210.0, 220.0]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_scale_to(cls):
    document = cls(THREE)
    document.pages[1].scale_to(105, 600)

    assert document.pages[1].mediabox == pytest.approx((0.0, 0.0, 105.0, 600.0))
    assert widths(document) == pytest.approx([200.0, 105.0, 220.0])


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_append_same_page_twice_gives_independent_pages(cls):
    document = cls()
    document.append(THREE, pages=[0, 0])
    document.pages[1].rotate(90)

    assert [page.rotation for page in document.pages] == [0, 90]
    data = document.to_bytes()
    assert written_rotations(data) == [0, 90]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_append_from_edited_document_and_from_itself(cls):
    other = cls(TWO)
    del other.pages[0]

    document = cls(THREE)
    document.append(other)
    document.append(document, pages=[0])

    assert widths(document) == [200.0, 210.0, 220.0, 210.0, 200.0]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_text_of_edited_document(cls):
    document = cls(THREE)
    del document.pages[0]
    document.append(TWO, pages=[0])

    assert [page.extract_text() for page in document.pages] == [
        "beta",
        "gamma",
        "delta",
    ]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_duplicate_page(cls):
    document = cls(THREE)

    copy = document.duplicate_page(0)
    assert copy.index == 3
    copy.rotate(90)

    front = document.duplicate_page(2, to=0)
    assert front.index == 0

    assert widths(document) == [220.0, 200.0, 210.0, 220.0, 200.0]
    # the copy was rotated, the page it was copied from was not
    assert [page.rotation for page in document.pages] == [0, 0, 0, 0, 90]

    data = document.to_bytes()
    assert written_widths(data) == [220.0, 200.0, 210.0, 220.0, 200.0]
    assert written_rotations(data) == [0, 0, 0, 0, 90]
    with PdfReader(data) as reader:
        assert reader.pages[4].extract_text() == "alpha"


@pytest.mark.parametrize("cls", EDITING_CLASSES)
@pytest.mark.parametrize("over", [True, False])
def test_merge_page_draws_both_pages(cls, over):
    stamp = PdfReader(STAMP).pages[0]

    document = cls(THREE)
    result = document.pages[0].merge_page(stamp, over=over)
    assert result.index == 0

    assert lines(document.pages[0]) == {"alpha", "STAMP"}
    assert lines(document.pages[1]) == {"beta"}
    # merging draws on the page; it does not resize it
    assert widths(document) == [200.0, 210.0, 220.0]

    data = document.to_bytes()
    assert written_widths(data) == [200.0, 210.0, 220.0]
    with PdfReader(data) as reader:
        assert lines(reader.pages[0]) == {"alpha", "STAMP"}


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_watermark_every_page(cls):
    stamp = PdfReader(STAMP).pages[0]

    document = cls(THREE)
    for page in document.pages:
        page.merge_page(stamp, over=False)

    with PdfReader(document.to_bytes()) as reader:
        assert [lines(page) for page in reader.pages] == [
            {"alpha", "STAMP"},
            {"beta", "STAMP"},
            {"gamma", "STAMP"},
        ]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_merge_page_fit_scales_into_the_page(cls):
    """The 2x2 image fills [20 30 120 130] of a 200x300 page. Fitted onto a
    400x600 page, that page is drawn at twice the size."""
    document = cls()
    document.add_blank_page(400, 600)
    document.pages[0].merge_page(PdfReader(IMAGE).pages[0], fit=True)

    image = document.pages[0].images[0]
    assert image.bbox == pytest.approx((40.0, 60.0, 240.0, 260.0))


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_merge_page_from_own_document_and_twice(cls):
    document = cls(THREE)
    document.pages[0].merge_page(PdfReader(STAMP).pages[0])
    document.pages[0].merge_page(PdfReader(TWO).pages[1])
    document.pages[2].merge_page(document.pages[1])

    assert lines(document.pages[0]) >= {"STAMP"}
    assert len(document.to_bytes()) > 0
    assert written_widths(document.to_bytes()) == [200.0, 210.0, 220.0]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_merge_page_rejects_stale_and_closed_sources(cls):
    other = cls(TWO)
    handle = other.pages[1]
    del other.pages[0]

    document = cls(THREE)
    with pytest.raises(PdfStaleHandleError):
        document.pages[0].merge_page(handle)

    reader = PdfReader(STAMP)
    stamp = reader.pages[0]
    reader.close()
    with pytest.raises(PdfEditError):
        document.pages[0].merge_page(stamp)

    # nothing was drawn by the failed calls
    assert lines(document.pages[0]) == {"alpha"}


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_scale_page(cls):
    document = cls(IMAGE)
    page = document.pages[0]

    page.scale(2, 0.5)
    assert page.mediabox == pytest.approx((0.0, 0.0, 400.0, 150.0))
    assert page.cropbox == pytest.approx((0.0, 0.0, 400.0, 150.0))
    assert page.images[0].bbox == pytest.approx((40.0, 15.0, 240.0, 65.0))

    page.scale_by(0.5)
    assert page.mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))

    with PdfReader(document.to_bytes()) as reader:
        assert reader.pages[0].mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))
        assert reader.pages[0].images[0].bbox == pytest.approx((20.0, 7.5, 120.0, 32.5))

    with pytest.raises(PdfEditError):
        page.scale_by(0)
    # the rejected call changed nothing
    assert page.mediabox == pytest.approx((0.0, 0.0, 200.0, 75.0))


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_add_transformation(cls):
    document = cls(IMAGE)
    page = document.pages[0]

    page.add_transformation((1, 0, 0, 1, 10, 5))
    # content moves, the page boundary does not
    assert page.mediabox == (0.0, 0.0, 200.0, 300.0)
    assert page.images[0].bbox == pytest.approx((30.0, 35.0, 130.0, 135.0))

    with pytest.raises(PdfEditError) as excinfo:
        page.add_transformation((1, 2, 2, 4, 0, 0))
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(ValueError):
        page.add_transformation((1, 0, 0, 1))


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_stale_page_handle(cls):
    document = cls(THREE)
    first = document.pages[0]
    last = document.pages[2]

    # in-place edits and appending at the end keep indices meaningful
    first.rotate(90)
    document.add_blank_page(10, 10)
    assert last.mediabox.width == 220.0

    del document.pages[1]

    with pytest.raises(PdfStaleHandleError):
        last.mediabox
    with pytest.raises(PdfStaleHandleError):
        first.rotate(90)
    with pytest.raises(PdfStaleHandleError):
        first.extract_text()

    assert document.pages[0].rotation == 90


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_write_to_path_and_stream(cls, tmp_path):
    document = cls(THREE)
    del document.pages[0]

    target = tmp_path / "out.pdf"
    target.write_bytes(b"previous content")
    document.write(target)
    assert written_widths(target.read_bytes()) == [210.0, 220.0]
    assert [p.name for p in tmp_path.iterdir()] == ["out.pdf"]

    stream = BytesIO()
    document.write(stream)
    assert not stream.closed
    assert stream.getvalue() == target.read_bytes()

    with pytest.raises(PdfWriteError):
        document.write(tmp_path / "no-such-directory" / "out.pdf")


def test_write_over_the_source_path(tmp_path):
    path = tmp_path / "document.pdf"
    path.write_bytes(THREE)

    with PdfEditor(path) as editor:
        del editor.pages[0]
        editor.write(path)

    assert written_widths(path.read_bytes()) == [210.0, 220.0]


# ---------------------------------------------------------------- document level


def reread(document):
    """What a fresh reader sees in the document as written."""
    return PdfReader(document.to_bytes())


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_write_metadata(cls):
    document = cls(THREE)
    document.add_metadata(
        {"/Title": "New title", "Subject": "Ünïcode ✓", "Author": None}
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
        document.add_metadata({"": "nameless"})


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_write_xmp_metadata(cls):
    packet = '<x:xmpmeta xmlns:x="adobe:ns:meta/">é</x:xmpmeta>'

    document = cls(THREE)
    assert document.xmp_metadata is None

    document.set_xmp_metadata(packet)
    with reread(document) as reader:
        assert reader.xmp_metadata == packet

    document.set_xmp_metadata(None)
    with reread(document) as reader:
        assert reader.xmp_metadata is None


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_viewer_settings(cls):
    document = cls(THREE)
    assert document.page_layout is None
    assert document.page_mode is None
    assert document.viewer_preferences is None
    assert document.open_destination is None

    document.set_page_layout("/TwoColumnLeft")
    document.set_page_mode("UseOutlines")
    document.set_viewer_preferences({"/HideToolbar": True, "Direction": "/R2L"})
    document.set_open_destination(2)

    with reread(document) as reader:
        assert reader.page_layout == "/TwoColumnLeft"
        assert reader.page_mode == "/UseOutlines"
        assert reader.viewer_preferences == {"/HideToolbar": True, "/Direction": "/R2L"}
        assert reader.open_destination == {"page": 2, "kind": "/Fit", "arguments": []}

    with pytest.raises(PdfEditError) as excinfo:
        document.set_page_layout("/Sideways")
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    document.set_page_layout(None)
    document.set_page_mode(None)
    document.set_viewer_preferences(None)
    document.set_open_destination(None)

    with reread(document) as reader:
        assert reader.page_layout is None
        assert reader.page_mode is None
        assert reader.viewer_preferences is None
        assert reader.open_destination is None


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_page_labels(cls):
    document = cls(make_pdf([f"p{i}" for i in range(6)]))
    assert document.page_labels == ["1", "2", "3", "4", "5", "6"]

    # front matter in roman numerals; the rest keeps counting 3, 4, ...
    document.set_page_label(0, 1, "/r")
    assert document.page_labels == ["i", "ii", "3", "4", "5", "6"]

    document.set_page_label(2, 3, "/D", prefix="A-", start=5)
    assert document.page_labels == ["i", "ii", "A-5", "A-6", "5", "6"]

    document.set_page_label(5, 5, "/A", start=27)
    expected = ["i", "ii", "A-5", "A-6", "5", "AA"]
    assert document.page_labels == expected

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert reader.page_labels == expected
    if pypdf is not None:
        assert pypdf.PdfReader(BytesIO(data)).page_labels == expected

    with pytest.raises(PdfEditError):
        document.set_page_label(0, 1, "/X")
    with pytest.raises(IndexError):
        document.set_page_label(0, 9, "/D")


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_named_destinations(cls):
    document = cls(THREE)
    assert document.named_destinations == {}

    document.add_named_destination("chapter two", 1)
    document.add_named_destination("end", -1)

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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_build_an_outline(cls):
    document = cls(THREE)
    assert document.outline == []

    one = document.add_outline_item("One", 0)
    document.add_outline_item("Sub ö", 1, parent=one)
    document.add_outline_item("Sub two", 2, parent=one)
    document.add_outline_item("Two", 2)

    expected = [(0, "One", 0), (1, "Sub ö", 1), (1, "Sub two", 2), (0, "Two", 2)]
    assert flat_outline(document.outline) == expected

    data = document.to_bytes()
    with PdfReader(data) as reader:
        assert flat_outline(reader.outline) == expected
    if pypdf is not None:
        assert pypdf_flat_outline(data) == expected

    with pytest.raises(PdfEditError):
        document.add_outline_item("orphan", 0, parent=987654)


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_removing_a_page_unhooks_its_bookmarks(cls):
    document = cls(THREE)
    document.add_outline_item("One", 0)
    document.add_outline_item("Two", 1)
    document.add_outline_item("Three", 2)
    document.set_open_destination(1)

    del document.pages[1]

    # the bookmark stays as a heading; the ones behind it follow their pages
    expected = [(0, "One", 0), (0, "Two", None), (0, "Three", 1)]
    with reread(document) as reader:
        assert flat_outline(reader.outline) == expected
        assert reader.open_destination is None
        assert len(reader.pages) == 2


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_append_brings_the_outline_of_copied_pages(cls):
    """build_destination_kinds: three pages, twelve bookmarks, eleven of them
    on the first page and one without a destination."""
    source = build_destination_kinds()
    with PdfReader(source) as reader:
        titles = [item["title"] for item in reader.outline]
        pages = [item["page"] for item in reader.outline]
    assert len(titles) == 12
    assert pages == [0] * 11 + [None]

    document = cls(THREE)
    document.append(source, pages=[1, 0])

    # the first page of the source is now page 4 of the document
    assert flat_outline(document.outline) == [(0, title, 4) for title in titles[:11]]

    without = cls(THREE)
    without.append(source, pages=[1, 2])
    assert without.outline == []

    declined = cls(THREE)
    declined.append(source, import_outline=False)
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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_fill_form_fields(cls):
    document = cls(FORM)
    assert (
        document.update_form_field_values({"name": "Grace Hopper", "agree": True}) == 2
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

    assert document.update_form_field_values({"agree": False}) == 1
    with reread(document) as reader:
        assert reader.get_fields()["agree"]["value"] == "/Off"


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_fill_form_rejects_what_it_cannot_set(cls):
    document = cls(FORM)

    with pytest.raises(PdfEditError) as excinfo:
        document.update_form_field_values({"no such field": "x"})
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT

    with pytest.raises(PdfEditError):
        document.update_form_field_values({"agree": "/Maybe"})

    with pytest.raises(PdfEditError):
        cls(THREE).update_form_field_values({"name": "x"})

    # nothing was changed by the rejected calls
    with reread(document) as reader:
        assert reader.get_form_text_fields() == {"name": "Ada"}


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_flatten_form_into_page_content(cls):
    document = cls(FORM)
    document.update_form_field_values(
        {"name": "Grace Hopper", "agree": True}, flatten=True
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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_add_read_and_remove_annotations(cls):
    document = cls(THREE)
    assert document.pages[0].annotations == []

    document.add_uri(0, "https://example.org/a b", (10, 10, 60, 30))
    document.add_link(0, (10, 40, 60, 60), 2)
    document.add_annotation(
        1,
        {"/Subtype": "/Text", "/Rect": [50, 200, 70, 220], "/Contents": "Nöte ✓"},
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
    assert document.pages[0].annotations == []
    assert len(document.pages[1].annotations) == 1

    assert document.remove_annotations() == 1
    assert document.pages[1].annotations == []


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_add_annotation_validates_its_input(cls):
    document = cls(THREE)

    with pytest.raises(PdfEditError):
        document.add_annotation(0, {"/Rect": [0, 0, 10, 10]})
    with pytest.raises(PdfEditError):
        document.add_annotation(0, {"/Subtype": "/Text"})
    with pytest.raises(IndexError):
        document.add_annotation(7, {"/Subtype": "/Text", "/Rect": [0, 0, 1, 1]})
    with pytest.raises(TypeError):
        document.add_annotation(0, {"/Subtype": "/Text", "/Rect": object()})

    assert document.pages[0].annotations == []


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_removing_widgets_removes_their_fields(cls):
    document = cls(FORM)

    # form fields are kept unless they are asked for by name
    assert document.remove_annotations() == 0
    assert set(document.get_fields()) == {"name", "agree"}

    assert document.remove_annotations("/Widget") == 2
    with reread(document) as reader:
        assert reader.get_fields() == {}
        assert reader.pages[0].annotations == []


# ---------------------------------------------------------------- attachments


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_attachments(cls):
    document = cls(THREE)
    assert document.attachments == {}

    document.add_attachment(
        "notes.txt", b"hello", description="a note", mime_type="text/plain"
    )
    document.add_attachment("data ü.bin", bytes(range(256)))
    document.add_attachment("notes.txt", "replaced")

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

    assert document.remove_attachment("notes.txt") is True
    assert document.remove_attachment("notes.txt") is False
    with reread(document) as reader:
        assert set(reader.attachments) == {"data ü.bin"}


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_attachment_metadata(cls):
    document = cls(THREE)
    document.add_attachment(
        "notes.txt", b"hello", description="a note", mime_type="text/plain"
    )

    with reread(document) as reader:
        (info,) = reader.attachment_info
    assert info["filename"] == "notes.txt"
    assert info["description"] == "a note"
    assert info["mime_type"] == "text/plain"
    assert info["creation_date"].startswith("D:")


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_document_scripts_are_stored_and_can_be_stripped(cls):
    document = cls(THREE)
    assert document.javascript == {}

    document.add_js("app.alert('one');")
    document.add_js("app.alert('zwei ü');", name="second")

    with reread(document) as reader:
        assert reader.javascript == {
            "script-1": "app.alert('one');",
            "second": "app.alert('zwei ü');",
        }

    assert document.remove_javascript() == 2
    with reread(document) as reader:
        assert reader.javascript == {}


# ---------------------------------------------------------------- output options


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_encrypt_with_permissions(cls):
    document = cls(THREE)
    document.encrypt(
        "user",
        "owner",
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

    document.encrypt("user", allow_print="low")
    with PdfReader(document.to_bytes(), password="user") as reader:
        assert reader.permissions["print"] is True
        assert reader.permissions["print_high_quality"] is False

    with pytest.raises(ValueError):
        document.encrypt("user", allow_print="sometimes")


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_output_options(cls):
    document = cls(THREE)
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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
@pytest.mark.parametrize(("over", "overlap"), [(True, BLUE), (False, RED)])
def test_merge_page_stacking_order_as_rendered(cls, over, overlap):
    document = cls(RED_SQUARE)
    document.pages[0].merge_page(PdfReader(BLUE_SQUARE).pages[0], over=over)

    image = rendered(document.to_bytes())

    assert image.size == (200, 200)
    assert color_at(image, 10, 10) == WHITE
    assert color_at(image, 60, 60) == RED
    assert color_at(image, 165, 165) == BLUE
    # the only place where the order of the two pages is visible
    assert color_at(image, 125, 125) == overlap


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_overlay_does_not_inherit_the_page_s_graphics_state(cls):
    """The page ends with a scaled CTM and a green fill still in force. An
    overlay drawn after it must not be scaled or recolored by them."""
    base = simple_page_pdf("0.5 0 0 0.5 0 0 cm 0 1 0 rg")

    document = cls(base)
    document.pages[0].merge_page(PdfReader(BLUE_SQUARE).pages[0])

    image = rendered(document.to_bytes())
    assert color_at(image, 165, 165) == BLUE
    # where the square would be if the page's 0.5 scale had leaked into it
    assert color_at(image, 60, 60) == WHITE


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_merge_transformed_pages_as_rendered(cls):
    """The blue square covers [100 180]^2 of its page."""
    blue = PdfReader(BLUE_SQUARE).pages[0]

    document = cls()
    for _ in range(4):
        document.add_blank_page(200, 200)

    # halved about the origin: [50 90]^2
    document.pages[0].merge_scaled_page(blue, 0.5)
    # moved left and down by 100: [0 80]^2
    document.pages[1].merge_translated_page(blue, -100, -100)
    # a quarter turn counter-clockwise takes (x, y) to (-y, x); moved right
    # by 200 the square lands on x in [20 100], y in [100 180]
    document.pages[2].merge_rotated_page(blue, 90)
    document.pages[2].add_transformation((1, 0, 0, 1, 200, 0))
    # the same, as one matrix
    document.pages[3].merge_transformed_page(blue, (0, 1, -1, 0, 200, 0))

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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_transfer_rotation_to_content_keeps_the_picture(cls):
    document = cls(RED_SQUARE)
    page = document.pages[0]
    page.cropbox = (50, 50, 200, 150)
    page.rotate(90)
    before = rendered(document.to_bytes())

    page.transfer_rotation_to_content()
    assert page.rotation == 0

    after = rendered(document.to_bytes())
    assert after.size == before.size == (100, 150)
    # Compared away from the square's edges, which lie on multiples of 50:
    # the two files may antialias an edge differently, not move it.
    points = [(x, y) for x in range(5, 100, 10) for y in range(5, 150, 10)]
    assert [after.getpixel(p) for p in points] == [before.getpixel(p) for p in points]
    assert {before.getpixel(p) for p in points} == {RED, WHITE}


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_scale_and_transformation_as_rendered(cls):
    document = cls(RED_SQUARE)
    document.pages[0].scale_by(0.5)

    image = rendered(document.to_bytes())
    assert image.size == (100, 100)
    assert color_at(image, 50, 50) == RED
    assert color_at(image, 10, 10) == WHITE
    assert color_at(image, 90, 90) == WHITE

    document = cls(RED_SQUARE)
    document.pages[0].add_transformation((1, 0, 0, 1, 40, 0))

    image = rendered(document.to_bytes())
    assert image.size == (200, 200)
    assert color_at(image, 60, 100) == WHITE
    assert color_at(image, 180, 100) == RED


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_rotation_and_crop_as_rendered(cls):
    document = cls(RED_SQUARE)
    page = document.pages[0]
    page.cropbox = (50, 50, 200, 150)
    page.rotate(90)

    image = rendered(document.to_bytes())
    # a 150x100 crop, turned a quarter: 100 wide, 150 high
    assert image.size == (100, 150)


# ---------------------------------------------------------------- editor only


def test_editor_reads_until_first_change():
    with PdfEditor(THREE) as editor:
        assert editor.is_modified is False
        assert editor.pages[0].extract_text() == "alpha"
        assert editor.is_modified is False

        editor.pages[0].rotate(90)
        assert editor.is_modified is True
        assert editor.pages[0].extract_text() == "alpha"

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

    with PdfEditor(source) as editor:
        before = text_leaves(editor.annotations)
        assert marker in before
        assert editor.is_modified is False

        # an edit that changes no geometry: make the default crop box explicit
        page = editor.pages[0]
        page.cropbox = page.mediabox
        assert editor.is_modified is True

        assert text_leaves(editor.annotations) == before
        data = editor.to_bytes()

    with PdfEditor(data) as reopened:
        assert text_leaves(reopened.annotations) == before


def encrypted_pdf(password: str) -> bytes:
    pytest.importorskip("pypdf", reason="pypdf writes the encrypted fixture")

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


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_encrypted_source_is_written_in_the_clear(cls):
    """The documented default: output is unencrypted unless asked otherwise."""
    document = cls(encrypted_pdf("secret"), password="secret")
    del document.pages[0]

    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False
        assert [page.extract_text() for page in reader.pages] == ["beta", "gamma"]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_pages_of_an_encrypted_document_can_be_imported(cls):
    document = cls()
    document.append(encrypted_pdf("secret"), pages=[2], password="secret")
    stamp = PdfReader(STAMP).pages[0]
    document.pages[0].merge_page(stamp)

    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False
        assert lines(reader.pages[0]) == {"gamma", "STAMP"}
        assert widths(reader) == [220.0]


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_encrypt_output(cls):
    document = cls(THREE)
    document.encrypt("secret")
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
    assert document.pages[2].extract_text() == "gamma"
    assert document.is_encrypted is True
    other = cls()
    other.append(document, pages=[1])
    assert other.pages[0].extract_text() == "beta"
    assert other.is_encrypted is False

    document.remove_encryption()
    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is False


@pytest.mark.parametrize("cls", EDITING_CLASSES)
def test_encrypt_with_owner_password_only(cls):
    document = cls(THREE)
    document.encrypt("", owner_password="owner")

    # no user password: anyone can open it, and it is still encrypted
    with PdfReader(document.to_bytes()) as reader:
        assert reader.is_encrypted is True
        assert reader.pages[0].extract_text() == "alpha"

    with pytest.raises(PdfEditError) as excinfo:
        document.encrypt("")
    assert excinfo.value.category is PdfEditErrorCategory.INVALID_EDIT


def test_editor_shows_raw_objects():
    with PdfEditor(THREE) as editor:
        trailer = editor.trailer
        assert trailer["/Size"] > 0
        assert "/Root" in trailer

        catalog = editor.root_object
        assert catalog["/Type"] == "/Catalog"
        assert catalog["/Pages"]["/Count"] == 3

        # object 3 of the fixture is its font
        assert editor.get_object(3) == {
            "/Type": "/Font",
            "/Subtype": "/Type1",
            "/BaseFont": "/Helvetica",
        }

        with pytest.raises(PdfEditError):
            editor.get_object(9999)
        with pytest.raises(ValueError):
            editor.get_object(0)

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
    assert not hasattr(writer, "annotations")
    assert not hasattr(writer._native, "annotations")


# ---------------------------------------------------------------- threading


def test_one_editor_shared_between_threads():
    """Thread safe, not parallel: mixed calls on one instance are serialized
    and leave a document that still writes and reads back."""
    editor = PdfEditor(make_pdf([f"page{i}" for i in range(12)]))
    errors: list[BaseException] = []

    def work(seed: int) -> None:
        try:
            for step in range(25):
                kind = (seed + step) % 5
                try:
                    if kind == 0:
                        editor.add_blank_page(50 + seed, 50)
                    elif kind == 1:
                        editor.pages[0].rotate(90)
                    elif kind == 2:
                        editor.pages[0].extract_text()
                    elif kind == 3:
                        editor.to_bytes()
                    else:
                        editor.append(TWO, pages=[0], index=0)
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
    assert len(written_widths(data)) == len(editor.pages)


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
