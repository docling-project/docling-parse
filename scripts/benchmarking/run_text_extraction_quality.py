#!/usr/bin/env python3
"""Report page-level text agreement between PdfEditor and pypdf."""

# ruff: noqa: E402

from __future__ import annotations

import argparse
import os
import re
import sys
import unicodedata
from collections.abc import Callable, Iterator, Mapping
from contextlib import AbstractContextManager, ExitStack, contextmanager
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT, Path(__file__).resolve().parent):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))

from _common import ProgressBar
from pypdf import PdfReader
from run_quality_benchmarking import resolve_quality_pdf_inputs, scheduled_pages

from docling_parse import PdfEditor, PdfExtractionMode, pdf_parsers
from tests.constants import (
    HF_DATASET_REPO_ID,
    HF_DATASET_REVISION,
    PARSER_PAGE_RESTRICTIONS,
    REGRESSION_DIR,
)

MODES = (PdfExtractionMode.LAYOUT, PdfExtractionMode.PLAIN)
SURROGATE = re.compile(r"[\ud800-\udfff]")


@dataclass(frozen=True)
class OpenDocument:
    page_count: int
    extract: Callable[[int, PdfExtractionMode], str]


@contextmanager
def open_editor(path: Path) -> Iterator[OpenDocument]:
    with PdfEditor(source=path) as editor:
        yield OpenDocument(
            editor.page_count(),
            lambda page, mode: editor.extract_text(page_no=page, extraction_mode=mode),
        )


@contextmanager
def open_pypdf(path: Path) -> Iterator[OpenDocument]:
    with PdfReader(path, strict=False) as reader:
        yield OpenDocument(
            len(reader.pages),
            lambda page, mode: reader.pages[page - 1].extract_text(
                extraction_mode=mode.value
            ),
        )


BACKENDS = {"docling": open_editor, "pypdf": open_pypdf}


@dataclass(frozen=True)
class Extraction:
    text: str | None = None
    error: str | None = None


def exception_message(error: Exception) -> str:
    return f"{type(error).__name__}: {error}"


def extract_pages(
    path: Path,
    pages: list[int] | None,
    backends: Mapping[
        str, Callable[[Path], AbstractContextManager[OpenDocument]]
    ] = BACKENDS,
) -> Iterator[tuple[int | None, PdfExtractionMode, dict[str, Extraction]]]:
    with ExitStack() as stack:
        opened = {}
        errors = {}
        for name, open_pdf in backends.items():
            try:
                opened[name] = stack.enter_context(open_pdf(path))
            except Exception as error:
                errors[name] = exception_message(error)

        max_pages = max((pdf.page_count for pdf in opened.values()), default=0)
        schedule = scheduled_pages([(path, pages)], known_page_counts={path: max_pages})
        page_numbers: list[int | None] = [page.page_number for page in schedule]
        for page in page_numbers or [None]:
            for mode in MODES:
                outputs = {}
                for name in backends:
                    if name in errors:
                        result = Extraction(error=errors[name])
                    elif page is None or page > opened[name].page_count:
                        result = Extraction(error="Page unavailable")
                    else:
                        try:
                            result = Extraction(text=opened[name].extract(page, mode))
                        except Exception as error:
                            result = Extraction(error=exception_message(error))
                    outputs[name] = result
                yield page, mode, outputs


def text_f1(docling: str, pypdf: str) -> float:
    """Compute F1 over unique whitespace-delimited tokens."""
    docling_tokens = set(
        unicodedata.normalize("NFC", SURROGATE.sub("\ufffd", docling)).split()
    )
    pypdf_tokens = set(
        unicodedata.normalize("NFC", SURROGATE.sub("\ufffd", pypdf)).split()
    )
    total = len(docling_tokens) + len(pypdf_tokens)
    return 2 * len(docling_tokens & pypdf_tokens) / total if total else 1.0


@dataclass(frozen=True)
class Row:
    score: float | None
    docling: str
    pypdf: str
    filename: str
    page: int | None
    source: Path


def write_text_artifact(folder: Path, name: str, extraction: Extraction) -> Path:
    if extraction.text is None:
        filename = f"{name}-error.txt"
        content = extraction.error or "No text was returned"
    else:
        filename = f"{name}.txt"
        content = extraction.text
    path = folder / filename
    # An invalid Unicode code unit is written visibly as an escape.
    path.write_bytes(content.encode("utf-8", errors="backslashreplace"))
    return path


def save_text(output: Path, folder: Path, name: str, extraction: Extraction) -> str:
    path = write_text_artifact(folder, name, extraction)
    return f"[{path.name}]({quote(path.relative_to(output).as_posix())})"


def link_pdf(output: Path, path: Path, page: int | None) -> str:
    try:
        target = quote(Path(os.path.relpath(path, start=output)).as_posix())
    except ValueError:
        # Windows paths on different drives cannot use a relative link.
        target = path.absolute().as_uri()
    if page is not None:
        target += f"#page={page}"
    return f"[Open PDF]({target})"


def write_histogram(
    path: Path,
    mode: PdfExtractionMode,
    scores: list[float],
    *,
    title: str | None = None,
) -> None:
    from matplotlib.backends.backend_agg import FigureCanvasAgg
    from matplotlib.figure import Figure

    figure = Figure(figsize=(7, 3.5), layout="constrained")
    FigureCanvasAgg(figure)
    axes = figure.subplots()
    axes.hist(
        scores,
        bins=[index / 10 for index in range(11)],
        color="#286f85",
        edgecolor="white",
    )
    axes.set(
        xlim=(0, 1),
        xlabel="F1",
        ylabel="Pages",
        title=title or f"{mode.value.title()} F1",
    )
    figure.savefig(path, dpi=150)


def write_report(output: Path, results: dict[PdfExtractionMode, list[Row]]) -> Path:
    report = ["# Text extraction agreement"]
    for mode in MODES:
        rows = results[mode]
        scores = [row.score for row in rows if row.score is not None]
        histogram = f"{mode.value}-f1.png"
        write_histogram(output / histogram, mode, scores)
        report.extend(
            [
                "",
                f"## {mode.value.title()}",
                "",
                f"![{mode.value.title()} F1 histogram]({histogram})",
                "",
                f"{len(scores)} scored pages · {len(rows) - len(scores)} unavailable",
                "",
                "| F1 ↓ | docling-parse | pypdf | File | Page | PDF |",
                "|---:|---|---|---|---:|---|",
            ]
        )
        for row in sorted(
            rows, key=lambda row: -1 if row.score is None else row.score, reverse=True
        ):
            score = "N/A" if row.score is None else f"{row.score:.4f}"
            filename = row.filename.replace("|", "&#124;")
            page = row.page if row.page is not None else "—"
            pdf = link_pdf(output, row.source, row.page)
            report.append(
                f"| {score} | {row.docling} | {row.pypdf} | {filename} | {page} | {pdf} |"
            )
    path = output / "report.md"
    path.write_text("\n".join(report) + "\n", encoding="utf-8")
    return path


def resolve_input(
    input_name: str | None, pinned: bool, recursive: bool
) -> tuple[list[Path], Path]:
    cached = ROOT / REGRESSION_DIR
    stamp = cached.parent / ".hf_revision"
    use_cache = (
        pinned
        and cached.is_dir()
        and stamp.is_file()
        and stamp.read_text(encoding="utf-8").strip() == HF_DATASET_REVISION
    )
    if use_cache:
        input_name = str(cached)
    elif pinned:
        input_name = HF_DATASET_REPO_ID
    assert input_name is not None
    paths, info = resolve_quality_pdf_inputs(input_name, recursive=recursive or pinned)
    if pinned and not use_cache and info["revision"] != HF_DATASET_REVISION:
        raise ValueError(f"Expected dataset revision {HF_DATASET_REVISION}")
    if not paths:
        raise ValueError(f"No PDFs found at {input_name!r}")
    # Preserve the lexical root for symlinked local input directories.
    root = Path(input_name if info["source"] == "local" else str(info["path"]))
    return paths, root.parent if root.is_file() else root


def run(
    input_name: str | None,
    pinned: bool,
    recursive: bool,
    output: Path,
    *,
    histograms_only: bool = False,
) -> Path:
    pinned = pinned or input_name == HF_DATASET_REPO_ID
    paths, root = resolve_input(input_name, pinned, recursive)
    output.mkdir(parents=True, exist_ok=histograms_only)
    pdf_parsers.pdf_parser("fatal")
    results: dict[PdfExtractionMode, list[Row]] = {mode: [] for mode in MODES}
    scores: dict[PdfExtractionMode, list[float]] = {mode: [] for mode in MODES}
    for path in ProgressBar(paths, desc="Comparing PDFs", unit="doc"):
        name = path.absolute().relative_to(root.absolute())
        schedule = PARSER_PAGE_RESTRICTIONS.get(name.as_posix()) if pinned else None
        for page, mode, outputs in extract_pages(path, schedule):
            docling = outputs["docling"]
            pypdf = outputs["pypdf"]
            score = None
            if docling.text is not None and pypdf.text is not None:
                score = text_f1(docling.text, pypdf.text)
            if histograms_only:
                if score is not None:
                    scores[mode].append(score)
                continue

            folder = (
                output
                / mode.value
                / name
                / (f"page-{page:04d}" if page else "document")
            )
            folder.mkdir(parents=True, exist_ok=True)
            results[mode].append(
                Row(
                    score,
                    save_text(output, folder, "docling", docling),
                    save_text(output, folder, "pypdf", pypdf),
                    name.as_posix(),
                    page,
                    path,
                )
            )
    if histograms_only:
        for mode, values in scores.items():
            write_histogram(output / f"histogram-f1-{mode.value}.pdf", mode, values)
        return output
    return write_report(output, results)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument(
        "input", nargs="?", help="PDF, directory, or Hugging Face dataset ID"
    )
    sources.add_argument(
        "--pinned", action="store_true", help="Use the pinned regression page selection"
    )
    parser.add_argument(
        "--recursive", action="store_true", help="Recurse through local directories"
    )
    parser.add_argument(
        "--histograms-only",
        action="store_true",
        help="Write the two F1 histogram PDFs without the report or extracted text",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New report directory or an existing directory for histogram PDFs",
    )
    args = parser.parse_args()
    try:
        result = run(
            args.input,
            args.pinned,
            args.recursive,
            args.output,
            histograms_only=args.histograms_only,
        )
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(result)


if __name__ == "__main__":
    main()
