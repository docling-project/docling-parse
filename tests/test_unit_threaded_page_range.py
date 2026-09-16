#!/usr/bin/env python
"""Threaded page ranges are normalized against the already-open document."""

import sys
from io import BytesIO
from pathlib import Path

import pytest

from docling_parse.pdf_parser import (
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)
from tests.pdf_builder import build_pdf, content_stream


def _multi_page_pdf(number_of_pages: int) -> bytes:
    first_page = 3
    first_content = first_page + number_of_pages
    kids = " ".join(f"{first_page + i} 0 R" for i in range(number_of_pages))
    objects: list[str | bytes] = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        f"<< /Type /Pages /Kids [{kids}] /Count {number_of_pages} >>",
    ]
    objects.extend(
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        f"/Contents {first_content + i} 0 R >>"
        for i in range(number_of_pages)
    )
    objects.extend(
        content_stream(f"0 0 1 rg 10 {10 + 10 * i} 50 20 re f")
        for i in range(number_of_pages)
    )
    return build_pdf(objects)


def _source(tmp_path: Path, source_kind: str) -> Path | BytesIO:
    payload = _multi_page_pdf(5)
    if source_kind == "bytesio":
        return BytesIO(payload)
    path = tmp_path / "five-pages.pdf"
    path.write_bytes(payload)
    return path


def _parser(*, render: bool = False) -> DoclingThreadedPdfParser:
    render_config = RenderConfig() if render else None
    return DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=2,
            max_concurrent_results=4,
            render_config=render_config,
        )
    )


@pytest.mark.parametrize("source_kind", ["path", "bytesio"])
def test_page_range_is_inclusive_for_file_and_bytesio(
    tmp_path: Path, source_kind: str
) -> None:
    parser = _parser()
    key = parser.load(_source(tmp_path, source_kind), page_range=(2, 4))

    assert parser.page_count(key) == 5
    assert parser.scheduled_page_count(key) == 3
    assert sorted(result.page_number for result in parser.iterate_results()) == [
        2,
        3,
        4,
    ]

    parser.unload(key)


def test_page_range_end_is_clipped_without_python_page_count(tmp_path: Path) -> None:
    parser = _parser()
    key = parser.load(_source(tmp_path, "path"), page_range=(3, sys.maxsize))

    assert parser.page_count(key) == 5
    assert parser.scheduled_page_count(key) == 3
    assert sorted(result.page_number for result in parser.iterate_results()) == [
        3,
        4,
        5,
    ]

    parser.unload(key)


def test_page_range_start_beyond_document_schedules_nothing(tmp_path: Path) -> None:
    parser = _parser()
    key = parser.load(_source(tmp_path, "path"), page_range=(6, sys.maxsize))

    assert parser.page_count(key) == 5
    assert parser.scheduled_page_count(key) == 0
    assert list(parser.iterate_results()) == []

    parser.unload(key)


@pytest.mark.parametrize("page_range", [(0, 2), (3, 2)])
def test_invalid_page_range_is_rejected(
    tmp_path: Path, page_range: tuple[int, int]
) -> None:
    parser = _parser()

    with pytest.raises(RuntimeError, match="Invalid page range"):
        parser.load(_source(tmp_path, "path"), page_range=page_range)


def test_page_numbers_and_page_range_are_mutually_exclusive(tmp_path: Path) -> None:
    parser = _parser()

    with pytest.raises(ValueError, match="mutually exclusive"):
        parser.load(
            _source(tmp_path, "path"),
            page_numbers=[1],
            page_range=(1, 2),
        )


def test_renderer_accepts_page_range(tmp_path: Path) -> None:
    parser = _parser(render=True)
    key = parser.load(_source(tmp_path, "path"), page_range=(2, 2))
    results = list(parser.iterate_results())

    assert parser.scheduled_page_count(key) == 1
    assert [result.page_number for result in results] == [2]
    assert results[0].get_image().size == (200, 200)

    parser.unload(key)
