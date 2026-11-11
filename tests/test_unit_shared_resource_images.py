#!/usr/bin/env python
"""Images listed in a /Resources dictionary that pages share (#424).

LibreOffice points every page at one /Resources dictionary that names every
image of the document. Each page decoder defiltered all of them up front, so
every page result held the pixels of images it never draws: a 100-page report
with a handful of Flate photos cost gigabytes as soon as a caller kept its
pages. Only an image a page actually paints with `Do` may cost that page its
pixels.
"""

from __future__ import annotations

import zlib
from io import BytesIO
from pathlib import Path

import pytest

from tests.pdf_builder import build_pdf, content_stream, stream_object

STATUS = Path("/proc/self/status")

N_PAGES = 8
IMAGE_W, IMAGE_H = 4000, 3000
DECODED_MB = IMAGE_W * IMAGE_H * 3 / 2**20  # ~34 MB


def _rss_mb() -> float:
    for line in STATUS.read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1]) / 1024
    raise AssertionError("no VmRSS in /proc/self/status")


def _shared_resources_pdf() -> bytes:
    """Pages 1..N share one /Resources naming one large image; page 1 draws it."""
    pixels = bytes([200, 40, 40]) * (IMAGE_W * IMAGE_H)
    image = stream_object(
        f"/Type /XObject /Subtype /Image /Width {IMAGE_W} /Height {IMAGE_H} "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode",
        zlib.compress(pixels, 9),
    )
    text = "BT /F1 12 Tf 20 170 Td (shared resources) Tj ET"
    draw = "q 160 0 0 120 20 20 cm /Im0 Do Q"

    # 1 catalog, 2 pages, 3 font, 4 image, 5 resources, then page + content pairs
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        image,
        "<< /Font << /F1 3 0 R >> /XObject << /Im0 4 0 R >> >>",
    ]
    kids = []
    for page in range(N_PAGES):
        page_obj = len(objects) + 1
        kids.append(f"{page_obj} 0 R")
        objects.append(
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            f"/Resources 5 0 R /Contents {page_obj + 1} 0 R >>"
        )
        objects.append(content_stream(text + ("\n" + draw if page == 0 else "")))
    objects[1] = f"<< /Type /Pages /Kids [{' '.join(kids)}] /Count {N_PAGES} >>"

    return build_pdf(objects)


@pytest.mark.skipif(not STATUS.exists(), reason="reads VmRSS from /proc")
def test_pages_that_do_not_draw_a_shared_image_do_not_hold_its_pixels():
    from docling_parse.pdf_parser import (
        DecodeConfig,
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=N_PAGES,
            render_config=None,
        ),
        decode_config=DecodeConfig(keep_qpdf_warnings=False),
    )
    parser.load(BytesIO(_shared_resources_pdf()))

    before = _rss_mb()
    try:
        kept = list(parser.iterate_results())
        grown = _rss_mb() - before

        assert [r.success for r in kept] == [True] * N_PAGES
        drawn = {r.page_number: len(r.get_page().bitmap_resources) for r in kept}
        assert drawn == {1: 1, **dict.fromkeys(range(2, N_PAGES + 1), 0)}

        # Page 1 holds the image once (as decoded samples); the other pages
        # hold nothing of it. Decoding it on every page grows by N_PAGES times
        # that.
        assert grown < 3 * DECODED_MB, (
            f"keeping {N_PAGES} page results grew RSS by {grown:.0f} MB; "
            f"one decoded copy of the shared image is {DECODED_MB:.0f} MB"
        )
    finally:
        parser.unload_all()
