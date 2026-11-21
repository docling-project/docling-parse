#!/usr/bin/env python
"""Measure editing workloads on generated PDFs in independent processes."""

from __future__ import annotations

import argparse
import json
import os
import resource
import statistics
import subprocess
import sys
import tempfile
import time
from contextlib import nullcontext
from pathlib import Path

from _performance_memory import peak_bytes

from docling_parse import PdfEditor, pdf_parsers

WORKLOADS = (
    "delete",
    "append",
    "append_outline",
    "form",
    "rotate_extract",
    "metadata",
    "empty_append",
)


def make_fixture(path: Path, pages: int) -> int:
    with PdfEditor() as editor:
        for page in range(1, pages + 1):
            editor.add_blank_page(width=300, height=400)
            editor.draw_text(page_no=page, text=f"Page {page}", x=20, y=350)
            editor.add_outline_item(title=f"Page {page}", page_no=page)
            editor.add_text_field(
                page_no=page,
                name=f"field-{page}",
                rect=(20, 30, 150, 50),
                value=str(page),
            )
            editor.add_link(page_no=page, rect=(20, 65, 80, 80), target_page_no=1)
        editor.write(target=path)
    with PdfEditor(source=path) as fixture:
        return int(fixture.trailer["/Size"]) - 1


def run_workload(
    kind: str, path: Path, pages: int, iterations: int
) -> dict[str, float]:
    start = time.perf_counter()
    with PdfEditor(source=path) as source:
        with (
            PdfEditor() if kind.startswith("append") else nullcontext(source) as editor
        ):
            opened = time.perf_counter()
            if kind == "delete":
                for _ in range(min(iterations, pages - 1)):
                    editor.remove_page(page_no=1)
            elif kind in {"append", "append_outline"}:
                editor.append(source=source, import_outline=kind == "append_outline")
            elif kind == "form":
                for _ in range(iterations):
                    editor.has_form()
                    editor.has_xfa()
                    editor.get_pages_showing_field(name=f"field-{pages}")
            elif kind == "rotate_extract":
                for _ in range(iterations):
                    editor.rotate_page(page_no=1, angle=90)
                    assert editor.extract_text(page_no=1)
            elif kind == "metadata":
                editor.encrypt(user_password="benchmark")
                for iteration in range(iterations):
                    editor.add_metadata(infos={"Title": str(iteration)})
                    assert editor.metadata["Title"] == str(iteration)
            elif kind == "empty_append":
                for _ in range(iterations):
                    editor.append(source=path, page_nos=[])
            edited = time.perf_counter()
            editor.write(target=path.with_name(f"output-{os.getpid()}.pdf"))
            written = time.perf_counter()
    return {
        "open_ms": (opened - start) * 1000,
        "operation_ms": (edited - opened) * 1000,
        "write_ms": (written - edited) * 1000,
        "peak_rss_mib": peak_bytes(
            resource.getrusage(resource.RUSAGE_SELF).ru_maxrss, sys.platform
        )
        / (1024 * 1024),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", nargs="+", type=int, default=[16, 128, 512])
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--worker", choices=WORKLOADS, help=argparse.SUPPRESS)
    parser.add_argument("--fixture", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.samples < 1 or args.iterations < 1 or min(args.sizes) < 2:
        parser.error(
            "samples and iterations must be positive, and sizes must be at least 2"
        )
    # Use the same logging and font initialization on both revisions.
    pdf_parsers.pdf_parser("fatal")
    if args.worker:
        if args.fixture is None:
            parser.error("a worker needs a fixture")
        print(
            json.dumps(
                run_workload(args.worker, args.fixture, args.sizes[0], args.iterations)
            )
        )
        return

    with tempfile.TemporaryDirectory() as directory:
        for pages in args.sizes:
            path = Path(directory) / f"{pages}-pages.pdf"
            objects = make_fixture(path, pages)
            for kind in WORKLOADS:
                rows = [
                    json.loads(
                        subprocess.check_output(
                            [
                                sys.executable,
                                __file__,
                                "--worker",
                                kind,
                                "--fixture",
                                str(path),
                                "--sizes",
                                str(pages),
                                "--iterations",
                                str(args.iterations),
                            ],
                            text=True,
                        )
                    )
                    for _ in range(args.samples)
                ]
                print(
                    json.dumps(
                        {
                            "pages": pages,
                            "objects": objects,
                            "widgets": pages,
                            "pdf_bytes": path.stat().st_size,
                            "workload": kind,
                            **{
                                key: statistics.median(row[key] for row in rows)
                                for key in rows[0]
                            },
                        }
                    ),
                    flush=True,
                )


if __name__ == "__main__":
    main()
