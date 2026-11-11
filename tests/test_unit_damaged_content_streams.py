#!/usr/bin/env python
"""Content streams damaged in transit, not malformed by their producer.

A flipped byte inside a Flate-compressed content stream has one of two
outcomes: zlib stops with an error part-way through, or it runs to the end
with a checksum mismatch and a garbled tail. Either way the operators in
front of the damage are intact, and that is what a reader should keep. mupdf
and poppler both log the bad operator and carry on.

docling-parse used to lose the whole page in four places, all hit by the
file in docling-project/docling-parse#160: an operator with too few operands
threw, `Tj` with a number where its string belongs threw, a zlib error in the
middle of the data made QPDF throw before a single token reached the parser,
and an unmatched `Q` popped the base graphics state so the next operator had
no state to run in.

These tests build the damaged document they are about.
"""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import build_pdf, content_stream, parse_page, stream_object

FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


def _page_pdf(content_object: bytes, *, height: int = 300) -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 {height}]"
            " /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
            content_object,
            FONT,
        ]
    )


def _lines(content_object: bytes, *, height: int = 300) -> list[str]:
    page = parse_page(_page_pdf(content_object, height=height))
    return [cell.text for cell in page.textline_cells]


def _before_after(damaged: str) -> str:
    """Two text lines; `damaged` is inserted between them inside the second."""
    return (
        "BT /F1 12 Tf 20 250 Td (before) Tj ET\n"
        f"BT /F1 12 Tf 20 200 Td {damaged} (after) Tj ET\n"
    )


def test_operator_with_too_few_operands_is_skipped():
    """A `Tw` that lost its operand (`Tj41.749 Tw` after a bit flip)."""
    lines = _lines(content_stream(_before_after("Tw")))

    assert lines == ["before", "after"]


def test_text_showing_operator_with_a_number_is_skipped():
    """`3.34 Tj`: the string operand was overwritten by the damage."""
    lines = _lines(content_stream(_before_after("3.34 Tj")))

    assert lines == ["before", "after"]


def test_operand_of_the_wrong_type_does_not_end_the_page():
    """`Tc` with a name: the operator fails and the next one still runs."""
    lines = _lines(content_stream(_before_after("/Oops Tc")))

    assert lines == ["before", "after"]


def test_unmatched_grestore_keeps_the_base_state():
    """A `Q` without its `q` must not leave the stream with no state."""
    content = (
        "BT /F1 12 Tf 20 250 Td (before) Tj ET\n"
        "Q\n"
        "BT /F1 12 Tf 20 200 Td (after) Tj ET\n"
    )
    lines = _lines(content_stream(content))

    assert lines == ["before", "after"]


def _flate_stream(payload: bytes) -> bytes:
    return stream_object("/Filter /FlateDecode", payload)


def _inflate_until_error(data: bytes) -> tuple[bytes, str | None]:
    """What zlib hands back before it gives up on `data`, and why."""
    inflater = zlib.decompressobj()
    out = b""
    for start in range(0, len(data), 16):
        try:
            out += inflater.decompress(data[start : start + 16])
        except zlib.error as exc:
            return out, str(exc)
    return out, None


def _damaged_flate_content() -> tuple[bytes, list[str]]:
    """A long text page whose Flate data fails part-way through.

    Returns the damaged compressed bytes and the words whose whole operator
    sequence zlib still produces in front of the error.
    """
    count = 150
    lines = [
        f"BT /F1 12 Tf 20 {1990 - 12 * i} Td (word{i:03d}) Tj ET" for i in range(count)
    ]
    plain = ("\n".join(lines) + "\n").encode("latin-1")
    good = zlib.compress(plain, 9)

    # Flip one byte in the second half and keep the first flip that makes
    # zlib stop with an error in the middle of the data. The end-of-stream
    # checksum mismatch does not count: QPDF already tolerates that one.
    for position in range(len(good) // 2, len(good)):
        damaged = (
            good[:position] + bytes([good[position] ^ 0xFF]) + good[position + 1 :]
        )
        prefix, error = _inflate_until_error(damaged)
        if error is None or "incorrect data check" in error:
            continue
        kept = [
            f"word{i:03d}"
            for i, line in enumerate(lines)
            if (line + "\n").encode() in prefix
        ]
        if len(kept) >= 5:
            return damaged, kept

    pytest.skip("could not craft a mid-stream zlib error with this zlib")


def test_flate_error_in_the_middle_keeps_the_operators_before_it():
    damaged, kept = _damaged_flate_content()

    lines = _lines(_flate_stream(damaged), height=2000)

    assert lines[: len(kept)] == kept
    assert len(lines) < 150


def test_intact_flate_stream_is_unaffected():
    plain = _before_after("").encode("latin-1")

    lines = _lines(_flate_stream(zlib.compress(plain)))

    assert lines == ["before", "after"]
