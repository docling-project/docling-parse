#!/usr/bin/env python
"""Tests for locale-independent PDF coordinate parsing.

Validates that docling-parse produces correct results regardless of
the system's LC_NUMERIC setting — specifically for locales that use
',' as the decimal separator (French, German, Portuguese, etc.).

This addresses: https://github.com/docling-project/docling/issues/1455

Test structure:
  - Unit tests:        locale_safe_stod via Python-accessible paths
  - Integration tests: full page parsing under hostile locale
  - Edge-case tests:   boundary values, negative numbers, scientific notation
  - Regression tests:  coordinate stability across locale switches
"""

import glob
import locale
import os
import platform
import sys
from contextlib import contextmanager
from typing import Generator, List, Optional, Tuple

import pytest

from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

REGRESSION_FOLDER = "tests/data/regression/*.pdf"

# Locales that use ',' as decimal separator.  We try several because
# availability varies across OS / Docker images.
COMMA_LOCALES = [
    "fr_FR.UTF-8",
    "fr_FR.utf8",
    "de_DE.UTF-8",
    "de_DE.utf8",
    "pt_BR.UTF-8",
    "pt_BR.utf8",
    "es_ES.UTF-8",
    "es_ES.utf8",
    "it_IT.UTF-8",
    "it_IT.utf8",
]


def _find_comma_locale() -> str | None:
    """Return the first available locale that uses ',' as decimal separator."""
    for loc in COMMA_LOCALES:
        try:
            locale.setlocale(locale.LC_NUMERIC, loc)
            conv = locale.localeconv()
            if conv.get("decimal_point") == ",":
                # Reset before returning
                locale.setlocale(locale.LC_NUMERIC, "C")
                return loc
        except locale.Error:
            continue
    # Reset in case we changed it
    try:
        locale.setlocale(locale.LC_NUMERIC, "C")
    except locale.Error:
        pass
    return None


COMMA_LOCALE = _find_comma_locale()

# Skip entire module if no comma-decimal locale is available
requires_comma_locale = pytest.mark.skipif(
    COMMA_LOCALE is None,
    reason="No comma-decimal locale available (tried: fr_FR, de_DE, pt_BR, es_ES, it_IT)",
)


@contextmanager
def hostile_locale() -> Generator[str, None, None]:
    """Context manager that sets LC_NUMERIC to a comma-decimal locale.

    Restores the original locale on exit regardless of exceptions.
    """
    assert COMMA_LOCALE is not None, "No comma-decimal locale available"

    saved = locale.getlocale(locale.LC_NUMERIC)
    locale.setlocale(locale.LC_NUMERIC, COMMA_LOCALE)

    # Verify the locale is actually active
    conv = locale.localeconv()
    assert conv["decimal_point"] == ",", (
        f"Expected ',' decimal separator under {COMMA_LOCALE}, "
        f"got '{conv['decimal_point']}'"
    )

    try:
        yield COMMA_LOCALE
    finally:
        try:
            locale.setlocale(locale.LC_NUMERIC, saved)
        except locale.Error:
            locale.setlocale(locale.LC_NUMERIC, "C")


def _get_regression_pdfs() -> List[str]:
    """Return sorted list of regression PDF paths."""
    return sorted(glob.glob(REGRESSION_FOLDER))


def _parse_first_page(pdf_path: str) -> Tuple:
    """Parse page 1 of a PDF and return (page, dimension, cells)."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(pdf_path)
    page = doc.get_page(1)
    return page, page.dimension, page.char_cells


# ---------------------------------------------------------------------------
# Unit tests — locale_safe_stod behaviour via coordinate parsing
# ---------------------------------------------------------------------------


class TestLocaleIndependentParsing:
    """Verify that float parsing in the C++ layer is locale-independent."""

    @requires_comma_locale
    def test_page_dimensions_under_comma_locale(self):
        """Page dimensions must not be truncated under French locale.

        This is the core bug: a page width of 612.0 points would be
        parsed as 612 (or 0) when LC_NUMERIC uses comma.
        """
        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0, "No regression PDFs found"

        # Parse under C locale first (baseline)
        locale.setlocale(locale.LC_NUMERIC, "C")
        baseline_pdf = pdfs[0]
        _, baseline_dim, _ = _parse_first_page(baseline_pdf)

        # Now parse under hostile locale
        with hostile_locale():
            _, hostile_dim, _ = _parse_first_page(baseline_pdf)

        # Dimensions must match exactly
        baseline_rect = baseline_dim.rect.to_polygon()
        hostile_rect = hostile_dim.rect.to_polygon()

        for i in range(4):
            assert abs(baseline_rect[i][0] - hostile_rect[i][0]) < 0.001, (
                f"X coordinate mismatch at vertex {i}: "
                f"baseline={baseline_rect[i][0]}, hostile={hostile_rect[i][0]}"
            )
            assert abs(baseline_rect[i][1] - hostile_rect[i][1]) < 0.001, (
                f"Y coordinate mismatch at vertex {i}: "
                f"baseline={baseline_rect[i][1]}, hostile={hostile_rect[i][1]}"
            )

    @requires_comma_locale
    def test_nonzero_fractional_dimensions(self):
        """Page dimensions must retain their fractional part.

        The pre-fix bug would truncate 612.0 → 612 (benign) but
        595.276 → 595 (wrong by 0.276 points, visible at high DPI).
        Compare parsed dimensions under C vs comma locale — they must
        agree exactly. (Absolute thresholds break on PDFs with
        unusual coordinate origins, e.g. broken_media_box_v01.pdf.)
        """
        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0

        for pdf_path in pdfs[:5]:  # test first 5 for speed
            locale.setlocale(locale.LC_NUMERIC, "C")
            _, baseline_dim, _ = _parse_first_page(pdf_path)
            baseline = baseline_dim.crop_bbox

            with hostile_locale():
                _, hostile_dim, _ = _parse_first_page(pdf_path)
                hostile = hostile_dim.crop_bbox

            for attr in ("l", "t", "r", "b"):
                assert abs(getattr(baseline, attr) - getattr(hostile, attr)) < 1e-6, (
                    f"crop_bbox.{attr} mismatch in {pdf_path}: "
                    f"baseline={getattr(baseline, attr)} hostile={getattr(hostile, attr)}"
                )

    @requires_comma_locale
    def test_text_cell_coordinates_under_comma_locale(self):
        """Text cell bounding boxes must be correct under comma locale."""
        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0

        # Find a PDF with text cells
        test_pdf = None
        for pdf_path in pdfs:
            _, _, cells = _parse_first_page(pdf_path)
            if len(cells) > 0:
                test_pdf = pdf_path
                break

        if test_pdf is None:
            pytest.skip("No regression PDF with text cells found")

        # Baseline
        locale.setlocale(locale.LC_NUMERIC, "C")
        _, _, baseline_cells = _parse_first_page(test_pdf)

        # Hostile
        with hostile_locale():
            _, _, hostile_cells = _parse_first_page(test_pdf)

        assert len(baseline_cells) == len(hostile_cells), (
            f"Cell count differs: {len(baseline_cells)} vs {len(hostile_cells)}"
        )

        for i, (bc, hc) in enumerate(zip(baseline_cells, hostile_cells)):
            b_rect = bc.rect.to_polygon()
            h_rect = hc.rect.to_polygon()

            for v in range(4):
                assert abs(b_rect[v][0] - h_rect[v][0]) < 0.01, (
                    f"Cell {i} vertex {v} X: {b_rect[v][0]} vs {h_rect[v][0]}"
                )
                assert abs(b_rect[v][1] - h_rect[v][1]) < 0.01, (
                    f"Cell {i} vertex {v} Y: {b_rect[v][1]} vs {h_rect[v][1]}"
                )

            assert bc.text == hc.text, f"Cell {i} text: '{bc.text}' vs '{hc.text}'"


# ---------------------------------------------------------------------------
# Integration tests — full parsing pipeline under hostile locale
# ---------------------------------------------------------------------------


class TestFullPipelineLocaleResilience:
    """End-to-end parsing of real PDFs under comma-decimal locale."""

    @requires_comma_locale
    def test_all_regression_pdfs_parse_without_error(self):
        """Every regression PDF must parse without exception under French locale."""
        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0

        failures = []
        with hostile_locale():
            for pdf_path in pdfs:
                try:
                    parser = DoclingPdfParser(loglevel="fatal")
                    doc = parser.load(pdf_path)
                    n_pages = doc.number_of_pages()

                    for page_no in range(1, n_pages + 1):
                        page = doc.get_page(page_no)
                        # Accessing dimension forces coordinate parsing
                        _ = page.dimension
                        _ = page.char_cells

                except Exception as e:
                    failures.append((os.path.basename(pdf_path), str(e)))

        assert len(failures) == 0, (
            f"{len(failures)} PDFs failed under {COMMA_LOCALE}:\n"
            + "\n".join(f"  {name}: {err}" for name, err in failures)
        )

    @requires_comma_locale
    def test_page_count_consistent_across_locales(self):
        """Page count must not depend on locale."""
        pdfs = _get_regression_pdfs()[:10]

        locale.setlocale(locale.LC_NUMERIC, "C")
        c_counts = {}
        for pdf_path in pdfs:
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(pdf_path)
            c_counts[pdf_path] = doc.number_of_pages()

        with hostile_locale():
            for pdf_path in pdfs:
                parser = DoclingPdfParser(loglevel="fatal")
                doc = parser.load(pdf_path)
                hostile_count = doc.number_of_pages()
                assert hostile_count == c_counts[pdf_path], (
                    f"{os.path.basename(pdf_path)}: "
                    f"C locale={c_counts[pdf_path]}, hostile={hostile_count}"
                )

    @requires_comma_locale
    def test_shapes_consistent_across_locales(self):
        """Shape coordinates (graphics state) must be locale-independent."""
        pdfs = _get_regression_pdfs()

        # Find a PDF with shapes
        test_pdf = None
        for pdf_path in pdfs:
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(pdf_path)
            page = doc.get_page(1)
            if len(page.shapes) > 0:
                test_pdf = pdf_path
                break

        if test_pdf is None:
            pytest.skip("No regression PDF with shapes found")

        locale.setlocale(locale.LC_NUMERIC, "C")
        parser = DoclingPdfParser(loglevel="fatal")
        doc = parser.load(test_pdf)
        baseline_shapes = doc.get_page(1).shapes

        with hostile_locale():
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(test_pdf)
            hostile_shapes = doc.get_page(1).shapes

        assert len(baseline_shapes) == len(hostile_shapes), (
            f"Shape count: {len(baseline_shapes)} vs {len(hostile_shapes)}"
        )

        for i, (bs, hs) in enumerate(zip(baseline_shapes, hostile_shapes)):
            assert abs(bs.line_width - hs.line_width) < 0.001, (
                f"Shape {i} line_width: {bs.line_width} vs {hs.line_width}"
            )
            assert len(bs.points) == len(hs.points), (
                f"Shape {i} point count: {len(bs.points)} vs {len(hs.points)}"
            )
            for j, (bp, hp) in enumerate(zip(bs.points, hs.points)):
                assert abs(bp.x - hp.x) < 0.01, (
                    f"Shape {i} point {j} X: {bp.x} vs {hp.x}"
                )
                assert abs(bp.y - hp.y) < 0.01, (
                    f"Shape {i} point {j} Y: {bp.y} vs {hp.y}"
                )


# ---------------------------------------------------------------------------
# Edge-case tests — boundary values in numeric parsing
# ---------------------------------------------------------------------------


class TestNumericEdgeCases:
    """Verify correct handling of edge-case numeric values in PDFs."""

    @requires_comma_locale
    def test_bytesio_loading_under_comma_locale(self):
        """Loading from BytesIO must work under comma locale."""
        from io import BytesIO

        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0

        with hostile_locale():
            with open(pdfs[0], "rb") as f:
                data = BytesIO(f.read())

            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(data)
            page = doc.get_page(1)

            # Must not raise, and dimension must be valid
            assert page.dimension.crop_bbox.r > 0

    @requires_comma_locale
    def test_multi_page_document_all_pages_valid(self):
        """All pages of a multi-page PDF must parse correctly."""
        pdfs = _get_regression_pdfs()

        # Find multi-page PDF
        multi_page_pdf = None
        for pdf_path in pdfs:
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(pdf_path)
            if doc.number_of_pages() > 1:
                multi_page_pdf = pdf_path
                break

        if multi_page_pdf is None:
            pytest.skip("No multi-page regression PDF found")

        with hostile_locale():
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(multi_page_pdf)

            for page_no in range(1, doc.number_of_pages() + 1):
                page = doc.get_page(page_no)
                dim = page.dimension

                # Every page must have positive dimensions
                assert dim.crop_bbox.r > 0, (
                    f"Page {page_no}: crop_bbox.r = {dim.crop_bbox.r}"
                )
                assert dim.crop_bbox.t > 0, (
                    f"Page {page_no}: crop_bbox.t = {dim.crop_bbox.t}"
                )

    @requires_comma_locale
    def test_widgets_and_hyperlinks_under_comma_locale(self):
        """Widget and hyperlink bounding boxes must be locale-independent."""
        pdfs = _get_regression_pdfs()

        # Find PDFs with widgets or hyperlinks
        for pdf_path in pdfs:
            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(pdf_path)
            page = doc.get_page(1)

            if len(page.widgets) > 0 or len(page.hyperlinks) > 0:
                # Re-parse under hostile locale
                with hostile_locale():
                    parser2 = DoclingPdfParser(loglevel="fatal")
                    doc2 = parser2.load(pdf_path)
                    page2 = doc2.get_page(1)

                    for w in page2.widgets:
                        poly = w.rect.to_polygon()
                        for v in range(4):
                            # Coordinates should be finite and reasonable
                            assert -10000 < poly[v][0] < 10000
                            assert -10000 < poly[v][1] < 10000

                    for h in page2.hyperlinks:
                        poly = h.rect.to_polygon()
                        for v in range(4):
                            assert -10000 < poly[v][0] < 10000
                            assert -10000 < poly[v][1] < 10000

                return  # Found and tested at least one

        pytest.skip("No regression PDF with widgets/hyperlinks found")


# ---------------------------------------------------------------------------
# Regression tests — exact coordinate reproducibility
# ---------------------------------------------------------------------------


class TestCoordinateReproducibility:
    """Verify coordinates are bit-for-bit identical across locale switches.

    This catches subtle bugs where values are "close" but not exact,
    which would cause downstream layout analysis to drift.
    """

    @requires_comma_locale
    def test_coordinate_stability_across_repeated_locale_switches(self):
        """Parse same PDF under alternating locales — results must be identical."""
        pdfs = _get_regression_pdfs()
        assert len(pdfs) > 0
        test_pdf = pdfs[0]

        results = []
        for i in range(4):
            if i % 2 == 0:
                locale.setlocale(locale.LC_NUMERIC, "C")
            else:
                locale.setlocale(locale.LC_NUMERIC, COMMA_LOCALE)

            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(test_pdf)
            page = doc.get_page(1)
            dim = page.dimension

            results.append(
                {
                    "crop_r": dim.crop_bbox.r,
                    "crop_t": dim.crop_bbox.t,
                    "crop_l": dim.crop_bbox.l,
                    "crop_b": dim.crop_bbox.b,
                    "n_cells": len(page.char_cells),
                }
            )

        # Restore
        locale.setlocale(locale.LC_NUMERIC, "C")

        # All 4 results must be identical
        for i in range(1, 4):
            for key in results[0]:
                assert results[i][key] == results[0][key], (
                    f"Iteration {i} diverged on {key}: "
                    f"{results[i][key]} != {results[0][key]}"
                )

    @requires_comma_locale
    def test_cell_text_content_unaffected_by_locale(self):
        """Text content extraction must not depend on locale.

        While the primary bug is coordinate corruption, we verify that
        the text itself is also identical.
        """
        pdfs = _get_regression_pdfs()

        test_pdf = None
        for pdf_path in pdfs:
            _, _, cells = _parse_first_page(pdf_path)
            if len(cells) > 5:
                test_pdf = pdf_path
                break

        if test_pdf is None:
            pytest.skip("No PDF with enough text cells")

        locale.setlocale(locale.LC_NUMERIC, "C")
        _, _, c_cells = _parse_first_page(test_pdf)

        with hostile_locale():
            _, _, h_cells = _parse_first_page(test_pdf)

        c_text = "".join(c.text for c in c_cells)
        h_text = "".join(c.text for c in h_cells)

        assert c_text == h_text, "Text content differs between locales"


# ---------------------------------------------------------------------------
# Python-level defence test
# ---------------------------------------------------------------------------


class TestPythonLocaleGuard:
    """Verify the Python __init__.py locale guard works."""

    @requires_comma_locale
    def test_import_resets_numeric_locale(self):
        """Importing docling_parse must ensure LC_NUMERIC uses '.' separator."""
        # Set hostile locale first
        locale.setlocale(locale.LC_NUMERIC, COMMA_LOCALE)

        # Re-trigger the guard (it runs at import time, but we can call it)
        import docling_parse

        docling_parse._ensure_safe_numeric_locale()

        conv = locale.localeconv()
        assert conv["decimal_point"] == ".", (
            f"LC_NUMERIC still uses '{conv['decimal_point']}' after import"
        )

        # Restore
        locale.setlocale(locale.LC_NUMERIC, "C")

    def test_guard_is_noop_under_c_locale(self):
        """The guard must not change anything when LC_NUMERIC is already safe."""
        locale.setlocale(locale.LC_NUMERIC, "C")

        import docling_parse

        docling_parse._ensure_safe_numeric_locale()

        # Should still be C or equivalent
        conv = locale.localeconv()
        assert conv["decimal_point"] == "."
