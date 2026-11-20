"""Scoring, text files and report ordering for the extraction benchmark."""

import re
from contextlib import contextmanager
from urllib.parse import unquote, urlsplit

import pytest

pytest.importorskip("matplotlib")

from scripts.benchmarking import run_text_extraction_quality as quality


@pytest.mark.parametrize(
    ("docling", "pypdf", "expected"),
    [
        ("  Café\talpha\n", "alpha Cafe\u0301 alpha", 1.0),
        ("alpha beta", "alpha beta gamma", 0.8),
        ("alpha", "beta", 0.0),
        ("", " \n", 1.0),
        ("", "alpha", 0.0),
        ("text\udb62", "text\ufffd", 1.0),
    ],
)
def test_text_f1(docling, pypdf, expected):
    assert quality.text_f1(docling, pypdf) == expected
    assert quality.text_f1(pypdf, docling) == expected


def test_report_links_raw_files_and_sorts_scores(tmp_path, monkeypatch):
    pdf = tmp_path / "fixture (sample #1).pdf"
    pdf.touch()
    opened = []

    @contextmanager
    def open_editor(path):
        opened.append(("docling", path))

        def extract(page, mode):
            if page == 1 and mode is quality.PdfExtractionMode.LAYOUT:
                return "  alpha   beta\r\ntext\udb62\n"
            return ("one two", "same", "only docling")[page - 1]

        yield quality.OpenDocument(3, extract)

    @contextmanager
    def open_pypdf(path):
        opened.append(("pypdf", path))

        def extract(page, mode):
            if page == 3:
                raise ValueError("failed to decode")
            return ("one three", "same")[page - 1]

        yield quality.OpenDocument(3, extract)

    monkeypatch.setattr(
        quality, "BACKENDS", {"docling": open_editor, "pypdf": open_pypdf}
    )
    output = tmp_path / "report"
    report = quality.run(str(pdf), False, False, output)
    assert opened == [("docling", pdf), ("pypdf", pdf)]
    layout, plain = report.read_text(encoding="utf-8").split("## Plain")
    assert "## Layout" in layout
    for section, scores in (
        (layout, ["1.0000", "0.0000", "N/A"]),
        (plain, ["1.0000", "0.5000", "N/A"]),
    ):
        assert "| F1 ↓ | docling-parse | pypdf | File | Page | PDF |" in section
        rows = [line for line in section.splitlines() if "[docling.txt]" in line]
        assert [line.split("|")[1].strip() for line in rows] == scores
        for row, page in zip(rows, (2, 1, 3)):
            assert row.split("|")[4:6] == [f" {pdf.name} ", f" {page} "]
            docling, pypdf, source = re.findall(r"\]\(([^)]+)\)", row)
            assert (output / unquote(docling)).is_file()
            assert (output / unquote(pypdf)).is_file()
            pdf_url = urlsplit(source)
            assert (output / unquote(pdf_url.path)).resolve() == pdf
            assert pdf_url.fragment == f"page={page}"
    for mode in ("layout", "plain"):
        assert (output / f"{mode}-f1.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        error = output / mode / pdf.name / "page-0003" / "pypdf-error.txt"
        assert error.read_text(encoding="utf-8") == "ValueError: failed to decode"
    assert (
        output / "layout" / pdf.name / "page-0001" / "docling.txt"
    ).read_bytes() == b"  alpha   beta\r\ntext\\udb62\n"

    histograms = tmp_path / "histograms"
    histograms.mkdir()
    (histograms / "histogram-f1-layout.pdf").write_bytes(b"old chart")
    assert (
        quality.run(str(pdf), False, False, histograms, histograms_only=True)
        == histograms
    )
    assert {item.name for item in histograms.iterdir()} == {
        "histogram-f1-layout.pdf",
        "histogram-f1-plain.pdf",
    }
    for chart in histograms.iterdir():
        assert chart.read_bytes().startswith(b"%PDF-")
