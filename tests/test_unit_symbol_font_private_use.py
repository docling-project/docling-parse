#!/usr/bin/env python
"""Symbol-font glyphs behind a private-use /ToUnicode map (docling-parse#416).

Office exporters embed Symbol, SymbolMT and ZapfDingbats as Identity-H fonts
whose /ToUnicode map sends every glyph to the "symbol charset" private-use
block, U+F000 plus the byte code. Taken at face value, a bullet is U+F0B7 and
an equals sign U+F03D: invisible characters that no search finds. These tests
pin the fold of that block back through the font's own encoding, which only
applies when the base font is one of the two core symbol fonts, so a CJK or
Arabic font that uses the block for real text keeps its code points.

Every page is built in memory around a non-embedded Identity-H font, as in
test_unit_cmap_encoding.py, so each test reads as the /ToUnicode map it is
about.
"""

from io import BytesIO

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import build_pdf, content_stream, simple_page_pdf, stream_object


def _to_unicode(entries: dict[str, str]) -> str:
    bfchar = "\n".join(f"<{code}> <{uni}>" for code, uni in entries.items())
    return (
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CMapName /Test-UCS2 def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
        f"{len(entries)} beginbfchar\n{bfchar}\nendbfchar\nendcmap\n"
        "CMapName currentdict /CMap defineresource pop\nend\nend"
    )


def _identity_h_words(
    base_font: str, entries: dict[str, str], text_bytes: str
) -> list[str]:
    """Words of a page that sets `text_bytes` in an Identity-H `base_font`."""
    content = f"BT /F1 12 Tf 20 100 Td <{text_bytes}> Tj ET"
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        content_stream(content),
        f"<< /Type /Font /Subtype /Type0 /BaseFont /{base_font} "
        "/Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>",
        f"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /{base_font} "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 8 0 R /DW 600 >>",
        stream_object("", _to_unicode(entries).encode("latin-1")),
        f"<< /Type /FontDescriptor /FontName /{base_font} /Flags 4 "
        "/FontBBox [0 -220 1113 1005] /ItalicAngle 0 /Ascent 1005 /Descent -220 "
        "/CapHeight 700 /StemV 80 >>",
    ]
    return _words(build_pdf(objects))


def _words(pdf: bytes) -> list[str]:
    doc = DoclingPdfParser(loglevel="fatal").load(
        path_or_stream=BytesIO(pdf),
        decode_config=DecodeConfig(do_sanitization=True, keep_glyphs=True),
    )
    _, page = next(doc.iterate_pages())
    return [cell.text for cell in page.word_cells]


def test_symbolmt_bullet_behind_a_private_use_map_is_a_bullet():
    # Word's bullet: SymbolMT glyph 0x78 mapped to U+F0B7, as in
    # 16293520921922171261-1.pdf. 7.22.3 reads an invisible U+F0B7.
    assert _identity_h_words("ABCDEE+SymbolMT", {"0078": "F0B7"}, "0078") == ["•"]


def test_symbolmt_operators_and_greek_fold_through_the_symbol_encoding():
    # `θ = <U+2212>`, as set by an equation editor with F0xx /ToUnicode values. The
    # fold gives `minus` U+2212, which the text sanitizer keeps.
    entries = {"0003": "F020", "0010": "F02D", "0020": "F03D", "0071": "F071"}
    words = _identity_h_words("CKIGNK+SymbolMT", entries, "007100030020000300100003")
    assert words == ["θ", "=", "\u2212"]


def test_zapfdingbats_square_behind_a_private_use_map_is_a_square():
    # ZapfDingbats a73 (code 0x6E) is U+25A0.
    assert _identity_h_words("ZapfDingbats", {"0001": "F06E"}, "0001") == ["■"]


def test_a_code_the_symbol_encoding_lacks_keeps_its_private_use_value():
    # Symbol has no glyph at 0xFF: the map's value is the best information.
    assert _identity_h_words("SymbolMT", {"0005": "F0FF"}, "0005") == [""]


def test_other_fonts_keep_their_private_use_text():
    # CJK gaiji and Arabic presentation fonts set real text in the block.
    assert _identity_h_words("Gaiji-Regular", {"0078": "F0B7"}, "0078") == [""]


def test_non_embedded_symbol_without_to_unicode_still_decodes_by_encoding():
    # The built-in encoding route, untouched by the fold: `abp` is `αβπ`.
    pdf = simple_page_pdf(
        "BT /F1 12 Tf 1 0 0 1 20 50 Tm (abp \\267) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        media_box="[0 0 300 100]",
        extra_objects=["<< /Type /Font /Subtype /Type1 /BaseFont /Symbol >>"],
    )
    assert _words(pdf) == ["αβπ", "•"]
