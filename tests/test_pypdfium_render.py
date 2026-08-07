#!/usr/bin/env python
"""Cross-renderer comparison of the Blend2D render path against pypdfium2.

This mirrors `test_rendered_pages_match_groundtruth`, but the reference image is
produced by pypdfium2 instead of being read from the regression dataset. The
two renderers are independent implementations, so the per-page numbers are a
report and not a contract: this test never fails on pixel differences. It only
fails when there is nothing to compare at all.

Three-panel visualizations (pypdfium2 | docling-parse | amplified difference)
are written to `tests/data/visualizations`; `--render-visualizations` selects
whether that happens for every page, only for pages above tolerance, or never.
The same folder gets `histogram_delta.png` and `histogram_mean_abs_error.png`,
the corpus-wide distribution of both headline metrics, unless the mode is
`none`.
"""

import glob
import os
from pathlib import Path

import pytest

from docling_parse.pdf_parser import RenderConfig
from tests.constants import PARSER_PAGE_RESTRICTIONS
from tests.rendering_regression import (
    RENDER_VISUALIZATION_FOLDER,
    ImageComparison,
    ImageTolerance,
    flatten_on_white,
    format_image_comparison_table,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
    write_comparison_visualization,
    write_metric_histogram,
)
from tests.test_parse import REGRESSION_FOLDER
from tests.test_threaded_render import _make_parser

PYPDFIUM_RENDER_SCALE = 2.0

# Not enforced: this is only the cut-off that decides which pages are reported
# as "above tolerance" and, by default, which ones get a visualization written.
PYPDFIUM_IMAGE_TOLERANCE = ImageTolerance(
    pixel_threshold=12,
    mean_abs_error=9.0,
    changed_pixels_ratio=0.09,
)


def _make_pypdfium_render_config() -> RenderConfig:
    render_config = RenderConfig()
    render_config.scale = PYPDFIUM_RENDER_SCALE
    return render_config


def _format_visualization_summary(paths: list[Path]) -> str:
    if not paths:
        return f"No comparison visualizations written to {RENDER_VISUALIZATION_FOLDER}."

    lines = [f"Wrote {len(paths)} comparison visualization(s):"]
    lines.extend(f"  {path}" for path in sorted(paths))
    return "\n".join(lines)


def _write_metric_histograms(comparisons: list[ImageComparison]) -> list[Path]:
    """Write the corpus-level distribution of both headline metrics."""
    written = [
        write_metric_histogram(
            [comparison.normalized_delta for comparison in comparisons],
            metric="delta",
            title="normalized_delta vs pypdfium2",
            xlabel="normalized_delta (0 = identical, 1 = maximally different)",
        ),
        write_metric_histogram(
            [comparison.mean_abs_error for comparison in comparisons],
            metric="mean_abs_error",
            title="mean_abs_error vs pypdfium2",
            xlabel="mean_abs_error (0-255 grey levels)",
            threshold=PYPDFIUM_IMAGE_TOLERANCE.mean_abs_error,
            threshold_label="reporting cut-off",
        ),
    ]
    return [path for path in written if path is not None]


@pytest.mark.pypdfium
def test_rendered_pages_match_pypdfium(render_visualizations: str) -> None:
    """Report how far the Blend2D render output is from pypdfium2, without failing."""
    pytest.importorskip(
        "pypdfium2",
        reason="pypdfium2 is required for the cross-renderer comparison",
    )

    pdf_docs = sorted(glob.glob(REGRESSION_FOLDER))
    assert len(pdf_docs) > 0, "len(pdf_docs)==0 -> nothing to test"

    parser = _make_parser(
        threads=4,
        max_concurrent=32,
        render_config=_make_pypdfium_render_config(),
    )

    key_to_path: dict[str, str] = {}
    for pdf_doc_path in pdf_docs:
        rname = os.path.basename(pdf_doc_path)
        key = parser.load(
            pdf_doc_path,
            page_numbers=PARSER_PAGE_RESTRICTIONS.get(rname),
        )
        key_to_path[key] = pdf_doc_path

    comparisons: list[ImageComparison] = []
    visualizations: list[Path] = []
    above_tolerance: list[str] = []
    skipped: list[str] = []

    for result in parser.iterate_results():
        pdf_doc_path = key_to_path[result.doc_key]
        rname = os.path.basename(pdf_doc_path)

        if not result.success:
            skipped.append(
                f"{rname}@{result.page_number}[render: {result.error_message}]"
            )
            continue

        try:
            reference = render_pypdfium_page(
                pdf_doc_path,
                result.page_number,
                scale=PYPDFIUM_RENDER_SCALE,
            )
        except Exception as exc:
            skipped.append(f"{rname}@{result.page_number}[pypdfium2: {exc}]")
            continue

        # both renderers must sit on the same page background before the pixels
        # are comparable at all
        actual = flatten_on_white(result.get_image())
        expected = flatten_on_white(reference)

        comparison = measure_image_pair(
            rname,
            result.page_number,
            actual,
            expected,
            tolerance=PYPDFIUM_IMAGE_TOLERANCE,
        )
        comparisons.append(comparison)

        failed = image_comparison_failed(
            comparison,
            tolerance=PYPDFIUM_IMAGE_TOLERANCE,
        )
        if failed:
            above_tolerance.append(f"{rname}@{result.page_number}")

        if render_visualizations == "all" or (
            render_visualizations == "above-tolerance" and failed
        ):
            visualizations.append(
                write_comparison_visualization(
                    rname,
                    result.page_number,
                    expected,
                    actual,
                    comparison.normalized_delta,
                )
            )

    parser.unload_all()

    # The histograms summarise the whole run rather than a single page, so they
    # are written for every mode except "none" -- including the default, where
    # only the above-tolerance pages get their own panel.
    if render_visualizations != "none":
        visualizations.extend(_write_metric_histograms(comparisons))

    print(format_image_comparison_table(comparisons))
    print()
    print(_format_visualization_summary(visualizations))

    if above_tolerance:
        print()
        print(
            f"{len(above_tolerance)} of {len(comparisons)} page(s) above the "
            f"pypdfium2 comparison tolerance (reported, not enforced): "
            + ", ".join(sorted(above_tolerance))
        )

    if skipped:
        print()
        print(f"{len(skipped)} page(s) could not be compared: " + ", ".join(skipped))

    assert len(comparisons) > 0, "no page could be compared against pypdfium2"
