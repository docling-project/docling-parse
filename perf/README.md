Perf tools for page-level parsing benchmarking.

Usage
- Install extras for optional parsers (not part of main package):
  - pip: `pip install .[perf-tools]`
  - uv (already configured): `uv sync --group perf-test`
- Run on a file or directory:
  - `python perf/run_perf.py ./docs/sample.pdf`
  - `python perf/run_perf.py ./dataset --recursive -p pdfplumber`

CLI
- `input`: PDF file or directory of PDFs.
- `--parser|-p`: one of `docling` (default), `pdfplumber`, `pypdfium2` (alias: `pypdfium`), `pymupdf`.
- `--recursive|-r`: recurse when input is a directory.
- `--output|-o`: output CSV path (default under `perf/results`).

CSV columns
- `filename,page_number,elapsed_sec,success,error`

Statistics
- Prints totals, avg sec/page, min/max, and percentiles (p50/p90/p95/p99) after the run.

