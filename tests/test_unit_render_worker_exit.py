"""Render workers must exit without touching freed thread-local storage.

The Windows wheel is built with MinGW, whose emulated TLS frees a thread's
storage before the C++ thread_local destructors run. The renderer kept its
spare page canvas (a BLImage) and the CMYK conversion memo (two std::vectors)
in thread_local objects, so every render worker that exited destroyed them on
freed memory. Windows swallows the fault inside the loader's thread-detach
callback, prints "Windows fatal exception: access violation" through
faulthandler, and lets the process continue with a corrupted heap; later
objects are garbage or the process segfaults (docling-parse#357).

The workers exit whenever a document's page queue is empty, so loading a
small rendered document repeatedly exits many workers. The page is filled in
DeviceCMYK so that workers also hold the CMYK memo: on an unfixed build 120
loads then gave 5 to 8 faults per run (5 of 5 runs), against 0 to 5 (4 of 5)
with an RGB fill. The test runs the loop in a subprocess so that a corrupted
heap cannot take pytest down with it.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

LOADS = 120

WORKER = f"""
import faulthandler
faulthandler.enable()

from io import BytesIO

from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream

pages = 3
kids = " ".join(f"{{3 + i}} 0 R" for i in range(pages))
objects = [
    "<< /Type /Catalog /Pages 2 0 R >>",
    f"<< /Type /Pages /Kids [{{kids}}] /Count {{pages}} >>",
]
objects += [
    f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents {{3 + pages}} 0 R >>"
    for _ in range(pages)
]
objects.append(content_stream("0.6 0.2 0 0.1 k 100 100 200 200 re f\\n"))
pdf = build_pdf(objects)

for _ in range({LOADS}):
    parser = DoclingThreadedPdfParser(
        ThreadedPdfParserConfig(loglevel="fatal", threads=4, render_config=RenderConfig()),
        DecodeConfig(),
    )
    key = parser.load(BytesIO(pdf))
    for result in parser.iterate_results():
        assert result.success, result.error_message
    parser.unload(key)
print("done")
"""


@pytest.mark.skipif(
    sys.platform != "win32",
    reason="the fault needs MinGW's emulated TLS, which only the Windows build uses",
)
def test_render_workers_exit_without_access_violation() -> None:
    result = subprocess.run(
        [sys.executable, "-c", WORKER],
        capture_output=True,
        timeout=600,
        cwd=Path(__file__).resolve().parents[1],
    )
    stderr = result.stderr.decode(errors="replace")
    faults = stderr.count("access violation")
    assert result.returncode == 0, (
        f"worker process died (exit {result.returncode}):\n{stderr[-2000:]}"
    )
    assert faults == 0, (
        f"{faults} access violation(s) while render workers exited:\n{stderr[-2000:]}"
    )
    assert result.stdout.decode().strip().endswith("done")
