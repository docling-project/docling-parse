#!/usr/bin/env python
"""Paths that contain non-ASCII characters.

Two routes carry such paths into the native code, and on Windows both go wrong
in the ANSI codepage while POSIX passes the bytes through unchanged -- so these
tests guard intent everywhere but only bite on Windows runners.

The filename travels UTF-8 encoded from Python into the native parser, which
must open it with UTF-8 semantics on every platform. A narrow byte-path open
goes through the ANSI codepage instead, so any path with characters outside it
fails to load (docling-parse#324).

The renderer's font resolver takes its scan directories from the environment
(LOCALAPPDATA, DOCLING_PARSE_*_FALLBACK_FONT); a narrow getenv() there broke
every threaded render under a non-ASCII user profile (docling-parse#354).
"""

import os
import subprocess
import sys
import textwrap
from io import BytesIO
from pathlib import Path

import pytest

import docling_parse
from docling_parse.pdf_parser import DoclingPdfParser
from tests.pdf_builder import build_pdf

CONTENT = "BT /F1 12 Tf 72 720 Td (Title Case Workers) Tj ET"

# Latin-1, Hangul and Cyrillic together: every common ANSI codepage turns at
# least one of them into bytes that are not valid UTF-8. Hangul alone becomes
# '?' under cp1252, the codepage of GitHub's Windows runners, and would pass.
NON_ASCII_DIR_NAME = "docling_é_한글_Привет"

_RENDER_ONE_PAGE = textwrap.dedent(
    """
    import sys
    from io import BytesIO

    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        RenderConfig,
        ThreadedPdfParserConfig,
    )

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal", threads=1, render_config=RenderConfig()
        )
    )
    parser.load(BytesIO(sys.stdin.buffer.read()))
    for result in parser.iterate_results():
        assert result.success, result.error_message
        assert result.get_image().width > 0
    parser.unload_all()
    """
)


def _tiny_pdf() -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            f"<< /Length {len(CONTENT)} >>\nstream\n{CONTENT}\nendstream",
        ]
    )


def test_load_from_non_ascii_path(tmp_path):
    """A path with CJK directory and filename components loads like any other."""
    cjk_dir = tmp_path / "导出的条目"
    cjk_dir.mkdir()
    pdf_path = cjk_dir / "2005 - Chiou 等 - test.pdf"
    pdf_path.write_bytes(_tiny_pdf())

    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=str(pdf_path))

    assert doc.number_of_pages() == 1


def test_load_from_bytesio_is_path_independent():
    """The BytesIO route never touches the filesystem and must always work."""
    parser = DoclingPdfParser(loglevel="fatal")
    doc = parser.load(path_or_stream=BytesIO(_tiny_pdf()))

    assert doc.number_of_pages() == 1


def _render_in_fresh_process(
    cwd: Path, **env_overrides: str
) -> subprocess.CompletedProcess:
    package_root = Path(docling_parse.__file__).resolve().parent.parent
    env = {**os.environ, **env_overrides}
    env["PYTHONPATH"] = os.pathsep.join(
        filter(None, [str(package_root), env.get("PYTHONPATH")])
    )
    return subprocess.run(
        [sys.executable, "-c", _RENDER_ONE_PAGE],
        input=_tiny_pdf(),
        env=env,
        cwd=cwd,
        capture_output=True,
        timeout=300,
    )


@pytest.mark.parametrize("dir_name", ["ascii_profile", NON_ASCII_DIR_NAME])
def test_renderer_starts_under_non_ascii_font_directory_env(tmp_path, dir_name):
    """The user font directory may sit under a non-ASCII profile path."""
    profile_dir = tmp_path / dir_name
    (profile_dir / "Microsoft" / "Windows" / "Fonts").mkdir(parents=True)

    proc = _render_in_fresh_process(
        tmp_path,
        LOCALAPPDATA=str(profile_dir),
        XDG_DATA_HOME=str(profile_dir),
    )

    assert proc.returncode == 0, proc.stderr.decode("utf-8", "replace")


def test_renderer_starts_with_non_ascii_fallback_font_override(tmp_path):
    """An override that names a non-ASCII path is skipped, not fatal."""
    font_dir = tmp_path / NON_ASCII_DIR_NAME
    font_dir.mkdir()

    proc = _render_in_fresh_process(
        tmp_path, DOCLING_PARSE_FALLBACK_FONT=str(font_dir / "missing.ttf")
    )

    assert proc.returncode == 0, proc.stderr.decode("utf-8", "replace")
