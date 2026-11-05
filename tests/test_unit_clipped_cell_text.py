#!/usr/bin/env python
"""Text clipped to a table cell keeps the cell it belongs to (#404).

Some producers fit long text into a table cell by clipping it to the cell's
box: `q <cell> re W n ... Q`. The page shows the part that fits; the text layer
keeps the whole string, whose hidden end runs under the next column. The char,
word and line cells carry the box of that clip, and two glyphs under different
clips, of which one is cut by its clip, never join into one word or line.
"""

from __future__ import annotations

from io import BytesIO

import pytest

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingPdfParser,
)
from tests.pdf_builder import parse_page, simple_page_pdf

ROWS = [
    ("Code", "Description", "Unit", "Amount"),
    ("AB-1001-XYZ", "Replacement of the ventilation unit, roof level", "pcs", "1"),
    ("AB-1002", "Gutter cleaning", "m", "120"),
    ("AB-1003-LONG", "Repainting of window frames and fascia boards", "m2", "85"),
]
COLUMNS = [72, 122, 252, 302, 372]
TOP, ROW_H = 720, 22
MEDIA_BOX = "[0 0 612 792]"
HELVETICA = "/Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >>"


def cell_box(row: int, col: int) -> tuple[float, float, float, float]:
    """Clip box (x0, y0, x1, y1) of a table cell, bottom-left page coordinates."""
    y_top = TOP - row * ROW_H
    return (COLUMNS[col], y_top - ROW_H, COLUMNS[col + 1], y_top)


def clipped_table_pdf(media_box: str = MEDIA_BOX) -> bytes:
    """The table of #404: every cell's text is drawn inside the cell's clip."""
    content = ["0.5 w"]
    for i in range(len(ROWS) + 1):
        y = TOP - i * ROW_H
        content.append(f"{COLUMNS[0]} {y} m {COLUMNS[-1]} {y} l S")
    for x in COLUMNS:
        content.append(f"{x} {TOP} m {x} {TOP - len(ROWS) * ROW_H} l S")
    for r, row in enumerate(ROWS):
        for c, text in enumerate(row):
            x0, y0, x1, y1 = cell_box(r, c)
            content.append(
                f"q {x0} {y0} {x1 - x0} {y1 - y0} re W n "
                f"BT /F1 9 Tf {x0 + 3} {y1 - 15} Td ({text}) Tj ET Q"
            )
    return simple_page_pdf("\n".join(content), resources=HELVETICA, media_box=media_box)


def words_of(page) -> list[str]:
    return [cell.text for cell in page.word_cells]


def lines_of(page) -> list[str]:
    return [cell.text for cell in page.textline_cells]


def clip_of(cell) -> tuple[float, float, float, float] | None:
    if cell.clip_bbox is None:
        return None
    bbox = cell.clip_bbox
    return (round(bbox.l, 3), round(bbox.b, 3), round(bbox.r, 3), round(bbox.t, 3))


def test_clipped_cell_text_stays_in_its_cell():
    page = parse_page(clipped_table_pdf())

    assert words_of(page) == [
        "Code", "Description", "Unit", "Amount",
        "AB-1001-XYZ", "Replacement", "of", "the", "ventilation", "unit,", "roof", "level",
        "pcs", "1",
        "AB-1002", "Gutter", "cleaning", "m", "120",
        "AB-1003-LONG", "Repainting", "of", "window", "frames", "and", "fascia", "boards",
        "m2", "85",
    ]  # fmt: skip
    # row 2 overflows nothing: its code and description join into one line
    # across the two clips, as they did before, since neither clip cuts a glyph
    assert lines_of(page) == [
        "Code", "Description", "Unit", "Amount",
        "AB-1001-XYZ", "Replacement of the ventilation unit, roof level", "pcs", "1",
        "AB-1002 Gutter cleaning", "m", "120",
        "AB-1003-LONG", "Repainting of window frames and fascia boards", "m2", "85",
    ]  # fmt: skip

    # every word and line carries the clip of the cell it was drawn in: the
    # hidden tail of the description lies under Unit and Amount by its glyph
    # box, and under Description by its clip box
    by_text = {cell.text: cell for cell in page.word_cells}
    assert clip_of(by_text["AB-1001-XYZ"]) == cell_box(1, 0)
    assert clip_of(by_text["level"]) == cell_box(1, 1)
    assert by_text["level"].rect.to_bounding_box().l > COLUMNS[2]
    assert clip_of(by_text["pcs"]) == cell_box(1, 2)
    assert clip_of(by_text["boards"]) == cell_box(3, 1)

    lines = {cell.text: cell for cell in page.textline_cells}
    assert clip_of(
        lines["Replacement of the ventilation unit, roof level"]
    ) == cell_box(1, 1)
    assert clip_of(lines["AB-1002 Gutter cleaning"]) is None

    chars = [cell for cell in page.char_cells if cell.text == "Z"]
    assert [clip_of(cell) for cell in chars] == [cell_box(1, 0)]


def test_clip_box_is_tracked_without_shapes():
    """docling skips shapes when it does not render; the clip box must survive."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        BytesIO(clipped_table_pdf()),
        content_config=ContentConfig(shapes_content_level=ContentLevel.SKIP),
    )
    try:
        page = doc.get_page(1)
        assert page.shapes == []
        assert "AB-1001-XYZ" in words_of(page)
        assert "AB-1001-XYZReplacement" not in words_of(page)
        assert clip_of(
            next(c for c in page.word_cells if c.text == "level")
        ) == cell_box(1, 1)
    finally:
        doc.unload()


def test_clip_box_moves_with_a_page_box_that_does_not_start_at_zero():
    """Cells are shifted to the page box origin; their clip box must shift too."""
    dx, dy = -15.0, -21.0
    page = parse_page(clipped_table_pdf(media_box=f"[{dx} {dy} {612 + dx} {792 + dy}]"))

    def shifted(box: tuple[float, float, float, float]) -> tuple[float, ...]:
        return (box[0] - dx, box[1] - dy, box[2] - dx, box[3] - dy)

    by_text = {cell.text: cell for cell in page.word_cells}
    assert clip_of(by_text["AB-1001-XYZ"]) == shifted(cell_box(1, 0))
    assert clip_of(by_text["level"]) == shifted(cell_box(1, 1))
    # a clip box left at the unshifted place cuts glyphs that the page shows,
    # which splits this line although no clip hides anything in it
    assert "AB-1002 Gutter cleaning" in lines_of(page)


def test_keep_clips_off_restores_the_glyph_only_cells():
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        BytesIO(clipped_table_pdf()),
        decode_config=DecodeConfig(keep_clips=False),
        content_config=ContentConfig(shapes_content_level=ContentLevel.SKIP),
    )
    try:
        page = doc.get_page(1)
        assert all(cell.clip_bbox is None for cell in page.word_cells)
        assert "AB-1001-XYZReplacement" in words_of(page)
    finally:
        doc.unload()


def run_clipped_pdf(runs: list[tuple[str, float, float, float, float]]) -> bytes:
    """Each run of text in its own clip, given as (text, x, y, clip width, clip height)."""
    content = []
    for text, x, y, width, height in runs:
        content.append(
            f"q {x} {y - 3} {width} {height} re W n "
            f"BT /F1 10 Tf {x} {y} Td ({text}) Tj ET Q"
        )
    return simple_page_pdf("\n".join(content), resources=HELVETICA, media_box=MEDIA_BOX)


# Helvetica at 10 pt, from the AFM widths: H 7.22, e 5.56, l 2.22, o 5.56,
# W 9.44, r 3.33, d 5.56, space 2.78
HEL_W, LO_W, HELLO_W, WORLD_W, SPACE_W = 15.0, 7.78, 22.78, 26.11, 2.78


def test_runs_under_clips_that_do_not_cut_them_still_join():
    """A producer that wraps every text run in a clip around it changes nothing."""
    page = parse_page(
        run_clipped_pdf(
            [
                ("Hel", 100.0, 700, HEL_W, 12),
                ("lo", 100.0 + HEL_W, 700, LO_W, 12),
                ("World", 100.0 + HELLO_W + SPACE_W, 700, WORLD_W, 12),
            ]
        )
    )
    assert words_of(page) == ["Hello", "World"]
    assert lines_of(page) == ["Hello World"]
    # the word that spans two clips carries none; the whole-run word keeps its
    assert clip_of(page.word_cells[0]) is None
    assert clip_of(page.word_cells[1]) == (125.56, 697, 151.67, 709)
    assert clip_of(page.textline_cells[0]) is None


@pytest.mark.parametrize("visible_width", [12.0, 20.0])
def test_a_cut_run_does_not_join_the_next_clip(visible_width: float):
    """`Hello` clipped short runs under `World`, drawn at x=115 in its own clip.

    By geometry alone the hidden `lo` overlaps `World`: the gap is negative, the
    word test clamps it to zero, and the line test allows a jump of one advance.
    """
    page = parse_page(
        run_clipped_pdf(
            [
                ("Hello", 100.0, 700, visible_width, 12),
                ("World", 115.0, 700, WORLD_W, 12),
            ]
        )
    )
    assert words_of(page) == ["Hello", "World"]
    assert lines_of(page) == ["Hello", "World"]
    assert clip_of(page.word_cells[0]) == (100.0, 697, 100.0 + visible_width, 709)
    assert clip_of(page.word_cells[1]) == (115.0, 697, 141.11, 709)


def test_a_clip_on_the_overflowing_glyph_alone_keeps_the_word():
    """Some producers draw a word unclipped and clip only the glyph that overflows.

    `Montclai` is drawn without a clip and `r` under the cell's clip, which cuts
    it. The unclipped glyphs lie inside that box, so the word stays whole.
    """
    # Helvetica 10 pt: "Montclai" is 37.23 wide, "r" 3.33; the cell ends 2.56 pt
    # into the r, more than a fifth of the em
    content = (
        "BT /F1 10 Tf 100 700 Td (Montclai) Tj ET\n"
        "q 100 697 38 12 re W n BT /F1 10 Tf 137.23 700 Td (r) Tj ET Q"
    )
    page = parse_page(
        simple_page_pdf(content, resources=HELVETICA, media_box=MEDIA_BOX)
    )
    assert words_of(page) == ["Montclair"]
    assert lines_of(page) == ["Montclair"]
    assert clip_of(page.word_cells[0]) is None


def test_ink_tight_clips_on_punctuation_do_not_cut():
    """A clip drawn around the ink of a hyphen is narrower than its advance box.

    Such per-glyph clips hide a share of the advance box of narrow glyphs, but
    nothing of the text; the word and the line stay whole.
    """
    # Helvetica 10 pt: a 5.56, u 5.56, f 2.78, g 5.56, e 5.56, hyphen 3.33; the
    # hyphen's clip covers only the middle 2.0 pt of its advance box, which
    # hides 1.33 pt of it: less than a fifth of the em
    glyphs = [
        ("a", 5.56),
        ("u", 5.56),
        ("f", 2.78),
        ("g", 5.56),
        ("e", 5.56),
        ("-", 3.33),
    ]
    content, x = [], 100.0
    for text, advance in glyphs:
        if text == "-":
            clip = f"{x + 0.6} 697 2.0 12 re W n "
        else:
            clip = f"{x} 697 {advance} 12 re W n "
        content.append(f"q {clip}BT /F1 10 Tf {x} 700 Td ({text}) Tj ET Q")
        x += advance
    page = parse_page(
        simple_page_pdf("\n".join(content), resources=HELVETICA, media_box=MEDIA_BOX)
    )
    assert words_of(page) == ["aufge-"]
    assert lines_of(page) == ["aufge-"]


def test_a_glyph_repainted_under_a_sliver_clip_does_not_split_the_word():
    """Some producers paint a glyph again under a clip a fraction of a point wide.

    The repaint is cut almost entirely by its clip, but a box that cannot hold
    one glyph is not a cell: the word stays whole.
    """
    # Helvetica 10 pt: d 5.56, e 5.56, n 5.56; the second d sits under a clip
    # 0.3 pt wide at the glyph's right edge
    content = (
        "BT /F1 10 Tf 100 700 Td (d) Tj ET\n"
        "q 105.3 697 0.3 12 re W n BT /F1 10 Tf 100 700 Td (d) Tj ET Q\n"
        "BT /F1 10 Tf 105.56 700 Td (en) Tj ET"
    )
    page = parse_page(
        simple_page_pdf(content, resources=HELVETICA, media_box=MEDIA_BOX)
    )
    assert "".join(words_of(page)).replace("dd", "d") == "den"
    assert len(lines_of(page)) == 1
