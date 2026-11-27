#!/usr/bin/env python
# SPDX-License-Identifier: MIT
"""Right-to-left lines that Word paints as several runs stay one line.

Word writes a right-to-left line as several text runs, often split at a
zero-width non-joiner inside a word. It paints the glyphs of each run left to
right, and the runs right to left, so each run starts left of the run before
it, and sometimes paints the runs of one line out of order. The runs of a line
must be joined, without a word boundary between two runs that touch.
"""

from io import BytesIO

import pytest

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf

# code -> (unicode, width in 1/1000 em)
GLYPHS = {
    "m": ("0645", 500),  # meem
    "y": ("06CC", 500),  # farsi yeh
    "k": ("06A9", 500),  # keheh
    "n": ("0646", 500),  # noon
    "d": ("062F", 500),  # dal
    "p": ("067E", 500),  # peh
    "A": ("0627", 500),  # alef
    "h": ("0647", 500),  # heh
    "s": ("0633", 500),  # seen
    "z": ("0632", 500),  # zain
    "S": ("0634", 500),  # sheen
    "2": ("06F2", 500),  # Persian digit two
    "8": ("06F8", 500),  # Persian digit eight
    "1": ("0031", 500),
    "9": ("0039", 500),
    " ": ("0020", 500),
}


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


def _text(codes: str) -> str:
    return "".join(chr(int(GLYPHS[code][0], 16)) for code in codes)


def _parse(runs: list[tuple[float, float, float, str]]):
    """Paints each run (font size, x, y, codes in visual order) as one text object."""
    content = " ".join(
        f"BT /F1 {size} Tf 1 0 0 1 {x} {y} Tm ({codes}) Tj ET"
        for size, x, y, codes in runs
    )
    pdf = simple_page_pdf(
        content,
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 100]",
        extra_objects=_font_objects(),
    )
    return DoclingPdfParser(loglevel="fatal").load(BytesIO(pdf)).get_page(1)


# The runs in paint order, then the expected lines in logical order. With a
# font size of 12, each glyph is 6 wide.
@pytest.mark.parametrize(
    "runs, lines",
    [
        # «می‌کند»: run «می» at x 118-130, then run «کند» left of it
        ([(12, 118, 70, "ym"), (12, 100, 70, "dnk")], ["myknd"]),
        # «پیاده‌سازی شد»: run «پیاده» at x 142-172, then run «سازی شد»
        ([(12, 142, 40, "hdAyp"), (12, 100, 40, "dS yzAs")], ["pyAdhsAzy Sd"]),
        # the same line with the runs painted out of order: «پیاده», «شد»,
        # then «سازی» between them
        (
            [(12, 142, 40, "hdAyp"), (12, 100, 40, "dS "), (12, 118, 40, "yzAs")],
            ["pyAdhsAzy Sd"],
        ),
        # «می‌کند» with the second run painted around a gap: «د», then «ک»
        # one glyph further right, then «ن» into the gap between them
        (
            [
                (12, 118, 70, "ym"),
                (12, 100, 70, "d"),
                (12, 112, 70, "k"),
                (12, 106, 70, "n"),
            ],
            ["myknd"],
        ),
    ],
)
def test_word_runs_of_rtl_line_are_joined(runs, lines):
    page = _parse(runs)
    assert [cell.text for cell in page.textline_cells] == [
        _text(line) for line in lines
    ]


def test_small_label_next_to_large_text_stays_apart():
    # a calendar day: a large Persian day number and, touching it on the left,
    # a small Latin date whose baseline is 2.5 higher (more than a tenth of
    # the small text height, less than a tenth of the large one)
    page = _parse([(30, 160, 40, "28"), (10, 150, 42.5, "19")])
    texts = sorted(cell.text for cell in page.textline_cells)
    assert texts == sorted([_text("28"), _text("19")])
