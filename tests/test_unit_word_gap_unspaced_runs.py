#!/usr/bin/env python
"""Word boundaries on runs without a space glyph (docling-parse#383).

TeX output and most positioned headings carry no space glyph: a word gap is a
TJ cursor move, and the contraction has to infer which unmarked gaps are word
gaps. Since 7.22.1 that decision divided each gap by the mean advance of the
two glyphs around it and compared the ratio with a 0.35 cap. The unit shrinks
a word gap next to a wide glyph (`d|W` in `Related Work`: 0.21 em, but 0.26 of
the pair's advance) below the cap, while a kern between narrow glyphs (`n|f`
in `Confidential`: 0.10 em, but 0.24 of the pair) rises above it, so no
threshold in that unit separates the two. These tests pin the replacement for
such runs: the gap in em (font-size units), minus the run's uniform letter
spacing, against 0.13 em. On a tightly justified line the threshold follows the
line's own word spacing (70 % of the median confirmed word gap, never below
0.09 em), because pair kerning between a space and a capital takes a tight word
gap under 0.13 em. Runs that carry space glyphs are unchanged.

Every page is built in memory with core-14 fonts, or with a non-embedded GBK
font for the CJK cases, so each test reads as the content stream it is about.
At 12 pt a TJ adjustment of -1000 moves the next glyph by one em.
"""

from tests.pdf_builder import parse_page, simple_page_pdf

HELVETICA = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)
# A Type0 font with a predefined CMap and no font program; the codes are GBK
# byte pairs, as in test_unit_cjk_fonts.py.
GBK_TYPE0 = (
    "<< /Type /Font /Subtype /Type0 /BaseFont /SimSun /Encoding /GBK-EUC-H "
    "/DescendantFonts [7 0 R] >>"
)
GBK_CIDFONT = (
    "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /SimSun "
    "/CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 2 >> "
    "/FontDescriptor 8 0 R /DW 1000 >>"
)
GBK_DESCRIPTOR = (
    "<< /Type /FontDescriptor /FontName /SimSun /Flags 4 "
    "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 859 /Descent -141 "
    "/CapHeight 674 /StemV 91 >>"
)


def _cells(
    content: str, *, font: str = "/F1", size: float = 12
) -> tuple[list[str], list[str]]:
    pdf = simple_page_pdf(
        f"BT {font} {size} Tf 1 0 0 1 20 50 Tm {content} ET",
        resources="/Font << /F1 5 0 R /F3 6 0 R >>",
        media_box="[0 0 500 150]",
        extra_objects=[HELVETICA, GBK_TYPE0, GBK_CIDFONT, GBK_DESCRIPTOR],
    )
    page = parse_page(pdf)
    return (
        [cell.text for cell in page.word_cells],
        [cell.text for cell in page.textline_cells],
    )


def _gbk(text: str) -> str:
    """`text` as the GBK bytes a content stream carries, latin-1 for pdf_builder."""
    raw = text.encode("gbk")
    assert not any(byte in raw for byte in b"()\\"), raw
    return raw.decode("latin-1")


def _spread(text: str, gaps: list[int]) -> str:
    """A TJ array of GBK characters with the given adjustments between them."""
    parts = [f"({_gbk(text[0])})"]
    for char, gap in zip(text[1:], gaps):
        parts.append(f"-{gap} ({_gbk(char)})")
    return "[" + " ".join(parts) + "] TJ"


def test_word_gap_next_to_a_wide_glyph_survives_a_section_number():
    # The geometry of `2 Related Work` in 2511.21686v1: the heading's word gap
    # is 0.21 em and sits between `d` and `W`, the section number is one em
    # away. 7.22.3 reads `2 RelatedWork`.
    words, lines = _cells("[(2) -1000 (Related) -211 (Work)] TJ")
    assert words == ["2", "Related", "Work"]
    assert lines == ["2 Related Work"]


def test_every_word_gap_of_a_numbered_heading_survives():
    # Four word gaps of 0.24 em; only the one between `m` and `a` was lost.
    words, _ = _cells(
        "[(5.2.1) -1140 (Evaluating) -240 (Parallelism) -240 (and) -240 (Throughput)] TJ"
    )
    assert words == ["5.2.1", "Evaluating", "Parallelism", "and", "Throughput"]


def test_body_text_word_gaps_measure_the_same_next_to_any_glyph():
    # Thirteen identical word gaps of a justified TeX line; `m|b` is one of them.
    words, _ = _cells(
        "[(We) -246 (curate) -246 (training) -246 (data) -246 (from) -246 (both) "
        "-246 (open-source)] TJ"
    )
    assert words == ["We", "curate", "training", "data", "from", "both", "open-source"]


def test_a_kern_between_narrow_glyphs_stays_joined():
    # `(Con) -99 (fidential.)` at 18 pt, case 3 of docling-parse#362: a 0.10 em
    # kern that measures 0.24 of the narrow pair `n|f`. 7.22.3 reads
    # `Con fidential.`.
    words, lines = _cells("[(Con) -99 (fidential.)] TJ", size=18)
    assert words == ["Confidential."]
    assert lines == ["Confidential."]


def test_a_per_glyph_correction_stays_joined():
    # The `(f) -106 (l)` correction of docling-parse#377 on a run without
    # spaces: 0.11 em, but 0.42 of the pair `f|l`.
    words, _ = _cells("[(Inf) -106 (lammation)] TJ", size=10.4)
    assert words == ["Inflammation"]


def test_a_kerned_word_gap_on_a_tightly_justified_line_survives():
    # newspaper-00.pdf: word gaps of 0.15 em in a condensed face, and pair
    # kerning between the space and `V` takes the gap before `Verteilung` down
    # to 0.115 em. The two other word gaps of the line put its threshold at
    # 0.105 em.
    words, lines = _cells("[(und) -115 (Verteilung) -150 (unserer) -150 (Gra-)] TJ")
    assert words == ["und", "Verteilung", "unserer", "Gra-"]
    assert lines == ["und Verteilung unserer Gra-"]


def test_a_kerned_seam_with_no_word_gaps_around_it_stays_joined():
    # The same 0.115 em gap with nothing on the line to compare it with.
    words, _ = _cells("[(Con) -115 (fidential)] TJ")
    assert words == ["Confidential"]


def test_ordinary_word_spacing_does_not_lower_the_threshold():
    # Word gaps of 0.25 em: 70 % of that is above 0.13 em, so the threshold
    # stays at 0.13 em and the 0.12 em gap inside `datasystem` stays joined.
    words, _ = _cells("[(an) -250 (ideal) -250 (data) -120 (system)] TJ")
    assert words == ["an", "ideal", "datasystem"]


def test_a_letter_spaced_heading_stays_whole():
    # Tracking of 0.15 em on every letter and a word gap of 0.6 em: the letter
    # spacing is removed from each gap before the threshold is applied.
    words, lines = _cells(
        "[(A) -150 (R) -150 (T) -150 (I) -150 (C) -150 (L) -150 (E) "
        "-600 (I) -150 (N) -150 (F) -150 (O)] TJ"
    )
    assert words == ["ARTICLE", "INFO"]
    assert lines == ["ARTICLE INFO"]


def test_tracking_wider_than_a_fifth_of_an_em_is_not_recognised():
    # The documented limit: 2.5 pt of character spacing at 12 pt is 0.21 em,
    # above the widest tracking the rule accepts, because positioned
    # mathematics reaches the same gaps. The heading still comes out one
    # letter per word, as on 7.22.3; docling-parse#358 addresses this case.
    words, _ = _cells("2.5 Tc [(article) -800 (info)] TJ")
    assert words == list("articleinfo")


def test_justified_cjk_text_contracts_to_one_cell():
    # Justification spreads every character by 0.3 em. 7.22.3 reads one word
    # per character; the uniform spread is the run's tracking and is removed.
    words, lines = _cells(_spread("中文排版测试", [300] * 5), font="/F3")
    assert words == ["中文排版测试"]
    assert lines == ["中文排版测试"]


def test_a_wider_gap_in_justified_cjk_text_still_splits():
    words, _ = _cells(_spread("中文排版测试", [300, 300, 1000, 300, 300]), font="/F3")
    assert words == ["中文排", "版测试"]


def test_touching_cjk_characters_stay_one_cell():
    words, _ = _cells(f"({_gbk('中文排版测试')}) Tj", font="/F3")
    assert words == ["中文排版测试"]
