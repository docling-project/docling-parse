"""Right-to-left text emitted in logical order under a left-to-right matrix.

Some generators (for example macOS Quartz output from word processors) write
Hebrew and Arabic in logical order: each glyph is painted with the ordinary
left-to-right text matrix, and a large negative character spacing plus a
per-glyph TJ offset places the next glyph immediately to the left of the
previous one. The painted glyphs abut exactly, but the cursor runs backwards.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf

# code -> (unicode, width in 1/1000 em)
GLYPHS = {
    "A": ("05D0", 700),  # alef
    "B": ("05DC", 560),  # lamed
    "C": ("05D5", 300),  # vav
    "D": ("05D4", 700),  # he
    "E": ("05D9", 300),  # yod
    "F": ("05DD", 700),  # final mem
    "G": ("05DE", 740),  # mem
    "H": ("05E8", 600),  # resh
    "I": ("05D7", 700),  # het
    " ": ("0020", 280),
}

# "אלוהים מרחם" in logical order
LOGICAL = "ABCDEF GHIF"
EXPECTED_WORDS = ["אלוהים", "מרחם"]


def _font_objects() -> list:
    codes = sorted(GLYPHS, key=ord)
    first, last = ord(codes[0]), ord(codes[-1])
    widths = " ".join(
        str(GLYPHS[chr(c)][1]) if chr(c) in GLYPHS else "0"
        for c in range(first, last + 1)
    )
    bfchars = "\n".join(f"<{ord(c):02X}> <{GLYPHS[c][0]}>" for c in codes)
    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Test-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
        f"{len(codes)} beginbfchar\n{bfchars}\nendbfchar\n"
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend"
    )
    return [
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        f"/FirstChar {first} /LastChar {last} /Widths [{widths}] "
        "/ToUnicode 6 0 R >>",
        content_stream(cmap),
    ]


def _parse(content: str):
    pdf = simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 100]",
        extra_objects=_font_objects(),
    )
    doc = DoclingPdfParser(loglevel="fatal").load(
        BytesIO(pdf), decode_config=DecodeConfig(keep_glyphs=True)
    )
    return doc.get_page(1)


def _logical_order_content(char_spacing: float) -> str:
    """One TJ that paints each glyph directly left of the previous one."""
    parts = []
    for i, code in enumerate(LOGICAL):
        parts.append(f"({code})")
        if i + 1 < len(LOGICAL):
            w_prev = GLYPHS[code][1] / 1000.0
            w_next = GLYPHS[LOGICAL[i + 1]][1] / 1000.0
            # cursor after the glyph: w_prev + Tc - n/1000 == -w_next
            n = (w_prev + char_spacing + w_next) * 1000.0
            parts.append(f"{n:.0f}")
    return (
        f"BT {char_spacing} Tc 12 0 0 12 250 50 Tm /F1 1 Tf [ {' '.join(parts)} ] TJ ET"
    )


def _visual_order_content() -> str:
    """Leftmost glyph first, advancing with the natural widths."""
    return f"BT /F1 12 Tf 150 50 Td ({LOGICAL[::-1]}) Tj ET"


@pytest.mark.parametrize("char_spacing", [-1.0, -1.5])
def test_logical_order_rtl_contracts_into_words(char_spacing):
    page = _parse(_logical_order_content(char_spacing))
    words = [cell.text for cell in page.word_cells]
    lines = [cell.text for cell in page.textline_cells]
    assert words == EXPECTED_WORDS
    assert lines == [" ".join(EXPECTED_WORDS)]


def test_visual_order_rtl_still_contracts_into_logical_words():
    page = _parse(_visual_order_content())
    words = sorted(cell.text for cell in page.word_cells)
    lines = [cell.text for cell in page.textline_cells]
    assert words == sorted(EXPECTED_WORDS)
    assert lines == [" ".join(EXPECTED_WORDS)]
