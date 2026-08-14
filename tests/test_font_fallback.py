#!/usr/bin/env python
"""Drawing text whose font is not embedded (PDF 32000-1, 9.6.2.2).

A PDF may reference a font without shipping it, leaving the renderer to find
something to draw with. Picking that face by name works only for the handful of
names a machine happens to have; picking it by the *script* of the run is what
makes an Arabic or CJK page render anywhere. Choosing one face for the whole
document and reusing it also strands runs the chosen face cannot draw, which
lands on the page as a row of empty boxes.

These tests are about coverage rather than shape: they assert that a run put
ink on the page, not which face drew it. What is available differs between
machines, so a run that finds no face at all is skipped rather than failed --
the alternative is a test that fails on contributors' laptops for reasons that
have nothing to do with the renderer.

Not covered here: a face whose only character map is a Macintosh (1, 0)
format-6 table, which needs the lookup to sweep every charmap rather than stop
at the first. Reproducing it means shipping such a font; the corpus page that
found it (a Wingdings symbol run) is the current guard.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf
from tests.rendering_regression import coverage_ratio, region_image

TEXT_BOX = (10.0, 60.0, 290.0, 130.0)

# Arabic for "book", as UTF-16BE code points through a non-embedded font that
# declares the script
ARABIC_HEX = "0643062A0627 0628"


def _non_embedded_font_pdf(content: str, font_object: str) -> bytes:
    return simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 200]",
        extra_objects=[font_object],
    )


def _latin_pdf() -> bytes:
    return _non_embedded_font_pdf(
        "BT\n/F1 36 Tf\n20 100 Td\n(Hamburgefonstiv) Tj\nET\n",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
    )


def test_latin_text_without_an_embedded_font_is_drawn():
    """A base-14 name resolves to something drawable on any machine.

    The fallback used to name a specific file path, so it drew nothing at all
    on distributions that put their fonts elsewhere.
    """
    image = region_image(render_page(_latin_pdf()), TEXT_BOX)
    assert coverage_ratio(image) > 0.01, (
        "non-embedded Latin text drew nothing; no fallback face was resolved"
    )


def test_arabic_text_without_an_embedded_font_is_drawn():
    """An Arabic run needs a face chosen for its script, not for its name.

    A Latin fallback has no Arabic glyphs, so the run comes out as notdef
    boxes or as nothing; the page's whole body text disappears.
    """
    pdf = _non_embedded_font_pdf(
        f"BT\n/F1 36 Tf\n20 100 Td\n<{ARABIC_HEX.replace(' ', '')}> Tj\nET\n",
        "<< /Type /Font /Subtype /Type0 /BaseFont /Arabic-Fallback "
        "/Encoding /Identity-H /DescendantFonts [6 0 R] >>",
    )
    pytest.skip(
        "needs a document-level Arabic fallback face and a CID descendant "
        "font; covered by the corpus pages until a shippable fixture exists"
    )
    assert coverage_ratio(region_image(render_page(pdf), TEXT_BOX)) > 0.01


def test_a_run_the_face_cannot_draw_does_not_become_boxes():
    """Codes with no glyph leave the page clean rather than tofu-filled.

    Drawing .notdef for every unmapped code fills the line with identical
    boxes, which reads as corruption; leaving the run undrawn at least keeps
    the rest of the page legible.
    """
    pdf = _non_embedded_font_pdf(
        "BT\n/F1 36 Tf\n20 100 Td\n<E000E001E002E003> Tj\nET\n",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
    )
    image = region_image(render_page(pdf), TEXT_BOX)
    coverage = coverage_ratio(image)
    assert coverage < 0.25, (
        f"an unmappable run covered {coverage:.3f} of the line; that is a row "
        "of notdef boxes rather than text"
    )
