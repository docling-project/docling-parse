#!/usr/bin/env python
# SPDX-License-Identifier: MIT
"""A page whose /Contents is an array of streams.

The streams of the array are one content stream, divided at token boundaries
(PDF 32000-1, 7.8.2): a token can begin in one stream and end in the next. A
producer can therefore end a stream with the `[` of a `TJ` array and start the
next one with its strings. docling-parse used to tokenize each stream on its
own, and so lost the text of every such `TJ`.

These tests build the document they are about.
"""

from __future__ import annotations

from tests.pdf_builder import build_pdf, content_stream, parse_page, stream_object

FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
)


def _lines(first: bytes, second: bytes) -> list[str]:
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300]"
            " /Resources << /Font << /F1 6 0 R >> >> /Contents [4 0 R 5 0 R] >>",
            first,
            second,
            FONT,
        ]
    )
    return [cell.text for cell in parse_page(pdf).textline_cells]


def test_tj_array_split_across_two_streams_keeps_its_text():
    lines = _lines(
        content_stream("BT /F1 12 Tf 20 250 Td [(one)] TJ 0 -20 Td ["),
        content_stream("(two)] TJ 0 -20 Td [(three)] TJ ET"),
    )

    assert lines == ["one", "two", "three"]


def test_damaged_stream_does_not_lose_the_intact_one():
    """QPDF inflates all the streams before the first token: one bad stream
    must not take the other with it."""
    lines = _lines(
        content_stream("BT /F1 12 Tf 20 250 Td (intact) Tj ET"),
        stream_object("/Filter /FlateDecode", b"this is not zlib data"),
    )

    assert lines == ["intact"]


def test_operands_split_across_streams_reach_their_operator():
    lines = _lines(
        content_stream("BT /F1 12 Tf 20 250 Td (before) Tj 0 -20"),
        content_stream("Td (after) Tj ET"),
    )

    assert lines == ["before", "after"]


def test_damaged_middle_stream_keeps_prefix_and_suffix_exactly_once():
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300]"
            " /Resources << /Font << /F1 7 0 R >> >>"
            " /Contents [4 0 R 5 0 R 6 0 R] >>",
            content_stream("BT /F1 12 Tf 20 250 Td (before) Tj ET"),
            stream_object("/Filter /FlateDecode", b"this is not zlib data"),
            content_stream("BT /F1 12 Tf 20 200 Td (after) Tj ET"),
            FONT,
        ]
    )

    assert [cell.text for cell in parse_page(pdf).textline_cells] == ["before", "after"]
