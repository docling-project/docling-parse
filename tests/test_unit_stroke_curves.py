"""Curve strokes must terminate and preserve loops and collinear reversals."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from tests.pdf_builder import render_page, simple_page_pdf


@pytest.mark.parametrize("scale", [1, 2, 4])
@pytest.mark.parametrize("paint", ["S", "B"])
def test_near_cusp_stroke_terminates(scale: int, paint: str) -> None:
    # One segment isolated from page 2 of the Dabolava geochronology PDF.
    # Keep the coordinates and page frame: rounding them hides the hang.
    pdf = simple_page_pdf(
        "1 w\n"
        "78.456305982435026 190.41444574216234 m\n"
        "78.214011651738019 189.68817224622376 "
        "78.508369227047623 190.18936098102438 "
        "77.884611507939169 189.84423101394611 c\n"
        f"{paint}\n",
        media_box="[0 0 700 800]",
    )
    # Isolate the native renderer so a broken stroker fails with a timeout
    # instead of hanging the entire pytest process (including its cleanup).
    worker = """
import sys
from tests.pdf_builder import render_page
scale = int(sys.argv[1])
image = render_page(sys.stdin.buffer.read(), scale=scale).get_image()
assert image.size == (700 * scale, 800 * scale), image.size
region = image.crop(tuple(v * scale for v in (75, 607, 81, 613)))
assert region.convert('L').getextrema()[0] < 200, 'stroke was dropped'
"""
    result = subprocess.run(
        [sys.executable, "-c", worker, str(scale)],
        input=pdf,
        capture_output=True,
        timeout=15,
        cwd=Path(__file__).resolve().parents[1],
    )
    assert result.returncode == 0, result.stderr.decode(errors="replace")


@pytest.mark.parametrize(
    "curve, ink_points",
    [
        # All controls are collinear, but the curve reverses before its end.
        ("20 100 m 180 100 180 100 40 100 c", [(125, 100), (40, 100)]),
        # A closed cubic with equal endpoints must retain its visible loop.
        ("50 50 m 50 150 150 150 50 50 c", [(88, 125), (64, 106)]),
    ],
)
def test_flattened_stroke_preserves_excursions(curve, ink_points) -> None:
    image = render_page(simple_page_pdf(f"4 w\n{curve} S\n")).get_image()
    for x, y in ink_points:
        # Default rendering scale is 2, with a top-left image origin.
        pixel = image.convert("RGB").getpixel((2 * x, 2 * (200 - y)))
        assert max(pixel) < 100, (x, y, pixel)


def test_curved_stroke_leaves_its_interior_empty() -> None:
    circle = (
        "150 100 m\n"
        "150 127.61423749 127.61423749 150 100 150 c\n"
        "72.38576251 150 50 127.61423749 50 100 c\n"
        "50 72.38576251 72.38576251 50 100 50 c\n"
        "127.61423749 50 150 72.38576251 150 100 c h\n"
    )
    image = render_page(simple_page_pdf(f"4 w\n{circle}S\n")).get_image()
    rgb = image.convert("RGB")
    assert rgb.getpixel((200, 200)) == (255, 255, 255)
    for point in ((300, 200), (200, 100), (100, 200), (200, 300)):
        assert max(rgb.getpixel(point)) < 100
