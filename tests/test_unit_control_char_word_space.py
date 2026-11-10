from io import BytesIO

from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import content_stream, simple_page_pdf


def _extract_words_and_lines(encoding: str) -> tuple[list[str], list[str]]:
    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Test-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
        "1 beginbfchar\n<20> <0008>\nendbfchar\n"
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend"
    )
    pdf = simple_page_pdf(
        "BT /F1 12 Tf 20 100 Td (Price per hour) Tj ET",
        resources="/Font << /F1 5 0 R >>",
        extra_objects=[
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
            f"/Encoding {encoding} /ToUnicode 6 0 R >>",
            content_stream(cmap),
        ],
    )
    doc = DoclingPdfParser(loglevel="fatal").load(BytesIO(pdf))
    page = doc.get_page(1)
    return (
        [cell.text for cell in page.word_cells],
        [cell.text for cell in page.textline_cells],
    )


def test_space_glyph_mapped_to_control_character_separates_words():
    words, lines = _extract_words_and_lines("/WinAnsiEncoding")

    assert words == ["Price", "per", "hour"]
    assert lines == ["Price per hour"]


def test_visible_glyph_mapped_to_control_character_stays_in_word():
    words, _ = _extract_words_and_lines(
        "<< /BaseEncoding /WinAnsiEncoding /Differences [32 /bullet] >>"
    )

    assert words == ["Price\x08per\x08hour"]
