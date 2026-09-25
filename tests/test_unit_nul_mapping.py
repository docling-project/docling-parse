from io import BytesIO

import pytest

from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf


def _extract_character(mapping: str, glyph_name: str) -> str:
    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Test-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
        f"1 beginbfrange\n<41> <41> <{mapping}>\nendbfrange\n"
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend"
    )
    pdf = simple_page_pdf(
        "BT /F1 24 Tf 20 100 Td (A) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        extra_objects=[
            "<< /Type /Font /Subtype /Type1 /BaseFont /XXXXXX+FakeSubset "
            "/FirstChar 65 /LastChar 65 /Widths [600] "
            f"/Encoding << /Differences [65 /{glyph_name}] >> "
            "/ToUnicode 6 0 R >>",
            content_stream(cmap),
        ],
    )
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        BytesIO(pdf),
        decode_config=DecodeConfig(keep_glyphs=True),
    )
    page = doc.get_page(1)
    assert len(page.char_cells) == 1
    return page.char_cells[0].text


def test_authoritative_minus_overrides_conflicting_identity_before_normalization():
    assert _extract_character(mapping="2212", glyph_name="plus") == "-"


@pytest.mark.parametrize(("glyph_name", "expected"), [("minus", "-"), ("plus", "+")])
def test_nul_mapping_recovers_from_known_glyph_identity(glyph_name: str, expected: str):
    assert _extract_character(mapping="0000", glyph_name=glyph_name) == expected


def test_nul_mapping_with_unknown_identity_remains_unresolved():
    assert (
        _extract_character(mapping="0000", glyph_name="gid00043")
        == "GLYPH<name:gid00043>"
    )
