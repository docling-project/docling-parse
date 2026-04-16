#!/usr/bin/env python
import hashlib
import json
import os
from pathlib import Path

from docling_parse.pdf_parser import DecodePageConfig, DoclingPdfParser, PdfDocument

GENERATE = False

GROUNDTRUTH_RENDERER_FOLDER = "tests/data/groundtruth_renderer"

RENDER_CASES = {
    "font_01.pdf": [1],
    "rotated_page_01.pdf": [1],
    "fillable_form.pdf": [1],
    "indexed_iccbased.pdf": [1],
    "rotated_image.pdf": [1],
}

BITMAP_RESTRICTIONS = {
    "indexed_iccbased.pdf": {
        1: [1, 5, 10, 15],
    },
}


def _round_floats(obj, ndigits=3):
    if isinstance(obj, float):
        return round(obj, ndigits)
    if isinstance(obj, dict):
        return {k: _round_floats(v, ndigits) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_round_floats(v, ndigits) for v in obj]
    return obj


def _page_prefix(pdf_name: str, page_no: int) -> Path:
    return Path(GROUNDTRUTH_RENDERER_FOLDER) / f"{pdf_name}.page_no_{page_no}"


def _instruction_path(pdf_name: str, page_no: int) -> Path:
    return Path(f"{_page_prefix(pdf_name, page_no)}.instructions.json")


def _bitmap_json_path(pdf_name: str, page_no: int, bitmap_index: int) -> Path:
    return Path(f"{_page_prefix(pdf_name, page_no)}.bitmap_{bitmap_index}.json")


def _write_json(path: Path, payload) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fw:
        json.dump(_round_floats(payload), fw, indent=2)


def _load_json(path: Path):
    with open(path, "r", encoding="utf-8") as fr:
        return json.load(fr)


def _artifact_basename(pdf_name: str, page_no: int, bitmap_index: int, extension: str) -> str:
    return f"{pdf_name}.page_no_{page_no}.bitmap_{bitmap_index}{extension}"


def _export_or_verify_bitmaps(pdf_name: str, page_no: int, bitmaps) -> None:
    for bitmap_index, bitmap in enumerate(bitmaps, start=1):
        allowed = BITMAP_RESTRICTIONS.get(pdf_name, {}).get(page_no)
        if allowed is not None and bitmap_index not in allowed:
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
        assert true_sidecar == _round_floats(
            sidecar
        ), f"bitmap metadata mismatch for {sidecar_path}"

        with open(artifact_path, "rb") as fr:
            true_bytes = fr.read()
        assert (
            true_bytes == bitmap["encoded_data"]
        ), f"bitmap artifact bytes mismatch for {artifact_path}"


def test_render_reference_documents():
    parser = DoclingPdfParser(loglevel="fatal")

    config = DecodePageConfig()
    config.page_boundary = "crop_box"
    config.do_sanitization = False
    config.keep_glyphs = True
    config.keep_qpdf_warnings = False

    results = []

    for pdf_name, page_numbers in RENDER_CASES.items():
        pdf_path = os.path.join("tests/data/regression", pdf_name)

        pdf_doc: PdfDocument = parser.load(path_or_stream=pdf_path, lazy=True)
        assert pdf_doc is not None

        for page_no in page_numbers:
            try:
                page_decoder = pdf_doc._parser.get_page_decoder(
                    key=pdf_doc._key,
                    page=page_no - 1,
                    config=config,
                )
                assert page_decoder is not None, f"failed to decode {pdf_name}@{page_no}"

                instructions = page_decoder.export_render_instructions_json()
                instruction_path = _instruction_path(pdf_name, page_no)

                if GENERATE or (not instruction_path.exists()):
                    _write_json(instruction_path, instructions)
                else:
                    true_instructions = _load_json(instruction_path)
                    assert true_instructions == _round_floats(
                        instructions
                    ), f"render instructions mismatch for {instruction_path}"

                bitmap_artifacts = page_decoder.export_bitmap_artifacts()
                _export_or_verify_bitmaps(pdf_name, page_no, bitmap_artifacts)

                results.append((pdf_name, page_no, True, ""))
            except Exception as exc:
                results.append((pdf_name, page_no, False, str(exc)))
            finally:
                pdf_doc.unload_pages(page_range=(page_no, page_no + 1))

    failed = [(doc, page, err) for doc, page, ok, err in results if not ok]
    assert not failed, f"{len(failed)} page(s) failed: " + ", ".join(
        f"{doc}@{page}: {err}" for doc, page, err in failed
    )
