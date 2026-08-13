#!/usr/bin/env python
"""Assembly of small PDFs in memory, for tests that need one exact construct.

The focused tests build the document they are about: a page carrying a single
/Separation fill, one tiling pattern, one Type3 glyph. Written out as a file
that would be a binary fixture nobody can review; written out as objects it is
the test's own input, and a reader can see what the renderer was handed.

`test_actual_text.py` and `test_standard_font_widths.py` each grew their own
`_build_pdf`; both are text-only, and half of what the render fixes touch is
image data. This builder takes `bytes` for stream payloads, so a fixture can
carry a real JPEG or a packed sub-byte raster.
"""

from __future__ import annotations

Object = str | bytes


def _as_bytes(obj: Object) -> bytes:
    return obj if isinstance(obj, bytes) else obj.encode("latin-1")


def build_pdf(objects: list[Object]) -> bytes:
    """Serialise `objects` as PDF objects 1..N with a valid xref table.

    Object *i* in the list is object number *i+1*, so a reference written into
    another object reads `<n> 0 R` with n counted from one. The catalog is
    expected first, as in every fixture here.
    """
    out = b"%PDF-1.7\n"
    offsets: list[int] = []

    for index, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{index} 0 obj\n".encode("latin-1") + _as_bytes(obj) + b"\nendobj\n"

    xref_offset = len(out)
    count = len(objects) + 1
    out += f"xref\n0 {count}\n".encode("latin-1")
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode("latin-1")
    out += (
        f"trailer\n<< /Size {count} /Root 1 0 R >>\nstartxref\n{xref_offset}\n".encode(
            "latin-1"
        )
    )
    out += b"%%EOF\n"
    return out


def stream_object(dictionary: str, payload: bytes) -> bytes:
    """A stream object whose /Length matches `payload` exactly.

    `dictionary` is the stream dictionary without the enclosing `<<`/`>>`, and
    without /Length: that is filled in here, since getting it wrong is the
    classic way to build a fixture that only some parsers accept.
    """
    head = f"<< {dictionary} /Length {len(payload)} >>\nstream\n".encode("latin-1")
    return head + payload + b"\nendstream"


def content_stream(content: str) -> bytes:
    return stream_object("", content.encode("latin-1"))


def simple_page_objects(
    content: str,
    *,
    resources: str = "",
    media_box: str = "[0 0 200 200]",
    extra_objects: list[Object] | None = None,
) -> list[Object]:
    """Catalog, page tree, one page and its content stream.

    The page's /Resources is given as raw dictionary text so a fixture can name
    the object numbers of whatever it appended through `extra_objects`, which
    start at 5.
    """
    resources_entry = f" /Resources << {resources} >>" if resources else ""
    objects: list[Object] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox {media_box}"
        f"{resources_entry} /Contents 4 0 R >>",
        content_stream(content),
    ]
    objects.extend(extra_objects or [])
    return objects


def simple_page_pdf(
    content: str,
    *,
    resources: str = "",
    media_box: str = "[0 0 200 200]",
    extra_objects: list[Object] | None = None,
) -> bytes:
    return build_pdf(
        simple_page_objects(
            content,
            resources=resources,
            media_box=media_box,
            extra_objects=extra_objects,
        )
    )


def postscript_function(body: str, *, domain: str, range_: str) -> bytes:
    """A Type 4 (PostScript calculator) function object."""
    return stream_object(
        f"/FunctionType 4 /Domain {domain} /Range {range_}",
        body.encode("latin-1"),
    )


def exponential_function(c0: str, c1: str, *, domain: str = "[0 1]") -> str:
    """A Type 2 (exponential interpolation) function object."""
    return f"<< /FunctionType 2 /Domain {domain} /C0 {c0} /C1 {c1} /N 1 >>"


def render_page(pdf_bytes: bytes, *, scale: float = 2.0, page_no: int = 1):
    """Parse and render one page of an in-memory PDF.

    Returns the threaded parser's result object, so a caller can reach both the
    rendered image and the parsed page. The parser is unloaded before returning;
    the result keeps what the tests need.
    """
    from io import BytesIO

    from docling_parse.pdf_parser import (
        DecodeConfig,
        DoclingThreadedPdfParser,
        RenderConfig,
        ThreadedPdfParserConfig,
    )

    render_config = RenderConfig()
    render_config.scale = scale
    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=4,
            render_config=render_config,
        ),
        decode_config=DecodeConfig(
            do_sanitization=True,
            keep_glyphs=True,
            keep_qpdf_warnings=False,
        ),
    )
    key = parser.load(BytesIO(pdf_bytes), page_numbers=[page_no])
    try:
        for result in parser.iterate_results():
            if result.doc_key == key and result.page_number == page_no:
                assert result.success, result.error_message
                # touch the image while the page is still loaded
                result.get_image()
                return result
    finally:
        parser.unload_all()

    raise AssertionError(f"page {page_no} produced no render result")


def parse_page(pdf_bytes: bytes, *, page_no: int = 1):
    """Parse one page of an in-memory PDF without rendering it."""
    from io import BytesIO

    from docling_parse.pdf_parser import DecodeConfig, DoclingPdfParser

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(
        BytesIO(pdf_bytes),
        decode_config=DecodeConfig(
            do_sanitization=True,
            keep_glyphs=True,
            keep_qpdf_warnings=False,
        ),
    )
    return doc.get_page(page_no)
