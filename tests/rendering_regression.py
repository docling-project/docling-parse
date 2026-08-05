import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFont, ImageStat

from tests.data_utils import (
    RENDER_GROUNDTRUTH_BITMAPS_DIR,
    RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR,
    RENDER_GROUNDTRUTH_PAGES_DIR,
)
from tests.test_parse import _round_floats

RENDER_DELTA_FOLDER = Path("tests/data/render_deltas")
RENDER_VISUALIZATION_FOLDER = Path("tests/data/visualizations")

# amplification of the per-pixel difference in the visualization panel: raw
# deltas of a few grey levels are invisible on screen
DIFFERENCE_AMPLIFICATION = 8


@dataclass(frozen=True)
class ImageTolerance:
    pixel_threshold: int = 12
    mean_abs_error: float = 3.0
    changed_pixels_ratio: float = 0.03


@dataclass(frozen=True)
class ImageComparison:
    document: str
    page: int
    actual_width: int
    actual_height: int
    expected_width: int
    expected_height: int
    size_matches: bool
    mean_abs_error: float
    max_abs_error: int
    changed_pixels_ratio: float
    changed_pixels: int
    total_pixels: int


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


def renderer_groundtruth_exists(doc_name: str, page_no: int, result) -> bool:
    """True when every renderer groundtruth artifact of this page is present.

    A page whose groundtruth is incomplete is regenerated as a whole, so that the
    png, the instructions and the bitmaps always describe the same render.
    """
    if not renderer_image_path(doc_name, page_no).exists():
        return False

    if not renderer_instructions_path(doc_name, page_no).exists():
        return False

    prefix = renderer_artifact_prefix(doc_name, page_no)
    for artifact in result._export_bitmap_artifacts():
        index = artifact["index"]
        extension = artifact["extension"]

        if not renderer_bitmap_path(f"{prefix}.bitmap_{index}{extension}").exists():
            return False

        if not renderer_bitmap_path(f"{prefix}.bitmap_{index}.json").exists():
            return False

    return True


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
    len_actual_instructions = len(actual_instructions)
    len_expected_instructions = len(expected_instructions)
    assert len_actual_instructions == len_expected_instructions, (
        f"render instruction count mismatch: {len_actual_instructions} != "
        f"{len_expected_instructions} in {path}"
    )
    actual_types = [instruction.get("type") for instruction in actual_instructions]
    expected_types = [instruction.get("type") for instruction in expected_instructions]
    # compare through a local flag: pytest would otherwise inline both full
    # instruction-type lists into the assertion failure report
    types_match = actual_types == expected_types
    assert types_match, f"render instruction type mismatch: {path}"


def _diff_artifact_path(doc_name: str, page_no: int, suffix: str) -> Path:
    return (
        RENDER_DELTA_FOLDER / f"{renderer_artifact_prefix(doc_name, page_no)}.{suffix}"
    )


def resize_to_match(actual: Image.Image, expected: Image.Image) -> Image.Image:
    """Scale `actual` onto the size of `expected`, if they differ.

    Both renders cover the same page box, so a page whose size in pixels comes
    out one or two pixels apart is showing the same content at a marginally
    different scale. Cropping to the common area would then compare pixels that
    drift further apart towards the far edge of the page, which reports a large
    error for renders that are visually identical. Resampling first puts the
    page content back on top of itself; the size difference itself is reported
    separately through `size_matches`.
    """
    if actual.size == expected.size:
        return actual

    return actual.resize(expected.size, Image.Resampling.LANCZOS)


def amplified_difference(
    actual: Image.Image,
    expected: Image.Image,
    *,
    amplification: int = DIFFERENCE_AMPLIFICATION,
) -> Image.Image:
    """Brightened per-pixel difference of both images, on the size of `expected`."""
    diff = ImageChops.difference(
        resize_to_match(actual.convert("RGB"), expected),
        expected.convert("RGB"),
    )
    return ImageEnhance.Brightness(diff).enhance(amplification)


def _write_diff_artifacts(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    expected: Image.Image,
) -> None:
    RENDER_DELTA_FOLDER.mkdir(parents=True, exist_ok=True)
    actual.save(_diff_artifact_path(doc_name, page_no, "actual.png"))
    expected.save(_diff_artifact_path(doc_name, page_no, "expected.png"))
    amplified_difference(actual, expected).save(
        _diff_artifact_path(doc_name, page_no, "diff.png")
    )


def measure_image_pair(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    expected: Image.Image,
    *,
    tolerance: ImageTolerance = ImageTolerance(),
) -> ImageComparison:
    """Compare two rendered images of the same page, whatever produced them.

    An image of differing size is resampled onto the size of `expected` before
    the pixels are compared, so the metrics describe the page content rather
    than the size difference; the size mismatch itself is reported through
    `size_matches`.
    """
    actual_full = actual.convert("RGBA")
    expected_full = expected.convert("RGBA")
    size_matches = actual_full.size == expected_full.size
    width, height = expected_full.size
    if width <= 0 or height <= 0 or actual_full.width <= 0 or actual_full.height <= 0:
        raise AssertionError(
            f"rendered image has empty comparison area for {doc_name}@{page_no}: "
            f"expected {expected_full.size}, got {actual_full.size}"
        )

    actual_rgba = resize_to_match(actual_full, expected_full)
    diff = ImageChops.difference(actual_rgba, expected_full)
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

    return ImageComparison(
        document=doc_name,
        page=page_no,
        actual_width=actual_full.width,
        actual_height=actual_full.height,
        expected_width=expected_full.width,
        expected_height=expected_full.height,
        size_matches=size_matches,
        mean_abs_error=mean_abs_error,
        max_abs_error=max_abs_error,
        changed_pixels_ratio=changed_pixels_ratio,
        changed_pixels=changed_pixels,
        total_pixels=actual_rgba.width * actual_rgba.height,
    )


def measure_image_comparison(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    *,
    tolerance: ImageTolerance = ImageTolerance(),
) -> ImageComparison:
    path = renderer_image_path(doc_name, page_no)
    assert path.exists(), f"missing rendered image groundtruth: {path}"

    return measure_image_pair(
        doc_name,
        page_no,
        actual,
        Image.open(path),
        tolerance=tolerance,
    )


def image_comparison_failed(
    comparison: ImageComparison,
    *,
    tolerance: ImageTolerance = ImageTolerance(),
) -> bool:
    return (
        not comparison.size_matches
        or comparison.mean_abs_error > tolerance.mean_abs_error
        or comparison.changed_pixels_ratio > tolerance.changed_pixels_ratio
    )


def format_image_comparison_table(comparisons: list[ImageComparison]) -> str:
    if not comparisons:
        return "No rendered image comparisons were collected."

    document_width = max(
        len("document"),
        *(len(comparison.document) for comparison in comparisons),
    )
    lines = [
        "Per-page image metrics",
        (
            f"  {'document':{document_width}}  page  actual_size  expected_size  "
            "size_match  mean_abs_error  changed_pixels_ratio  max_abs_error  "
            "changed_pixels"
        ),
    ]

    for comparison in sorted(
        comparisons,
        key=lambda item: (item.document, item.page),
    ):
        actual_size = f"{comparison.actual_width}x{comparison.actual_height}"
        expected_size = f"{comparison.expected_width}x{comparison.expected_height}"
        lines.append(
            f"  {comparison.document:{document_width}}  "
            f"{comparison.page:>4}  "
            f"{actual_size:11}  "
            f"{expected_size:13}  "
            f"{comparison.size_matches!s:10}  "
            f"{comparison.mean_abs_error:14.4f}  "
            f"{comparison.changed_pixels_ratio:20.4f}  "
            f"{comparison.max_abs_error:13d}  "
            f"{comparison.changed_pixels:14d}"
        )

    return "\n".join(lines)


def compare_images(
    doc_name: str,
    page_no: int,
    actual: Image.Image,
    *,
    tolerance: ImageTolerance = ImageTolerance(),
) -> ImageComparison:
    comparison = measure_image_comparison(
        doc_name,
        page_no,
        actual,
        tolerance=tolerance,
    )
    path = renderer_image_path(doc_name, page_no)
    actual_rgba = actual.convert("RGBA")
    expected_rgba = Image.open(path).convert("RGBA")

    if not comparison.size_matches:
        _write_diff_artifacts(doc_name, page_no, actual_rgba, expected_rgba)
        raise AssertionError(
            f"rendered image size mismatch for {path}: "
            f"expected {expected_rgba.size}, got {actual_rgba.size}"
        )

    if image_comparison_failed(comparison, tolerance=tolerance):
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


def render_pypdfium_page(
    pdf_path: str | Path,
    page_no: int,
    *,
    scale: float,
) -> Image.Image:
    """Render one page with pypdfium2, as the cross-renderer reference."""
    import pypdfium2 as pdfium

    pdf = pdfium.PdfDocument(str(pdf_path))
    try:
        page = pdf[page_no - 1]
        return page.render(scale=scale).to_pil().convert("RGBA")
    finally:
        pdf.close()


def flatten_on_white(image: Image.Image) -> Image.Image:
    """Composite an image onto opaque white.

    The two renderers do not agree on what an untouched pixel is: pypdfium2
    paints an opaque white page, while the Blend2D path can leave the page
    background transparent. Comparing them only makes sense on a common
    background.
    """
    rgba = image.convert("RGBA")
    canvas = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    canvas.alpha_composite(rgba)
    return canvas.convert("RGB")


def _label_font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        # Pillow < 10.1 only offers the fixed-size bitmap default font
        return ImageFont.load_default()


def _labelled_panel(
    image: Image.Image,
    label: str,
    *,
    size: tuple[int, int],
    label_height: int,
    font_size: int,
    font,
    background: tuple[int, int, int],
    foreground: tuple[int, int, int],
) -> Image.Image:
    panel = Image.new("RGB", (size[0], size[1] + label_height), background)
    panel.paste(image.convert("RGB"), (0, label_height))
    ImageDraw.Draw(panel).text(
        (label_height // 3, max(0, (label_height - font_size) // 2)),
        label,
        fill=foreground,
        font=font,
    )
    return panel


def visualization_path(
    doc_name: str,
    page_no: int,
    delta: float,
    *,
    folder: Path = RENDER_VISUALIZATION_FOLDER,
) -> Path:
    return (
        folder / f"delta_{delta:.3f}_{renderer_artifact_prefix(doc_name, page_no)}.png"
    )


def write_comparison_visualization(
    doc_name: str,
    page_no: int,
    reference: Image.Image,
    actual: Image.Image,
    delta: float,
    *,
    reference_label: str = "pypdfium2",
    actual_label: str = "docling-parse",
    folder: Path = RENDER_VISUALIZATION_FOLDER,
    gap: int = 8,
) -> Path:
    """Write a three-panel png: reference, actual and their amplified difference."""
    reference_rgb = flatten_on_white(reference)
    actual_rgb = flatten_on_white(actual)
    difference = amplified_difference(actual_rgb, reference_rgb)

    panel_width = max(reference_rgb.width, actual_rgb.width, difference.width)
    panel_height = max(reference_rgb.height, actual_rgb.height, difference.height)
    panel_size = (panel_width, panel_height)
    # the label has to stay readable when the full three-panel image is scaled
    # down to fit on screen, so it grows with the page
    font_size = max(12, panel_width // 40)
    label_height = font_size * 2
    font = _label_font(font_size)
    background = (24, 24, 24)
    foreground = (240, 240, 240)

    panels = [
        _labelled_panel(
            reference_rgb,
            reference_label,
            size=panel_size,
            label_height=label_height,
            font_size=font_size,
            font=font,
            background=background,
            foreground=foreground,
        ),
        _labelled_panel(
            actual_rgb,
            actual_label,
            size=panel_size,
            label_height=label_height,
            font_size=font_size,
            font=font,
            background=background,
            foreground=foreground,
        ),
        _labelled_panel(
            difference,
            f"difference (x{DIFFERENCE_AMPLIFICATION}, mean_abs_error={delta:.3f})",
            size=panel_size,
            label_height=label_height,
            font_size=font_size,
            font=font,
            background=background,
            foreground=foreground,
        ),
    ]

    canvas = Image.new(
        "RGB",
        (
            len(panels) * panel_width + (len(panels) - 1) * gap,
            panel_height + label_height,
        ),
        background,
    )
    for index, panel in enumerate(panels):
        canvas.paste(panel, (index * (panel_width + gap), 0))

    path = visualization_path(doc_name, page_no, delta, folder=folder)
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)
    return path
