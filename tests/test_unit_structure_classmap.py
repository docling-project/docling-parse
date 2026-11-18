# SPDX-FileCopyrightText: The Docling Contributors
# SPDX-License-Identifier: MIT

"""Class-defined tagged-PDF attributes use the same public view as direct /A."""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import (
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    PdfStructure,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import Object, build_pdf, content_stream


def _tagged_pdf(
    element_entries: str | list[str],
    class_map: str | None = None,
    *,
    rotation: int = 0,
    extra_objects: list[Object] | None = None,
) -> bytes:
    class_map_entry = f" /ClassMap {class_map}" if class_map is not None else ""
    entries = [element_entries] if isinstance(element_entries, str) else element_entries
    element_refs = " ".join(f"{index + 7} 0 R" for index in range(len(entries)))
    objects: list[Object] = [
        "<< /Type /Catalog /Pages 2 0 R /MarkInfo << /Marked true >> "
        "/StructTreeRoot 6 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
        f"/CropBox [10 20 290 190] /Rotate {rotation} "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream("/P <</MCID 0>> BDC BT /F1 12 Tf 100 100 Td (Box) Tj ET EMC"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
        f"<< /Type /StructTreeRoot /K [{element_refs}]{class_map_entry} >>",
    ]
    for index, attributes in enumerate(entries):
        kids = "[0]" if index == 0 else "[]"
        objects.append(
            "<< /Type /StructElem /S /P /P 6 0 R /Pg 3 0 R "
            f"/K {kids} /R 9 {attributes} >>"
        )
    objects.extend(extra_objects or [])
    return build_pdf(objects)


def _read_structure(pdf: bytes, *, threaded: bool = False) -> PdfStructure:
    if threaded:
        parser = DoclingThreadedPdfParser(
            parser_config=ThreadedPdfParserConfig(loglevel="fatal", threads=1)
        )
        key = parser.load(BytesIO(pdf))
        try:
            annotations = parser.get_annotations(key)
            assert annotations is not None
            assert annotations.structure is not None
            return annotations.structure
        finally:
            parser.unload(key)

    doc = DoclingPdfParser(loglevel="fatal").load(BytesIO(pdf))
    try:
        structure = doc.get_structure()
        assert structure is not None
        return structure
    finally:
        doc.unload()


@pytest.mark.parametrize("threaded", [False, True], ids=["serial", "threaded"])
def test_class_only_attributes_reach_public_structure(threaded: bool) -> None:
    pdf = _tagged_pdf(
        "/C /HeadCell",
        "<< /HeadCell << /O /Table /Scope /Column /ColSpan 2 >> >>",
    )
    (element,) = _read_structure(pdf, threaded=threaded).elements

    assert element.attributes == {"/Table": {"/Scope": "/Column", "/ColSpan": 2}}
    assert element.page_no == 1


def test_classes_and_direct_attributes_merge_by_owner_and_attribute() -> None:
    # ISO 32000-2, 14.7.6: later classes win conflicts; /A wins over /C.
    # Revisions are interleaved with names/objects, not sorting priorities.
    pdf = _tagged_pdf(
        "/C [/Base 9 /Override 1] "
        "/A [<< /O /Layout /SpaceAfter 12 >> 0 "
        "<< /O /Table /Headers [(direct)] >> 3]",
        "<< /Base [<< /O /Layout /TextAlign /Start /SpaceAfter 5 >> "
        "<< /O /Table /ColSpan 2 /Scope /Column /Headers [(first) (second)] >>] "
        "/Override [<< /O /Layout /TextAlign /Center >> "
        "<< /O /Table /ColSpan 3 >>] >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/Layout": {"/TextAlign": "/Center", "/SpaceAfter": 12},
        "/Table": {"/ColSpan": 3, "/Scope": "/Column", "/Headers": ["direct"]},
    }


@pytest.mark.parametrize("class_defined", [False, True], ids=["direct", "class"])
def test_attribute_objects_with_the_same_owner_merge_per_key(
    class_defined: bool,
) -> None:
    first = "<< /O /Layout /TextAlign /Start /SpaceAfter 5 >>"
    second = "<< /O /Layout /TextAlign /End /LineHeight 12 >>"
    # Revision numbers belong to the element's /A array, not to the shared
    # attribute objects that define a class.
    pdf = (
        _tagged_pdf("/C /Paragraph", f"<< /Paragraph [{first} {second}] >>")
        if class_defined
        else _tagged_pdf(f"/A [{first} 7 {second} 1]")
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/Layout": {"/TextAlign": "/End", "/SpaceAfter": 5, "/LineHeight": 12}
    }


def test_attribute_values_are_replaced_without_merging_their_contents() -> None:
    pdf = _tagged_pdf(
        "/C /Base /A << /O /CustomApp /Settings << /Mode (new) >> /Values [3] >>",
        "<< /Base << /O /CustomApp "
        "/Settings << /Mode (old) /ClassOnly true >> /Values [1 2] >> >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/CustomApp": {"/Settings": {"/Mode": "new"}, "/Values": [3]}
    }


@pytest.mark.parametrize(
    "same_namespace", [False, True], ids=["different-ns", "same-ns"]
)
@pytest.mark.parametrize(
    "class_and_direct", [False, True], ids=["direct", "class-direct"]
)
def test_nso_attributes_do_not_mix_different_namespace_dictionaries(
    same_namespace: bool, class_and_direct: bool
) -> None:
    first_uri = "https://example.org/ns/first"
    second_uri = first_uri if same_namespace else "https://example.org/ns/second"
    first = "<< /O /NSO /NS 8 0 R /Shared (old) /OnlyFirst true >>"
    second = "<< /O /NSO /NS 9 0 R /Shared (new) /OnlySecond true >>"
    namespaces: list[Object] = [
        f"<< /Type /Namespace /NS ({first_uri}) >>",
        f"<< /Type /Namespace /NS ({second_uri}) >>",
    ]
    pdf = (
        _tagged_pdf(
            f"/C /Base /A {second}", f"<< /Base {first} >>", extra_objects=namespaces
        )
        if class_and_direct
        else _tagged_pdf(f"/A [{first} {second}]", extra_objects=namespaces)
    )
    (element,) = _read_structure(pdf).elements

    # The owner-keyed public schema cannot retain two /NSO namespaces. Keep
    # its existing last-owner result for different namespaces, rather than
    # attaching attributes from the first namespace to the second one.
    expected = {
        "/NS": {"/Type": "/Namespace", "/NS": second_uri},
        "/Shared": "new",
        "/OnlySecond": True,
    }
    if same_namespace:
        expected["/OnlyFirst"] = True
    assert element.attributes == {"/NSO": expected}


def test_null_dictionary_values_do_not_hide_class_attributes() -> None:
    # A null-valued PDF dictionary entry is equivalent to a missing entry.
    pdf = _tagged_pdf(
        "/C /Paragraph /A << /O /Layout /TextAlign null /SpaceAfter 5 >>",
        "<< /Paragraph << /O /Layout /TextAlign /Center >> >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/Layout": {"/TextAlign": "/Center", "/SpaceAfter": 5}
    }


def test_direct_overrides_do_not_change_other_elements_using_the_same_class() -> None:
    pdf = _tagged_pdf(
        [
            "/C /Paragraph /A << /O /Layout /TextAlign /End >>",
            "/C /Paragraph",
        ],
        "<< /Paragraph << /O /Layout /TextAlign /Center /LineHeight 12 >> >>",
    )
    overridden, class_only = _read_structure(pdf).elements

    assert overridden.attributes == {
        "/Layout": {"/TextAlign": "/End", "/LineHeight": 12}
    }
    assert class_only.attributes == {
        "/Layout": {"/TextAlign": "/Center", "/LineHeight": 12}
    }


@pytest.mark.parametrize(
    "class_map",
    [None, "null", "/NotADictionary", "<< /Other << /O /Layout /SpaceAfter 20 >> >>"],
    ids=["absent", "null", "invalid", "missing-class"],
)
def test_unavailable_class_map_keeps_direct_attributes(class_map: str | None) -> None:
    pdf = _tagged_pdf("/C /Unknown /A << /O /Layout /TextAlign /Center >>", class_map)
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {"/Layout": {"/TextAlign": "/Center"}}


def test_unknown_and_invalid_classes_do_not_discard_valid_classes() -> None:
    pdf = _tagged_pdf(
        "/C [/Missing 2 /Broken (not a class name) /Paragraph]",
        "<< /Broken 42 /Paragraph << /O /Layout /TextAlign /End >> >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {"/Layout": {"/TextAlign": "/End"}}


@pytest.mark.parametrize("class_reference", ["/Paragraph", "[/Paragraph 2]"])
def test_indirect_class_map_names_and_attribute_objects(class_reference: str) -> None:
    pdf = _tagged_pdf(
        "/C 9 0 R",
        "8 0 R",
        extra_objects=[
            "<< /Paragraph 10 0 R >>",
            class_reference,
            "<< /O /Layout /TextAlign /Center /LineHeight 12 >>",
        ],
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/Layout": {"/TextAlign": "/Center", "/LineHeight": 12}
    }


def test_class_names_are_resolved_as_pdf_names_before_transcoding() -> None:
    # #fc is a PDFDocEncoding byte, not valid UTF-8. The lookup must use raw
    # names even though the exposed attribute owner and keys are transcoded.
    pdf = _tagged_pdf(
        "/C /Absatz#fc",
        "<< /Absatz#fc << /O /Layout#e9 /Platzierung#e9 /Block >> >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {"/Layouté": {"/Platzierungé": "/Block"}}


def test_direct_bbox_overrides_the_class_bbox_before_coordinate_conversion() -> None:
    pdf = _tagged_pdf(
        "/C /Block /A << /O /Layout /BBox [95 95 135 115] >>",
        "<< /Block << /O /Layout /BBox [0 0 1 1] /Placement /Block >> >>",
    )
    (element,) = _read_structure(pdf).elements

    assert element.attributes == {
        "/Layout": {"/BBox": [95, 95, 135, 115], "/Placement": "/Block"}
    }
    bbox = element.bbox
    assert bbox is not None
    assert (bbox.l, bbox.b, bbox.r, bbox.t) == (85, 75, 125, 95)


@pytest.mark.parametrize("rotation", [0, 90, 180, 270])
def test_class_bbox_uses_the_page_cell_coordinate_frame(rotation: int) -> None:
    pdf = _tagged_pdf(
        "/C /Block /A << /O /Layout /Placement /Block >>",
        "<< /Block << /O /Layout /BBox [95 95 135 115] >> >>",
        rotation=rotation,
    )
    doc = DoclingPdfParser(loglevel="fatal").load(BytesIO(pdf))
    try:
        structure = doc.get_structure()
        assert structure is not None
        (element,) = structure.elements
        assert element.attributes == {
            "/Layout": {"/BBox": [95, 95, 135, 115], "/Placement": "/Block"}
        }
        bbox = element.bbox
        assert bbox is not None
        width, height = (40, 20) if rotation in (0, 180) else (20, 40)
        assert bbox.width == pytest.approx(width)
        assert bbox.height == pytest.approx(height)

        tags = [tag for tag in doc.get_page_marked_content(1) if tag.mcid == 0]
        assert "".join(tag.text for tag in tags) == "Box"
        for tag in tags:
            assert tag.rect is not None
            cell = tag.rect.to_bounding_box()
            cx, cy = (cell.l + cell.r) / 2, (cell.b + cell.t) / 2
            assert bbox.l < cx < bbox.r and bbox.b < cy < bbox.t
        if rotation == 0:
            assert (bbox.l, bbox.b, bbox.r, bbox.t) == (85, 75, 125, 95)
    finally:
        doc.unload()
