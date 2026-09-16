#!/usr/bin/env python3
"""Cross-renderer and cross-parser quality benchmark for PDF pages.

This is a script version of the pypdfium render comparison test, with two
extensions:

  * render output can be compared for several renderer pairs
  * textual parse output can be compared for several parser pairs

Inputs may be a local PDF file, a local directory of PDFs, or a Hugging Face
dataset repo id whose `pdf/` subdirectory contains PDFs.

Examples:
  python scripts/benchmarking/run_quality_benchmarking.py ./pdfs
  python scripts/benchmarking/run_quality_benchmarking.py ./pdfs --compare parse
  python scripts/benchmarking/run_quality_benchmarking.py ./pdfs \
      --reference-renderer pypdfium2 --renderers docling-parse,pymupdf
  python scripts/benchmarking/run_quality_benchmarking.py \
      docling-project/performance-dataset-bo767 --max-pages 100
"""

# ruff: noqa: E402,I001

from __future__ import annotations

import argparse
import contextlib
import csv
import difflib
import json
import os
import re
import sys
from concurrent.futures import BrokenExecutor, ProcessPoolExecutor
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Iterable, List, Mapping

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from PIL import Image
from tabulate import tabulate

from _common import ProgressBar, ensure_parent_dir, safe_name
from run_performance_benchmarking import (
    apply_max_pages,
    find_pdfs,
    resolve_pdf_inputs,
)
from tests.constants import HF_DATASET_REPO_ID, HF_DATASET_REVISION, REGRESSION_DIR
from tests.rendering_regression import (
    ImageComparison,
    ImageTolerance,
    flatten_on_white,
    format_image_comparison_table,
    image_comparison_failed,
    measure_image_pair,
    render_pypdfium_page,
    write_comparison_visualization,
    write_metric_histogram,
)

DEFAULT_SCALE = 2.0
# Capped so a many-core CI box does not hold dozens of page images at once.
DEFAULT_WORKERS = min(os.cpu_count() or 1, 16)
DEFAULT_HF_REPO_ID = HF_DATASET_REPO_ID
DEFAULT_HF_PDF_SUBDIR = Path(REGRESSION_DIR).name
DEFAULT_OUTPUT_DIR = Path(
    f"scratch-quality-benchmarks-{datetime.now().strftime('%Y-%m-%d-%H-%M')}"
)

DEFAULT_IMAGE_TOLERANCE = ImageTolerance(
    pixel_threshold=12,
    mean_abs_error=9.0,
    changed_pixels_ratio=0.09,
)


@dataclass(frozen=True)
class ScheduledPage:
    pdf_path: Path
    page_number: int


@dataclass(frozen=True)
class RenderQualityRow:
    actual_renderer: str
    reference_renderer: str
    document: str
    page_number: int
    success: bool
    actual_width: int = 0
    actual_height: int = 0
    expected_width: int = 0
    expected_height: int = 0
    size_matches: bool = False
    normalized_delta: float = 0.0
    mean_abs_error: float = 0.0
    max_abs_error: int = 0
    changed_pixels_ratio: float = 0.0
    changed_pixels: int = 0
    total_pixels: int = 0
    above_tolerance: bool = False
    error_message: str = ""


@dataclass(frozen=True)
class TextQualityRow:
    actual_parser: str
    reference_parser: str
    document: str
    page_number: int
    success: bool
    ratio: float = 0.0
    actual_chars: int = 0
    expected_chars: int = 0
    equal_chars: int = 0
    inserted_chars: int = 0
    deleted_chars: int = 0
    replaced_chars: int = 0
    error_message: str = ""


RenderFn = Callable[[Path, int, float], Image.Image]
TextFn = Callable[[Path, int], str]


def _parse_list(value: str) -> list[str]:
    return [item.strip() for item in re.split(r"[,;]", value) if item.strip()]


def _validate_names(
    kind: str, names: Iterable[str], registry: Mapping[str, object]
) -> None:
    unknown = sorted(set(names) - set(registry))
    if unknown:
        raise SystemExit(
            f"Unknown {kind}: {', '.join(unknown)}. Choose from: "
            f"{', '.join(sorted(registry))}"
        )


def scheduled_pages(
    pdf_schedule: list[tuple[Path, list[int] | None]],
) -> list[ScheduledPage]:
    pages: list[ScheduledPage] = []
    for pdf_path, page_numbers in pdf_schedule:
        if page_numbers is None:
            from docling_parse.pdf_parser import DoclingPdfParser

            parser = DoclingPdfParser(loglevel="fatal")
            doc = parser.load(str(pdf_path), lazy=True)
            try:
                page_numbers = list(range(1, doc.number_of_pages() + 1))
            finally:
                with contextlib.suppress(Exception):
                    doc.unload()
        pages.extend(
            ScheduledPage(pdf_path, page_number) for page_number in page_numbers
        )
    return pages


def resolve_quality_pdf_inputs(
    input_str: str, recursive: bool = False
) -> tuple[list[Path], dict[str, object]]:
    if input_str != DEFAULT_HF_REPO_ID:
        return resolve_pdf_inputs(input_str, recursive=recursive)

    p = Path(input_str)
    if p.exists():
        return resolve_pdf_inputs(input_str, recursive=recursive)

    from huggingface_hub import snapshot_download

    print(
        f"Downloading HF dataset {input_str!r} "
        f"(revision {HF_DATASET_REVISION}, pattern {DEFAULT_HF_PDF_SUBDIR}/**) ..."
    )
    local_dir = snapshot_download(
        repo_id=input_str,
        repo_type="dataset",
        revision=HF_DATASET_REVISION,
        allow_patterns=[f"{DEFAULT_HF_PDF_SUBDIR}/**"],
    )
    pdf_dir = Path(local_dir) / DEFAULT_HF_PDF_SUBDIR
    if not pdf_dir.is_dir():
        raise RuntimeError(
            f"HF dataset {input_str!r} has no {DEFAULT_HF_PDF_SUBDIR}/ "
            f"subfolder at {pdf_dir}"
        )
    info: dict[str, object] = {
        "source": "huggingface",
        "name": input_str,
        "path": str(pdf_dir),
        "revision": Path(local_dir).name,
    }
    return find_pdfs(pdf_dir, recursive=True), info


@dataclass(frozen=True)
class DoclingRenderOptions:
    """The docling-parse render switches, as one picklable bundle."""

    scale: float = DEFAULT_SCALE
    threads: int = 1
    render_text: bool = True
    render_shapes: bool = True
    render_shadings: bool = True
    render_non_rect_clip_masks: bool = True


class DoclingPageDecode:
    """One decode of one page, shared by the render and the text comparison.

    The C++ side decodes a page once and can hand out both the rendered image
    and the char cells from that same result. Asking for them through separate
    entry points -- which is what a render pass followed by a text pass does --
    decodes the page twice for no gain.

    Not thread-safe, and does not need to be: one instance serves one page of
    one job.
    """

    def __init__(self, pdf_path: Path, page_number: int, options: DoclingRenderOptions):
        self._pdf_path = pdf_path
        self._page_number = page_number
        self._options = options
        self._want_image = False
        self._want_text = False
        self._image: Image.Image | None = None
        self._text: str | None = None
        self._decoded = False

    def expect(self, *, image: bool = False, text: bool = False) -> None:
        """Declares up front what the page will be asked for.

        Called before the first `image()`/`text()` so the single decode
        produces everything the job needs.
        """
        self._want_image = self._want_image or image
        self._want_text = self._want_text or text

    def image(self) -> Image.Image:
        self.expect(image=True)
        self._decode()
        if self._image is None:
            raise RuntimeError("docling-parse produced no render result")
        return self._image

    def text(self) -> str:
        self.expect(text=True)
        self._decode()
        if self._text is None:
            raise RuntimeError("docling-parse produced no page content")
        return self._text

    def _decode(self) -> None:
        if self._decoded:
            return

        from docling_parse.pdf_parser import (
            ContentConfig,
            ContentLevel,
            DoclingThreadedPdfParser,
            ThreadedPdfParserConfig,
        )

        render_config = None
        if self._want_image:
            render_config = build_docling_render_config(self._options)

        content_config = None
        if self._want_text:
            content_config = ContentConfig(
                char_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE
            )

        parser = DoclingThreadedPdfParser(
            parser_config=ThreadedPdfParserConfig(
                loglevel="fatal",
                threads=self._options.threads,
                max_concurrent_results=1,
                render_config=render_config,
                page_content_config=content_config,
            )
        )
        parser.load(str(self._pdf_path), page_numbers=[self._page_number])
        try:
            for result in parser.iterate_results():
                if not result.success:
                    raise RuntimeError(result.error_message)
                if self._want_image:
                    self._image = result.get_image().convert("RGBA")
                if self._want_text and content_config is not None:
                    page = result.get_page(content_config=content_config)
                    self._text = "".join(cell.text for cell in page.char_cells)
                break
        finally:
            self._decoded = True
            with contextlib.suppress(Exception):
                parser.unload_all()


def build_docling_render_config(options: DoclingRenderOptions):
    from docling_parse.pdf_parser import RenderConfig

    render_config = RenderConfig()
    render_config.scale = options.scale
    render_config.render_text = options.render_text
    if hasattr(render_config, "render_shapes"):
        render_config.render_shapes = options.render_shapes
    elif not options.render_shapes:
        raise RuntimeError(
            "This docling_parse build does not expose RenderConfig.render_shapes; "
            "rebuild the extension before using --no-docling-render-shapes."
        )
    if hasattr(render_config, "render_shadings"):
        render_config.render_shadings = options.render_shadings
    elif not options.render_shadings:
        raise RuntimeError(
            "This docling_parse build does not expose RenderConfig.render_shadings; "
            "rebuild the extension before using --no-docling-render-shadings."
        )
    if hasattr(render_config, "render_non_rect_clip_masks"):
        render_config.render_non_rect_clip_masks = options.render_non_rect_clip_masks
    elif not options.render_non_rect_clip_masks:
        raise RuntimeError(
            "This docling_parse build does not expose "
            "RenderConfig.render_non_rect_clip_masks; rebuild the extension before "
            "using --no-docling-render-non-rect-clip-masks."
        )
    return render_config


def render_docling_parse(
    pdf_path: Path,
    page_number: int,
    scale: float,
    *,
    threads: int = 1,
    render_text: bool = True,
    render_shapes: bool = True,
    render_shadings: bool = True,
    render_non_rect_clip_masks: bool = True,
) -> Image.Image:
    options = DoclingRenderOptions(
        scale=scale,
        threads=threads,
        render_text=render_text,
        render_shapes=render_shapes,
        render_shadings=render_shadings,
        render_non_rect_clip_masks=render_non_rect_clip_masks,
    )
    return DoclingPageDecode(pdf_path, page_number, options).image()


def render_pymupdf(pdf_path: Path, page_number: int, scale: float) -> Image.Image:
    import fitz

    with contextlib.suppress(Exception):
        fitz.TOOLS.mupdf_display_errors(False)

    doc = fitz.open(str(pdf_path))
    try:
        page = doc[page_number - 1]
        pix = page.get_pixmap(matrix=fitz.Matrix(scale, scale), alpha=True)
        return pix.pil_image().convert("RGBA")
    finally:
        with contextlib.suppress(Exception):
            doc.close()


def render_pdfium(pdf_path: Path, page_number: int, scale: float) -> Image.Image:
    return render_pypdfium_page(pdf_path, page_number, scale=scale).convert("RGBA")


def text_docling_parse(pdf_path: Path, page_number: int) -> str:
    return DoclingPageDecode(pdf_path, page_number, DoclingRenderOptions()).text()


def text_pypdfium2(pdf_path: Path, page_number: int) -> str:
    import pypdfium2 as pdfium

    doc = pdfium.PdfDocument(str(pdf_path))
    try:
        page = doc[page_number - 1]
        text_page = page.get_textpage()
        try:
            if hasattr(text_page, "get_text_range"):
                return text_page.get_text_range()
            chunks = []
            for rect_idx in range(text_page.count_rects()):
                rect = text_page.get_rect(rect_idx)
                chunks.append(text_page.get_text_bounded(*rect))
            return "\n".join(chunks)
        finally:
            with contextlib.suppress(Exception):
                text_page.close()
            with contextlib.suppress(Exception):
                page.close()
    finally:
        with contextlib.suppress(Exception):
            doc.close()


def text_pymupdf(pdf_path: Path, page_number: int) -> str:
    import fitz

    with contextlib.suppress(Exception):
        fitz.TOOLS.mupdf_display_errors(False)

    doc = fitz.open(str(pdf_path))
    try:
        return doc[page_number - 1].get_text("text")
    finally:
        with contextlib.suppress(Exception):
            doc.close()


def text_pdfplumber(pdf_path: Path, page_number: int) -> str:
    import pdfplumber

    pdf = pdfplumber.open(str(pdf_path))
    try:
        page = pdf.pages[page_number - 1]
        try:
            return page.extract_text() or ""
        finally:
            with contextlib.suppress(Exception):
                page.close()
    finally:
        with contextlib.suppress(Exception):
            pdf.close()


def text_pdfminer(pdf_path: Path, page_number: int) -> str:
    from pdfminer.high_level import extract_text

    return extract_text(str(pdf_path), page_numbers=[page_number - 1]) or ""


def text_pypdf(pdf_path: Path, page_number: int) -> str:
    from pypdf import PdfReader

    reader = PdfReader(str(pdf_path))
    return reader.pages[page_number - 1].extract_text() or ""


RENDERERS: dict[str, RenderFn] = {
    "docling-parse": render_docling_parse,
    "pypdfium2": render_pdfium,
    "pymupdf": render_pymupdf,
}

TEXT_PARSERS: dict[str, TextFn] = {
    "docling-parse": text_docling_parse,
    "pypdfium2": text_pypdfium2,
    "pymupdf": text_pymupdf,
    "pdfplumber": text_pdfplumber,
    "pdfminer.six": text_pdfminer,
    "pypdf": text_pypdf,
}


def normalize_text(text: str, mode: str) -> str:
    if mode == "none":
        return text
    return re.sub(r"\s+", " ", text).strip()


def compare_text(
    actual_parser: str,
    reference_parser: str,
    page: ScheduledPage,
    *,
    normalization: str,
    docling_decode: DoclingPageDecode | None = None,
) -> TextQualityRow:
    def text_with(name: str) -> str:
        if name == "docling-parse" and docling_decode is not None:
            return docling_decode.text()
        return TEXT_PARSERS[name](page.pdf_path, page.page_number)

    try:
        actual = normalize_text(text_with(actual_parser), normalization)
        expected = normalize_text(text_with(reference_parser), normalization)
        matcher = difflib.SequenceMatcher(None, expected, actual, autojunk=False)
        inserted = deleted = replaced = equal = 0
        for tag, i1, i2, j1, j2 in matcher.get_opcodes():
            if tag == "equal":
                equal += i2 - i1
            elif tag == "insert":
                inserted += j2 - j1
            elif tag == "delete":
                deleted += i2 - i1
            elif tag == "replace":
                replaced += max(i2 - i1, j2 - j1)
        return TextQualityRow(
            actual_parser=actual_parser,
            reference_parser=reference_parser,
            document=str(page.pdf_path),
            page_number=page.page_number,
            success=True,
            ratio=matcher.ratio(),
            actual_chars=len(actual),
            expected_chars=len(expected),
            equal_chars=equal,
            inserted_chars=inserted,
            deleted_chars=deleted,
            replaced_chars=replaced,
        )
    except Exception as exc:
        return TextQualityRow(
            actual_parser=actual_parser,
            reference_parser=reference_parser,
            document=str(page.pdf_path),
            page_number=page.page_number,
            success=False,
            error_message=str(exc),
        )


def compare_render(
    actual_renderer: str,
    reference_renderer: str,
    page: ScheduledPage,
    *,
    scale: float,
    tolerance: ImageTolerance,
    docling_render_threads: int,
    docling_render_text: bool,
    docling_render_shapes: bool,
    docling_render_shadings: bool,
    docling_render_non_rect_clip_masks: bool,
    docling_decode: DoclingPageDecode | None = None,
) -> tuple[
    RenderQualityRow, ImageComparison | None, Image.Image | None, Image.Image | None
]:
    def render_with(name: str) -> Image.Image:
        if name == "docling-parse":
            if docling_decode is not None:
                return docling_decode.image()
            return render_docling_parse(
                page.pdf_path,
                page.page_number,
                scale,
                threads=docling_render_threads,
                render_text=docling_render_text,
                render_shapes=docling_render_shapes,
                render_shadings=docling_render_shadings,
                render_non_rect_clip_masks=docling_render_non_rect_clip_masks,
            )
        return RENDERERS[name](page.pdf_path, page.page_number, scale)

    try:
        actual_image = render_with(actual_renderer)
        expected_image = render_with(reference_renderer)

        actual = flatten_on_white(actual_image)
        expected = flatten_on_white(expected_image)
        document_label = (
            f"{actual_renderer}_vs_{reference_renderer}_{page.pdf_path.name}"
        )
        comparison = measure_image_pair(
            document_label,
            page.page_number,
            actual,
            expected,
            tolerance=tolerance,
        )
        failed = image_comparison_failed(comparison, tolerance=tolerance)
        return (
            RenderQualityRow(
                actual_renderer=actual_renderer,
                reference_renderer=reference_renderer,
                document=str(page.pdf_path),
                page_number=page.page_number,
                success=True,
                actual_width=comparison.actual_width,
                actual_height=comparison.actual_height,
                expected_width=comparison.expected_width,
                expected_height=comparison.expected_height,
                size_matches=comparison.size_matches,
                normalized_delta=comparison.normalized_delta,
                mean_abs_error=comparison.mean_abs_error,
                max_abs_error=comparison.max_abs_error,
                changed_pixels_ratio=comparison.changed_pixels_ratio,
                changed_pixels=comparison.changed_pixels,
                total_pixels=comparison.total_pixels,
                above_tolerance=failed,
            ),
            comparison,
            actual,
            expected,
        )
    except Exception as exc:
        return (
            RenderQualityRow(
                actual_renderer=actual_renderer,
                reference_renderer=reference_renderer,
                document=str(page.pdf_path),
                page_number=page.page_number,
                success=False,
                error_message=str(exc),
            ),
            None,
            None,
            None,
        )


@dataclass(frozen=True)
class PageJob:
    """Every comparison one page needs, in a form that survives pickling.

    Render and text comparisons for a page travel together so that the
    docling-parse decode behind them happens once instead of once per pass.
    """

    pdf_path: Path
    page_number: int
    renderers: tuple[str, ...]
    reference_renderer: str
    parsers: tuple[str, ...]
    reference_parser: str
    docling_options: DoclingRenderOptions
    visualizations: str
    viz_dir: Path
    text_normalization: str


@dataclass(frozen=True)
class RenderResult:
    row: RenderQualityRow
    comparison: ImageComparison | None
    visualization: Path | None


@dataclass(frozen=True)
class PageResult:
    renders: tuple[RenderResult, ...]
    texts: tuple[TextQualityRow, ...]


def run_page_job(job: PageJob) -> PageResult:
    """Runs one page's comparisons and draws its visualizations.

    The visualizations are written here rather than by the caller so that the
    page images never leave the worker: shipping a pair of scale-2 RGBA pages
    back through a pipe costs more than rendering them.
    """
    page = ScheduledPage(job.pdf_path, job.page_number)
    options = job.docling_options

    # One decode serves whichever of the two comparisons name docling-parse.
    wants_render = "docling-parse" in (*job.renderers, job.reference_renderer)
    wants_text = "docling-parse" in (*job.parsers, job.reference_parser)

    decode: DoclingPageDecode | None = None
    if wants_render or wants_text:
        decode = DoclingPageDecode(job.pdf_path, job.page_number, options)
        decode.expect(image=wants_render, text=wants_text)

    renders: list[RenderResult] = []
    for actual_renderer in job.renderers:
        row, comparison, actual, expected = compare_render(
            actual_renderer,
            job.reference_renderer,
            page,
            scale=options.scale,
            tolerance=DEFAULT_IMAGE_TOLERANCE,
            docling_render_threads=options.threads,
            docling_render_text=options.render_text,
            docling_render_shapes=options.render_shapes,
            docling_render_shadings=options.render_shadings,
            docling_render_non_rect_clip_masks=options.render_non_rect_clip_masks,
            docling_decode=decode,
        )

        visualization: Path | None = None
        if comparison is not None and actual is not None and expected is not None:
            wanted = job.visualizations == "all" or (
                job.visualizations == "above-tolerance" and row.above_tolerance
            )
            if wanted:
                visualization = write_comparison_visualization(
                    safe_name(comparison.document),
                    job.page_number,
                    expected,
                    actual,
                    comparison.normalized_delta,
                    reference_label=job.reference_renderer,
                    actual_label=actual_renderer,
                    folder=job.viz_dir,
                )

        renders.append(RenderResult(row, comparison, visualization))

    texts = tuple(
        compare_text(
            actual_parser,
            job.reference_parser,
            page,
            normalization=job.text_normalization,
            docling_decode=decode,
        )
        for actual_parser in job.parsers
    )

    return PageResult(tuple(renders), texts)


def imap_jobs(fn, jobs: list, workers: int):
    """Runs `fn` over `jobs`, yielding results in job order.

    Processes, not threads: pdfium -- the default reference renderer -- is not
    thread-safe and aborts the interpreter when several threads render at once.
    A worker per process gives each its own pdfium, so the results are the ones
    a serial run produces, only sooner.

    `chunksize=1` keeps the progress log meaningful: a result is logged as it
    arrives, so a run that dies leaves a breadcrumb next to the page that
    killed it rather than a chunk boundary away from it.
    """
    if workers <= 1 or len(jobs) <= 1:
        for job in jobs:
            yield fn(job)
        return

    try:
        with ProcessPoolExecutor(max_workers=workers) as executor:
            yield from executor.map(fn, jobs, chunksize=1)
    except BrokenExecutor as exc:
        # A worker died outright -- a segfault in a renderer, not an exception
        # the comparison could catch. The breadcrumb log names the last page
        # that finished, and the culprit is within `workers` pages of it.
        raise SystemExit(
            f"A comparison worker died ({exc}). Re-run the last pages of the "
            f"progress log with --workers 1 to see which page is responsible."
        ) from exc


def write_render_csv(path: Path, rows: list[RenderQualityRow]) -> None:
    ensure_parent_dir(path)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(RenderQualityRow.__dataclass_fields__)
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def write_text_csv(path: Path, rows: list[TextQualityRow]) -> None:
    ensure_parent_dir(path)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(TextQualityRow.__dataclass_fields__)
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def write_progress_log(
    handle,
    *,
    phase: str,
    actual_engine: str,
    reference_engine: str,
    page: ScheduledPage,
    index: int,
    total: int,
) -> None:
    handle.write(
        json.dumps(
            {
                "phase": phase,
                "actual_engine": actual_engine,
                "reference_engine": reference_engine,
                "document": str(page.pdf_path),
                "page_number": page.page_number,
                "index": index,
                "total": total,
            },
            sort_keys=True,
        )
        + "\n"
    )
    handle.flush()


def format_text_table(rows: list[TextQualityRow]) -> str:
    successful = [row for row in rows if row.success]
    if not successful:
        return "No textual comparisons were collected."

    table = []
    for row in sorted(
        successful, key=lambda item: (item.ratio, item.document, item.page_number)
    ):
        table.append(
            [
                f"{row.actual_parser} vs {row.reference_parser}",
                Path(row.document).name,
                row.page_number,
                f"{row.ratio:.4f}",
                row.actual_chars,
                row.expected_chars,
                row.inserted_chars,
                row.deleted_chars,
                row.replaced_chars,
            ]
        )
    return "\n".join(
        [
            "Per-page text metrics (worst first)",
            tabulate(
                table,
                headers=[
                    "parser_pair",
                    "document",
                    "page",
                    "ratio",
                    "actual_chars",
                    "expected_chars",
                    "inserted",
                    "deleted",
                    "replaced",
                ],
                tablefmt="github",
            ),
        ]
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Compare textual parse quality and rendered page quality"
    )
    parser.add_argument(
        "input",
        nargs="?",
        default=DEFAULT_HF_REPO_ID,
        help=(
            "Local PDF, local PDF directory, or Hugging Face dataset repo id "
            f"(default: {DEFAULT_HF_REPO_ID})"
        ),
    )
    parser.add_argument(
        "--compare",
        choices=["parse", "render", "both"],
        default="both",
        help="Which quality comparison to run (default: both)",
    )
    parser.add_argument(
        "--reference-renderer",
        default="pypdfium2",
        help="Renderer used as image reference (default: pypdfium2)",
    )
    parser.add_argument(
        "--renderers",
        default="docling-parse",
        help="Comma/semicolon-separated renderers to compare against the reference",
    )
    parser.add_argument(
        "--reference-parser",
        default="pypdfium2",
        help="Parser used as text reference (default: pypdfium2)",
    )
    parser.add_argument(
        "--parsers",
        default="docling-parse",
        help="Comma/semicolon-separated parsers to compare against the reference",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Search local input directories recursively",
    )
    parser.add_argument(
        "--max-pages",
        "-l",
        type=int,
        default=None,
        help="Exact total page cap across the input set",
    )
    parser.add_argument(
        "--scale",
        type=float,
        default=DEFAULT_SCALE,
        help="Render scale, where 1.0 is 72 dpi (default: 2.0)",
    )
    parser.add_argument(
        "--workers",
        "-j",
        type=int,
        default=DEFAULT_WORKERS,
        help=(
            f"Worker processes for the page comparisons (default: {DEFAULT_WORKERS}). "
            "Processes rather than threads: pdfium is not thread-safe. "
            "Use 1 to run in-process, which is what a debugger wants."
        ),
    )
    parser.add_argument(
        "--docling-render-threads",
        type=int,
        default=1,
        help="Worker threads for docling-parse rendering (default: 1)",
    )
    parser.add_argument(
        "--no-docling-render-text",
        action="store_true",
        help="Disable text painting in docling-parse render comparisons",
    )
    parser.add_argument(
        "--no-docling-render-shapes",
        action="store_true",
        help="Disable vector shape painting in docling-parse render comparisons",
    )
    parser.add_argument(
        "--no-docling-render-shadings",
        action="store_true",
        help="Disable shading painting in docling-parse render comparisons",
    )
    parser.add_argument(
        "--no-docling-render-non-rect-clip-masks",
        action="store_true",
        help=("Disable non-rectangular clip masks in docling-parse render comparisons"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Output directory (default: {DEFAULT_OUTPUT_DIR})",
    )
    parser.add_argument(
        "--progress-log",
        type=Path,
        default=None,
        help=(
            "JSONL breadcrumb log for the page currently being processed. "
            "Defaults to <output-dir>/current_page.jsonl"
        ),
    )
    parser.add_argument(
        "--render-visualizations",
        choices=["all", "above-tolerance", "none"],
        default="above-tolerance",
        help=(
            "Write three-panel render visualizations "
            "(default: above-tolerance). Drawing one per page costs about a "
            "third of the run and gigabytes of output, nearly all of it "
            "panels of pages that already match."
        ),
    )
    parser.add_argument(
        "--text-normalization",
        choices=["whitespace", "none"],
        default="whitespace",
        help="Normalize text before comparison (default: whitespace)",
    )
    args = parser.parse_args(argv)

    actual_renderers = _parse_list(args.renderers)
    actual_parsers = _parse_list(args.parsers)
    if args.docling_render_threads < 1:
        raise SystemExit("--docling-render-threads must be >= 1")
    if args.workers < 1:
        raise SystemExit("--workers must be >= 1")
    _validate_names(
        "renderer",
        [args.reference_renderer, *actual_renderers],
        RENDERERS,
    )
    _validate_names("parser", [args.reference_parser, *actual_parsers], TEXT_PARSERS)

    pdf_paths, dataset_info = resolve_quality_pdf_inputs(
        args.input, recursive=args.recursive
    )
    schedule, total_pages = apply_max_pages(pdf_paths, args.max_pages)
    pages = scheduled_pages(schedule)
    if not pages:
        print("No PDF pages found to compare.")
        return 2

    args.output_dir.mkdir(parents=True, exist_ok=True)
    print(
        f"Resolved {len(pdf_paths)} document(s), scheduled {total_pages} page(s) "
        f"from {dataset_info['name']}"
    )
    print(f"Writing quality outputs to: {args.output_dir}")
    progress_log_path = args.progress_log or (args.output_dir / "current_page.jsonl")
    print(f"Writing current-page breadcrumbs to: {progress_log_path}")

    do_render = args.compare in {"render", "both"}
    do_parse = args.compare in {"parse", "both"}

    viz_dir = args.output_dir / "visualizations"
    page_jobs = [
        PageJob(
            pdf_path=page.pdf_path,
            page_number=page.page_number,
            renderers=tuple(actual_renderers) if do_render else (),
            reference_renderer=args.reference_renderer,
            parsers=tuple(actual_parsers) if do_parse else (),
            reference_parser=args.reference_parser,
            docling_options=DoclingRenderOptions(
                scale=args.scale,
                threads=args.docling_render_threads,
                render_text=not args.no_docling_render_text,
                render_shapes=not args.no_docling_render_shapes,
                render_shadings=not args.no_docling_render_shadings,
                render_non_rect_clip_masks=not args.no_docling_render_non_rect_clip_masks,
            ),
            visualizations=args.render_visualizations,
            viz_dir=viz_dir,
            text_normalization=args.text_normalization,
        )
        for page in pages
    ]

    render_rows: list[RenderQualityRow] = []
    image_comparisons: list[ImageComparison] = []
    visualizations: list[Path] = []
    text_rows: list[TextQualityRow] = []

    ensure_parent_dir(progress_log_path)
    with (
        ProgressBar(total=len(page_jobs), desc="quality", unit="page") as progress,
        progress_log_path.open("a", encoding="utf-8") as log_handle,
    ):
        for index, (job, result) in enumerate(
            zip(page_jobs, imap_jobs(run_page_job, page_jobs, args.workers)), start=1
        ):
            write_progress_log(
                log_handle,
                phase=args.compare,
                actual_engine=",".join((*job.renderers, *job.parsers)),
                reference_engine=args.reference_renderer,
                page=ScheduledPage(job.pdf_path, job.page_number),
                index=index,
                total=len(page_jobs),
            )
            for render in result.renders:
                render_rows.append(render.row)
                if render.comparison is not None:
                    image_comparisons.append(render.comparison)
                if render.visualization is not None:
                    visualizations.append(render.visualization)
            text_rows.extend(result.texts)
            progress.update(1)

    if do_render:
        if args.render_visualizations != "none":
            written = [
                write_metric_histogram(
                    [comparison.normalized_delta for comparison in image_comparisons],
                    metric="render_delta",
                    title="render normalized_delta",
                    xlabel="normalized_delta (0 = identical, 1 = maximally different)",
                    folder=viz_dir,
                ),
                write_metric_histogram(
                    [comparison.mean_abs_error for comparison in image_comparisons],
                    metric="render_mean_abs_error",
                    title="render mean_abs_error",
                    xlabel="mean_abs_error (0-255 grey levels)",
                    threshold=DEFAULT_IMAGE_TOLERANCE.mean_abs_error,
                    threshold_label="reporting cut-off",
                    folder=viz_dir,
                ),
            ]
            visualizations.extend(path for path in written if path is not None)
        write_render_csv(args.output_dir / "render_quality.csv", render_rows)
        print()
        print(format_image_comparison_table(image_comparisons))
        if visualizations:
            print(f"\nWrote {len(visualizations)} render visualization(s) to {viz_dir}")

    if do_parse:
        write_text_csv(args.output_dir / "text_quality.csv", text_rows)
        print()
        print(format_text_table(text_rows))

    render_successes = sum(1 for row in render_rows if row.success)
    text_successes = sum(1 for row in text_rows if row.success)
    if args.compare == "render" and render_successes == 0:
        return 1
    if args.compare == "parse" and text_successes == 0:
        return 1
    if args.compare == "both" and render_successes == 0 and text_successes == 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
