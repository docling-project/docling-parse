#!/usr/bin/env python
"""A sheared image that runs off the right edge of a canvas whose width is
252..255 mod 256 must render, not crash the process.

docling-parse #365: page 7 of a scanned manual segfaulted in `get_image()` at
exactly scale 3.0. The page is one CCITT bitmap placed with a 0.17 degree skew,
so it is blitted through the affine path. At that scale the canvas is 1279 px
wide, and 1279 % 256 is 255, which puts the clip's x_end cell on bit 63 of the
last 64-bit coverage word in Blend2D's analytic rasterizer. The skewed top edge
crossing the right clip edge flags every cell of that word, and Blend2D's JIT
fill then reused a stale span length, ran a zero-length span for 2^32 pixels,
and walked the destination pointer off the heap (blend2d/blend2d#267, fixed by
blend2d/blend2d#268, carried here as `cmake/blend2d-fillanalytic-stale-i.patch`).

The fixture reproduces that configuration on a tiny page: 255.5 x 5 pt renders
to 511 x 10 px at scale 2, and a 16 x 16 image is placed so that its skewed top
edge crosses the right edge within the first scanline. The render runs in a
child process, so a regression shows up as a failed assertion naming the signal
instead of taking the whole test run down with it.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from tests.pdf_builder import simple_page_pdf, stream_object

REPO_ROOT = Path(__file__).resolve().parents[1]

SCALE = 2.0
PAGE_WIDTH_PT = 255.5  # ceil(255.5 * 2) = 511 = 2 * 256 - 1
PAGE_HEIGHT_PT = 5.0
EXPECTED_SIZE = (511, 10)

IMAGE_SIZE = 16

# The placement that crashed, expressed as the canvas-space affine map
# (pixels, y down) of a 2592 x 3312 blit rectangle: a scale of about 1.14 with
# a slight skew, starting 288 px left of the canvas and 2.4 px below its top.
# This is the geometry of the minimal reproducer attached to blend2d/blend2d#267.
BLIT_WIDTH, BLIT_HEIGHT = 2592, 3312
CANVAS_AFFINE = (
    1.1372183005498808,
    0.0012802436069027732,
    0.0044605010442717481,
    1.1372183005498808,
    -288.41887602788347,
    2.3954065681413779,
)


def image_matrix() -> str:
    """The `cm` operands that put the image's unit square on `CANVAS_AFFINE`.

    Canvas x is PDF x times the scale; canvas y is the canvas height minus PDF
    y times the scale. The image's top-left pixel is the unit square's (0, 1)
    corner, so the blit rectangle's v axis runs against the unit square's.
    """
    m00, m01, m10, m11, m20, m21 = CANVAS_AFFINE
    a = m00 * BLIT_WIDTH / SCALE
    b = -m01 * BLIT_WIDTH / SCALE
    c = -m10 * BLIT_HEIGHT / SCALE
    d = m11 * BLIT_HEIGHT / SCALE
    e = (m20 + m10 * BLIT_HEIGHT) / SCALE
    f = PAGE_HEIGHT_PT - (m21 + m11 * BLIT_HEIGHT) / SCALE
    return f"{a:.9f} {b:.9f} {c:.9f} {d:.9f} {e:.9f} {f:.9f} cm"


def skewed_image_pdf() -> bytes:
    payload = bytes(
        (x * 15) & 0xFF for _ in range(IMAGE_SIZE) for x in range(IMAGE_SIZE)
    )
    image = stream_object(
        f"/Type /XObject /Subtype /Image /Width {IMAGE_SIZE} /Height {IMAGE_SIZE} "
        "/ColorSpace /DeviceGray /BitsPerComponent 8",
        payload,
    )
    return simple_page_pdf(
        f"q {image_matrix()} /Im1 Do Q",
        resources="/XObject << /Im1 5 0 R >>",
        media_box=f"[0 0 {PAGE_WIDTH_PT} {PAGE_HEIGHT_PT}]",
        extra_objects=[image],
    )


RENDER_CHILD = """
import sys
from pathlib import Path

from tests.pdf_builder import render_page

result = render_page(Path(sys.argv[1]).read_bytes(), scale=float(sys.argv[2]))
width, height = result.get_image().size
print(f"{width} {height}")
"""


def test_skewed_image_past_right_clip_edge_renders(tmp_path: Path) -> None:
    pdf_path = tmp_path / "skewed_image.pdf"
    pdf_path.write_bytes(skewed_image_pdf())

    completed = subprocess.run(
        [sys.executable, "-c", RENDER_CHILD, str(pdf_path), str(SCALE)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=300,
    )

    assert completed.returncode == 0, (
        f"render exited with {completed.returncode} "
        f"(negative values are signals; -11 is SIGSEGV)\n{completed.stderr[-2000:]}"
    )
    width, height = (int(v) for v in completed.stdout.split())
    assert (width, height) == EXPECTED_SIZE
