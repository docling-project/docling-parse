"""Procedural Type 3 callbacks, including queue and recursion boundaries."""

from __future__ import annotations

import zlib

import pytest
from docling_core.types.doc.page import TextCellUnit

from tests.pdf_builder import (
    build_pdf,
    content_stream,
    parse_page,
    render_page,
    stream_object,
)


@pytest.mark.parametrize("damaged", [False, True])
def test_queued_charprocs_keep_font_resources_and_restore_parent_state(
    damaged: bool,
) -> None:
    # Strokes require the procedural fallback. Two glyphs share one Tj queue;
    # half-opacity ink reveals an accidental replay as a darker stroke.
    source = b"1000 0 d0 /G1 gs 1 0 0 RG 100 w 0 0 m 1000 1000 l S\n1 0 0 1 1000 0 cm\n"
    if damaged:
        encoder = zlib.compressobj()
        source = encoder.compress(source) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources << /Font << /T3 5 0 R >> /ExtGState << /G1 << /CA 0.25 >> >> >> "
            "/Contents 4 0 R >>",
            content_stream(
                "/G1 gs 0 0 1 RG BT /T3 40 Tf 20 30 Td (aa) Tj ET "
                "4 w 140 20 m 180 60 l S"
            ),
            "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
            "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs 6 0 R "
            "/Encoding << /Type /Encoding /Differences [97 /a] >> "
            "/FirstChar 97 /LastChar 97 /Widths [1000] "
            "/Resources << /ExtGState << /G1 << /CA 0.5 >> >> >> >>",
            "<< /a 7 0 R >>",
            stream_object("/Filter /FlateDecode" if damaged else "", source),
        ]
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    for x in (40, 80):
        assert image.getpixel((x, 149)) == pytest.approx((255, 128, 128), abs=2)
    assert image.getpixel((160, 159)) == pytest.approx((191, 191, 255), abs=2)


def test_eager_repair_preserves_damaged_cached_glyph_bounds() -> None:
    # The font's declared box is deliberately loose. Eager validation must
    # repair the usable prefix before lazy mask analysis computes ink bounds;
    # otherwise analysis fails and the text cell falls back to the wrong box.
    source = b"1 0 -10 -10 10 10 d1\n0 -0.2 0.6 1 re f\n"
    encoder = zlib.compressobj()
    damaged = encoder.compress(source) + encoder.flush(zlib.Z_SYNC_FLUSH) + b"\x07"
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources << /Font << /T3 5 0 R >> >> /Contents 4 0 R >>",
            content_stream("BT /T3 100 Tf 50 50 Td (a) Tj ET"),
            "<< /Type /Font /Subtype /Type3 /FontBBox [-10 -10 10 10] "
            "/FontMatrix [1 0 0 1 0 0] /CharProcs 6 0 R "
            "/Encoding << /Type /Encoding /Differences [97 /a] >> "
            "/FirstChar 97 /LastChar 97 /Widths [1] /Resources << >> >>",
            "<< /a 7 0 R >>",
            stream_object("/Filter /FlateDecode", damaged),
        ]
    )
    cells = list(parse_page(pdf).iterate_cells(TextCellUnit.CHAR))
    assert len(cells) == 1
    box = cells[0].rect.to_bounding_box()
    assert box.width == pytest.approx(60.0, abs=0.1)
    assert box.height == pytest.approx(100.0, abs=0.1)


def test_recursive_charproc_stops_at_eight_and_the_next_glyph_still_paints() -> None:
    recursive = (
        b"1000 0 d0 100 w 0 0 m 1000 1000 l S BT /T3 1000 Tf 1 0 0 1 0 0 Tm (a) Tj ET"
    )
    plain = b"1000 0 d0 100 w 0 0 m 1000 1000 l S"
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources << /Font << /T3 5 0 R >> /ExtGState << /G1 << /CA 0.25 >> >> >> "
            "/Contents 4 0 R >>",
            content_stream(
                "/G1 gs 0 G BT /T3 40 Tf 20 30 Td (a) Tj "
                "1 0 0 1 80 30 Tm (b) Tj ET "
                "0 0 1 RG 4 w 140 20 m 180 60 l S"
            ),
            "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
            "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs 6 0 R "
            "/Encoding << /Type /Encoding /Differences [97 /a /b] >> "
            "/FirstChar 97 /LastChar 98 /Widths [1000 1000] "
            "/Resources << /Font << /T3 5 0 R >> >> >>",
            "<< /a 7 0 R /b 8 0 R >>",
            stream_object("", recursive),
            stream_object("", plain),
        ]
    )
    image = render_page(pdf, scale=1.0).get_image().convert("RGB")

    # Eight quarter-opacity strokes: white * (3/4)^8, allowing 8-bit rounding.
    assert image.getpixel((40, 149)) == pytest.approx((26, 26, 26), abs=3)
    assert image.getpixel((100, 149)) == pytest.approx((191, 191, 191), abs=2)
    assert image.getpixel((160, 159)) == pytest.approx((191, 191, 255), abs=2)
