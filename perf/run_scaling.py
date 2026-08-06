#!/usr/bin/env python3
"""
Thread-scaling benchmark for docling-parse.

Runs DoclingThreadedPdfParser at increasing thread counts and prints a
scaling table.  Three modes are supported:

  parse   — decode-only (render_config=None); always includes a
            single-threaded DoclingPdfParser baseline.
  render  — decode + rasterise (RenderConfig.scale=...).
  both    — runs both of the above and prints two tables.

Third-party single-threaded backends (selected via --other) are run as
additional baselines, in both parse and render modes.  Supported names:
  - pypdfium2  (default)
  - pymupdf

Passing --compare switches to the comparison suite, which reports a per-page
time distribution (mean/median/p95/p99) plus a wall-time speedup table for
docling-parse at each thread count against every single-threaded backend.  It
also captures the hardware, package versions and dataset revision, and can emit
the markdown tables used by docs/performance_benchmarks.md.

Inputs may be either a local PDF file/directory, or a Hugging Face dataset
repo-id whose `pdf/` subfolder contains the PDFs.  When omitted, defaults to
the HF repo `docling-project/performance-dataset-bo767`.

Usage:
    python perf/run_scaling.py                                   # HF default, render mode, pypdfium2
    python perf/run_scaling.py ./pdfs --mode parse
    python perf/run_scaling.py --mode both --other "pypdfium2;pymupdf"
    python perf/run_scaling.py ./pdfs --mode render --keep-char-cells=true \
        --create-word-cells=true --create-line-cells=true \
        --keep-shapes=true --keep-bitmaps=true
    python perf/run_scaling.py --mode both --compare                 # docling-parse vs pypdfium2
    python perf/run_scaling.py --threads 1,4,8,12 --compare all --mode render \
        --output-dir ./docs/performance_benchmarks/

Every run writes `<output-dir>/<cpu>_<dataset>_<mode>.md` (a self-contained
report: the exact command, the dataset, the machine, every config table, and
the result tables) alongside a `.csv` of per-page timings for perf/run_eval.py
and perf/run_analysis.py.  The output directory defaults to ./scratch.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import platform
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Tuple

from _common import (
    TASK_PARSE,
    TASK_RENDER,
    PageRow,
    find_pdfs,
    percentile,
    series_label,
    write_page_rows,
)
from tabulate import tabulate
from tqdm import tqdm

DEFAULT_HF_REPO_ID = "docling-project/performance-dataset-bo767"
HF_PDF_SUBDIR = "pdf"
DEFAULT_OUTPUT_DIR = Path("scratch")


# -------- Input resolution --------


def resolve_pdf_inputs(
    input_str: str, recursive: bool = False
) -> Tuple[List[Path], Dict[str, object]]:
    """Resolve `input_str` to a list of PDFs plus a provenance record.

    If it matches an existing local file or directory, search it for PDFs.
    Otherwise treat it as a Hugging Face dataset repo-id, download via
    snapshot_download (restricted to the `pdf/` subfolder), and iterate
    the downloaded `pdf/` directory recursively.

    The second return value records where the PDFs came from, so a benchmark
    run can be attributed to an exact dataset revision.
    """
    p = Path(input_str)
    if p.exists():
        info: Dict[str, object] = {
            "source": "local",
            "name": p.name,
            "path": str(p.resolve()),
            "revision": None,
        }
        return find_pdfs(p, recursive=recursive), info

    from huggingface_hub import snapshot_download

    print(f"Downloading HF dataset {input_str!r} (pattern {HF_PDF_SUBDIR}/**) ...")
    local_dir = snapshot_download(
        repo_id=input_str,
        repo_type="dataset",
        allow_patterns=[f"{HF_PDF_SUBDIR}/**"],
    )
    pdf_dir = Path(local_dir) / HF_PDF_SUBDIR
    if not pdf_dir.is_dir():
        raise RuntimeError(
            f"HF dataset {input_str!r} has no {HF_PDF_SUBDIR}/ subfolder at {pdf_dir}"
        )
    # snapshot_download resolves to <cache>/snapshots/<commit-sha>/, so the
    # parent directory name is the exact revision the numbers refer to.
    info = {
        "source": "huggingface",
        "name": input_str,
        "path": str(pdf_dir),
        "revision": Path(local_dir).name,
    }
    return find_pdfs(pdf_dir, recursive=True), info


def page_counts(pdf_paths: List[Path]) -> List[Tuple[Path, int]]:
    """Count pages per PDF using DoclingPdfParser."""
    from docling_parse.pdf_parser import DoclingPdfParser

    parser = DoclingPdfParser(loglevel="fatal")
    counts: List[Tuple[Path, int]] = []
    for pdf_path in tqdm(pdf_paths, desc="counting pages", unit="doc"):
        try:
            d = parser.load(str(pdf_path), lazy=True)
            counts.append((pdf_path, d.number_of_pages()))
            d.unload()
        except Exception:
            pass
    return counts


def apply_max_pages(
    pdf_paths: List[Path], max_pages: int | None
) -> Tuple[List[Tuple[Path, List[int] | None]], int]:
    """Apply an exact total-page cap across PDFs in input order.

    Returns a schedule of `(pdf_path, page_numbers)` where `page_numbers` is
    `None` for all pages in a document, or an explicit 1-indexed subset for the
    final truncated document. The second return value is the total scheduled
    page count.
    """
    counts = page_counts(pdf_paths)
    if max_pages is None:
        return [(pdf_path, None) for pdf_path, _ in counts], sum(
            count for _, count in counts
        )

    if max_pages <= 0:
        return [], 0

    schedule: List[Tuple[Path, List[int] | None]] = []
    remaining = max_pages
    total = 0

    for pdf_path, count in counts:
        if remaining <= 0:
            break
        if count <= remaining:
            schedule.append((pdf_path, None))
            remaining -= count
            total += count
        else:
            page_numbers = list(range(1, remaining + 1))
            schedule.append((pdf_path, page_numbers))
            total += remaining
            remaining = 0
            break

    return schedule, total


# -------- Provenance: hardware, software, dataset --------

# Packages whose versions are pinned into every benchmark report, so a table
# row can always be traced back to the exact stack that produced it.
REPORTED_PACKAGES = [
    "docling-parse",
    "pymupdf",
    "pypdfium2",
    "pdfplumber",
    "pdfminer.six",
    "pypdf",
    "pillow",
]


def _shell(cmd: List[str]) -> str | None:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
    except Exception:
        return None
    value = out.stdout.strip()
    return value or None


def _cpu_model() -> str:
    if sys.platform == "darwin":
        value = _shell(["sysctl", "-n", "machdep.cpu.brand_string"])
        if value:
            return value
    elif sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except Exception:
            pass
    return platform.processor() or platform.machine() or "unknown"


def _physical_cores() -> int | None:
    if sys.platform == "darwin":
        value = _shell(["sysctl", "-n", "hw.physicalcpu"])
        if value and value.isdigit():
            return int(value)
    try:
        # Python 3.13+ exposes the cgroup/affinity-aware count.
        count = os.process_cpu_count()  # type: ignore[attr-defined]
        if count:
            return count
    except AttributeError:
        pass
    return None


def _memory_gb() -> float | None:
    try:
        if sys.platform == "darwin":
            value = _shell(["sysctl", "-n", "hw.memsize"])
            if value and value.isdigit():
                return int(value) / 1024**3
        page_size = os.sysconf("SC_PAGE_SIZE")
        page_count = os.sysconf("SC_PHYS_PAGES")
        return page_size * page_count / 1024**3
    except Exception:
        return None


def _package_versions() -> Dict[str, str]:
    import importlib.metadata as md

    versions: Dict[str, str] = {}
    for name in REPORTED_PACKAGES:
        try:
            versions[name] = md.version(name)
        except Exception:
            versions[name] = "not installed"
    return versions


def collect_system_info() -> Dict[str, object]:
    """Snapshot the machine and software stack the benchmark ran on."""
    return {
        "cpu": _cpu_model(),
        "physical_cores": _physical_cores(),
        "logical_cores": os.cpu_count(),
        "memory_gb": _memory_gb(),
        "platform": f"{platform.system()} {platform.release()}",
        "arch": platform.machine(),
        "python": platform.python_version(),
        "packages": _package_versions(),
    }


def format_hardware_label(system_info: Dict[str, object]) -> str:
    """Compact one-cell description of the machine, for the markdown table."""
    cores = system_info.get("physical_cores") or system_info.get("logical_cores")
    memory = system_info.get("memory_gb")
    parts = [str(system_info["cpu"])]
    if cores:
        parts.append(f"{cores} cores")
    if memory:
        parts.append(f"{memory:.0f} GB RAM")
    parts.append(str(system_info["platform"]))
    return ", ".join(parts)


def format_dataset_label(dataset_info: Dict[str, object]) -> str:
    """Compact one-cell description of the corpus, for the markdown table."""
    label = str(dataset_info["name"])
    revision = dataset_info.get("revision")
    if revision:
        label += f"@{str(revision)[:7]}"
    return f"{label} ({dataset_info['documents']} docs, {dataset_info['pages']} pages)"


def system_rows(system_info: Dict[str, object]) -> List[List[object]]:
    rows: List[List[object]] = [
        ["cpu", system_info["cpu"]],
        ["physical cores", system_info["physical_cores"]],
        ["logical cores", system_info["logical_cores"]],
        [
            "memory (GB)",
            f"{system_info['memory_gb']:.1f}" if system_info["memory_gb"] else "n/a",
        ],
        ["platform", system_info["platform"]],
        ["arch", system_info["arch"]],
        ["python", system_info["python"]],
    ]
    packages = system_info["packages"]
    assert isinstance(packages, dict)
    rows.extend([[name, version] for name, version in packages.items()])
    return rows


def benchmark_rows(
    dataset_info: Dict[str, object], settings: Dict[str, object]
) -> List[List[object]]:
    rows: List[List[object]] = [
        ["dataset", dataset_info["name"]],
        ["dataset source", dataset_info["source"]],
    ]
    if dataset_info.get("revision"):
        rows.append(["dataset revision", dataset_info["revision"]])
    rows.extend(
        [
            ["documents", dataset_info["documents"]],
            ["pages", dataset_info["pages"]],
        ]
    )
    rows.extend([key, value] for key, value in settings.items())
    return rows


# -------- Output naming --------


def _slug(value: str) -> str:
    """Filesystem-friendly token: lowercase, only [a-z0-9-], others collapsed."""
    slug = re.sub(r"[^a-z0-9-]+", "_", str(value).lower()).strip("_")
    return slug or "unknown"


def output_basename(
    system_info: Dict[str, object], dataset_info: Dict[str, object], mode: str
) -> str:
    """`<cpu>_<dataset>_<mode>`, so runs never overwrite each other."""
    dataset = str(dataset_info["name"]).rstrip("/").split("/")[-1]
    dataset = re.sub(r"\.pdf$", "", dataset, flags=re.IGNORECASE)
    return f"{_slug(system_info['cpu'])}_{_slug(dataset)}_{_slug(mode)}"


# -------- Decode config helper --------


def _str_to_bool(value: str | bool) -> bool:
    if isinstance(value, bool):
        return value
    normalized = value.strip().lower()
    if normalized in {"1", "true", "t", "yes", "y", "on"}:
        return True
    if normalized in {"0", "false", "f", "no", "n", "off"}:
        return False
    raise argparse.ArgumentTypeError(
        f"expected a boolean value, got {value!r}; use true or false"
    )


def _add_bool_value_arg(
    ap: argparse.ArgumentParser,
    name: str,
    *,
    default: bool,
    help: str,
) -> None:
    ap.add_argument(
        f"--{name}",
        type=_str_to_bool,
        default=default,
        metavar="{true,false}",
        help=f"{help} (default: {str(default).lower()})",
    )


def _decode_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "keep_char_cells": args.keep_char_cells,
        "keep_shapes": args.keep_shapes,
        "keep_bitmaps": args.keep_bitmaps,
        "create_word_cells": args.create_word_cells,
        "create_line_cells": args.create_line_cells,
    }


def _materialization_options_from_args(args: argparse.Namespace) -> dict[str, bool]:
    return {
        "materialize_char_cells": args.materialize_char_cells,
        "materialize_word_cells": args.materialize_word_cells,
        "materialize_line_cells": args.materialize_line_cells,
        "materialize_shapes": args.materialize_shapes,
        "materialize_bitmaps": args.materialize_bitmaps,
        "materialize_bitmap_bytes": args.materialize_bitmap_bytes,
    }


def _materializes_page_data(materialization_options: dict[str, bool]) -> bool:
    return any(
        materialization_options[name]
        for name in (
            "materialize_char_cells",
            "materialize_word_cells",
            "materialize_line_cells",
            "materialize_shapes",
            "materialize_bitmaps",
        )
    )


def _decode_config():
    from docling_parse.pdf_parser import DecodeConfig

    return DecodeConfig()


def _content_config(
    decode_options: dict[str, bool], materialization_options: dict[str, bool]
):
    from docling_parse.pdf_parser import ContentConfig, ContentLevel

    def _level(keep: bool, materialize: bool) -> ContentLevel:
        if materialize:
            return ContentLevel.COMPUTE_AND_MATERIALIZE
        if keep:
            return ContentLevel.COMPUTE
        return ContentLevel.SKIP

    return ContentConfig(
        char_cells_content_level=_level(
            decode_options["keep_char_cells"],
            materialization_options["materialize_char_cells"],
        ),
        word_cells_content_level=_level(
            decode_options["create_word_cells"],
            materialization_options["materialize_word_cells"],
        ),
        line_cells_content_level=_level(
            decode_options["create_line_cells"],
            materialization_options["materialize_line_cells"],
        ),
        shapes_content_level=_level(
            decode_options["keep_shapes"],
            materialization_options["materialize_shapes"],
        ),
        bitmaps_content_level=_level(
            decode_options["keep_bitmaps"],
            materialization_options["materialize_bitmaps"],
        ),
        include_bitmap_bytes=materialization_options["materialize_bitmap_bytes"],
    )


def _config_rows(values: dict[str, object], fields: List[str]) -> List[List[str]]:
    return [[field, values[field]] for field in fields]


def config_tables(
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
) -> List[Tuple[str, List[List[object]]]]:
    """(title, parameter/value rows) for each config the run was driven with.

    Returned rather than printed so the terminal output and the markdown report
    always show the same values.
    """
    from docling_parse.pdf_parsers import RenderConfig  # type: ignore[import]

    decode_fields = [
        "do_sanitization",
        "enforce_same_font",
        "horizontal_cell_tolerance",
        "word_space_width_factor_for_merge",
        "line_space_width_factor_for_merge",
        "line_space_width_factor_for_merge_with_space",
        "max_num_lines",
        "max_num_bitmaps",
        "do_thread_safe",
        "release_native_memory_every_n_pages",
        "keep_glyphs",
        "keep_qpdf_warnings",
    ]
    content_fields = [
        "char_cells_content_level",
        "word_cells_content_level",
        "line_cells_content_level",
        "shapes_content_level",
        "bitmaps_content_level",
        "include_bitmap_bytes",
    ]
    render_fields = [
        "render_text",
        "draw_text_bbox",
        "draw_text_basepoint",
        "display_widgets",
        "fit_glyph_bbox_to_target",
        "resolve_fonts",
        "font_similarity_cutoff",
        "scale",
        "canvas_width",
        "canvas_height",
    ]

    tables: List[Tuple[str, List[List[object]]]] = [
        (
            "Decode config",
            _config_rows(_decode_config().model_dump(), decode_fields),
        ),
        (
            "Content config",
            _config_rows(
                _content_config(decode_options, materialization_options).model_dump(),
                content_fields,
            ),
        ),
    ]

    if not render:
        tables.append(("Render config", [["enabled", False]]))
        return tables

    render_config = RenderConfig()
    render_config.scale = scale
    render_values = {field: getattr(render_config, field) for field in render_fields}
    tables.append(("Render config", _config_rows(render_values, render_fields)))
    return tables


def print_parameter_tables(tables: List[Tuple[str, List[List[object]]]]) -> None:
    for title, rows in tables:
        print(f"{title}:")
        print(tabulate(rows, headers=["parameter", "value"]))
        print()


def _stage_timings(result) -> Dict[str, float]:
    """C++ per-stage timings for one threaded page result."""
    from docling_parse.pdf_parser import PageRenderTimings

    if not result.success:
        return {}
    timings = result.timings
    return {
        "make_page_decoder_s": timings.make_page_decoder_s,
        "decode_page_s": timings.decode_page_s,
        "create_word_cells_s": timings.create_word_cells_s,
        "create_line_cells_s": timings.create_line_cells_s,
        "render_page_s": (
            timings.render_page_s if isinstance(timings, PageRenderTimings) else 0.0
        ),
    }


# -------- Baselines --------


def run_sequential_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
) -> float:
    """Sequential DoclingPdfParser decode (no render). Returns wall time in seconds."""
    from docling_parse.pdf_parser import DoclingPdfParser

    config = _decode_config()
    config.do_thread_safe = False  # no need for isolated QPDF per page
    content_config = _content_config(decode_options, materialization_options)

    parser = DoclingPdfParser(loglevel="fatal")

    t0 = time.perf_counter()
    for pdf_path, page_numbers in tqdm(
        pdf_schedule, desc="  sequential parse", unit="doc", leave=False
    ):
        try:
            doc = parser.load(
                str(pdf_path),
                lazy=True,
                decode_config=config,
                content_config=content_config,
            )
            if page_numbers is None:
                for _, _ in doc.iterate_pages():
                    pass
            else:
                for page_number in page_numbers:
                    _ = doc.get_page(page_number)
            doc.unload()
        except Exception as e:
            print(f"  sequential error on {pdf_path}: {e}")
    return time.perf_counter() - t0


def run_pypdfium_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pypdfium2 text extraction."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-parse", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    range(len(doc))
                    if page_numbers is None
                    else (page_number - 1 for page_number in page_numbers)
                )
                for i in pages:
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for rect_idx in range(text_page.count_rects()):
                            rect = text_page.get_rect(rect_idx)
                            _ = text_page.get_text_bounded(*rect)
                        text_page.close()
                        page.close()
                    except Exception as e:
                        print(f"  pypdfium2 page error on {pdf_path} page {i}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pypdfium2: {errors} errors")
    return time.perf_counter() - t0


def run_pypdfium_render(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pypdfium2: text extract + scale=2 render to PIL."""
    try:
        import pypdfium2 as pdfium  # type: ignore
    except ImportError as e:
        print(f"  pypdfium2 not available: {e}", file=sys.stderr)
        return float("nan")

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pypdfium2-render", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = pdfium.PdfDocument(str(pdf_path))
            except Exception as e:
                print(f"  pypdfium2 open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    range(len(doc))
                    if page_numbers is None
                    else (page_number - 1 for page_number in page_numbers)
                )
                for i in pages:
                    try:
                        page = doc[i]
                        text_page = page.get_textpage()
                        for rect_idx in range(text_page.count_rects()):
                            rect = text_page.get_rect(rect_idx)
                            _ = text_page.get_text_bounded(*rect)
                        text_page.close()
                        bitmap = page.render(scale=2)
                        _ = bitmap.to_pil()
                        bitmap.close()
                        page.close()
                    except Exception as e:
                        print(f"  pypdfium2 page error on {pdf_path} page {i}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pypdfium2: {errors} errors")
    return time.perf_counter() - t0


def run_pymupdf_parse(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pymupdf text extraction."""
    try:
        import fitz  # PyMuPDF
    except ImportError as e:
        print(f"  pymupdf not available: {e}", file=sys.stderr)
        return float("nan")

    # MuPDF writes "MuPDF error: ..." lines to stderr from the C layer;
    # silence them so perf output stays clean.
    try:
        fitz.TOOLS.mupdf_display_errors(False)
    except Exception:
        pass

    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pymupdf-parse", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    doc
                    if page_numbers is None
                    else (doc[page_number - 1] for page_number in page_numbers)
                )
                for page in pages:
                    try:
                        _ = page.get_text("text")
                    except Exception as e:
                        print(f"  pymupdf page error on {pdf_path}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pymupdf: {errors} errors")
    return time.perf_counter() - t0


def run_pymupdf_render(
    pdf_schedule: List[Tuple[Path, List[int] | None]], total_pages: int
) -> float:
    """Single-threaded pymupdf: text extract + scale=2 render to PIL."""
    try:
        import fitz  # PyMuPDF
    except ImportError as e:
        print(f"  pymupdf not available: {e}", file=sys.stderr)
        return float("nan")

    try:
        fitz.TOOLS.mupdf_display_errors(False)
    except Exception:
        pass

    matrix = fitz.Matrix(2, 2)
    t0 = time.perf_counter()
    errors = 0
    with tqdm(total=total_pages, desc="  pymupdf-render", unit="page") as pbar:
        for pdf_path, page_numbers in pdf_schedule:
            try:
                doc = fitz.open(str(pdf_path))
            except Exception as e:
                print(f"  pymupdf open error on {pdf_path}: {e}")
                errors += 1
                continue
            try:
                pages = (
                    doc
                    if page_numbers is None
                    else (doc[page_number - 1] for page_number in page_numbers)
                )
                for page in pages:
                    try:
                        _ = page.get_text("text")
                        pix = page.get_pixmap(matrix=matrix)
                        _ = pix.pil_image()
                    except Exception as e:
                        print(f"  pymupdf page error on {pdf_path}: {e}")
                        errors += 1
                    pbar.update(1)
            finally:
                try:
                    doc.close()
                except Exception:
                    pass
    if errors:
        print(f"  pymupdf: {errors} errors")
    return time.perf_counter() - t0


# Registry: 3rd-party single-threaded backends.
# Each entry maps a name to {"parse": fn, "render": fn} where each fn has
# signature (pdf_paths, total_pages) -> wall_time_seconds.
OTHER_BACKENDS = {
    "pypdfium2": {
        "parse": run_pypdfium_parse,
        "render": run_pypdfium_render,
    },
    "pymupdf": {
        "parse": run_pymupdf_parse,
        "render": run_pymupdf_render,
    },
}


def parse_other_arg(arg: str) -> List[str]:
    names = [n.strip() for n in arg.split(";") if n.strip()]
    unknown = [n for n in names if n not in OTHER_BACKENDS]
    if unknown:
        raise SystemExit(
            f"Unknown --other backend(s): {unknown}. "
            f"Choose from: {sorted(OTHER_BACKENDS)}"
        )
    return names


# -------- Comparison suite --------
#
# The scaling tables report wall time per *run*.  The comparison suite reports a
# distribution over *pages*, which is what `docs/performance_benchmarks.md`
# tabulates and the only shape from which median / p95 / p99 per page mean
# anything.
#
# docling-parse is measured once per requested thread count, because decoding
# pages in parallel is precisely what sets it apart: none of the third-party
# backends expose a thread-safe multi-page pipeline, so they are measured
# single-threaded and reported with `threads = 1`.
#
# Conventions applied to every row:
#   * `total time` is a wall clock around the whole corpus, including opening
#     documents.  This is the number that shows the benefit of threading.
#   * the per-page distribution is the *cost of one page*, so it stays roughly
#     flat as threads increase while `total time` drops.  For third-party
#     backends it is a wall-clock timer around each page's work (document
#     open/close excluded); for docling-parse it is the C++-reported page
#     timing, since under concurrency no wall-clock interval belongs to a
#     single page.
#   * `parse` means "extract text with position for every page", so each
#     backend uses its position-bearing extraction API, not a plain-text dump.
#   * `parse+render` additionally rasterises the page at `--scale`
#     (scale 1.0 = 72 dpi) and materialises it as a PIL image.

PER_PAGE_WALL = "wall clock per page"
PER_PAGE_INTERNAL = "C++ page timings"


@dataclass
class _PageOutput:
    """Slot a timed page body fills in with what it produced."""

    image_size: Tuple[int, int] | None = None


@dataclass
class BackendRun:
    backend: str
    task: str
    threads: int
    wall_s: float
    per_page_source: str
    samples: List[PageRow] = field(default_factory=list)
    doc_errors: int = 0

    @property
    def durations(self) -> List[float]:
        return [s.elapsed_s for s in self.samples if s.success]

    @property
    def page_errors(self) -> int:
        return sum(1 for s in self.samples if not s.success)

    def stats(self) -> Dict[str, float]:
        times = self.durations
        return {
            "pages": len(times),
            "total_s": self.wall_s,
            "mean_s": sum(times) / len(times) if times else 0.0,
            "median_s": percentile(times, 50),
            "p95_s": percentile(times, 95),
            "p99_s": percentile(times, 99),
        }


class _Collector:
    """Wall clock plus per-page timers for one comparison run."""

    def __init__(
        self,
        backend: str,
        task: str,
        total_pages: int,
        *,
        threads: int = 1,
        per_page_source: str = PER_PAGE_WALL,
    ):
        self.backend = backend
        self.task = task
        self.threads = threads
        self.per_page_source = per_page_source
        self.samples: List[PageRow] = []
        self.doc_errors = 0
        label = series_label(backend, task, threads)
        self._pbar = tqdm(
            total=total_pages, desc=f"  {label}", unit="page", leave=False
        )
        self._t0 = time.perf_counter()

    def add(
        self,
        doc_key: str,
        page_number: int,
        elapsed_s: float,
        success: bool,
        wall_gap_s: float | None = None,
        image_size: Tuple[int, int] | None = None,
        stage_timings: Dict[str, float] | None = None,
        error_message: str = "",
    ) -> None:
        width, height = image_size if image_size else (0, 0)
        self.samples.append(
            PageRow(
                backend=self.backend,
                task=self.task,
                threads=self.threads,
                doc_key=doc_key,
                page_number=page_number,
                success=success,
                elapsed_s=elapsed_s,
                wall_gap_s=elapsed_s if wall_gap_s is None else wall_gap_s,
                image_width=int(width),
                image_height=int(height),
                stage_timings=stage_timings or {},
                error_message=error_message,
            )
        )
        self._pbar.update(1)

    @contextlib.contextmanager
    def page(self, doc_key: str, page_number: int) -> Iterator[_PageOutput]:
        """Time one page; a raised exception is recorded as a failed page.

        The yielded slot lets the body report the rasterised image size, which
        is recorded alongside the timing.
        """
        out = _PageOutput()
        t0 = time.perf_counter()
        success = True
        error = ""
        try:
            yield out
        except Exception as e:
            success = False
            error = str(e)
            print(f"  {self.backend} page error on {doc_key} p{page_number}: {e}")
        finally:
            self.add(
                doc_key,
                page_number,
                time.perf_counter() - t0,
                success,
                image_size=out.image_size,
                error_message=error,
            )

    def document_failed(self, doc_key: str, error: Exception) -> None:
        self.doc_errors += 1
        print(f"  {self.backend} open error on {doc_key}: {error}")

    def finish(self) -> BackendRun:
        wall_s = time.perf_counter() - self._t0
        self._pbar.close()
        return BackendRun(
            backend=self.backend,
            task=self.task,
            threads=self.threads,
            wall_s=wall_s,
            per_page_source=self.per_page_source,
            samples=self.samples,
            doc_errors=self.doc_errors,
        )


def _page_indices(page_numbers: List[int] | None, num_pages: int) -> Iterable[int]:
    """Zero-based page indices to visit for one document."""
    if page_numbers is None:
        return range(num_pages)
    return [n - 1 for n in page_numbers if 1 <= n <= num_pages]


def cmp_docling(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    *,
    render: bool,
    scale: float,
    threads: int,
    max_concurrent_results: int,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    bytesio: bool = False,
    **_: object,
) -> BackendRun:
    """docling-parse via DoclingThreadedPdfParser at `threads` worker threads.

    The threaded parser is the only docling API with a render path, so both
    tasks go through it and thread counts stay comparable across tasks.  Loads
    are inside the wall clock, matching the third-party backends where opening
    a document is also part of the total.
    """
    from io import BytesIO

    from docling_parse.pdf_parsers import RenderConfig  # type: ignore[import]

    from docling_parse.pdf_parser import (
        DoclingThreadedPdfParser,
        ThreadedPdfParserConfig,
    )

    render_config = None
    if render:
        render_config = RenderConfig()
        render_config.scale = scale

    content_config = _content_config(decode_options, materialization_options)
    materialize_page = _materializes_page_data(materialization_options)

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            loglevel="fatal",
            threads=threads,
            max_concurrent_results=max_concurrent_results,
            render_config=render_config,
            page_content_config=content_config,
        ),
        decode_config=_decode_config(),
    )

    c = _Collector(
        "docling-parse",
        TASK_RENDER if render else TASK_PARSE,
        total_pages,
        threads=threads,
        per_page_source=PER_PAGE_INTERNAL,
    )
    for pdf_path, page_numbers in schedule:
        try:
            source = BytesIO(pdf_path.read_bytes()) if bytesio else str(pdf_path)
            parser.load(source, page_numbers=page_numbers)
        except Exception as e:
            c.document_failed(str(pdf_path), e)

    previous = time.perf_counter()
    for result in parser.iterate_results():
        image_size = None
        if result.success:
            if render:
                image_size = result.get_image().size
            if materialize_page:
                result.get_page()
        now = time.perf_counter()
        c.add(
            # DoclingThreadedPdfParser keys documents as "key=<path>"; strip the
            # prefix so samples join with the other backends on the plain path.
            result.doc_key.removeprefix("key="),
            result.page_number,
            result.timings.total_s,
            result.success,
            wall_gap_s=now - previous,
            image_size=image_size,
            stage_timings=_stage_timings(result),
            error_message=result.error_message,
        )
        previous = now
    return c.finish()


def cmp_pypdfium2(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    *,
    render: bool,
    scale: float,
    **_: object,
) -> BackendRun:
    """pypdfium2: text rects with bounding boxes, plus optional raster."""
    import pypdfium2 as pdfium  # type: ignore

    c = _Collector("pypdfium2", TASK_RENDER if render else TASK_PARSE, total_pages)
    for pdf_path, page_numbers in schedule:
        try:
            doc = pdfium.PdfDocument(str(pdf_path))
        except Exception as e:
            c.document_failed(str(pdf_path), e)
            continue
        try:
            for i in _page_indices(page_numbers, len(doc)):
                with c.page(str(pdf_path), i + 1) as out:
                    page = doc[i]
                    text_page = page.get_textpage()
                    for rect_idx in range(text_page.count_rects()):
                        rect = text_page.get_rect(rect_idx)
                        _ = text_page.get_text_bounded(*rect)
                    text_page.close()
                    if render:
                        bitmap = page.render(scale=scale)
                        out.image_size = bitmap.to_pil().size
                        bitmap.close()
                    page.close()
        finally:
            with contextlib.suppress(Exception):
                doc.close()
    return c.finish()


def cmp_pymupdf(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    *,
    render: bool,
    scale: float,
    **_: object,
) -> BackendRun:
    """PyMuPDF: `rawdict` extraction (char-level text + bbox), plus raster.

    `get_text("text")` is deliberately not used: it returns a plain string with
    no geometry, which would not be the same task as the other backends.
    """
    import fitz  # PyMuPDF

    # MuPDF writes "MuPDF error: ..." lines to stderr from the C layer;
    # silence them so perf output stays clean.
    with contextlib.suppress(Exception):
        fitz.TOOLS.mupdf_display_errors(False)

    matrix = fitz.Matrix(scale, scale)
    c = _Collector("pymupdf", TASK_RENDER if render else TASK_PARSE, total_pages)
    for pdf_path, page_numbers in schedule:
        try:
            doc = fitz.open(str(pdf_path))
        except Exception as e:
            c.document_failed(str(pdf_path), e)
            continue
        try:
            for i in _page_indices(page_numbers, doc.page_count):
                with c.page(str(pdf_path), i + 1) as out:
                    page = doc[i]
                    _ = page.get_text("rawdict")
                    if render:
                        pix = page.get_pixmap(matrix=matrix)
                        out.image_size = pix.pil_image().size
        finally:
            with contextlib.suppress(Exception):
                doc.close()
    return c.finish()


def cmp_pdfplumber(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    *,
    render: bool,
    scale: float,
    **_: object,
) -> BackendRun:
    """pdfplumber: `page.chars` (pdfminer.six under the hood), plus raster.

    pdfplumber has no rasteriser of its own; `to_image()` delegates to
    pypdfium2, so the render row measures pdfminer extraction plus a pdfium
    raster, which is how a pdfplumber user would actually obtain both.
    """
    import pdfplumber  # type: ignore

    c = _Collector("pdfplumber", TASK_RENDER if render else TASK_PARSE, total_pages)
    for pdf_path, page_numbers in schedule:
        try:
            pdf = pdfplumber.open(str(pdf_path))
        except Exception as e:
            c.document_failed(str(pdf_path), e)
            continue
        try:
            for i in _page_indices(page_numbers, len(pdf.pages)):
                page = pdf.pages[i]
                with c.page(str(pdf_path), i + 1) as out:
                    _ = page.chars
                    if render:
                        out.image_size = page.to_image(
                            resolution=72.0 * scale
                        ).original.size
                # pdfplumber caches every visited page, which turns a large
                # corpus into an out-of-memory run.
                with contextlib.suppress(Exception):
                    page.close()
        finally:
            with contextlib.suppress(Exception):
                pdf.close()
    return c.finish()


def cmp_pdfminer(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    **_: object,
) -> BackendRun:
    """pdfminer.six: layout analysis, walking every LTChar for its bbox.

    `extract_pages` analyses one page per `next()`, so the timer wraps the
    `next()` call plus the traversal of the resulting layout tree.
    """
    from pdfminer.high_level import extract_pages  # type: ignore
    from pdfminer.layout import LTChar, LTContainer  # type: ignore

    def _visit_chars(item) -> int:
        if isinstance(item, LTChar):
            _ = item.bbox
            return 1
        if isinstance(item, LTContainer):
            return sum(_visit_chars(child) for child in item)
        return 0

    c = _Collector("pdfminer.six", TASK_PARSE, total_pages)
    for pdf_path, page_numbers in schedule:
        selected = None if page_numbers is None else {n - 1 for n in page_numbers}
        try:
            pages = iter(extract_pages(str(pdf_path), page_numbers=selected))
        except Exception as e:
            c.document_failed(str(pdf_path), e)
            continue
        page_index = 0
        while True:
            t0 = time.perf_counter()
            try:
                lt_page = next(pages)
            except StopIteration:
                break
            except Exception as e:
                print(f"  pdfminer.six page error on {pdf_path}: {e}")
                c.add(str(pdf_path), page_index + 1, time.perf_counter() - t0, False)
                break
            success = True
            try:
                _visit_chars(lt_page)
            except Exception as e:
                success = False
                print(f"  pdfminer.six layout error on {pdf_path}: {e}")
            c.add(
                str(pdf_path),
                getattr(lt_page, "pageid", page_index + 1),
                time.perf_counter() - t0,
                success,
            )
            page_index += 1
    return c.finish()


def cmp_pypdf(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    **_: object,
) -> BackendRun:
    """pypdf: `extract_text` with a visitor that receives the text matrix.

    The bare `extract_text()` return value carries no geometry; `visitor_text`
    is the supported way to get a position per text chunk, so that is what the
    parse task uses.
    """
    import inspect

    from pypdf import PdfReader  # type: ignore
    from pypdf._page import PageObject  # type: ignore

    # The visitor argument has moved around across pypdf majors; if it is gone,
    # fall back to plain extraction and say so, rather than reporting a row for
    # a cheaper task than every other backend performed.
    use_visitor = (
        "visitor_text" in inspect.signature(PageObject.extract_text).parameters
    )
    if not use_visitor:
        print(
            "  pypdf: no visitor_text parameter in this version; falling back to "
            "extract_text() without positions (row is not task-comparable)"
        )

    c = _Collector("pypdf", TASK_PARSE, total_pages)
    for pdf_path, page_numbers in schedule:
        try:
            reader = PdfReader(str(pdf_path))
            num_pages = len(reader.pages)
        except Exception as e:
            c.document_failed(str(pdf_path), e)
            continue
        for i in _page_indices(page_numbers, num_pages):
            with c.page(str(pdf_path), i + 1):
                page = reader.pages[i]
                if use_visitor:
                    located: List[Tuple[str, float, float]] = []

                    def visitor(text, cm, tm, font_dict, font_size, sink=located):
                        if text.strip():
                            sink.append((text, tm[4], tm[5]))

                    page.extract_text(visitor_text=visitor)
                else:
                    _ = page.extract_text()
    return c.finish()


# Registry for the comparison suite: name -> (runner, supported tasks).
# Insertion order is the row order of the generated markdown table.
COMPARISON_BACKENDS: Dict[str, Tuple[object, Tuple[str, ...]]] = {
    "docling-parse": (cmp_docling, (TASK_PARSE, TASK_RENDER)),
    "pymupdf": (cmp_pymupdf, (TASK_PARSE, TASK_RENDER)),
    "pypdfium2": (cmp_pypdfium2, (TASK_PARSE, TASK_RENDER)),
    "pdfplumber": (cmp_pdfplumber, (TASK_PARSE, TASK_RENDER)),
    "pdfminer.six": (cmp_pdfminer, (TASK_PARSE,)),
    "pypdf": (cmp_pypdf, (TASK_PARSE,)),
}

# Only docling-parse has a thread-safe multi-page pipeline; every other backend
# is driven single-threaded and reported as such.
THREADED_COMPARISON_BACKENDS = {"docling-parse"}

# Bare `--compare` reproduces the script's long-standing default pairing:
# docling-parse measured against pypdfium2.
DEFAULT_COMPARE = "docling-parse;pypdfium2"

# Ground truth for the rendered canvas, most-preferred first. PDFium is the
# reference because it is the rasteriser three of the six packages ultimately
# rely on, so its page geometry is the one to agree with.
RENDER_SIZE_REFERENCE_ORDER = ("pypdfium2", "docling-parse", "pymupdf")

# Row order of the printed and generated tables: registry order, then threads.
_BACKEND_ORDER = {name: index for index, name in enumerate(COMPARISON_BACKENDS)}
_TASK_ORDER = {TASK_PARSE: 0, TASK_RENDER: 1}


def parse_compare_arg(arg: str) -> List[str]:
    """Parse `--compare`; "" disables the suite, "all" selects every backend."""
    value = arg.strip()
    if not value:
        return []
    if value.lower() == "all":
        return list(COMPARISON_BACKENDS)
    names = [n.strip() for n in value.split(";") if n.strip()]
    unknown = [n for n in names if n not in COMPARISON_BACKENDS]
    if unknown:
        raise SystemExit(
            f"Unknown --compare backend(s): {unknown}. "
            f"Choose from: {list(COMPARISON_BACKENDS)}, or 'all'."
        )
    return names


def run_comparison(
    schedule: List[Tuple[Path, List[int] | None]],
    total_pages: int,
    backends: List[str],
    tasks: List[str],
    thread_counts: List[int],
    *,
    scale: float,
    max_concurrent_results: int,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    bytesio: bool = False,
) -> List[BackendRun]:
    """Run every (backend, task, threads) combination once.

    The render-size reference runs first so its page geometry is established
    before anything is compared against it; the tables are sorted back into
    registry order afterwards.
    """
    runs: List[BackendRun] = []
    skipped: List[str] = []
    ordered = sorted(
        backends,
        key=lambda name: (
            RENDER_SIZE_REFERENCE_ORDER.index(name)
            if name in RENDER_SIZE_REFERENCE_ORDER
            else len(RENDER_SIZE_REFERENCE_ORDER)
        ),
    )
    for task in tasks:
        for name in ordered:
            runner, supported = COMPARISON_BACKENDS[name]
            if task not in supported:
                reason = f"{name} [{task}]: backend cannot rasterise"
                print(f"Skipping {reason}")
                skipped.append(reason)
                continue

            threads_for_backend = (
                thread_counts if name in THREADED_COMPARISON_BACKENDS else [1]
            )
            for threads in threads_for_backend:
                label = f"{name} [{task}]"
                if name in THREADED_COMPARISON_BACKENDS:
                    label += f" with {threads} thread(s)"
                print(f"Running {label} ...")
                try:
                    run = runner(  # type: ignore[operator]
                        schedule,
                        total_pages,
                        render=task == TASK_RENDER,
                        scale=scale,
                        threads=threads,
                        max_concurrent_results=max_concurrent_results,
                        decode_options=decode_options,
                        materialization_options=materialization_options,
                        bytesio=bytesio,
                    )
                except ImportError as e:
                    reason = (
                        f"{name} [{task}]: not installed ({e}). "
                        f"Install the perf group: uv sync --group perf"
                    )
                    print(f"  Skipping {reason}")
                    skipped.append(reason)
                    break
                print(
                    f"  {name} [{task}]: {run.wall_s:.3f}s wall, "
                    f"{len(run.durations)} pages timed, "
                    f"{run.page_errors} page errors, {run.doc_errors} doc errors"
                )
                runs.append(run)

    if skipped:
        # Missing rows are easy to overlook in a long run, so restate them
        # right before the tables are printed.
        print("\nSkipped runs (absent from the tables below):")
        for reason in skipped:
            print(f"  - {reason}")

    runs.sort(key=lambda r: (_TASK_ORDER[r.task], _BACKEND_ORDER[r.backend], r.threads))
    return runs


# -------- Comparison reporting --------


def _fmt_duration(seconds: float) -> str:
    """Plain seconds, so totals stay directly comparable across rows."""
    return f"{seconds:.2f}"


def _fmt_ms(seconds: float) -> str:
    return f"{seconds * 1000:.1f} ms"


def print_comparison_table(runs: List[BackendRun]) -> None:
    headers = [
        "python package",
        "task",
        "threads",
        "pages",
        "total time (s)",
        "avg/page",
        "median/page",
        "p95/page",
        "p99/page",
        "pages/sec",
        "per-page source",
    ]
    rows = []
    for run in runs:
        s = run.stats()
        rows.append(
            [
                run.backend,
                run.task,
                run.threads,
                int(s["pages"]),
                _fmt_duration(s["total_s"]),
                _fmt_ms(s["mean_s"]),
                _fmt_ms(s["median_s"]),
                _fmt_ms(s["p95_s"]),
                _fmt_ms(s["p99_s"]),
                f"{s['pages'] / s['total_s']:.1f}" if s["total_s"] > 0 else "n/a",
                run.per_page_source,
            ]
        )
    print()
    print("=== COMPARISON (total time is wall clock; quantiles are per page) ===")
    print(tabulate(rows, headers=headers))


def _run_label(run: BackendRun) -> str:
    if run.backend in THREADED_COMPARISON_BACKENDS:
        return f"{run.backend} ({run.threads}t)"
    return f"{run.backend} (1t)"


def _speedup_baselines(runs: List[BackendRun]) -> List[BackendRun]:
    """Reference runs every row is divided by: the single-threaded ones.

    That is docling-parse at threads=1 plus each third-party backend, which is
    what makes "how many threads until we overtake pypdfium2" readable straight
    off the table.
    """
    baselines = [
        run
        for run in runs
        if run.backend in THREADED_COMPARISON_BACKENDS and run.threads == 1
    ]
    baselines.extend(
        run for run in runs if run.backend not in THREADED_COMPARISON_BACKENDS
    )
    return baselines


def _speedup_rows(runs: List[BackendRun]) -> Tuple[List[str], List[List[str]]]:
    """Speedup grid for one task: `baseline_total / row_total` per cell."""
    baselines = _speedup_baselines(runs)
    headers = ["python package", "threads", "total time (s)", "pages/sec"]
    headers.extend(f"vs {_run_label(b)}" for b in baselines)

    rows: List[List[str]] = []
    for run in runs:
        s = run.stats()
        cells = [
            run.backend,
            str(run.threads),
            _fmt_duration(s["total_s"]),
            f"{s['pages'] / s['total_s']:.1f}" if s["total_s"] > 0 else "n/a",
        ]
        for baseline in baselines:
            if run.wall_s <= 0 or baseline.wall_s <= 0:
                cells.append("n/a")
            elif baseline is run:
                cells.append("1.00x")
            else:
                cells.append(f"{baseline.wall_s / run.wall_s:.2f}x")
        rows.append(cells)
    return headers, rows


def print_speedup_table(runs: List[BackendRun]) -> None:
    """Wall-time speedup of every run against each single-threaded reference."""
    for task in (TASK_PARSE, TASK_RENDER):
        task_runs = [run for run in runs if run.task == task]
        if not task_runs:
            continue
        headers, rows = _speedup_rows(task_runs)
        print()
        print(f"=== SPEEDUP — {task} (wall time; higher is faster than column) ===")
        print(tabulate(rows, headers=headers))


RENDER_SIZE_TOLERANCE_PX = 2


def _render_sizes(run: BackendRun) -> Dict[Tuple[str, int], Tuple[int, int]]:
    return {
        (s.doc_key, s.page_number): (s.image_width, s.image_height)
        for s in run.samples
        if s.success and s.image_width and s.image_height
    }


RENDER_SIZE_HEADERS = [
    "python package",
    "pages compared",
    "within tolerance",
    "max delta (px)",
    "worst page",
    "pages w/o reference",
]


def render_size_check(
    runs: List[BackendRun],
) -> Tuple[str | None, List[List[object]], str]:
    """Compare the rasterised canvas each backend produced, page by page.

    A timing comparison only means something if every backend rendered the same
    canvas, so each package is checked against the reference (PDFium) on the
    pages both of them produced.  Thread count cannot change the canvas, so
    only the first run per package is compared.

    Returns `(reference_name, rows, note)`; `reference_name` is None when the
    check could not run, and `note` says why.
    """
    render_runs = [run for run in runs if run.task == TASK_RENDER]
    if not render_runs:
        return None, [], ""

    # One run per package; thread count does not affect the rasterised size.
    per_backend: Dict[str, BackendRun] = {}
    for run in render_runs:
        per_backend.setdefault(run.backend, run)

    reference_name = next(
        (name for name in RENDER_SIZE_REFERENCE_ORDER if name in per_backend), None
    )
    if reference_name is None:
        return (
            None,
            [],
            "Render size check skipped: none of "
            f"{list(RENDER_SIZE_REFERENCE_ORDER)} was part of the run",
        )

    ref_sizes = _render_sizes(per_backend[reference_name])
    if not ref_sizes:
        return (
            None,
            [],
            f"Render size check skipped: {reference_name} reported no image sizes",
        )

    rows: List[List[object]] = []
    for name, run in per_backend.items():
        if name == reference_name:
            continue
        compared = 0
        within = 0
        worst = 0
        worst_page = ""
        missing = 0
        for s in run.samples:
            if not s.success or not s.image_width:
                continue
            ref = ref_sizes.get((s.doc_key, s.page_number))
            if ref is None:
                missing += 1
                continue
            compared += 1
            delta = max(abs(s.image_width - ref[0]), abs(s.image_height - ref[1]))
            if delta <= RENDER_SIZE_TOLERANCE_PX:
                within += 1
            if delta > worst:
                worst = delta
                worst_page = (
                    f"{Path(s.doc_key).name} p{s.page_number} "
                    f"({s.image_width}x{s.image_height} vs {ref[0]}x{ref[1]})"
                )
        rows.append(
            [
                name,
                compared,
                f"{100.0 * within / compared:.1f}%" if compared else "n/a",
                worst,
                worst_page or "-",
                missing,
            ]
        )

    return reference_name, rows, ""


def print_render_size_check(runs: List[BackendRun]) -> None:
    reference_name, rows, note = render_size_check(runs)
    if reference_name is None:
        if note:
            print(f"\n{note}")
        return
    print()
    print(
        f"=== RENDER SIZE CHECK vs {reference_name} "
        f"(tolerance {RENDER_SIZE_TOLERANCE_PX} px) ==="
    )
    print(tabulate(rows, headers=RENDER_SIZE_HEADERS))


def _md_table(headers: List[str], rows: List[List[object]]) -> str:
    return tabulate(rows, headers=headers, tablefmt="github")


def _parameter_table(rows: List[List[object]]) -> str:
    return _md_table(["parameter", "value"], rows)


def write_markdown_report(
    path: Path,
    runs: List[BackendRun],
    *,
    system_info: Dict[str, object],
    dataset_info: Dict[str, object],
    settings: Dict[str, object],
    parameter_tables: List[Tuple[str, List[List[object]]]],
    command: str,
) -> None:
    """Write a self-contained report: how it was run, on what, and the results.

    The command and every config table are included so a published number can
    be traced back to an exact invocation without consulting the terminal
    scrollback it came from.
    """
    hardware = format_hardware_label(system_info)
    dataset = format_dataset_label(dataset_info)

    lines = [
        "<!-- generated by perf/run_scaling.py -->",
        "",
        f"# Benchmark — {dataset_info['name']} on {system_info['cpu']}",
        "",
        f"Generated: {datetime.now().isoformat(timespec='seconds')}",
        "",
        "## Command",
        "",
        "```sh",
        command,
        "```",
        "",
        "## Benchmark",
        "",
        _parameter_table(benchmark_rows(dataset_info, settings)),
        "",
        "## System",
        "",
        _parameter_table(system_rows(system_info)),
        "",
    ]

    for title, rows in parameter_tables:
        lines.extend([f"## {title}", "", _parameter_table(rows), ""])

    lines.extend(["## Parsing and rendering performance", ""])
    result_headers = [
        "System hardware",
        "dataset",
        "Python package",
        "Task",
        "threads",
        "total time (s)",
        "average time/page",
        "median time/page",
        "95 quantile time/page",
        "99 quantile time/page",
    ]
    result_rows: List[List[object]] = []
    for run in runs:
        s = run.stats()
        result_rows.append(
            [
                hardware,
                dataset,
                run.backend,
                f"`{run.task}`",
                run.threads,
                _fmt_duration(s["total_s"]),
                _fmt_ms(s["mean_s"]),
                _fmt_ms(s["median_s"]),
                _fmt_ms(s["p95_s"]),
                _fmt_ms(s["p99_s"]),
            ]
        )
    lines.extend([_md_table(result_headers, result_rows), ""])

    lines.extend(["## Thread scaling and speedup", ""])
    for task in (TASK_PARSE, TASK_RENDER):
        task_runs = [run for run in runs if run.task == task]
        if not task_runs:
            continue
        headers, rows = _speedup_rows(task_runs)
        lines.extend([f"Task: `{task}`", "", _md_table(headers, list(rows)), ""])

    reference_name, size_rows, note = render_size_check(runs)
    if reference_name is not None:
        lines.extend(
            [
                "## Render size check",
                "",
                f"Rasterised page size versus `{reference_name}`, tolerance "
                f"{RENDER_SIZE_TOLERANCE_PX} px.",
                "",
                _md_table(RENDER_SIZE_HEADERS, size_rows),
                "",
            ]
        )
    elif note:
        lines.extend(["## Render size check", "", note, ""])

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWrote markdown report to {path}")


def write_pages_csv(path: Path, runs: List[BackendRun]) -> None:
    """One row per (backend, task, threads, document, page)."""
    rows = [sample for run in runs for sample in run.samples]
    write_page_rows(path, rows)
    print(f"Wrote {len(rows)} per-page timings to {path}")


# -------- Threaded run --------


def run_threaded(
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    num_threads: int,
    max_concurrent_results: int,
    total_pages: int,
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    bytesio: bool = False,
) -> BackendRun:
    """Run DoclingThreadedPdfParser; render=True enables rasterisation.

    This is `cmp_docling` under a scaling-sweep name: the two used to be
    separate near-identical loops, which meant the sweep and the comparison
    could silently drift apart.
    """
    run = cmp_docling(
        pdf_schedule,
        total_pages,
        render=render,
        scale=scale,
        threads=num_threads,
        max_concurrent_results=max_concurrent_results,
        decode_options=decode_options,
        materialization_options=materialization_options,
        bytesio=bytesio,
    )
    if run.page_errors:
        print(f"  threads={num_threads}: {run.page_errors} page errors")
    return run


# -------- Reporting --------


def _isnan(x: float) -> bool:
    return x != x


def _fmt_speedup(s: float) -> str:
    return "n/a" if _isnan(s) else f"{s:.2f}x"


def _print_table(
    title: str,
    baselines: List[Tuple[str, float]],
    threaded_results: List[Tuple[int, float]],
    total_pages: int,
) -> None:
    """Print one unified table.

    `baselines` is a list of (label, wall_time) for non-threaded reference
    runs (sequential docling, plus selected 3rd-party backends).
    `threaded_results` is a list of (num_threads, wall_time) for the docling
    threaded scaling sweep.

    Columns: backend, threads, wall_time, vs threaded(1), one `vs <baseline>`
    column per baseline (sequential docling and each selected `--other`),
    then pages/sec and ms/page.  All `vs X` values are `X_time / row_time`,
    so higher means the row is faster than X.
    """
    threaded_1 = threaded_results[0][1] if threaded_results else float("nan")

    headers = ["backend", "threads", "wall_time (s)", "vs threaded(1)"]
    for label, _ in baselines:
        headers.append(f"vs {label}")
    headers.extend(["pages/sec", "ms/page"])

    n_vs_baseline = len(baselines)

    def _row(name: str, threads, t: float) -> List[str]:
        if _isnan(t):
            return (
                [name, str(threads), "n/a", "n/a"]
                + ["n/a"] * n_vs_baseline
                + ["n/a", "n/a"]
            )
        cells: List[str] = [name, str(threads), f"{t:.3f}"]
        vs_t1 = threaded_1 / t if t > 0 and not _isnan(threaded_1) else float("nan")
        cells.append(_fmt_speedup(vs_t1))
        for _, bt in baselines:
            vs_b = bt / t if t > 0 and not _isnan(bt) else float("nan")
            cells.append(_fmt_speedup(vs_b))
        cells.append(f"{total_pages / t:.1f}" if t > 0 else "n/a")
        cells.append(
            f"{1000.0 * t / total_pages:.2f}" if total_pages > 0 and t > 0 else "n/a"
        )
        return cells

    rows: List[List[str]] = []
    for label, t in baselines:
        rows.append(_row(label, "-", t))
    for n, t in threaded_results:
        rows.append(_row("docling threaded", n, t))

    print()
    print(f"=== {title} ===")
    print(tabulate(rows, headers=headers))


# -------- Mode runner --------


def _run_one_mode(
    pdf_schedule: List[Tuple[Path, List[int] | None]],
    thread_counts: List[int],
    max_concurrent_results: int,
    total_pages: int,
    other_backends: List[str],
    *,
    render: bool,
    scale: float,
    decode_options: dict[str, bool],
    materialization_options: dict[str, bool],
    bytesio: bool,
    collected_runs: List[BackendRun],
) -> Tuple[List[Tuple[str, float]], List[Tuple[int, float]]]:
    baselines: List[Tuple[str, float]] = []

    # Sequential docling baseline is only meaningful for parse mode
    # (DoclingPdfParser has no rendering path).
    if not render:
        print("Running sequential (DoclingPdfParser) ...")
        t = run_sequential_parse(
            pdf_schedule,
            decode_options,
            materialization_options,
        )
        print(f"  sequential: {t:.3f}s")
        baselines.append(("sequential docling (1t)", t))
        print()

    stage = "render" if render else "parse"
    for name in other_backends:
        fn = OTHER_BACKENDS[name][stage]
        print(f"Running {name} {stage} reference (1 thread) ...")
        t = fn(pdf_schedule, total_pages)
        print(f"  {name}: {t:.3f}s")
        baselines.append((f"{name} (1t)", t))
        print()

    threaded_results: List[Tuple[int, float]] = []
    stage_label = "renderer" if render else "parser"
    for n in thread_counts:
        print(f"Running threaded {stage_label} with {n} threads ...")
        run = run_threaded(
            pdf_schedule,
            num_threads=n,
            max_concurrent_results=max_concurrent_results,
            total_pages=total_pages,
            render=render,
            scale=scale,
            decode_options=decode_options,
            materialization_options=materialization_options,
            bytesio=bytesio,
        )
        collected_runs.append(run)
        threaded_results.append((n, run.wall_s))
        print(f"  threads={n}: {run.wall_s:.3f}s")

    return baselines, threaded_results


# -------- Main --------


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Thread-scaling benchmark for docling-parse (parse and/or render)"
    )
    ap.add_argument(
        "input",
        nargs="?",
        default=DEFAULT_HF_REPO_ID,
        help=(
            "Local PDF file/directory, or a Hugging Face dataset repo-id whose "
            f"`{HF_PDF_SUBDIR}/` subfolder contains the PDFs. "
            f"Default: {DEFAULT_HF_REPO_ID}"
        ),
    )
    ap.add_argument(
        "--mode",
        choices=["parse", "render", "both"],
        default="render",
        help="Benchmark stage: parse (decode-only), render (decode+raster), or both (default: render)",
    )
    ap.add_argument(
        "--recursive",
        "-r",
        action="store_true",
        help="Recurse into subdirectories (local paths only; HF downloads always recurse)",
    )
    ap.add_argument(
        "--max-pages",
        "-l",
        type=int,
        default=None,
        help="Maximum number of pages to process across all input PDFs",
    )
    ap.add_argument(
        "--max-concurrent-results",
        type=int,
        default=64,
        help="Max buffered results for the threaded parser/renderer (default: 64)",
    )
    ap.add_argument(
        "--threads",
        type=str,
        default="1,2,4,8,12,16",
        help="Comma-separated list of thread counts to test (default: 1,2,4,8,12,16)",
    )
    ap.add_argument(
        "--scale",
        type=float,
        default=1.0,
        help="Render scale for rendering (default: 1.0; render/both modes only)",
    )
    _add_bool_value_arg(
        ap,
        "keep-char-cells",
        default=True,
        help="Populate character cells and emit text render instructions",
    )
    _add_bool_value_arg(
        ap,
        "create-word-cells",
        default=False,
        help="Create word cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "create-line-cells",
        default=False,
        help="Create line cells during decoding",
    )
    _add_bool_value_arg(
        ap,
        "keep-shapes",
        default=False,
        help="Keep vector shape cells",
    )
    _add_bool_value_arg(
        ap,
        "keep-bitmaps",
        default=False,
        help="Keep bitmap resources/cells",
    )
    _add_bool_value_arg(
        ap,
        "materialize-char-cells",
        default=False,
        help="Materialize character cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-word-cells",
        default=False,
        help="Materialize word cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-line-cells",
        default=True,
        help="Materialize line cells into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-shapes",
        default=False,
        help="Materialize vector shapes into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmaps",
        default=True,
        help="Materialize bitmap locations into SegmentedPdfPage",
    )
    _add_bool_value_arg(
        ap,
        "materialize-bitmap-bytes",
        default=False,
        help="Materialize bitmap image bytes when bitmap locations are materialized",
    )
    ap.add_argument(
        "--other",
        type=str,
        default="pypdfium2",
        help=(
            "Semicolon-separated 3rd-party single-threaded backends to run as "
            f"reference baselines. Available: {';'.join(sorted(OTHER_BACKENDS))}. "
            'Default: "pypdfium2". Use "" to skip.'
        ),
    )
    ap.add_argument(
        "--bytesio",
        action="store_true",
        help="(docling-parse only) Read PDFs into memory and load them as BytesIO",
    )
    ap.add_argument(
        "--compare",
        type=str,
        nargs="?",
        const=DEFAULT_COMPARE,
        default="",
        help=(
            "Run the per-page comparison suite instead of only the scaling "
            "tables. Bare --compare uses "
            f'"{DEFAULT_COMPARE}"; pass a semicolon-separated list from '
            f'{list(COMPARISON_BACKENDS)} or "all". docling-parse is run once '
            "per --threads value; every other backend is single-threaded. "
            "Omit the flag entirely to keep the previous behaviour."
        ),
    )
    ap.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=(
            "Directory for the markdown report and the per-page CSV; created if "
            "missing. Both are named <cpu>_<dataset>_<mode>, so runs on "
            f"different machines never collide. Default: {DEFAULT_OUTPUT_DIR}"
        ),
    )

    args = ap.parse_args(argv)

    # Recorded verbatim in the report so a published number can be traced back
    # to the invocation that produced it.
    command = shlex.join(["python", *sys.argv])

    # Validate CLI args before doing any I/O (HF download, page counting).
    thread_counts = [int(x.strip()) for x in args.threads.split(",")]
    other_backends = parse_other_arg(args.other)
    compare_backends = parse_compare_arg(args.compare)
    decode_options = _decode_options_from_args(args)
    materialization_options = _materialization_options_from_args(args)

    system_info = collect_system_info()

    pdfs, dataset_info = resolve_pdf_inputs(args.input, recursive=args.recursive)
    if not pdfs:
        print(f"No PDFs found for input: {args.input}", file=sys.stderr)
        return 2

    pdf_schedule, total_pages = apply_max_pages(pdfs, args.max_pages)
    if not pdf_schedule or total_pages <= 0:
        print("No pages selected for benchmarking", file=sys.stderr)
        return 2

    dataset_info["documents"] = len(pdf_schedule)
    dataset_info["pages"] = total_pages

    settings: Dict[str, object] = {
        "mode": args.mode,
        "thread counts": thread_counts,
        "max concurrent results": args.max_concurrent_results,
        "other backends": other_backends or "(none)",
        "comparison suite": compare_backends or "(off)",
        "max pages": args.max_pages if args.max_pages else "(all)",
        "bytesio": args.bytesio,
    }
    if args.mode in ("render", "both"):
        settings["render scale"] = args.scale

    parameter_tables = config_tables(
        render=args.mode in ("render", "both"),
        scale=args.scale,
        decode_options=decode_options,
        materialization_options=materialization_options,
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    basename = output_basename(system_info, dataset_info, args.mode)
    markdown_path = args.output_dir / f"{basename}.md"
    pages_csv_path = args.output_dir / f"{basename}.csv"

    print("Benchmark:")
    print(
        tabulate(benchmark_rows(dataset_info, settings), headers=["parameter", "value"])
    )
    print()
    print("System:")
    print(tabulate(system_rows(system_info), headers=["parameter", "value"]))
    print()
    print_parameter_tables(parameter_tables)
    print(f"Output directory: {args.output_dir}")
    if compare_backends:
        # The markdown report is a rendering of the comparison tables, so the
        # scaling sweep writes the per-page CSV only.
        print(f"  markdown: {markdown_path}")
    print(f"  per-page: {pages_csv_path}")
    print()

    if compare_backends:
        tasks = []
        if args.mode in ("parse", "both"):
            tasks.append(TASK_PARSE)
        if args.mode in ("render", "both"):
            tasks.append(TASK_RENDER)

        print("\n##### COMPARISON SUITE #####")
        runs = run_comparison(
            pdf_schedule,
            total_pages,
            compare_backends,
            tasks,
            thread_counts,
            scale=args.scale,
            max_concurrent_results=args.max_concurrent_results,
            decode_options=decode_options,
            materialization_options=materialization_options,
            bytesio=args.bytesio,
        )
        print_comparison_table(runs)
        print_speedup_table(runs)
        print_render_size_check(runs)

        write_markdown_report(
            markdown_path,
            runs,
            system_info=system_info,
            dataset_info=dataset_info,
            settings=settings,
            parameter_tables=parameter_tables,
            command=command,
        )
        write_pages_csv(pages_csv_path, runs)
        return 0

    collected_runs: List[BackendRun] = []
    modes_to_run = ["parse", "render"] if args.mode == "both" else [args.mode]
    for m in modes_to_run:
        render = m == "render"
        title = "RENDER (decode + rasterise)" if render else "PARSE (decode only)"
        print(f"\n##### {title} #####")
        baselines, threaded_results = _run_one_mode(
            pdf_schedule,
            thread_counts,
            args.max_concurrent_results,
            total_pages,
            other_backends,
            render=render,
            scale=args.scale,
            decode_options=decode_options,
            materialization_options=materialization_options,
            bytesio=args.bytesio,
            collected_runs=collected_runs,
        )
        _print_table(title, baselines, threaded_results, total_pages)

    write_pages_csv(pages_csv_path, collected_runs)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
