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

GENERATE = False
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

    results = []

    pdf_paths = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_paths) > 0, "len(pdf_paths)==0 -> nothing to test"

    for pdf_path in pdf_paths:
        pdf_name = os.path.basename(pdf_path)

        pdf_doc: PdfRenderDocument = renderer.load(path_or_stream=pdf_path, lazy=True)
        assert pdf_doc is not None

        for page_no in range(1, pdf_doc.number_of_pages() + 1):
            if (
                pdf_name in PAGE_RESTRICTIONS
                and page_no not in PAGE_RESTRICTIONS[pdf_name]
            ):
                continue

            try:
                render_result = pdf_doc.get_page(page_no)
                assert render_result is not None, (
                    f"failed to render {pdf_name}@{page_no}"
                )
                page_decoder, _timings = render_result.get()

                pred_instructions = page_decoder.export_render_instructions_json()
                true_instruction_path = _instruction_path(pdf_name, page_no)

                if GENERATE or (not true_instruction_path.exists()):
                    _write_json(true_instruction_path, pred_instructions)
                else:
                    true_instructions = _load_json(true_instruction_path)

                    true_instructions_len = len(true_instructions["instructions"])
                    pred_instructions_len = len(pred_instructions["instructions"])

                    assert true_instructions_len == pred_instructions_len, (
                        f"true_instructions_len==pred_instructions_len ({true_instructions_len}=={pred_instructions_len}) for {true_instruction_path}"
                    )

                    for ind, true_instruction in enumerate(
                        true_instructions["instructions"]
                    ):
                        _assert_json_matches_with_float_delta(
                            true_instruction,
                            pred_instructions["instructions"][ind],
                            eps=RENDER_INSTRUCTION_EPS,
                            path=f"instructions[{ind}]",
                        )

                bitmap_artifacts = page_decoder.export_bitmap_artifacts()
                _export_or_verify_bitmaps(pdf_name, page_no, bitmap_artifacts)
                _export_full_page_png(pdf_name, page_no, render_result.get_image())

                results.append((pdf_name, page_no, True, ""))
            except Exception as exc:
                results.append((pdf_name, page_no, False, str(exc)))

        pdf_doc.unload()

    failed = [(doc, page, err) for doc, page, ok, err in results if not ok]
    assert not failed, f"{len(failed)} page(s) failed: " + ", ".join(
        f"{doc}@{page}: {err}" for doc, page, err in failed
    )
