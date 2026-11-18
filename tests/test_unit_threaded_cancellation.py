"""Document-key cancellation through the threaded Python API."""

import subprocess
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


@pytest.fixture
def pdf_bytes() -> bytes:
    return build_pdf(
        [
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [4 0 R 5 0 R 6 0 R 7 0 R 8 0 R] /Count 5 >>",
            content_stream("0 0 1 rg 10 10 50 20 re f"),
            *[
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 3 0 R >>"
                for _ in range(5)
            ],
        ]
    )


@pytest.fixture(params=["parse", "render"])
def parser(request: pytest.FixtureRequest) -> DoclingThreadedPdfParser:
    return DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            threads=1,
            max_concurrent_results=1,
            render_config=RenderConfig() if request.param == "render" else None,
        )
    )


def test_cancel_before_start_and_reload(
    parser: DoclingThreadedPdfParser, pdf_bytes: bytes
) -> None:
    key = parser.load(BytesIO(pdf_bytes), page_numbers=[4, 2, 2])
    assert parser.cancel("unknown") == 0
    assert parser.cancel(key) == 2
    assert parser.cancel(key) == 0
    assert parser.page_count(key) == 5
    assert parser.scheduled_page_count(key) == 2
    assert list(parser.iterate_results()) == []
    assert parser.unload(key)

    assert parser.load(BytesIO(pdf_bytes), page_range=(2, 3)) == key
    assert parser.cancel(key) == 2
    # Loading the same key again before startup replaces the cancelled schedule.
    assert parser.load(BytesIO(pdf_bytes), page_numbers=[5]) == key
    assert [result.page_number for result in parser.iterate_results()] == [5]
    assert parser.cancel(key) == 0
    assert parser.unload(key)


def test_cancel_partway_through_batch(
    parser: DoclingThreadedPdfParser, pdf_bytes: bytes, tmp_path: Path
) -> None:
    path = tmp_path / "document.pdf"
    path.write_bytes(pdf_bytes)
    keys = {parser.load(path), parser.load(BytesIO(pdf_bytes))}
    results = parser.iterate_results()
    first = next(results)
    assert first.success
    assert first.page_number == 1
    # Four pages of this key remain. The worker may already have claimed one.
    assert parser.cancel(first.doc_key) in {3, 4}
    assert parser.cancel(first.doc_key) == 0

    other_key = (keys - {first.doc_key}).pop()
    assert [(result.doc_key, result.page_number) for result in results] == [
        (other_key, page) for page in range(1, 6)
    ]
    assert parser.cancel(other_key) == 0
    parser.unload_all()


def test_cancel_between_has_tasks_and_get_task(
    parser: DoclingThreadedPdfParser,
    pdf_bytes: bytes,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    key = parser.load(BytesIO(pdf_bytes))
    has_tasks = parser.has_tasks

    def cancel_after_check() -> bool:
        pending = has_tasks()
        parser.cancel(key)
        return pending

    monkeypatch.setattr(parser, "has_tasks", cancel_after_check)
    assert list(parser.iterate_results()) == []
    assert parser.unload(key)


def test_scheduled_work_status(
    parser: DoclingThreadedPdfParser, pdf_bytes: bytes
) -> None:
    key = parser.load(BytesIO(pdf_bytes))
    assert parser.get_number_of_active_workers() == 0
    assert parser.get_number_of_scheduled_tasks() == 0
    assert parser.all_scheduled_work_completed()

    assert parser.has_tasks()
    assert parser.get_number_of_active_workers() == 1
    assert parser.get_number_of_scheduled_tasks() == 5
    assert not parser.all_scheduled_work_completed()

    parser.cancel(key)
    assert list(parser.iterate_results()) == []
    parser.unload_all()
    assert parser.get_number_of_active_workers() == 0
    assert parser.get_number_of_scheduled_tasks() == 0
    assert parser.all_scheduled_work_completed()


@pytest.mark.parametrize("mode", ["parse", "render"])
def test_destroy_after_cancelling_one_document(pdf_bytes: bytes, mode: str) -> None:
    # Use a child process so a shutdown regression fails with a bounded wait.
    code = """
import sys
from io import BytesIO
from docling_parse.pdf_parser import (
    DoclingThreadedPdfParser, RenderConfig, ThreadedPdfParserConfig,
)

payload = sys.stdin.buffer.read()
parser = DoclingThreadedPdfParser(ThreadedPdfParserConfig(
    threads=1,
    max_concurrent_results=1,
    render_config=RenderConfig() if sys.argv[1] == "render" else None,
))
parser.load(BytesIO(payload))
parser.load(BytesIO(payload + b"\\n% second document\\n"))
assert parser.has_tasks()
first = parser.get_task()
assert first.success
assert parser.cancel(first.doc_key) > 0
assert not parser.all_scheduled_work_completed()
del parser
"""
    subprocess.run(
        [sys.executable, "-c", code, mode], input=pdf_bytes, check=True, timeout=15
    )
