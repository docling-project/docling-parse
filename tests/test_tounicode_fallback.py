#!/usr/bin/env python
"""Fallback behavior for char codes not covered by an explicit /ToUnicode CMap.

Symbolic fonts (e.g. subsetted Arabic fonts) assign character codes with no
relation to the standard Latin encodings. When such a font carries a
/ToUnicode CMap that misses a code (a common generator bug for ligature
glyphs), falling back to Standard/WinAnsi tables fabricates unrelated ASCII
text — e.g. an Arabic ligature at code 0x23 coming out as '#'
(docling-project/docling#3802). The parser must emit a GLYPH marker instead.

These tests build the PDFs in memory: a simple TrueType font, text bytes
0x21 0x22 0x23, and a /ToUnicode CMap that covers 0x21/0x22 but — depending
on the variant — omits the ligature entry for 0x23.
"""

from io import BytesIO

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser


def _build_pdf(include_ligature: bool, symbolic: bool) -> bytes:
    bfchars = ["<21> <0634>", "<22> <0623>"]  # ش , أ
    if include_ligature:
        bfchars.append("<23> <0641064A>")  # في (one glyph, two codepoints)
    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<00> <FF>\n"
        "endcodespacerange\n"
        f"{len(bfchars)} beginbfchar\n" + "\n".join(bfchars) + "\nendbfchar\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end"
    )

    flags = 4 if symbolic else 32  # symbolic vs nonsymbolic (bit 3 vs bit 6)
    content = 'BT /F1 24 Tf 72 700 Td (!"#) Tj ET'
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /TrueType /BaseFont /Arial "
        "/FirstChar 33 /LastChar 35 /Widths [500 500 500] "
        "/Encoding /WinAnsiEncoding /FontDescriptor 7 0 R /ToUnicode 6 0 R >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        f"<< /Length {len(cmap)} >>\nstream\n{cmap}\nendstream",
        f"<< /Type /FontDescriptor /FontName /Arial /Flags {flags} "
        "/FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900 /Descent -200 "
        "/CapHeight 700 /StemV 80 >>",
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


def _extract_text(
    include_ligature: bool, symbolic: bool, keep_glyphs: bool = True
) -> str:
    parser = DoclingPdfParser(loglevel="fatal")
    config = DecodeConfig(keep_glyphs=keep_glyphs)
    doc = parser.load(
        path_or_stream=BytesIO(_build_pdf(include_ligature, symbolic)),
        decode_config=config,
    )
    _, page = next(doc.iterate_pages())
    return "".join(cell.text for cell in page.textline_cells)


def test_symbolic_font_tounicode_miss_yields_glyph_marker():
    # No fabricated '#' from the WinAnsi/Standard tables: the unmapped code
    # of a symbolic font must surface as a GLYPH marker.
    text = _extract_text(include_ligature=False, symbolic=True)
    assert "#" not in text
    assert "GLYPH<35>" in text
    assert "ش" in text and "أ" in text  # covered codes still resolve


def test_symbolic_font_complete_tounicode_resolves_ligature():
    text = _extract_text(include_ligature=True, symbolic=True)
    assert "#" not in text
    assert "GLYPH" not in text
    assert "في" in text


def test_nonsymbolic_font_keeps_encoding_fallback():
    # Latin fonts with a partial /ToUnicode still fall back to the declared
    # encoding: 0x23 in WinAnsi genuinely is '#'.
    text = _extract_text(include_ligature=False, symbolic=False)
    assert "#" in text
    assert "GLYPH" not in text


def test_default_config_strips_glyph_markers():
    # Production default (keep_glyphs=False, see config.h): the marker is
    # stripped to a space in pdf_states/text.h, so neither GLYPH<...> nor
    # a fabricated '#' ever reaches the output.
    text = _extract_text(include_ligature=False, symbolic=True, keep_glyphs=False)
    assert "GLYPH" not in text
    assert "#" not in text
    assert "\u0634" in text and "\u0623" in text  # covered codes still resolve
