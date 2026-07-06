import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

from PIL import Image, ImageChops, ImageEnhance, ImageStat

from tests.data_utils import (
    RENDER_GROUNDTRUTH_BITMAPS_DIR,
    RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR,
    RENDER_GROUNDTRUTH_PAGES_DIR,
)
from tests.test_parse import _round_floats

RENDER_DELTA_FOLDER = Path("tests/data/render_deltas")


@dataclass(frozen=True)
class ImageTolerance:
    pixel_threshold: int = 12
    mean_abs_error: float = 3.0
    changed_pixels_ratio: float = 0.03


@dataclass(frozen=True)
class ImageComparison:
    mean_abs_error: float
    max_abs_error: int
    changed_pixels_ratio: float


def renderer_artifact_prefix(doc_name: str, page_no: int) -> str:
    return f"{doc_name}.page_no_{page_no}"


def renderer_image_path(doc_name: str, page_no: int) -> Path:
    return RENDER_GROUNDTRUTH_PAGES_DIR / (
        renderer_artifact_prefix(doc_name, page_no) + ".full_page.png"
    )


def renderer_instructions_path(doc_name: str, page_no: int) -> Path:
    return RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR / (
        renderer_artifact_prefix(doc_name, page_no) + ".instructions.json"
    )


def renderer_bitmap_path(filename: str) -> Path:
    return RENDER_GROUNDTRUTH_BITMAPS_DIR / filename


def _write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fw:
        json.dump(data, fw, indent=2)
        fw.write("\n")


def _load_json(path: Path) -> Any:
    with open(path, encoding="utf-8") as fr:
        return json.load(fr)


def normalized_render_instructions(result) -> dict[str, Any]:
    return _round_floats(result._export_render_instructions_json(), ndigits=3)


def _bitmap_metadata(
    artifact: dict[str, Any], exported_filename: str
) -> dict[str, Any]:
    raw_data = bytes(artifact.get("raw_data", b""))
    metadata = {
        key: _round_floats(value, ndigits=3)
        for key, value in artifact.items()
        if key not in {"raw_data", "encoded_data", "extension"}
    }
    metadata["exported_filename"] = exported_filename
    metadata["raw_sha256"] = hashlib.sha256(raw_data).hexdigest()
    return metadata


def write_renderer_groundtruth(doc_name: str, page_no: int, result) -> None:
    RENDER_GROUNDTRUTH_PAGES_DIR.mkdir(parents=True, exist_ok=True)
    RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR.mkdir(parents=True, exist_ok=True)
    RENDER_GROUNDTRUTH_BITMAPS_DIR.mkdir(parents=True, exist_ok=True)

    prefix = renderer_artifact_prefix(doc_name, page_no)
    result.get_image().save(renderer_image_path(doc_name, page_no))
    _write_json(
        renderer_instructions_path(doc_name, page_no),
        normalized_render_instructions(result),
    )

    for artifact in result._export_bitmap_artifacts():
        index = artifact["index"]
        extension = artifact["extension"]
        exported_filename = f"{prefix}.bitmap_{index}{extension}"
        image_path = renderer_bitmap_path(exported_filename)
        image_path.write_bytes(bytes(artifact.get("encoded_data", b"")))
        _write_json(
            renderer_bitmap_path(f"{prefix}.bitmap_{index}.json"),
            _bitmap_metadata(artifact, exported_filename),
        )


def compare_render_instructions(doc_name: str, page_no: int, result) -> None:
    path = renderer_instructions_path(doc_name, page_no)
    assert path.exists(), f"missing render instruction groundtruth: {path}"
    expected = _load_json(path)
    actual = normalized_render_instructions(result)
    if actual == expected:
        return

    assert actual.get("size_instruction") == expected.get("size_instruction"), (
        f"render size instruction mismatch: {path}"
    )
    actual_instructions = actual.get("instructions", [])
    expected_instructions = expected.get("instructions", [])
    assert len(actual_instructions) == len(expected_instructions), (
        f"render instruction count mismatch: {path}"
    )
    actual_types = [instruction.get("type") for instruction in actual_instructions]
    expected_types = [instruction.get("type") for instruction in expected_instructions]
    assert actual_types == expected_types, f"render instruction type mismatch: {path}"


def _diff_artifact_path(doc_name: str, page_no: int, suffix: str) -> Path:
    return (
        RENDER_DELTA_FOLDER / f"{renderer_artifact_prefix(doc_name, page_no)}.{suffix}"
    )


def _write_diff_artifacts(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    expected: Image.Image,
) -> None:
    RENDER_DELTA_FOLDER.mkdir(parents=True, exist_ok=True)
    actual.save(_diff_artifact_path(doc_name, page_no, "actual.png"))
    expected.save(_diff_artifact_path(doc_name, page_no, "expected.png"))

    diff = ImageChops.difference(actual, expected)
    amplified = ImageEnhance.Brightness(diff).enhance(8)
    amplified.save(_diff_artifact_path(doc_name, page_no, "diff.png"))


def compare_images(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    *,
    tolerance: ImageTolerance = ImageTolerance(),
) -> ImageComparison:
    path = renderer_image_path(doc_name, page_no)
    assert path.exists(), f"missing rendered image groundtruth: {path}"

    actual_rgba = actual.convert("RGBA")
    expected_rgba = Image.open(path).convert("RGBA")
    if actual_rgba.size != expected_rgba.size:
        _write_diff_artifacts(doc_name, page_no, actual_rgba, expected_rgba)
        raise AssertionError(
            f"rendered image size mismatch for {path}: "
            f"expected {expected_rgba.size}, got {actual_rgba.size}"
        )

    diff = ImageChops.difference(actual_rgba, expected_rgba)
    stat = ImageStat.Stat(diff)
    mean_abs_error = sum(stat.mean) / len(stat.mean)
    extrema = diff.getextrema()
    if extrema and isinstance(extrema[0], tuple):
        channel_extrema = cast(tuple[tuple[int, int], ...], extrema)
        max_abs_error = max(high for _, high in channel_extrema)
    else:
        single_extrema = cast(tuple[int, int], extrema)
        max_abs_error = single_extrema[1]

    changed_mask = diff.convert("L").point(
        lambda value: 255 if value > tolerance.pixel_threshold else 0
    )
    changed_pixels = changed_mask.histogram()[255]
    changed_pixels_ratio = changed_pixels / (actual_rgba.width * actual_rgba.height)

    comparison = ImageComparison(
        mean_abs_error=mean_abs_error,
        max_abs_error=max_abs_error,
        changed_pixels_ratio=changed_pixels_ratio,
    )

    if (
        comparison.mean_abs_error > tolerance.mean_abs_error
        or comparison.changed_pixels_ratio > tolerance.changed_pixels_ratio
    ):
        _write_diff_artifacts(doc_name, page_no, actual_rgba, expected_rgba)
        raise AssertionError(
            "rendered image mismatch for "
            f"{path}: mean_abs_error={comparison.mean_abs_error:.4f} "
            f"(limit {tolerance.mean_abs_error}), "
            f"changed_pixels_ratio={comparison.changed_pixels_ratio:.4f} "
            f"(limit {tolerance.changed_pixels_ratio}), "
            f"max_abs_error={comparison.max_abs_error}; "
            f"diff artifacts written to {RENDER_DELTA_FOLDER}"
        )

    return comparison


def compare_bitmap_artifacts(doc_name: str, page_no: int, result) -> None:
    prefix = renderer_artifact_prefix(doc_name, page_no)
    artifacts = result._export_bitmap_artifacts()

    for artifact in artifacts:
        index = artifact["index"]
        extension = artifact["extension"]
        exported_filename = f"{prefix}.bitmap_{index}{extension}"
        image_path = renderer_bitmap_path(exported_filename)
        metadata_path = renderer_bitmap_path(f"{prefix}.bitmap_{index}.json")

        assert image_path.exists(), f"missing bitmap image groundtruth: {image_path}"
        assert metadata_path.exists(), (
            f"missing bitmap metadata groundtruth: {metadata_path}"
        )

        expected_metadata = _load_json(metadata_path)
        actual_metadata = _bitmap_metadata(artifact, exported_filename)
        stable_keys = [
            "index",
            "xobject_key",
            "shape",
            "pixel_format",
            "image_mask",
            "quad",
            "exported_filename",
            "raw_sha256",
        ]
        for key in stable_keys:
            if key not in expected_metadata:
                continue
            value = expected_metadata[key]
            assert actual_metadata.get(key) == value, (
                f"bitmap metadata mismatch for {key}: {metadata_path}"
            )
        assert bytes(artifact.get("encoded_data", b"")) == image_path.read_bytes(), (
            f"bitmap image mismatch: {image_path}"
        )
