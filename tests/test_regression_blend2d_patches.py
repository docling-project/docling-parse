"""Enforced real-document comparison for the affine-fill crash (#365/#387).

The original manual is stored with the PR evidence pending its later addition
to the regression dataset. DOCLING_PARSE_HONDA_MANUAL can override that path.
Source distributions without the evidence directory skip unless it is supplied.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

import pytest
from PIL import Image

from tests.rendering_regression import (
    flatten_on_white,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
)
from tests.test_regression_pypdfium_render import PYPDFIUM_IMAGE_TOLERANCE

ROOT = Path(__file__).resolve().parents[1]
HONDA_SHA256 = "cf9ddf8f0890ed28082d9c5dfbea383635755dbbc56d56c0c6c68d15e64c6141"


def test_honda_page_7_matches_pypdfium(tmp_path: Path) -> None:
    pytest.importorskip("pypdfium2")
    path = Path(
        os.getenv(
            "DOCLING_PARSE_HONDA_MANUAL",
            str(ROOT / ".plans/blend2d-evidence/pdfs/31ZH7600.pdf"),
        )
    ).resolve()
    if not path.is_file():
        pytest.skip("Honda source PDF unavailable; set DOCLING_PARSE_HONDA_MANUAL")
    assert hashlib.sha256(path.read_bytes()).hexdigest() == HONDA_SHA256
    output = tmp_path / "honda-page-7.png"
    child = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/compare_blend2d_patches.py"),
            "worker",
            "--pdf",
            str(path),
            "--page",
            "7",
            "--scale",
            "3",
            "--output",
            str(output),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert child.returncode == 0, (
        f"Honda page 7 render failed ({child.returncode}): {child.stderr[-2000:]}"
    )
    reference = render_pypdfium_page(path, 7, scale=3.0)
    with Image.open(output) as image:
        comparison = measure_image_pair(
            "31ZH7600.pdf",
            7,
            flatten_on_white(image),
            flatten_on_white(reference),
            tolerance=PYPDFIUM_IMAGE_TOLERANCE,
        )
    assert not image_comparison_failed(
        comparison, tolerance=PYPDFIUM_IMAGE_TOLERANCE
    ), comparison
