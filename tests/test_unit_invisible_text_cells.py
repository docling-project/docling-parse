#!/usr/bin/env python
"""Word and line cells never mix painted, invisible and clip-only glyphs.

Text rendering mode 3 paints nothing, and mode 7 paints nothing but adds the
glyph to the clipping path (PDF 32000-1, 9.3.6); every other mode paints. Each
char cell carries its glyph's mode, but a word or line cell carries one mode,
its first glyph's. A word built across a change of class was therefore labelled
with the wrong class: hidden text drawn straight after a visible word read as
visible, and mode-3 text drawn straight after mode-7 text read as mode 7.

Painted modes still join each other: a producer that emboldens part of a word
with a stroke, or sets `0 Tr` in the middle of one, draws one word.
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import parse_page, simple_page_pdf, stream_object

MEDIA_BOX = "[0 0 612 792]"
HELVETICA = "/Font << /F1 5 0 R >>"
INVISIBLE = {3, 7}


def _utf16(text: str) -> str:
    return text.encode("utf-16-be").hex().upper()


def _to_unicode(mapping: dict[int, str]) -> bytes:
    entries = "".join(
        f"<{code:02X}> <{_utf16(text)}>\n" for code, text in mapping.items()
    )
    cmap = (
        "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
        "/CMapName /Adobe-Identity-UCS def /CMapType 2 def\n"
        "1 begincodespacerange <00> <FF> endcodespacerange\n"
        f"{len(mapping)} beginbfchar\n{entries}endbfchar\n"
        "endcmap CMapName currentdict /CMap defineresource pop end end\n"
    )
    return stream_object("", cmap.encode("latin-1"))


def _page(shows: str, extra_mapping: dict[int, str] | None = None):
    """One line of Helvetica at 11 pt; `shows` sets modes and shows strings."""
    mapping = {code: chr(code) for code in range(0x20, 0x7F)}
    mapping.update(extra_mapping or {})
    font = (
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding /ToUnicode 6 0 R >>"
    )
    content = f"BT /F1 11 Tf 1 0 0 1 72 700 Tm {shows} ET"
    pdf = simple_page_pdf(
        content,
        resources=HELVETICA,
        media_box=MEDIA_BOX,
        extra_objects=[font, _to_unicode(mapping)],
    )
    return parse_page(pdf)


def _modes(cells) -> list[tuple[str, int]]:
    return [(cell.text, int(cell.rendering_mode)) for cell in cells]


def _visible_text(cells) -> list[str]:
    return [c.text.strip() for c in cells if int(c.rendering_mode) not in INVISIBLE]


def _invisible_text(cells) -> list[str]:
    return [c.text.strip() for c in cells if int(c.rendering_mode) in INVISIBLE]


def test_hidden_text_after_a_visible_word_is_its_own_invisible_word_and_line():
    page = _page("0 Tr (Hello) Tj 3 Tr (SECRET) Tj 0 Tr ( world) Tj")

    assert _modes(page.word_cells) == [("Hello", 0), ("SECRET", 3), ("world", 0)]
    assert _modes(page.textline_cells) == [("Hello", 0), ("SECRET", 3), ("world", 0)]


def test_visible_word_after_hidden_text_stays_visible():
    page = _page("3 Tr (SECRET) Tj 0 Tr (Hello world) Tj")

    assert _modes(page.word_cells) == [("SECRET", 3), ("Hello", 0), ("world", 0)]
    assert _modes(page.textline_cells) == [("SECRET", 3), ("Hello world", 0)]


def test_hidden_sentence_in_one_glyph_is_not_part_of_the_visible_word():
    """One invisible glyph whose /ToUnicode maps to a whole sentence."""
    sentence = "see the attached schedule for details"
    page = _page(
        "0 Tr (Total:) Tj 3 Tr (\\200) Tj 0 Tr ( 100 dollars) Tj",
        {0x80: f" {sentence}"},
    )

    assert _visible_text(page.word_cells) == ["Total:", "100", "dollars"]
    assert _invisible_text(page.word_cells) == [sentence]
    assert _visible_text(page.textline_cells) == ["Total:", "100 dollars"]
    assert _invisible_text(page.textline_cells) == [sentence]


@pytest.mark.parametrize(
    ("shows", "expected"),
    [
        ("7 Tr (Clip) Tj 3 Tr (Hidden) Tj", [("Clip", 7), ("Hidden", 3)]),
        ("3 Tr (Hidden) Tj 7 Tr (Clip) Tj", [("Hidden", 3), ("Clip", 7)]),
    ],
    ids=["clip_only_then_invisible", "invisible_then_clip_only"],
)
def test_clip_only_and_invisible_glyphs_with_no_gap_are_separate_words(shows, expected):
    page = _page(shows)

    assert _modes(page.word_cells) == expected
    assert _modes(page.textline_cells) == expected


@pytest.mark.parametrize(
    ("shows", "expected"),
    [
        ("7 Tr (Clip ) Tj 3 Tr (Hidden) Tj", [("Clip", 7), ("Hidden", 3)]),
        ("3 Tr (Hidden ) Tj 7 Tr (Clip) Tj", [("Hidden", 3), ("Clip", 7)]),
    ],
    ids=["clip_only_then_invisible", "invisible_then_clip_only"],
)
def test_clip_only_and_invisible_words_on_one_line_are_separate_lines(shows, expected):
    page = _page(shows)

    assert _modes(page.word_cells) == expected
    assert _modes(page.textline_cells) == expected


def test_invisible_line_keeps_its_words_together():
    """The OCR layer of a scanned page is invisible throughout."""
    page = _page("3 Tr (Scanned page text) Tj")

    assert _modes(page.word_cells) == [("Scanned", 3), ("page", 3), ("text", 3)]
    assert _modes(page.textline_cells) == [("Scanned page text", 3)]


def test_visible_modes_mixed_inside_a_word_keep_it_whole():
    """Fill, fill-and-stroke, stroke and fill-and-clip all paint the glyph."""
    page = _page(
        "0 Tr (Em) Tj 2 Tr (bol) Tj 1 Tr (den) Tj 4 Tr (ed) Tj 0 Tr ( text) Tj"
    )

    assert [cell.text for cell in page.word_cells] == ["Emboldened", "text"]
    assert [cell.text for cell in page.textline_cells] == ["Emboldened text"]


def test_unset_mode_joins_an_explicit_fill_mode():
    """No `Tr` before the first glyph is fill; `0 Tr` mid-word changes nothing."""
    page = _page("(Wo) Tj 0 Tr (rd) Tj")

    assert [cell.text for cell in page.word_cells] == ["Word"]
    assert [cell.text for cell in page.textline_cells] == ["Word"]


def test_painting_clip_modes_join_each_other_and_fill():
    """Modes 4 to 6 paint the glyph as 0 to 2 do, and also clip."""
    page = _page("4 Tr (Cl) Tj 5 Tr (ip) Tj 6 Tr (pe) Tj 0 Tr (d) Tj")

    assert [cell.text for cell in page.word_cells] == ["Clipped"]
    assert [cell.text for cell in page.textline_cells] == ["Clipped"]


def test_invisible_copy_over_visible_text_is_still_removed_as_a_duplicate():
    """A text layer laid over the same visible text repeats it, glyph by glyph."""
    page = _page("0 Tr (Hello world) Tj 1 0 0 1 72 700 Tm 3 Tr (Hello world) Tj")

    assert _modes(page.word_cells) == [("Hello", 0), ("world", 0)]
    assert _modes(page.textline_cells) == [("Hello world", 0)]
