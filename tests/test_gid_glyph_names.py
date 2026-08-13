#!/usr/bin/env python
"""Glyph-index names in /Encoding/Differences must not leak as reading text.

Subset generators (FontForge, fontTools, mPDF, ...) name glyphs by bare
index: /gid00043, /g43, /glyph43. Such a name identifies the glyph inside
the embedded font program but carries no reading text. When neither a
/ToUnicode CMap nor the glyph-tables can resolve the code, keeping the name
fabricates plausible-looking garbage ('gid00043gid00049...') that downstream
quality gates cannot detect (docling-project/docling-parse#238). The parser
must emit a GLYPH marker instead.

Meaningful unknown names (custom ligatures like /Th) keep the existing
name-as-text fallback, and a valid /ToUnicode CMap stays authoritative.

These tests build the PDFs in memory: a Type1 font whose /Differences maps
codes 0x41-0x43 to gid-style names, drawn as the string (ABC).
"""

from io import BytesIO

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser

_TOUNICODE = (
    "/CIDInit /ProcSet findresource begin\n"
    "12 dict begin\n"
    "begincmap\n"
    "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
    "/CMapName /Adobe-Identity-UCS def\n"
    "/CMapType 2 def\n"
    "1 begincodespacerange\n"
    "<00> <FF>\n"
    "endcodespacerange\n"
    "3 beginbfchar\n"
    "<41> <0043>\n"  # C
    "<42> <0049>\n"  # I
    "<43> <0041>\n"  # A
    "endbfchar\n"
    "endcmap\n"
    "CMapName currentdict /CMap defineresource pop\n"
    "end\n"
    "end"
)


def _build_pdf(glyph_names: list, include_tounicode: bool) -> bytes:
    differences = " ".join(f"/{name}" for name in glyph_names)
    tounicode = " /ToUnicode 6 0 R" if include_tounicode else ""
    content = "BT /F1 24 Tf 72 700 Td (ABC) Tj ET"
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /XXXXXX+FakeSubset "
        "/Encoding << /Type /Encoding "
        f"/Differences [ 65 {differences} ] >>{tounicode} >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
    ]
    if include_tounicode:
        objects.append(
            f"<< /Length {len(_TOUNICODE)} >>\nstream\n{_TOUNICODE}\nendstream"
        )

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
    glyph_names: list, include_tounicode: bool, keep_glyphs: bool = True
) -> str:
    parser = DoclingPdfParser(loglevel="fatal")
    config = DecodeConfig(keep_glyphs=keep_glyphs)
    doc = parser.load(
        path_or_stream=BytesIO(_build_pdf(glyph_names, include_tounicode)),
        decode_config=config,
    )
    _, page = next(doc.iterate_pages())
    return "".join(cell.text for cell in page.textline_cells)


def test_gid_names_without_tounicode_yield_glyph_markers():
    # No fabricated 'gid00043...' reading text: an unresolvable glyph-index
    # name must surface as a GLYPH marker.
    text = _extract_text(["gid00043", "gid00049", "gid00041"], include_tounicode=False)
    assert "GLYPH<name:gid00043>" in text
    assert "GLYPH<name:gid00049>" in text
    assert "GLYPH<name:gid00041>" in text
    assert "gid00043gid00049" not in text.replace("GLYPH<name:gid00043>", "")


def test_gid_name_variants_yield_glyph_markers():
    text = _extract_text(["g43", "glyph49", "index41"], include_tounicode=False)
    assert "GLYPH<name:g43>" in text
    assert "GLYPH<name:glyph49>" in text
    assert "GLYPH<name:index41>" in text


def test_tounicode_stays_authoritative_over_gid_names():
    # PDF 32000-1 section 9.10.2: the /ToUnicode CMap is the first method;
    # gid-style names never shadow it.
    text = _extract_text(["gid00043", "gid00049", "gid00041"], include_tounicode=True)
    assert text == "CIA"


def test_meaningful_unknown_names_keep_name_fallback():
    # Custom ligature names like /Th carry reading text; the existing
    # name-as-text fallback stays.
    text = _extract_text(["Th", "ft", "tt"], include_tounicode=False)
    assert text == "Thfttt"
    assert "GLYPH" not in text


def test_default_config_strips_gid_markers():
    # Production default (keep_glyphs=False, see config.h): the marker is
    # stripped to a space in pdf_states/text.h, so neither GLYPH<...> nor
    # the fabricated gid-name text ever reaches the output.
    text = _extract_text(
        ["gid00043", "gid00049", "gid00041"],
        include_tounicode=False,
        keep_glyphs=False,
    )
    assert "GLYPH" not in text
    assert "gid" not in text
    assert text.strip() == ""
