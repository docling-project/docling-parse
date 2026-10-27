#!/usr/bin/env python
"""Images listed in a /Resources dictionary that pages share (#424).

LibreOffice points every page at one /Resources dictionary that names every
image of the document. Each page decoder defiltered all of them up front, so
every page result held the pixels of images it never draws: a 100-page report
with a handful of Flate photos cost gigabytes as soon as a caller kept its
pages. Only an image a page actually paints with `Do` may cost that page its
pixels.

The threaded parser decodes each page from a one-page copy of the document,
and the page result keeps that copy. The copy took along every stream the
shared dictionary names, so a page also held the compressed bytes of images
it never draws. That costs little for a flat-colour image and as much as the
pixels for a photograph.
"""

from __future__ import annotations

import random
import time
import zlib
from io import BytesIO
from pathlib import Path

import pytest

from tests.pdf_builder import build_pdf, content_stream, render_page, stream_object
from tests.rendering_regression import average_color, coverage_ratio, region_image

STATUS = Path("/proc/self/status")

N_PAGES = 8
IMAGE_W, IMAGE_H = 4000, 3000
DECODED_MB = IMAGE_W * IMAGE_H * 3 / 2**20  # ~34 MB

NOISE_W, NOISE_H = 2000, 2000
NOISE_MB = NOISE_W * NOISE_H * 3 / 2**20  # ~11 MB, and Flate cannot shrink it


def _rss_mb() -> float:
    for line in STATUS.read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1]) / 1024
    raise AssertionError("no VmRSS in /proc/self/status")


def _flate_rgb_image(width: int, height: int, pixels: bytes, level: int) -> bytes:
    return stream_object(
        f"/Type /XObject /Subtype /Image /Width {width} /Height {height} "
        "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode",
        zlib.compress(pixels, level),
    )


def _shared_resources_pdf(image: bytes, *, drawn: bool = True) -> bytes:
    """Pages 1..N share one /Resources naming one image; page 1 draws it if `drawn`."""
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
        objects.append(
            content_stream(text + ("\n" + draw if drawn and page == 0 else ""))
        )
    objects[1] = f"<< /Type /Pages /Kids [{' '.join(kids)}] /Count {N_PAGES} >>"

    return build_pdf(objects)


def _keep_all_pages(
    pdf: bytes, *, n_pages: int = N_PAGES
) -> tuple[dict[int, int], float]:
    """Parse every page and keep the results.

    Returns the number of bitmaps on each page and the RSS growth in MB while
    the results are alive.
    """
    from docling_parse.pdf_parser import (
        DecodeConfig,
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=1,
            max_concurrent_results=n_pages,
            render_config=None,
        ),
        decode_config=DecodeConfig(keep_qpdf_warnings=False),
    )
    parser.load(BytesIO(pdf))

    before = _rss_mb()
    try:
        kept = list(parser.iterate_results())
        grown = _rss_mb() - before

        assert [r.success for r in kept] == [True] * n_pages
        drawn = {r.page_number: len(r.get_page().bitmap_resources) for r in kept}
        return drawn, grown
    finally:
        parser.unload_all()


@pytest.mark.skipif(not STATUS.exists(), reason="reads VmRSS from /proc")
def test_pages_that_do_not_draw_a_shared_image_do_not_hold_its_pixels():
    pixels = bytes([200, 40, 40]) * (IMAGE_W * IMAGE_H)
    pdf = _shared_resources_pdf(_flate_rgb_image(IMAGE_W, IMAGE_H, pixels, 9))

    drawn, grown = _keep_all_pages(pdf)

    assert drawn == {1: 1, **dict.fromkeys(range(2, N_PAGES + 1), 0)}

    # Page 1 holds the image once (as decoded samples); the other pages
    # hold nothing of it. Decoding it on every page grows by N_PAGES times
    # that.
    assert grown < 3 * DECODED_MB, (
        f"keeping {N_PAGES} page results grew RSS by {grown:.0f} MB; "
        f"one decoded copy of the shared image is {DECODED_MB:.0f} MB"
    )


@pytest.mark.skipif(not STATUS.exists(), reason="reads VmRSS from /proc")
def test_pages_that_do_not_draw_a_shared_image_do_not_hold_its_stream():
    pixels = random.Random(424).randbytes(NOISE_W * NOISE_H * 3)
    image = _flate_rgb_image(NOISE_W, NOISE_H, pixels, 1)
    pdf = _shared_resources_pdf(image, drawn=False)

    drawn, grown = _keep_all_pages(pdf)

    assert drawn == dict.fromkeys(range(1, N_PAGES + 1), 0)

    # No page draws the image, so no page result needs any of it. Copying
    # the stream into every page grows by N_PAGES times its size.
    assert grown < 2 * NOISE_MB, (
        f"keeping {N_PAGES} page results grew RSS by {grown:.0f} MB; "
        f"the shared image stream is {NOISE_MB:.0f} MB"
    )


def test_type3_glyph_keeps_an_image_it_takes_from_the_page_resources():
    """A Type3 font without /Resources draws from the page's (9.6.5).

    The page content names only the font, never the image, but the image is
    still in use and must stay in the page's copy of the document.
    """
    red = bytes([0xFF, 0x00, 0x00])
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 8 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources 9 0 R /Contents 4 0 R >>",
            content_stream("BT\n/T3 100 Tf\n50 50 Td\n(a) Tj\nET\n"),
            "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
            "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs 6 0 R "
            "/Encoding << /Type /Encoding /Differences [97 /square] >> "
            "/FirstChar 97 /LastChar 97 /Widths [1000] >>",
            "<< /square 7 0 R >>",
            stream_object("", b"1000 0 d0\nq 1000 0 0 1000 0 0 cm /Im Do Q\n"),
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources 9 0 R >>",
            "<< /Font << /T3 5 0 R >> /XObject << /Im 10 0 R >> >>",
            stream_object(
                "/Type /XObject /Subtype /Image /Width 2 /Height 2 "
                "/ColorSpace /DeviceRGB /BitsPerComponent 8",
                red * 4,
            ),
        ]
    )

    image = region_image(render_page(pdf), (50.0, 50.0, 150.0, 150.0))

    assert coverage_ratio(image) > 0.8, coverage_ratio(image)
    red_, green, blue = average_color(image)
    assert red_ > 200 and green < 60 and blue < 60, (red_, green, blue)


def test_forms_that_share_one_xobject_dictionary_are_visited_once():
    """The forms of one figure may share an /XObject dictionary naming all.

    A walk that visits each form once per entry naming it (as qpdf's
    forEachFormXObject does) takes N * N steps, and parsing the content of
    each form on every step took a 23-page arXiv paper with 2301 such forms
    from half a minute to over an hour.
    """
    n_forms = 1000
    # 1 catalog, 2 pages, 3-4 pages, 5 resources, 6 content, 7 /XObject, forms
    names = " ".join(f"/Fm{i} {8 + i} 0 R" for i in range(n_forms))
    form = stream_object(
        "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject 7 0 R >>",
        b"0 0 1 1 re f",
    )
    pdf = build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            "/Resources 5 0 R /Contents 6 0 R >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources 5 0 R >>",
            "<< /XObject 7 0 R >>",
            content_stream("q 100 0 0 100 50 50 cm /Fm0 Do Q"),
            f"<< {names} >>",
            *([form] * n_forms),
        ]
    )

    start = time.monotonic()
    drawn, _ = _keep_all_pages(pdf, n_pages=2)
    elapsed = time.monotonic() - start

    assert drawn == {1: 0, 2: 0}
    assert elapsed < 10, f"parsing 2 pages took {elapsed:.1f} s"
