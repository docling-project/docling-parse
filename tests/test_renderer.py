#!/usr/bin/env python
import glob
import hashlib
import json
import os
from pathlib import Path
from typing import Any

from docling_parse.pdf_parser import (
    DecodePageConfig,
    DoclingPdfRenderer,
    PdfRenderDocument,
)

GENERATE = True
RENDER_INSTRUCTION_EPS = 0.005

GROUNDTRUTH_RENDERER_FOLDER = "tests/data/groundtruth_renderer"
REGRESSION_FOLDER = "tests/data/regression/*.pdf"

PAGE_RESTRICTIONS = {
    "deep-mediabox-inheritance.pdf": [2],
    "font_06.pdf": [1],
    "font_07.pdf": [1],
    "font_08.pdf": [1],
    "font_09.pdf": [1],
    "font_10.pdf": [1],
}

BITMAP_RESTRICTIONS = {
    "indexed_iccbased.pdf": {
        1: [1, 5, 10, 15],
    },
}
MAX_BITMAPS_PER_PAGE = 5


def _round_floats(obj, ndigits=3):
    if isinstance(obj, float):
        return round(obj, ndigits)
    if isinstance(obj, dict):
        return {k: _round_floats(v, ndigits) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_round_floats(v, ndigits) for v in obj]
    return obj


def _assert_json_matches_with_float_delta(
    expected: Any, actual: Any, eps: float, path: str = "root"
) -> None:
    if isinstance(expected, bool) or isinstance(actual, bool):
        assert expected == actual, f"{path}: {expected!r} != {actual!r}"
        return

    if isinstance(expected, float):
        assert isinstance(actual, (int, float)), (
            f"{path}: expected float, got {type(actual).__name__}"
        )
        assert abs(expected - float(actual)) <= eps, (
            f"{path}: abs({expected} - {actual}) > {eps}"
        )
        return

    if isinstance(expected, dict):
        assert isinstance(actual, dict), (
            f"{path}: expected dict, got {type(actual).__name__}"
        )
        assert expected.keys() == actual.keys(), f"{path}: key mismatch"
        for key in expected:
            _assert_json_matches_with_float_delta(
                expected[key], actual[key], eps, path=f"{path}.{key}"
            )
        return

    if isinstance(expected, list):
        assert isinstance(actual, list), (
            f"{path}: expected list, got {type(actual).__name__}"
        )
        assert len(expected) == len(actual), f"{path}: length mismatch"
        for idx, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            _assert_json_matches_with_float_delta(
                expected_item, actual_item, eps, path=f"{path}[{idx}]"
            )
        return

    assert expected == actual, f"{path}: {expected!r} != {actual!r}"


def _page_prefix(pdf_name: str, page_no: int) -> Path:
    return Path(GROUNDTRUTH_RENDERER_FOLDER) / f"{pdf_name}.page_no_{page_no}"


def _instruction_path(pdf_name: str, page_no: int) -> Path:
    return Path(f"{_page_prefix(pdf_name, page_no)}.instructions.json")


def _bitmap_json_path(pdf_name: str, page_no: int, bitmap_index: int) -> Path:
    return Path(f"{_page_prefix(pdf_name, page_no)}.bitmap_{bitmap_index}.json")


def _full_page_png_path(pdf_name: str, page_no: int) -> Path:
    return Path(f"{_page_prefix(pdf_name, page_no)}.full_page.png")


def _write_json(path: Path, payload) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fw:
        json.dump(_round_floats(payload), fw, indent=2)


def _load_json(path: Path):
    with open(path, encoding="utf-8") as fr:
        return json.load(fr)


def _artifact_basename(
    pdf_name: str, page_no: int, bitmap_index: int, extension: str
) -> str:
    return f"{pdf_name}.page_no_{page_no}.bitmap_{bitmap_index}{extension}"


def _selected_bitmap_indices(pdf_name: str, page_no: int, num_bitmaps: int) -> set[int]:
    restricted = BITMAP_RESTRICTIONS.get(pdf_name, {}).get(page_no)

    if restricted is None:
        return set(range(1, min(num_bitmaps, MAX_BITMAPS_PER_PAGE) + 1))

    return set(restricted[:MAX_BITMAPS_PER_PAGE])


def _export_or_verify_bitmaps(pdf_name: str, page_no: int, bitmaps) -> None:
    selected = _selected_bitmap_indices(pdf_name, page_no, len(bitmaps))

    for bitmap_index, bitmap in enumerate(bitmaps, start=1):
        if bitmap_index not in selected:
            continue

        raw_sha256 = hashlib.sha256(bitmap["raw_data"]).hexdigest()
        extension = bitmap["extension"]
        artifact_name = _artifact_basename(pdf_name, page_no, bitmap_index, extension)
        artifact_path = Path(GROUNDTRUTH_RENDERER_FOLDER) / artifact_name
        sidecar_path = _bitmap_json_path(pdf_name, page_no, bitmap_index)

        sidecar = {
            "index": bitmap["index"],
            "xobject_key": bitmap["xobject_key"],
            "shape": bitmap["shape"],
            "pixel_format": bitmap["pixel_format"],
            "image_mask": bitmap["image_mask"],
            "rgb_filling": bitmap["rgb_filling"],
            "quad": bitmap["quad"],
            "exported_filename": artifact_name,
            "raw_sha256": raw_sha256,
        }

        if GENERATE or (not sidecar_path.exists()) or (not artifact_path.exists()):
            _write_json(sidecar_path, sidecar)
            with open(artifact_path, "wb") as fw:
                fw.write(bitmap["encoded_data"])
            continue

        true_sidecar = _load_json(sidecar_path)
        assert true_sidecar == _round_floats(sidecar), (
            f"bitmap metadata mismatch for {sidecar_path}"
        )

        with open(artifact_path, "rb") as fr:
            true_bytes = fr.read()
        assert true_bytes == bitmap["encoded_data"], (
            f"bitmap artifact bytes mismatch for {artifact_path}"
        )


def _export_full_page_png(pdf_name: str, page_no: int, image) -> None:
    out_path = _full_page_png_path(pdf_name, page_no)
    if out_path.exists():
        return

    if image is None:
        return

    out_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(out_path, format="PNG")


def test_render_reference_documents():
    config = DecodePageConfig()
    config.page_boundary = "crop_box"
    config.do_sanitization = False
    config.keep_glyphs = True
    config.keep_qpdf_warnings = False
    renderer = DoclingPdfRenderer(loglevel="fatal", decode_config=config)

    pdf_path = "docs/dln-v1.pdf"
    pdf_doc: PdfRenderDocument = renderer.load(path_or_stream=pdf_path, lazy=True)
    assert pdf_doc.number_of_pages() == 1

    render_result = pdf_doc.get_page(1)
    pred_instructions = render_result._export_render_instructions_json()
    bitmap_artifacts = render_result._export_bitmap_artifacts()
    image = render_result.get_image()

    assert pred_instructions["instructions"]
    assert isinstance(bitmap_artifacts, list)
    assert image.mode == "RGBA"
    assert image.width > 0
    assert image.height > 0

    pdf_doc.unload()
