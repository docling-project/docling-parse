#!/usr/bin/env python
"""Character widths for standard Type1 fonts without an explicit /Widths table.

A PDF may reference one of the Core-14 fonts (or a common family alias such as
Arial or Times New Roman) and rely on the built-in font metrics instead of
shipping a /Widths array. These tests build such PDFs in memory and check that
the parser reports the real per-glyph advances rather than a flat fallback.
"""

from io import BytesIO
from itertools import pairwise
from typing import List

from docling_parse.pdf_parser import DoclingPdfParser


def _build_pdf(
    base_font: str, text: str, font_size: int = 10, widths: str = ""
) -> bytes:
    content = f"BT\n/F001 {font_size} Tf\n72 720 Td\n({text}) Tj\nET\n"
    font = (
        f"<< /Type /Font /Subtype /Type1 /BaseFont /{base_font} /Name /F001"
        f"{widths} /Encoding /WinAnsiEncoding >>"
    )
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 857 792] /Contents 4 0 R "
        "/Resources << /Font << /F001 5 0 R >> >> >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        font,
    ]

    out = b"%PDF-1.4\n"
    offsets = []
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1")

    startxref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF"
    ).encode("latin-1")
    return out


def _char_advances(base_font: str, text: str, **kwargs) -> List[float]:
    """Left-edge spacing between consecutive glyphs of the first text line."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(_build_pdf(base_font, text, **kwargs)))

    _, page = next(doc.iterate_pages())
    cells = list(page.char_cells)
    assert cells, f"no char cells for {base_font}"

    def left(cell) -> float:
        rect = cell.rect
        return min(rect.r_x0, rect.r_x1, rect.r_x2, rect.r_x3)

    lefts = sorted(left(cell) for cell in cells)
    return [round(b - a, 3) for a, b in pairwise(lefts)]


FLAT_FALLBACK_ADVANCE = 5.0  # 500 units/em at 10pt, the pre-fix wrong value


def test_courier_char_advance_is_six_points():
    """Courier is monospace at 600 units/em, so every 10pt glyph advances 6pt."""
    advances = _char_advances("Courier", "ABC12345 COMPANY")
    assert advances
    for advance in advances:
        assert abs(advance - 6.0) < 0.01


def test_helvetica_widths_vary_and_are_not_flat():
    """A proportional Core-14 font yields real per-glyph widths, not a flat 5pt."""
    # "imW" exercises a narrow, a wide and an extra-wide glyph.
    advances = _char_advances("Helvetica", "imW")
    assert len(advances) >= 2
    assert len(set(advances)) > 1
    for advance in advances:
        assert abs(advance - FLAT_FALLBACK_ADVANCE) > 0.01


def test_arial_alias_resolves_to_helvetica_metrics():
    """Arial has no bundled AFM, so it must fall back to the Helvetica metrics.

    Without the alias it drops to the flat 5pt fallback; with it the advances
    match Helvetica glyph for glyph.
    """
    text = "imWl0"
    arial = _char_advances("Arial", text)
    helvetica = _char_advances("Helvetica", text)

    assert arial == helvetica
    for advance in arial:
        assert abs(advance - FLAT_FALLBACK_ADVANCE) > 0.01


def test_times_aliases_resolve_to_times_roman_metrics():
    """Both "Times" and "TimesNewRoman" map onto the Times-Roman metrics."""
    text = "imWl0"
    times_roman = _char_advances("Times-Roman", text)

    assert _char_advances("Times", text) == times_roman
    assert _char_advances("TimesNewRoman", text) == times_roman
    assert times_roman[0] != times_roman[1]


def test_explicit_widths_take_precedence_over_base_metrics():
    """An explicit /Widths table overrides the built-in Core-14 metrics."""
    widths = " /FirstChar 65 /LastChar 90 /Widths [" + " ".join(["1000"] * 26) + "]"
    advances = _char_advances("Courier", "AWX", widths=widths)

    assert advances
    for advance in advances:
        assert abs(advance - 10.0) < 0.01
