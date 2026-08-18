#!/usr/bin/env python
"""Form-field widget appearance streams (PDF 32000-1, 12.5.5 and 12.7).

A widget's /AP normal appearance draws the field: its frame, its background,
and whatever mark the field's value puts inside it. The frame is form chrome
rather than page content, and painting it is visible damage on real documents
-- a producer that cannot embed a glyph is free to leave a bordered
placeholder button in its place, and those pages come out peppered with empty
squares that no viewer draws. pdfium paints none of it, even with the form
environment initialised and form drawing requested.

What the field draws *inside* itself is content and has to survive; a checkbox
whose tick disappeared would be a worse bug than the one being fixed. These
tests pin both halves of that split.
"""

from __future__ import annotations

from tests.pdf_builder import build_pdf, content_stream, render_page, stream_object
from tests.rendering_regression import coverage_ratio, region_image

# the widget sits well inside the page so its frame has empty margin around it
RECT = (60.0, 60.0, 140.0, 140.0)
SIDE = RECT[2] - RECT[0]

# region_image takes top-left coordinates; the page is 200pt tall
FRAME_BAND = (58.0, 58.0, 142.0, 72.0)
INSIDE = (80.0, 80.0, 120.0, 120.0)


def _widget_pdf(appearance: bytes) -> bytes:
    """One page whose only mark is a widget annotation's normal appearance."""
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Contents 4 0 R /Annots [5 0 R] >>",
            content_stream(""),
            "<< /Type /Annot /Subtype /Widget /FT /Btn /F 4 "
            f"/Rect [{RECT[0]} {RECT[1]} {RECT[2]} {RECT[3]}] "
            "/T (field0) /MK << /BC [0 0 0] >> /AP << /N 6 0 R >> >>",
            stream_object(
                f"/Type /XObject /Subtype /Form /BBox [0 0 {SIDE} {SIDE}]",
                appearance,
            ),
        ]
    )


def _frame_appearance() -> bytes:
    """Exactly what the placeholder buttons carry: a stroked border, nothing else."""
    return f"0 G\n1 w\n0.5 0.5 {SIDE - 1} {SIDE - 1} re s\n".encode("latin-1")


def test_widget_frame_is_not_painted():
    """The border a field draws around itself does not reach the page."""
    page = render_page(_widget_pdf(_frame_appearance()))

    coverage = coverage_ratio(region_image(page, FRAME_BAND))
    assert coverage < 0.02, (
        f"the widget's frame painted ({coverage:.3f} coverage along its top "
        "edge); an empty field must leave the page as it was"
    )


def test_widget_background_fill_is_not_painted():
    """A field that fills its whole rectangle is background, not content."""
    appearance = f"0.5 g\n0 0 {SIDE} {SIDE} re f\n".encode("latin-1")
    page = render_page(_widget_pdf(appearance))

    coverage = coverage_ratio(region_image(page, INSIDE))
    assert coverage < 0.02, (
        f"the widget's background fill painted ({coverage:.3f} coverage); it "
        "covers the field rectangle and is chrome like the frame"
    )


def test_widget_mark_inside_the_frame_is_kept():
    """A checkbox tick is the field's value and has to survive.

    Drawn together with the frame, so this also pins that dropping the frame
    does not take the rest of the appearance stream with it.
    """
    quarter = SIDE / 4
    appearance = (
        f"0 G\n1 w\n0.5 0.5 {SIDE - 1} {SIDE - 1} re s\n"
        f"4 w\n{quarter} {quarter} m\n{SIDE - quarter} {SIDE - quarter} l\ns\n"
    ).encode("latin-1")
    page = render_page(_widget_pdf(appearance))

    assert coverage_ratio(region_image(page, INSIDE)) > 0.02, (
        "the mark drawn inside the field did not paint; only the frame is chrome"
    )
    frame = coverage_ratio(region_image(page, FRAME_BAND))
    assert frame < 0.06, (
        f"the frame still painted ({frame:.3f} coverage) when a mark was drawn "
        "alongside it"
    )
