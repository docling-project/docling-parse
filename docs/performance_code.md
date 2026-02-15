# Performance Benchmarking Guide

This page explains how to benchmark page-level PDF parsing with the `perf/run_perf.py` script. It supports multiple backends — including a multi-threaded mode — and produces a CSV plus summary stats for quick comparison.

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
- For docling backends, additional columns with per-step timing breakdowns and percentages are appended.

Summary includes overall totals and per-document tables with mean/median/min/max and p50/p90/p95/p99 percentiles.

## Parser Backends
Use `--parser/-p` to select the backend. Available options:
- `docling` (default) — sequential, one page at a time
- `docling-threaded` — parallel, multi-threaded with backpressure
- `pdfplumber`
- `pypdfium2` (alias: `pypdfium`)
- `pymupdf`

## CLI Reference

```
python perf/run_perf.py <input> [options]

positional arguments:
  input                      Path to a PDF file or directory of PDFs

options:
  --parser, -p PARSER        Parser backend (default: docling)
  --recursive, -r            Recurse into subdirectories
  --output, -o PATH          Output CSV path (default: perf/results/perf_<parser>_<timestamp>.csv)
  --limit, -l N              Maximum number of documents to process
  --bytesio                  (docling only) Load PDFs via BytesIO instead of file path
  --threads, -t N            (docling-threaded only) Number of worker threads (default: 4)
  --max-concurrent-results N (docling-threaded only) Max buffered results before workers pause (default: 64)
```

## Common Examples

### Sequential (single-threaded) docling parsing

```sh
# Single file
python perf/run_perf.py ./docs/sample.pdf

# Directory, recursive
python perf/run_perf.py ./dataset -r -p docling
```

### Parallel (multi-threaded) docling parsing

```sh
# 4 threads (default), up to 64 buffered results
python perf/run_perf.py ./dataset -r -p docling-threaded

# 8 threads, tighter backpressure
python perf/run_perf.py ./dataset -r -p docling-threaded --threads 8 --max-concurrent-results 32

# Single thread (useful as a baseline to measure thread overhead)
python perf/run_perf.py ./dataset -r -p docling-threaded --threads 1
```

### Compare with non-docling backends

```sh
python perf/run_perf.py ./dataset -r -p pdfplumber
python perf/run_perf.py ./dataset -r -p pypdfium2
python perf/run_perf.py ./dataset -r -p pymupdf
```

## Output Location
By default, results are written to:
- `perf/results/perf_<parser>_<timestamp>.csv`

Customize output path with `--output/-o`:
```sh
python perf/run_perf.py ./dataset -r -p docling          -o perf/results/sequential.csv
python perf/run_perf.py ./dataset -r -p docling-threaded  -o perf/results/parallel_4t.csv
```

## Tips for Fair Comparisons
- Run sequential vs threaded in separate invocations and compare CSVs and printed summaries.
- Pin the same `--recursive` and input set for both runs.
- For `docling-threaded`, note that `elapsed_sec` per row measures the *wait time* for each result (not CPU time). The total wall time printed at the end is the true parallel throughput measure.
- Consider two passes:
  - Cold run: after a reboot or clearing OS caches (if feasible).
  - Warm run: repeat the same command to observe cache effects.
- Avoid other heavy workloads while benchmarking.
- Record environment details (CPU, RAM, OS, Python, library versions).

## Interpreting Results

### Sequential (`docling`)
- `elapsed_sec` measures wall-clock seconds per page parse.

### Parallel (`docling-threaded`)
- `elapsed_sec` per row is the time spent waiting for that specific result (includes queue wait time).
- The **total wall time** printed at the end is the key metric — it reflects actual parallel throughput.
- Compare `total wall time` of `docling-threaded` with `time_total_sec` of sequential `docling` to see the speedup.

### General
- The summary shows overall averages and percentiles across all successful pages.
- The per-document table helps identify outliers at the file level.
- For docling backends, a timing breakdown table shows average time and percentage for each internal parsing step (resource decoding, content parsing, etc.).

## Troubleshooting
- "No PDFs found": verify the input path and file extensions (`.pdf`). Use `-r` for directories with nested PDFs.
- Import errors for optional parsers: install extras (see Prerequisites) or switch `-p` to another backend.
- Permission errors writing CSV: pass `-o` to a writable location.

## Reproducible Runs (uv)
If you use `uv`, sync the perf group and run:
```sh
uv run python perf/run_perf.py ./dataset -r -p docling
uv run python perf/run_perf.py ./dataset -r -p docling-threaded --threads 4
```

This ensures a consistent environment for fair, repeatable measurements.
