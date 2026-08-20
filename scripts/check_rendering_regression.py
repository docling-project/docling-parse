#!/usr/bin/env python
"""Report image-comparison metrics for pairs of rendered pages.

This is a triage and calibration tool, not a regression check: the regression
itself is `tests/test_threaded_render.py`, which renders the corpus and asserts
against the stored groundtruth. What this adds is the analysis that a pass/fail
assertion cannot give you --- a per-page table, the distribution of the error,
and, with `--json`, the `observed_limits` you would use to set `ImageTolerance`.

It therefore works on images that already exist rather than rendering anything,
which is also why it needs nothing from the test-suite:

    # re-read the artifacts a failing render regression left behind
    python scripts/check_rendering_regression.py --from-deltas tests/data/render_deltas

    # compare two arbitrary images
    python scripts/check_rendering_regression.py --actual a.png --expected b.png --json
"""

from __future__ import annotations

import argparse
import json
import math
from collections.abc import Iterable, Sequence
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import cast

from PIL import Image, ImageChops, ImageStat

DEFAULT_DELTA_DIR = Path("tests/data/render_deltas")
DEFAULT_PIXEL_THRESHOLD = 12


@dataclass(frozen=True)
class ImagePair:
    name: str
    actual: Path
    expected: Path


@dataclass(frozen=True)
class ImageMetrics:
    document: str
    page: int | None
    name: str
    expected_source: str
    actual_source: str
    width: int
    height: int
    expected_width: int
    expected_height: int
    size_matches: bool
    mean_abs_error: float
    changed_pixels_ratio: float
    max_abs_error: int
    changed_pixels: int
    total_pixels: int


@dataclass(frozen=True)
class CheckedImage:
    metrics: ImageMetrics
    abs_error_histogram: list[int]


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Summarize image-comparison metrics for pairs of rendered pages. "
            "Reads existing images; use --from-deltas or --actual/--expected."
        )
    )
    parser.add_argument(
        "--from-deltas",
        type=Path,
        help=(
            "Directory of *.actual.png/*.expected.png pairs, as written by a "
            "failing render regression (tests/data/render_deltas)."
        ),
    )
    parser.add_argument(
        "--actual",
        type=Path,
        help="Actual rendered image. Must be used together with --expected.",
    )
    parser.add_argument(
        "--expected",
        type=Path,
        help="Expected/reference image. Must be used together with --actual.",
    )
    parser.add_argument(
        "--pixel-threshold",
        type=int,
        default=DEFAULT_PIXEL_THRESHOLD,
        help=(
            "Per-pixel grayscale diff threshold used for changed_pixels_ratio "
            f"({DEFAULT_PIXEL_THRESHOLD})."
        ),
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of text.",
    )
    return parser.parse_args()


def _discover_delta_pairs(delta_dir: Path) -> list[ImagePair]:
    pairs: list[ImagePair] = []
    for actual in sorted(delta_dir.glob("*.actual.png")):
        prefix = actual.name.removesuffix(".actual.png")
        expected = actual.with_name(f"{prefix}.expected.png")
        if expected.exists():
            pairs.append(ImagePair(prefix, actual, expected))
    return pairs


def _merge_histograms(histograms: Iterable[Sequence[int]]) -> list[int]:
    merged = [0] * 256
    for histogram in histograms:
        for value, count in enumerate(histogram):
            merged[value] += count
    return merged


def _percentile(sorted_values: Sequence[float], percentile: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]

    position = (len(sorted_values) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]

    lower_value = sorted_values[lower]
    upper_value = sorted_values[upper]
    return lower_value + (upper_value - lower_value) * (position - lower)


def _percentile_from_histogram(histogram: Sequence[int], percentile: float) -> int:
    total = sum(histogram)
    if total == 0:
        return 0

    target = max(1, math.ceil(total * percentile))
    seen = 0
    for value, count in enumerate(histogram):
        seen += count
        if seen >= target:
            return value
    return len(histogram) - 1


def _bucket_count(histogram: Sequence[int], start: int, end: int) -> int:
    return sum(histogram[start : end + 1])


def _format_percent(value: float) -> str:
    return f"{value * 100:.4f}%"


def _bar(count: int, largest: int, width: int = 36) -> str:
    if count <= 0 or largest <= 0:
        return ""
    return "#" * max(1, round(count / largest * width))


def _compare_images(
    *,
    document: str,
    page: int | None,
    name: str,
    actual: Image.Image,
    expected: Image.Image,
    actual_source: str,
    expected_source: str,
    pixel_threshold: int,
) -> CheckedImage:
    actual_full = actual.convert("RGBA")
    expected_full = expected.convert("RGBA")
    size_matches = actual_full.size == expected_full.size
    width = min(actual_full.width, expected_full.width)
    height = min(actual_full.height, expected_full.height)
    if width <= 0 or height <= 0:
        raise ValueError(
            f"image has empty comparison area for {name}: "
            f"actual={actual_full.size}, expected={expected_full.size}"
        )

    actual_rgba = actual_full.crop((0, 0, width, height))
    expected_rgba = expected_full.crop((0, 0, width, height))

    diff = ImageChops.difference(actual_rgba, expected_rgba)
    stat = ImageStat.Stat(diff)
    mean_abs_error = sum(stat.mean) / len(stat.mean)
    # `diff` is RGBA, so getextrema() yields one (low, high) pair per band.
    extrema = cast("tuple[tuple[int, int], ...]", diff.getextrema())
    max_abs_error = max(high for _, high in extrema)

    changed_mask = diff.convert("L").point(
        lambda value: 255 if value > pixel_threshold else 0
    )
    changed_pixels = changed_mask.histogram()[255]
    total_pixels = actual_rgba.width * actual_rgba.height

    channel_histograms = diff.histogram()
    abs_error_histogram = [
        sum(channel_histograms[value + channel * 256] for channel in range(4))
        for value in range(256)
    ]

    return CheckedImage(
        metrics=ImageMetrics(
            document=document,
            page=page,
            name=name,
            expected_source=expected_source,
            actual_source=actual_source,
            width=actual_full.width,
            height=actual_full.height,
            expected_width=expected_full.width,
            expected_height=expected_full.height,
            size_matches=size_matches,
            mean_abs_error=mean_abs_error,
            changed_pixels_ratio=changed_pixels / total_pixels,
            max_abs_error=max_abs_error,
            changed_pixels=changed_pixels,
            total_pixels=total_pixels,
        ),
        abs_error_histogram=abs_error_histogram,
    )


def _compare_pair(pair: ImagePair, pixel_threshold: int) -> CheckedImage:
    actual = Image.open(pair.actual)
    expected = Image.open(pair.expected)
    return _compare_images(
        document=pair.name,
        page=None,
        name=pair.name,
        actual=actual,
        expected=expected,
        actual_source=str(pair.actual),
        expected_source=str(pair.expected),
        pixel_threshold=pixel_threshold,
    )


def _load_existing_pairs(args: argparse.Namespace) -> list[CheckedImage] | None:
    if args.actual or args.expected:
        if not args.actual or not args.expected:
            raise SystemExit("--actual and --expected must be provided together")
        pair = ImagePair(args.actual.stem, args.actual, args.expected)
        return [_compare_pair(pair, args.pixel_threshold)]

    if args.from_deltas is not None:
        delta_dir = args.from_deltas or DEFAULT_DELTA_DIR
        pairs = _discover_delta_pairs(delta_dir)
        if not pairs:
            raise SystemExit(
                f"no *.actual.png/*.expected.png pairs found in {delta_dir}"
            )
        return [_compare_pair(pair, args.pixel_threshold) for pair in pairs]

    return None


def _print_page_table(metrics: Sequence[ImageMetrics]) -> None:
    document_width = max(
        len("document"),
        *(len(item.document) for item in metrics),
    )
    expected_width = max(
        len("expected"),
        *(len(item.expected_source) for item in metrics),
    )
    print("Per-page image metrics")
    print(
        f"  {'document':{document_width}}  page  "
        f"{'expected':{expected_width}}  actual_size  expected_size  "
        "size_match  mean_abs_error  changed_pixels_ratio  max_abs_error  "
        "changed_pixels"
    )
    for item in metrics:
        page = "-" if item.page is None else str(item.page)
        actual_size = f"{item.width}x{item.height}"
        expected_size = f"{item.expected_width}x{item.expected_height}"
        print(
            f"  {item.document:{document_width}}  "
            f"{page:>4}  "
            f"{item.expected_source:{expected_width}}  "
            f"{actual_size:11}  "
            f"{expected_size:13}  "
            f"{item.size_matches!s:10}  "
            f"{item.mean_abs_error:14.4f}  "
            f"{item.changed_pixels_ratio:20.4f}  "
            f"{item.max_abs_error:13d}  "
            f"{item.changed_pixels:14d}"
        )


def _print_summary(
    metrics: Sequence[ImageMetrics], abs_error_histogram: Sequence[int]
) -> None:
    mean_values = sorted(metric.mean_abs_error for metric in metrics)
    changed_values = sorted(metric.changed_pixels_ratio for metric in metrics)
    max_values = [metric.max_abs_error for metric in metrics]

    print()
    print("Observed limits needed to pass this run")
    print(f"  mean_abs_error:       {max(mean_values):.4f}")
    print(f"  changed_pixels_ratio: {max(changed_values):.4f}")
    print(f"  max_abs_error:        {max(max_values)}")

    print()
    print("Page-level percentiles")
    for percentile in (0.50, 0.75, 0.90, 0.95, 0.99):
        print(
            f"  p{percentile * 100:>5.1f}: "
            f"mean_abs_error={_percentile(mean_values, percentile):.4f}, "
            f"changed_pixels_ratio={_percentile(changed_values, percentile):.4f}"
        )

    print()
    print("All per-channel absolute-error percentiles")
    for percentile in (0.50, 0.75, 0.90, 0.95, 0.99, 0.999):
        print(
            f"  p{percentile * 100:>5.1f}: "
            f"{_percentile_from_histogram(abs_error_histogram, percentile)}"
        )


def _print_mean_histogram(metrics: Sequence[ImageMetrics]) -> None:
    buckets = [
        (0.0, 0.5),
        (0.5, 1.0),
        (1.0, 2.0),
        (2.0, 3.0),
        (3.0, 4.0),
        (4.0, 5.0),
        (5.0, 10.0),
        (10.0, math.inf),
    ]
    counts: list[tuple[str, int]] = []
    for low, high in buckets:
        if math.isinf(high):
            label = f">= {low:g}"
            count = sum(metric.mean_abs_error >= low for metric in metrics)
        else:
            label = f"{low:g}-{high:g}"
            count = sum(low <= metric.mean_abs_error < high for metric in metrics)
        counts.append((label, count))

    largest = max(count for _, count in counts)
    print()
    print("Histogram: page mean_abs_error")
    for label, count in counts:
        print(f"  {label:>8}  {count:5d}  {_bar(count, largest)}")


def _print_abs_error_histogram(histogram: Sequence[int]) -> None:
    buckets = [
        ("0", 0, 0),
        ("1", 1, 1),
        ("2", 2, 2),
        ("3", 3, 3),
        ("4", 4, 4),
        ("5", 5, 5),
        ("6-10", 6, 10),
        ("11-12", 11, 12),
        ("13-20", 13, 20),
        ("21-50", 21, 50),
        ("51-100", 51, 100),
        ("101-255", 101, 255),
    ]
    total = sum(histogram)
    counts = [
        (label, _bucket_count(histogram, low, high)) for label, low, high in buckets
    ]
    largest = max(count for _, count in counts)

    print()
    print("Histogram: all per-channel abs_error")
    for label, count in counts:
        ratio = count / total if total else 0.0
        print(
            f"  {label:>8}  {count:12d}  "
            f"{_format_percent(ratio):>10}  {_bar(count, largest)}"
        )


def _emit_json(checked_images: Sequence[CheckedImage]) -> None:
    metrics = [checked.metrics for checked in checked_images]
    abs_error_histogram = _merge_histograms(
        checked.abs_error_histogram for checked in checked_images
    )
    payload = {
        "images": [asdict(metric) for metric in metrics],
        "observed_limits": {
            "mean_abs_error": max(metric.mean_abs_error for metric in metrics),
            "changed_pixels_ratio": max(
                metric.changed_pixels_ratio for metric in metrics
            ),
            "max_abs_error": max(metric.max_abs_error for metric in metrics),
        },
        "abs_error_histogram": abs_error_histogram,
    }
    print(json.dumps(payload, indent=2))


def main() -> int:
    args = _parse_args()
    checked_images = _load_existing_pairs(args)

    if checked_images is None:
        raise SystemExit(
            "nothing to check: pass --from-deltas DIR, or --actual and --expected"
        )

    if not checked_images:
        raise SystemExit("no images checked")

    if args.json:
        _emit_json(checked_images)
        return 0

    metrics = [checked.metrics for checked in checked_images]
    abs_error_histogram = _merge_histograms(
        checked.abs_error_histogram for checked in checked_images
    )

    print(f"Checked {len(metrics)} image(s)")
    _print_page_table(metrics)
    _print_summary(metrics, abs_error_histogram)
    _print_mean_histogram(metrics)
    _print_abs_error_histogram(abs_error_histogram)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
