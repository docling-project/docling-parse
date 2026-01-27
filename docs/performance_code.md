# Performance Benchmarking Guide

This page explains how to benchmark page‑level PDF parsing with the `perf/run_perf.py` script. It supports multiple backends and produces a CSV plus summary stats for quick comparison.

## Prerequisites
- Python 3.9+
- Install the project in your environment. For optional parsers, install the perf extras:
  - pip: `pip install .[perf-tools]`
  - uv: `uv sync --group perf-test`

Optional parsers (pdfplumber, pypdfium2, pymupdf) are only needed when you select them via `-p`.

## Script Overview
- Entry point: `python perf/run_perf.py <input> [options]`
- Input can be a single PDF file or a directory containing PDFs.
- Output is a CSV file with one row per page and a printed summary.

CSV columns:
- `filename,page_number,elapsed_sec,success,error`

Summary includes overall totals and per‑document tables with mean/median/min/max and p50/p90/p95/p99 percentiles.

## Parser Backends
Use `--parser/-p` to select the backend. Available options:
- `docling-mode=typed` (default)
- `docling-mode=json`
- `pdfplumber`
- `pypdfium2` (alias: `pypdfium`)
- `pymupdf`

Docling modes correspond to `CONVERSION_MODE` used by the library:
- `docling-mode=typed`: uses the typed, zero‑copy bindings.
- `docling-mode=json`: uses the JSON serialization pipeline.

## Common Examples
- Single file with default (typed) docling mode:
  - `python perf/run_perf.py ./docs/sample.pdf`

- Directory, recurse, typed mode:
  - `python perf/run_perf.py ./dataset -r -p docling-mode=typed`

- Directory, recurse, JSON mode:
  - `python perf/run_perf.py ./dataset -r -p docling-mode=json`

- Compare non‑docling backends:
  - `python perf/run_perf.py ./dataset -r -p pdfplumber`
  - `python perf/run_perf.py ./dataset -r -p pypdfium2`
  - `python perf/run_perf.py ./dataset -r -p pymupdf`

## Output Location
By default, results are written to:
- `perf/results/perf_<parser>_<timestamp>.csv`

Customize output path with `--output/-o`:
- `python perf/run_perf.py ./dataset -r -p docling-mode=typed -o perf/results/typed.csv`
- `python perf/run_perf.py ./dataset -r -p docling-mode=json  -o perf/results/json.csv`

## Tips for Fair Comparisons
- Run typed vs JSON in separate invocations and compare CSVs and printed summaries.
- Pin the same `--recursive` and input set for both runs.
- Consider two passes:
  - Cold run: after a reboot or clearing OS caches (if feasible).
  - Warm run: repeat the same command to observe cache effects.
- Avoid other heavy workloads while benchmarking.
- Record environment details (CPU, RAM, OS, Python, library versions).

## Interpreting Results
- `elapsed_sec` measures wall‑clock seconds per page parse.
- The summary shows overall averages and percentiles across all successful pages.
- The per‑document table helps identify outliers at the file level.

## Troubleshooting
- “No PDFs found”: verify the input path and file extensions (`.pdf`). Use `-r` for directories with nested PDFs.
- Import errors for optional parsers: install extras (see Prerequisites) or switch `-p` to another backend.
- Permission errors writing CSV: pass `-o` to a writable location.

## Reproducible Runs (uv)
If you use `uv`, sync the perf group and run:
- `uv run python perf/run_perf.py ./dataset -r -p docling-mode=typed`
- `uv run python perf/run_perf.py ./dataset -r -p docling-mode=json`

This ensures a consistent environment for fair, repeatable measurements.
