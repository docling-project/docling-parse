#!/usr/bin/env python
"""/ActualText replacement text of marked-content spans (PDF 32000-1, 14.9.4).

/ActualText declares the exact Unicode of a glyph run whose glyph->Unicode
mapping cannot be recovered from the font encoding alone: composed accents,
ligatures, discretionary hyphens. A common accent-composition pattern draws a
base letter plus a zero-advance combining glyph whose character code collides
with an unrelated Latin letter — e.g. a caron at code 0x4D (ASCII 'M'), so
'Oreši' extracts as 'OresMi' when the span is ignored
(docling-project/docling#3783).

Substitution is anchored to the text cells drawn inside the span: spans over
non-text content are skipped, and replacement strings implausibly long for the
glyph run (descriptive misuse of the key, /Alt semantics) are rejected.

These tests build the PDFs in memory: base-14 Helvetica for regular text plus
a non-embedded TrueType 'caron' font whose only code 0x4D has zero advance.
"""

from io import BytesIO

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser

# UTF-16BE hex string for 'š' (U+0161)
SCARON = "<FEFF0161>"


def _build_pdf(content: str) -> bytes:
    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 220 100] "
        "/Resources << /Font << /F1 4 0 R /F2 6 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>",
        f"<< /Length {len(content)} >>\nstream\n{content}\nendstream",
        "<< /Type /Font /Subtype /TrueType /BaseFont /SynthCaron "
        "/FirstChar 77 /LastChar 77 /Widths [0] "
        "/Encoding /WinAnsiEncoding /FontDescriptor 7 0 R >>",
        "<< /Type /FontDescriptor /FontName /SynthCaron /Flags 32 "
        "/FontBBox [-540 -200 0 800] /ItalicAngle 0 /Ascent 800 /Descent -200 "
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


def _extract_text(content: str, apply_actual_text: bool = True) -> str:
    parser = DoclingPdfParser(loglevel="fatal")
    config = DecodeConfig(apply_actual_text=apply_actual_text)
    doc = parser.load(
        path_or_stream=BytesIO(_build_pdf(content)),
        decode_config=config,
    )
    _, page = next(doc.iterate_pages())
    return "".join(cell.text for cell in page.textline_cells)


CARON_CONTENT = (
    "BT /F1 24 Tf 40 50 Td (Ore) Tj "
    f"/Span <</ActualText {SCARON}>> BDC (s) Tj /F2 24 Tf <4D> Tj EMC "
    "/F1 24 Tf (i) Tj ET"
)


def test_actual_text_merges_composed_accent():
    # 2 cells ('s' + zero-advance caron decoded as 'M') -> 1 char 'š'
    assert _extract_text(CARON_CONTENT) == "Oreši"


def test_actual_text_disabled_keeps_raw_glyphs():
    assert _extract_text(CARON_CONTENT, apply_actual_text=False) == "OresMi"


def test_actual_text_one_to_one_substitution():
    # character count matches cell count: per-glyph geometry is preserved
    content = (
        "BT /F1 24 Tf 40 50 Td /Span <</ActualText (ab)>> BDC (xy) Tj EMC (z) Tj ET"
    )
    assert _extract_text(content) == "abz"


def test_empty_actual_text_removes_span_cells():
    # an empty /ActualText declares non-textual content (eg a purely visual
    # line-break hyphen). The glyph's advance still occupies space on the
    # page, so the line sanitizer may render the gap as a space; the point
    # here is that the hyphen character itself is gone.
    content = (
        "BT /F1 24 Tf 40 50 Td (exam) Tj "
        "/Span <</ActualText <FEFF>>> BDC (-) Tj EMC (ple) Tj ET"
    )
    text = _extract_text(content)
    assert "-" not in text
    assert text.replace(" ", "") == "example"


def test_oversized_actual_text_is_rejected():
    # descriptive misuse (/Alt semantics in the /ActualText key): implausibly
    # long for the glyph run, keep the drawn glyphs
    desc = "A long description that belongs in Alt not here"
    content = f"BT /F1 24 Tf 40 50 Td /Span <</ActualText ({desc})>> BDC (ab) Tj EMC ET"
    assert _extract_text(content) == "ab"


def test_actual_text_span_without_text_cells_is_skipped():
    # alternate-text-like usage over non-text content has no anchor cell
    content = (
        "BT /F1 24 Tf 40 50 Td (x) Tj ET "
        "/Span <</ActualText (ghost)>> BDC 10 10 50 50 re f EMC"
    )
    assert _extract_text(content) == "x"


def test_named_properties_are_ignored_gracefully():
    # BDC with a name operand (resolved via /Properties) is not inspected;
    # the span decodes as if unmarked
    content = "BT /F1 24 Tf 40 50 Td /OC /MC0 BDC (ok) Tj EMC ET"
    assert _extract_text(content) == "ok"


def test_nested_spans_outer_actual_text_wins():
    # the outer replacement describes the whole run, including nested spans
    content = (
        "BT /F1 24 Tf 40 50 Td "
        "/Span <</ActualText (AB)>> BDC (a) Tj "
        "/Span <</ActualText (Z)>> BDC (b) Tj EMC EMC ET"
    )
    assert _extract_text(content) == "AB"
