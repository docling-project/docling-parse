#!/usr/bin/env python
"""Which of a font's names decides the typeface (PDF 32000-1, 9.6.2).

A font dictionary can carry two names. /BaseFont is the PostScript name of the
typeface. /Name is the label the resource dictionary references the font by,
and is deprecated as of PDF 1.7 -- producers put arbitrary strings there. An
arXiv stamp declaring `/BaseFont /Times-Roman /Name /arXivStAmP` matched
nothing under its label and fell through to the generic sans fallback, while
every other renderer set it in a serif.

The test compares two renders of the same /BaseFont against each other rather
than against a named face, so it holds whatever fonts the machine has
installed: adding a label must not move the result.

The other half of that fix -- dropping the Monotype "PS"/"MT" vendor tags
wherever they appear, so TimesNewRomanPS-BoldMT resolves with the same family
as TimesNewRomanPSMT instead of missing the alias on an infix "ps" -- has no
test here. Two font names that differ only in those tags also reach the width
metrics differently in the parse layer, so comparing renders cannot isolate the
face, and asserting a specific face would only hold on a machine that has it.
That change is measured on the comparison corpus instead: 13 pages improved and
none regressed.
"""

from __future__ import annotations

import numpy as np

from tests.pdf_builder import build_pdf, content_stream, render_page
from tests.rendering_regression import coverage_ratio, region_image

TEXT_BOX = (10.0, 60.0, 290.0, 140.0)
CONTENT = "BT\n/FT 36 Tf\n20 100 Td\n(Hamburg 123) Tj\nET\n"


def _pdf(font_entries: str) -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
            "/Resources << /Font << /FT 5 0 R >> >> /Contents 4 0 R >>",
            content_stream(CONTENT),
            f"<< /Type /Font /Subtype /Type1 {font_entries} >>",
        ]
    )


def _render(font_entries: str):
    return region_image(render_page(_pdf(font_entries)), TEXT_BOX)


def test_resource_label_does_not_decide_the_typeface():
    """/Name is a label; adding one must not change the face that is drawn.

    This is the arXiv stamp: same /BaseFont, one of them additionally
    labelled, and the labelled one used to resolve somewhere else entirely.
    """
    plain_image = _render("/BaseFont /Times-Roman")
    labelled_image = _render("/BaseFont /Times-Roman /Name /arXivStAmP")

    assert coverage_ratio(plain_image) > 0.01, (
        "the fixture drew no text, so the comparison would be vacuous"
    )

    plain = np.asarray(plain_image.convert("L"), dtype=np.float64)
    labelled = np.asarray(labelled_image.convert("L"), dtype=np.float64)
    assert plain.shape == labelled.shape, (
        f"renders differ in size: {plain.shape} vs {labelled.shape}"
    )

    difference = float(np.abs(plain - labelled).mean())
    assert difference < 1.0, (
        f"a /Name label changed the rendered face (mean abs difference "
        f"{difference:.2f}); /BaseFont names the typeface, /Name does not"
    )
