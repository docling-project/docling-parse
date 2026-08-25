#!/usr/bin/env python
"""Vertical writing mode of composite fonts (ISO 32000-1, 9.4.4 and 9.7.4.3).

A CMap whose name ends in `-V` puts the font in writing mode 1. That changes
what a glyph advance *means*: the pen moves down by the vertical displacement
w1 (from /W2 or /DW2) instead of right by the glyph width, and each glyph hangs
from its vertical origin rather than sitting on it. A reader that ignores the
writing mode lays the same run out horizontally, which sends it off the edge of
whatever column it was meant to fill.

These tests assert on cell geometry rather than pixels: the layout is what the
writing mode decides, and it is visible without resolving a single glyph.
"""

from io import BytesIO
from typing import List

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingPdfParser,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

PAGE_WIDTH = 200
PAGE_HEIGHT = 200

# /DW2 defaults to [880 -1000]: the vertical origin sits 0.88 em above the
# glyph's horizontal origin, and each glyph advances a full em downwards.
DEFAULT_DW2 = "[880 -1000]"


# CIDs 1, 2, 3 as 'A', 'B', 'C', so the run produces text cells to measure
TO_UNICODE = (
    "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
    "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
    "/CMapName /Adobe-Identity-UCS def /CMapType 2 def\n"
    "1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
    "3 beginbfchar\n<0001> <0041>\n<0002> <0042>\n<0003> <0043>\nendbfchar\n"
    "endcmap CMapName currentdict /CMap defineresource pop end end"
)


def _build_pdf(content: str, encoding: str, dw2: str = DEFAULT_DW2) -> bytes:
    """One page whose /C0_0 is a Type0 font over a non-embedded CIDFontType0.

    No font program is needed: every metric the layout uses comes from /DW,
    /DW2 and the CMap name.
    """
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
        "/Resources << /Font << /C0_0 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type0 /BaseFont /SynthGothic "
        f"/Encoding {encoding} /DescendantFonts [6 0 R] /ToUnicode 8 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        "<< /Type /Font /Subtype /CIDFontType0 /BaseFont /SynthGothic "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        f"/DW 1000 /DW2 {dw2} /FontDescriptor 7 0 R >>",
        "<< /Type /FontDescriptor /FontName /SynthGothic /Flags 4 "
        "/FontBBox [0 -120 1000 880] /ItalicAngle 0 /Ascent 880 /Descent -120 "
        "/CapHeight 700 /StemV 80 >>",
        f"<< /Length {len(TO_UNICODE)} >>\nstream\n{TO_UNICODE}\nendstream",
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


def _char_boxes(content: str, encoding: str, dw2: str = DEFAULT_DW2) -> List[tuple]:
    """(left, bottom, right, top) of each character cell, in reading order."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        path_or_stream=BytesIO(_build_pdf(content, encoding, dw2)),
        decode_config=DecodeConfig(),
    )
    _, page = next(doc.iterate_pages())

    boxes = []
    for cell in page.char_cells:
        rect = cell.rect.to_bounding_box()
        boxes.append((rect.l, rect.b, rect.r, rect.t))
    return boxes


def _text_render_instructions(
    content: str, encoding: str, dw2: str = DEFAULT_DW2
) -> List[dict]:
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=1,
            render_config=RenderConfig(),
        ),
        decode_config=DecodeConfig(),
    )
    parser.load(BytesIO(_build_pdf(content, encoding, dw2)))

    try:
        result = next(parser.iterate_results())
        assert result.success, result.error_message
        instructions = result._export_render_instructions_json()["instructions"]
        return [item for item in instructions if item.get("type") == "text"]
    finally:
        parser.unload_all()


# three CIDs at 20 pt from (100, 150). The size lives in the text matrix and
# /Tf carries 1, which is how the composite fonts in real documents are set.
CONTENT = "BT /C0_0 1 Tf 20 0 0 20 100 150 Tm <000100020003> Tj ET"


def test_vertical_font_stacks_its_glyphs_downwards():
    """Writing mode 1 advances down by w1, not right by the glyph width."""
    boxes = _char_boxes(CONTENT, "/Identity-V")
    assert len(boxes) == 3, f"expected three character cells, got {len(boxes)}"

    for index in range(1, len(boxes)):
        previous, current = boxes[index - 1], boxes[index]

        assert current[3] < previous[3], (
            f"glyph {index} is not below glyph {index - 1}: "
            f"top {current[3]} vs {previous[3]}"
        )
        assert abs(current[0] - previous[0]) < 0.5, (
            f"glyph {index} drifted sideways: left {current[0]} vs {previous[0]}"
        )

    # /DW2 says one em per glyph, and the font is set at 20 pt
    pitch = boxes[0][3] - boxes[1][3]
    assert abs(pitch - 20.0) < 0.5, f"expected a 20 pt pitch, got {pitch}"


def test_horizontal_font_advances_to_the_right():
    """The same run through the horizontal CMap is laid out the other way."""
    boxes = _char_boxes(CONTENT, "/Identity-H")
    assert len(boxes) == 3

    for index in range(1, len(boxes)):
        previous, current = boxes[index - 1], boxes[index]

        assert current[0] > previous[0], (
            f"glyph {index} is not right of glyph {index - 1}"
        )
        assert abs(current[3] - previous[3]) < 0.5, (
            f"glyph {index} drifted vertically: top {current[3]} vs {previous[3]}"
        )


def test_vertical_glyphs_hang_from_the_vertical_origin():
    """The pen is at the glyph's *vertical* origin, so the glyph sits below it.

    With /DW2 [880 -1000] the horizontal origin is 0.88 em down, which puts the
    top of a 0.88-ascent glyph level with the pen. A reader that skips the
    position vector draws the first glyph a full 0.88 em too high.
    """
    boxes = _char_boxes(CONTENT, "/Identity-V")

    # Tm places the pen at y = 150; the glyph box may not reach above it
    assert boxes[0][3] <= 150.5, f"first glyph reaches above the pen: {boxes[0][3]}"
    assert boxes[0][3] > 140.0, f"first glyph is far below the pen: {boxes[0][3]}"


def test_vertical_pitch_follows_dw2():
    """A different /DW2 displacement changes the spacing, not the direction."""
    boxes = _char_boxes(CONTENT, "/Identity-V", dw2="[880 -500]")

    pitch = boxes[0][3] - boxes[1][3]
    assert abs(pitch - 10.0) < 0.5, f"expected a 10 pt pitch from -500, got {pitch}"


def test_vertical_render_basepoint_is_horizontal_glyph_origin():
    """The renderer needs the glyph origin, not the vertical text origin."""
    instructions = _text_render_instructions(CONTENT, "/Identity-V")
    assert len(instructions) == 3

    first = instructions[0]
    quad = first["quad"]

    assert abs(first["base_x0"] - quad["r_x0"]) < 0.5
    assert abs(first["base_y0"] - 132.4) < 0.5
