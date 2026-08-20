import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat

from tests.data_utils import (
    RENDER_GROUNDTRUTH_BITMAPS_DIR,
    RENDER_GROUNDTRUTH_GLYPHS_DIR,
    RENDER_GROUNDTRUTH_INSTRUCTIONS_DIR,
    RENDER_GROUNDTRUTH_PAGES_DIR,
)
from tests.test_parse import _round_floats

RENDER_DELTA_FOLDER = Path("tests/data/render_deltas")
RENDER_VISUALIZATION_FOLDER = Path("tests/data/visualizations")


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
    # Page-level dissimilarity in [0, 1]; see normalized_delta() for what it
    # measures and why it is not mean_abs_error/255.
    normalized_delta: float = 0.0


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


def bitmap_artifact_targets(prefix: str, result) -> list[tuple[dict[str, Any], Path]]:
    """Pair every bitmap artifact with the path stem it is stored under.

    A rasterised Type3 glyph is a character rather than page artwork, and one
    is emitted per painted character, so a single page of Type3 text buries
    the handful of real images under hundreds of glyph masks. The decoder
    labels each bitmap with its `source`; glyphs go to their own directory and
    carry their own numbering, so adding an image to a page does not renumber
    every glyph on it.
    """
    targets: list[tuple[dict[str, Any], Path]] = []
    counters = {"bitmap": 0, "glyph": 0}

    for artifact in result._export_bitmap_artifacts():
        kind = "glyph" if artifact.get("source") == "type3_glyph" else "bitmap"
        counters[kind] += 1
        directory = (
            RENDER_GROUNDTRUTH_GLYPHS_DIR
            if kind == "glyph"
            else RENDER_GROUNDTRUTH_BITMAPS_DIR
        )
        targets.append((artifact, directory / f"{prefix}.{kind}_{counters[kind]}"))

    return targets


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
    RENDER_GROUNDTRUTH_GLYPHS_DIR.mkdir(parents=True, exist_ok=True)

    prefix = renderer_artifact_prefix(doc_name, page_no)
    result.get_image().save(renderer_image_path(doc_name, page_no))
    _write_json(
        renderer_instructions_path(doc_name, page_no),
        normalized_render_instructions(result),
    )

    for artifact, stem in bitmap_artifact_targets(prefix, result):
        image_path = stem.with_name(stem.name + artifact["extension"])
        image_path.write_bytes(bytes(artifact.get("encoded_data", b"")))
        _write_json(
            stem.with_name(stem.name + ".json"),
            _bitmap_metadata(artifact, image_path.name),
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
    for artifact, stem in bitmap_artifact_targets(prefix, result):
        if not stem.with_name(stem.name + artifact["extension"]).exists():
            return False

        if not stem.with_name(stem.name + ".json").exists():
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


def difference_image(actual: Image.Image, expected: Image.Image) -> Image.Image:
    """Per-pixel absolute difference of both images, on the size of `expected`.

    The difference is shown as it is, with no brightness scaling. A panel is
    then directly readable -- a grey level of 40 means the two renders are 40
    apart there -- and, more importantly, two panels of different pages are
    produced by the same fixed transform, so they can be held next to each
    other and compared. Any per-image or even constant amplification breaks
    that: it makes a near-identical page look as damaged as a broken one.

    The cost is that an accurate page renders as an almost black panel, which
    is the honest depiction of "these two renders agree".
    """
    return ImageChops.difference(
        resize_to_match(actual.convert("RGB"), expected),
        expected.convert("RGB"),
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
    difference_image(actual, expected).save(
        _diff_artifact_path(doc_name, page_no, "diff.png")
    )


def normalized_delta(actual: Image.Image, expected: Image.Image) -> float:
    """Dissimilarity of two same-size renders on a 0.0 - 1.0 scale.

    Per pixel the deviation is the *largest* of the three channel differences,
    divided by 255; the score is the mean of that over the page. So 0.0 is a
    pixel-identical render and 1.0 is every pixel maximally inverted -- a bound
    that is actually attainable, which is what makes the number comparable
    across pages and documents.

    Two deliberate differences to `mean_abs_error`:

    - Alpha is excluded. Both renders are flattened onto an opaque background
      before they are compared, so their alpha channels are identical by
      construction and averaging over four channels only scales the result by
      3/4.
    - Channels are combined with max() rather than with a mean or a luma
      weighting. A glyph painted pure blue where it should be black differs by
      255 in one channel; averaging reports 85 and luma reports 29, both of
      which understate a difference that is entirely obvious on screen.

    The score stays an average over the whole page, so it is dominated by page
    area: a mostly-white page with badly wrong text scores low in absolute
    terms. It ranks pages against each other, it does not say "this render is
    4% wrong".
    """
    diff = ImageChops.difference(
        resize_to_match(actual.convert("RGB"), expected.convert("RGB")),
        expected.convert("RGB"),
    )
    red, green, blue = diff.split()
    per_pixel_max = ImageChops.lighter(ImageChops.lighter(red, green), blue)

    return ImageStat.Stat(per_pixel_max).mean[0] / 255.0


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
        normalized_delta=normalized_delta(actual_full, expected_full),
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
        "Per-page image metrics (worst first)",
        (
            f"  {'document':{document_width}}  page  actual_size  expected_size  "
            "size_match  delta  mean_abs_error  changed_pixels_ratio  "
            "max_abs_error  changed_pixels"
        ),
    ]

    # worst first: the pages worth looking at are the ones at the top, and the
    # long tail of near-identical pages scrolls off the bottom
    for comparison in sorted(
        comparisons,
        key=lambda item: (-item.mean_abs_error, item.document, item.page),
    ):
        actual_size = f"{comparison.actual_width}x{comparison.actual_height}"
        expected_size = f"{comparison.expected_width}x{comparison.expected_height}"
        lines.append(
            f"  {comparison.document:{document_width}}  "
            f"{comparison.page:>4}  "
            f"{actual_size:11}  "
            f"{expected_size:13}  "
            f"{comparison.size_matches!s:10}  "
            f"{comparison.normalized_delta:.4f}  "
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

    for artifact, stem in bitmap_artifact_targets(prefix, result):
        image_path = stem.with_name(stem.name + artifact["extension"])
        metadata_path = stem.with_name(stem.name + ".json")

        assert image_path.exists(), f"missing bitmap image groundtruth: {image_path}"
        assert metadata_path.exists(), (
            f"missing bitmap metadata groundtruth: {metadata_path}"
        )

        expected_metadata = _load_json(metadata_path)
        actual_metadata = _bitmap_metadata(artifact, image_path.name)
        stable_keys = [
            "index",
            "xobject_key",
            "source",
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
    # `delta` is normalized_delta, so every name is "0.xxxx" and a plain
    # alphabetical listing of the folder is also a worst-last ranking. An
    # unbounded score does not have that property: "delta_16.308" sorts
    # between "delta_1.6" and "delta_2.0".
    return (
        folder / f"delta_{delta:.4f}_{renderer_artifact_prefix(doc_name, page_no)}.png"
    )


def write_comparison_visualization(
    doc_name: str,
    page_no: int,
    reference: Image.Image,
    actual: Image.Image,
    delta: float,  # normalized_delta of the pair, used in the name and the label
    *,
    reference_label: str = "pypdfium2",
    actual_label: str = "docling-parse",
    folder: Path = RENDER_VISUALIZATION_FOLDER,
    gap: int = 8,
) -> Path:
    """Write a three-panel png: reference, actual and their amplified difference."""
    reference_rgb = flatten_on_white(reference)
    actual_rgb = flatten_on_white(actual)
    difference = difference_image(actual_rgb, reference_rgb)

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
            f"difference (delta={delta:.4f})",
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


def histogram_path(metric: str, *, folder: Path = RENDER_VISUALIZATION_FOLDER) -> Path:
    return folder / f"../histogram_{metric}.png"


def write_metric_histogram(
    values: list[float],
    *,
    metric: str,
    title: str,
    xlabel: str,
    threshold: float | None = None,
    threshold_label: str = "tolerance",
    folder: Path = RENDER_VISUALIZATION_FOLDER,
    bins: int = 40,
) -> Path | None:
    """Write the distribution of one per-page metric over a whole comparison run.

    The per-page panels answer "what is wrong with this page"; this answers
    "how is the corpus doing", which is the number that moves when a decoder
    changes. Returns the written path, or None when there is nothing to plot or
    matplotlib is unavailable -- it lives in the `perf` dependency group, so a
    run without that group still gets the table and the panels.
    """
    if not values:
        return None

    try:
        import matplotlib
    except ImportError:
        return None

    matplotlib.use("Agg")  # headless: never try to open a window in CI

    import matplotlib.pyplot as plt

    # same palette as the three-panel visualizations, so the folder reads as
    # one set of artifacts
    background = "#181818"
    foreground = "#f0f0f0"
    bar_color = "#4da3ff"
    median_color = "#ffc14d"
    threshold_color = "#ff6b6b"

    ordered = sorted(values)
    count = len(ordered)
    mean_value = sum(ordered) / count
    median_value = (
        ordered[count // 2]
        if count % 2
        else 0.5 * (ordered[count // 2 - 1] + ordered[count // 2])
    )
    # a degenerate range (every page pixel-identical) has no bin edges to place,
    # so fall back to a nominal axis rather than plotting a 1e-9-wide one
    upper = ordered[-1] if ordered[-1] > 0.0 else 1.0

    figure, axes = plt.subplots(figsize=(9.0, 5.0), dpi=160)
    figure.patch.set_facecolor(background)
    axes.set_facecolor(background)

    axes.hist(
        ordered, bins=bins, range=(0.0, upper), color=bar_color, edgecolor=background
    )

    # Counts are heavily skewed -- most pages sit in the first bin and the tail
    # that matters is one or two pages deep. On a linear count axis that tail is
    # invisible, so the y axis is logarithmic and the floor is held below 1 to
    # keep single-page bins on screen.
    axes.set_yscale("log")
    axes.set_ylim(bottom=0.5)

    axes.axvline(
        median_value,
        color=median_color,
        linestyle="--",
        linewidth=1.4,
        label=f"median {median_value:.4f}",
    )
    axes.axvline(
        mean_value,
        color=foreground,
        linestyle=":",
        linewidth=1.4,
        label=f"mean {mean_value:.4f}",
    )
    if threshold is not None:
        above = sum(1 for value in ordered if value > threshold)
        axes.axvline(
            threshold,
            color=threshold_color,
            linestyle="-",
            linewidth=1.4,
            label=f"{threshold_label} {threshold:g} ({above} page(s) above)",
        )

    axes.set_title(f"{title}  --  {count} page(s)", color=foreground, fontsize=12)
    axes.set_xlabel(xlabel, color=foreground)
    axes.set_ylabel("pages (log scale)", color=foreground)
    axes.tick_params(colors=foreground)
    for spine in axes.spines.values():
        spine.set_color("#404040")
    axes.grid(axis="y", color="#303030", linewidth=0.6)
    axes.set_axisbelow(True)

    legend = axes.legend(facecolor=background, edgecolor="#404040", fontsize=9)
    for text in legend.get_texts():
        text.set_color(foreground)

    figure.tight_layout()

    path = histogram_path(metric, folder=folder)
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, facecolor=background)
    plt.close(figure)
    return path


# ---------------------------------------------------------------------------
# Region probes
#
# The page-wide comparison above is a coarse instrument on purpose: it guards
# whole renders against wholesale drift, with a tolerance loose enough to
# survive CI font differences. A defect confined to one graphic moves that
# number by a few hundredths and passes untouched -- a clip that degraded a
# crescent to its bounding box cost its page 0.231 mean_abs_error.
#
# So the focused tests measure the region they are about, and assert on what
# the fix was: the colour that came out of a tint transform, whether a filled
# region is a disc or the square around it, whether a pattern repeats.
# ---------------------------------------------------------------------------

BACKGROUND_THRESHOLD = 250


def region_image(result, box: tuple[float, float, float, float], *, scale: float = 2.0):
    """Render one page-coordinate box of an already-parsed page.

    `box` is (left, top, right, bottom) in PDF points from the top-left of the
    page, matching how the renderer's crop is specified.
    """
    from docling_core.types.doc.base import BoundingBox, CoordOrigin

    left, top, right, bottom = box
    cropbox = BoundingBox(
        l=left, t=top, r=right, b=bottom, coord_origin=CoordOrigin.TOPLEFT
    )
    return flatten_on_white(result.get_image(scale=scale, cropbox=cropbox))


def average_color(image: Image.Image) -> tuple[float, float, float]:
    stat = ImageStat.Stat(image.convert("RGB"))
    return (stat.mean[0], stat.mean[1], stat.mean[2])


def center_color(image: Image.Image) -> tuple[int, int, int]:
    """Colour at the middle of `image`, away from any antialiased border."""
    rgb = image.convert("RGB")
    pixel = rgb.getpixel((rgb.width // 2, rgb.height // 2))
    assert isinstance(pixel, tuple), "RGB images yield 3-tuple pixels"
    red, green, blue = pixel
    return (red, green, blue)


def color_distance(
    actual: tuple[float, float, float], expected: tuple[float, float, float]
) -> float:
    return max(abs(a - b) for a, b in zip(actual, expected))


def assert_color_near(
    actual: tuple[float, float, float],
    expected: tuple[float, float, float],
    *,
    tolerance: float,
    what: str,
) -> None:
    distance = color_distance(actual, expected)
    assert distance <= tolerance, (
        f"{what}: expected rgb≈{tuple(round(v) for v in expected)}, "
        f"got {tuple(round(v) for v in actual)} "
        f"(max channel distance {distance:.1f} > {tolerance})"
    )


def ink_mask(image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD):
    """Boolean-ish mask of pixels darker than the page background."""
    return image.convert("L").point(lambda value: 255 if value < threshold else 0)


def coverage_ratio(
    image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD
) -> float:
    """Fraction of `image` carrying ink.

    This is what separates a shape from its bounding box without pinning exact
    pixels: a disc inscribed in its box covers pi/4 of it, a square covers all
    of it.
    """
    mask = ink_mask(image, threshold=threshold)
    return mask.histogram()[255] / (image.width * image.height)


def quadrant_coverage(
    image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD
) -> dict[str, float]:
    """Ink coverage of the four corners and the centre of `image`.

    A round shape leaves its corners empty while filling its centre; the
    bounding box that replaces it when clipping fails fills both.
    """
    width, height = image.size
    corner_w = max(1, width // 6)
    corner_h = max(1, height // 6)
    boxes = {
        "top_left": (0, 0, corner_w, corner_h),
        "top_right": (width - corner_w, 0, width, corner_h),
        "bottom_left": (0, height - corner_h, corner_w, height),
        "bottom_right": (width - corner_w, height - corner_h, width, height),
        "center": (
            width // 2 - corner_w // 2,
            height // 2 - corner_h // 2,
            width // 2 + corner_w // 2 + 1,
            height // 2 + corner_h // 2 + 1,
        ),
    }
    return {
        name: coverage_ratio(image.crop(box), threshold=threshold)
        for name, box in boxes.items()
    }


def row_ink_profile(
    image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD
) -> list[int]:
    """Per-row ink pixel counts, top to bottom."""
    mask = ink_mask(image, threshold=threshold)
    width = mask.width
    data = list(mask.getdata())
    return [
        sum(1 for value in data[y * width : (y + 1) * width] if value)
        for y in range(mask.height)
    ]


def column_ink_profile(
    image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD
) -> list[int]:
    """Per-column ink pixel counts, left to right."""
    mask = ink_mask(image, threshold=threshold)
    width, height = mask.size
    data = list(mask.getdata())
    return [sum(1 for y in range(height) if data[y * width + x]) for x in range(width)]


def ink_bounds(
    image: Image.Image, *, threshold: int = BACKGROUND_THRESHOLD
) -> tuple[int, int, int, int] | None:
    """Bounding box of the ink in `image`, or None when the region is blank."""
    return ink_mask(image, threshold=threshold).getbbox()
