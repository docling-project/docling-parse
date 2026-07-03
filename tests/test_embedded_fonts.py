#!/usr/bin/env python
"""Tests for embedded font extraction and rendering.

The fixture ``tests/data/regression/2508.13113v2.pdf`` embeds two kinds of
font programs:

- raw Type 1 (``/FontFile``: NimbusRomNo9L, CMR/CMSY/CMTT/CMMI TeX fonts) on
  the text pages (e.g. page 1) — Blend2D cannot load these, so the renderer
  draws them as FreeType-decomposed outline paths;
- TrueType (``/FontFile2``: DejaVu Serif/Sans subsets) inside the figure Form
  XObjects on pages 8, 17, 20, 21 and 22 — Blend2D loads these natively, so
  the embedded face is used for drawing directly.
"""

from io import BytesIO

from docling_parse.pdf_parsers import (  # type: ignore[import]
    DecodePageConfig as _DecodePageConfig,
)
from PIL import Image as PILImage

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

ARXIV_PDF = "tests/data/regression/2508.13113v2.pdf"

TYPE1_ONLY_PAGE = 1  # only /FontFile (Type 1) fonts
TRUETYPE_PAGE = 21  # /FontFile2 DejaVu subsets inside figure XObjects


def _make_parser(render_config: RenderConfig) -> DoclingThreadedPdfParser:
    return DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=32,
            render_config=render_config,
        ),
        decode_config=DecodeConfig(),
    )


def _render_page(page_number: int, render_config: RenderConfig) -> PILImage.Image:
    parser = _make_parser(render_config)
    parser.load(ARXIV_PDF, page_numbers=[page_number])

    result = next(parser.iterate_results())
    assert result.success, result.error_message
    assert result.page_number == page_number

    return result.get_image()


def _is_blank(image: PILImage.Image) -> bool:
    lo, hi = image.convert("L").getextrema()
    return lo == hi


def test_render_config_exposes_use_embedded_fonts():
    """RenderConfig exposes the embedded-font opt-out, defaulting to on."""
    render_config = RenderConfig()
    assert render_config.use_embedded_fonts is True

    render_config.use_embedded_fonts = False
    assert render_config.use_embedded_fonts is False


def test_decode_page_config_exposes_extract_font_programs():
    """The C++ decode config exposes extraction, defaulting to off.

    Parse-only pipelines must not pay for font stream decoding; the threaded
    render pipeline switches it on internally.
    """
    cpp_config = _DecodePageConfig()
    assert cpp_config.extract_font_programs is False

    cpp_config.extract_font_programs = True
    assert cpp_config.extract_font_programs is True


def test_embedded_truetype_changes_rendering():
    """An embedded TrueType face must actually be used for drawing.

    With resolve_fonts=False the only alternatives are the hardcoded fallback
    font or the bbox outline, so using the embedded DejaVu subsets on the
    figure page must change the output on every platform. (With
    resolve_fonts=True the system resolver may find the same DejaVu family
    and produce near-identical pixels, which would make this test flaky.)
    """
    embedded_on = RenderConfig()
    embedded_on.resolve_fonts = False
    embedded_on.use_embedded_fonts = True

    embedded_off = RenderConfig()
    embedded_off.resolve_fonts = False
    embedded_off.use_embedded_fonts = False

    image_on = _render_page(TRUETYPE_PAGE, embedded_on)
    image_off = _render_page(TRUETYPE_PAGE, embedded_off)

    assert not _is_blank(image_on)
    assert not _is_blank(image_off)
    assert image_on.size == image_off.size
    assert image_on.tobytes() != image_off.tobytes()


def test_embedded_type1_changes_rendering():
    """Type 1 embedded fonts render through the FreeType outline path.

    Blend2D rejects raw /FontFile programs, so the renderer decomposes their
    glyph outlines with FreeType instead of substituting a system font. With
    resolve_fonts=False the alternative is the hardcoded fallback font, so
    using the embedded CM/Nimbus outlines must change the output on every
    platform — and must not crash on the TeX subset fonts.
    """
    embedded_on = RenderConfig()
    embedded_on.resolve_fonts = False
    embedded_on.use_embedded_fonts = True

    embedded_off = RenderConfig()
    embedded_off.resolve_fonts = False
    embedded_off.use_embedded_fonts = False

    image_on = _render_page(TYPE1_ONLY_PAGE, embedded_on)
    image_off = _render_page(TYPE1_ONLY_PAGE, embedded_off)

    assert not _is_blank(image_on)
    assert not _is_blank(image_off)
    assert image_on.size == image_off.size
    assert image_on.tobytes() != image_off.tobytes()


def test_embedded_fonts_render_all_font_pages():
    """Smoke test: every embedded-font-bearing page renders successfully.

    Covers the .notdef fallback (subset fonts missing glyphs must not produce
    blank cells or crashes) across Type 1, TrueType and mixed pages.
    """
    render_config = RenderConfig()

    parser = _make_parser(render_config)
    pages = [1, 2, 8, 9, 17, 21]
    parser.load(ARXIV_PDF, page_numbers=pages)

    rendered: dict[int, PILImage.Image] = {}
    for result in parser.iterate_results():
        assert result.success, (
            f"Render failed page {result.page_number}: {result.error_message}"
        )
        rendered[result.page_number] = result.get_image()

    assert sorted(rendered) == pages
    for page_number, image in rendered.items():
        assert not _is_blank(image), f"page {page_number} rendered blank"


def _export_text_instructions(page_number: int) -> list:
    parser = _make_parser(RenderConfig())
    parser.load(ARXIV_PDF, page_numbers=[page_number])

    result = next(parser.iterate_results())
    assert result.success, result.error_message

    exported = result._export_render_instructions_json()
    return [row for row in exported["instructions"] if row["type"] == "text"]


def test_extraction_attaches_embedded_font_metadata():
    """Text instructions carry embedded font metadata (never the bytes)."""
    rows = _export_text_instructions(TYPE1_ONLY_PAGE)
    assert len(rows) > 0

    embedded_rows = [row for row in rows if row["has_embedded_font"]]
    assert len(embedded_rows) > 0, "no instruction carries an embedded font"

    for row in embedded_rows:
        embedded = row["embedded_font"]
        assert embedded["source_key"] in (
            "/FontDescriptor/FontFile",
            "/FontDescriptor/FontFile2",
            "/FontDescriptor/FontFile3",
            "/FontFile",
            "/FontFile2",
            "/FontFile3",
        )
        assert embedded["byte_size"] > 0
        assert embedded["cache_key"] != ""
        assert "bytes" not in embedded

    # page 1 embeds only raw Type 1 programs
    formats = {row["embedded_font"]["format"] for row in embedded_rows}
    assert formats == {"TYPE1"}


def test_embedded_font_blob_is_shared_per_font():
    """All instructions of one PDF font reference the same blob.

    The content-hash cache key is the observable identity of the shared
    blob: one font resource must yield exactly one cache key.
    """
    rows = _export_text_instructions(TYPE1_ONLY_PAGE)

    keys_per_font: dict[str, set[str]] = {}
    for row in rows:
        if not row["has_embedded_font"]:
            continue
        keys_per_font.setdefault(row["font_key"], set()).add(
            row["embedded_font"]["cache_key"]
        )

    assert len(keys_per_font) > 0
    for font_key, cache_keys in keys_per_font.items():
        assert len(cache_keys) == 1, (
            f"font {font_key} produced multiple blobs: {cache_keys}"
        )


def test_char_code_is_exported_for_single_char_cells():
    """Single-character cells expose their decoded PDF character code."""
    rows = _export_text_instructions(TYPE1_ONLY_PAGE)

    coded = [row for row in rows if row["char_code"] >= 0]
    assert len(coded) > 0, "no instruction carries a character code"


def test_glyph_names_are_exported_for_differences_encodings():
    """Cells of fonts with /Encoding /Differences expose their glyph name.

    The page-1 NimbusRomNo9L fonts carry a /Differences array, so at least
    some single-character cells must expose the glyph name that identifies
    the glyph inside the embedded font program.
    """
    rows = _export_text_instructions(TYPE1_ONLY_PAGE)

    named = [row for row in rows if row["glyph_name"] != ""]
    assert len(named) > 0, "no instruction carries a glyph name"

    for row in named:
        assert row["char_code"] >= 0
        assert not row["glyph_name"].startswith("/")


def test_embedded_face_cache_is_shared_across_documents():
    """Two documents with identical fonts share one embedded-face cache.

    The cache inside one parser is shared by all workers and documents and is
    keyed by a content hash of the font bytes, so loading the same PDF twice
    makes the second document hit cached faces (and cached Type 1 failures)
    instead of re-loading — and both must render identically.
    """
    render_config = RenderConfig()

    parser = _make_parser(render_config)
    path_key = parser.load(ARXIV_PDF, page_numbers=[TRUETYPE_PAGE])
    with open(ARXIV_PDF, "rb") as f:
        bytes_key = parser.load(BytesIO(f.read()), page_numbers=[TRUETYPE_PAGE])

    images: dict[str, PILImage.Image] = {}
    for result in parser.iterate_results():
        assert result.success, result.error_message
        images[result.doc_key] = result.get_image()

    assert set(images) == {path_key, bytes_key}
    assert images[path_key].tobytes() == images[bytes_key].tobytes()
