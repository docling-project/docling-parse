#!/usr/bin/env python
"""Word boundaries on a spaced run whose glyphs are letter-spaced (docling-parse#377).

On a run that carries explicit spaces, a gap between two glyphs used to start a
new word whenever it passed the word-gap threshold, so a tracked word such as
`T R A C K I N G` came out as one word per letter. The contraction now drops
such a boundary when the gap is small against the run's own explicit spaces and
is either under half a font space or no wider than the font's usual letter gap
on that run. Two bounds keep the second test from merging real words:

* the evidence comes only from other gaps in the same font, so a tracked span
  in another font cannot vouch for a gap;
* the gap must not stand out from the other letter gaps of its own segment
  (the glyphs between two explicit spaces), so a tracked span elsewhere on the
  line cannot vouch for a gap between two untracked words.

Every page below is built in memory with core-14 fonts, so each test reads as
the content stream it is about. The streams come from the review of #377. /F1
is Helvetica and /F2 is Times-Roman; at 12 pt a TJ adjustment of -278 moves the
next glyph by one Helvetica space (278/1000 em), and `14 Tw` widens the
explicit space after `X` so the run's space reference is several spaces wide.
"""

from tests.pdf_builder import parse_page, simple_page_pdf

HELVETICA = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)
TIMES = "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>"

TRACKED = "[(T) -278 (R) -278 (A) -278 (C) -278 (K) -278 (I) -278 (N) -278 (G)] TJ"
ONE_TWO = "14 Tw (X ) Tj 0 Tw [(ONE) -278 (TWO)] TJ ( ) Tj"
SPLIT_ONE_TWO = ["X", "ONE", "TWO", "TRACKING"]


def _words(content: str) -> list[str]:
    pdf = simple_page_pdf(
        f"BT /F1 12 Tf 1 0 0 1 50 50 Tm {content} ET",
        resources="/Font << /F1 5 0 R /F2 6 0 R >>",
        media_box="[0 0 300 150]",
        extra_objects=[HELVETICA, TIMES],
    )
    return [cell.text for cell in parse_page(pdf).word_cells]


def test_tracked_word_in_the_same_font_rejoins():
    # The repair itself: every letter gap of TRACKING is one space, as wide as
    # the gap between ONE and TWO, but only TRACKING has letter gaps that wide.
    assert _words(f"{ONE_TWO} /F1 12 Tf {TRACKED}") == SPLIT_ONE_TWO


def test_plain_suffix_leaves_one_two_split():
    assert _words(f"{ONE_TWO} /F1 12 Tf (TRACKING) Tj") == SPLIT_ONE_TWO


def test_tracked_word_in_another_font_does_not_vouch():
    assert _words(f"{ONE_TWO} /F2 12 Tf {TRACKED}") == SPLIT_ONE_TWO


def test_plain_suffix_in_another_font():
    assert _words(f"{ONE_TWO} /F2 12 Tf (TRACKING) Tj") == SPLIT_ONE_TWO


def test_single_kerned_gap_after_expanded_space_stays_split():
    # One gap of a nominal space and nothing else to judge it by: a lone kerned
    # gap cannot vouch for itself.
    assert _words("14 Tw (X ) Tj 0 Tw [(A) -278 (B)] TJ") == ["X", "A", "B"]


def test_small_text_after_large_spaced_text_stays_split():
    # The spaces of 48 pt text say nothing about a gap in 12 pt text.
    pdf = simple_page_pdf(
        "BT /F1 48 Tf 1 0 0 1 50 50 Tm (X Y ) Tj /F1 12 Tf [(A) -278 (B)] TJ ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 150]",
        extra_objects=[HELVETICA],
    )
    assert [cell.text for cell in parse_page(pdf).word_cells] == ["X", "Y", "A", "B"]


def test_space_collapsed_by_negative_word_spacing_is_no_reference():
    # -3.336 Tw cancels the space's advance, so the run has no usable space
    # width and the rule must not act; the kerned HE / LLO stays as before.
    assert _words("[(HE) -166.6666667 (LLO)] TJ -3.336 Tw ( X) Tj") == ["HELLO", "X"]


def test_tracking_under_a_measured_space_rejoins():
    # Ordinary explicit spaces, so the run's space reference is one font space,
    # and the letter gaps of `little` are 0.7 of it (-195 = 0.7 x 278): wider
    # than half a space, which real tracking routinely is, and still one word.
    # Narrow letters, so each gap also passes the word-gap threshold.
    tracked = "[(l) -195 (i) -195 (t) -195 (t) -195 (l) -195 (e)] TJ"
    assert _words(f"(AB CD ) Tj {tracked}") == ["AB", "CD", "little"]


def test_untracked_words_at_the_same_gap_stay_split():
    # The same 0.7-space gap between two untracked words: no other gap on the
    # run is that wide, so nothing vouches for it.
    assert _words("(AB CD ) Tj [(lit) -195 (tilt)] TJ") == ["AB", "CD", "lit", "tilt"]


def test_uniform_tracking_across_a_boundary_is_not_detectable():
    # The documented limit: when ONE and TWO are tracked by the same amount as
    # the gap between them, nothing on the page marks that boundary, and the
    # rule joins them. Kept here so that a change to it is a visible decision.
    content = (
        "14 Tw (X ) Tj 0 Tw "
        "[(O) -278 (N) -278 (E) -278 (T) -278 (W) -278 (O)] TJ ( ) Tj "
        f"{TRACKED}"
    )
    assert _words(content) == ["X", "ONETWO", "TRACKING"]
