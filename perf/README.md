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

Visualization
- `python perf/run_eval.py perf/results` creates plots under `perf/viz`:
  - Per-parser page-time histograms (log-log)
  - Superposed histogram across parsers
  - Stacked histograms with common x-axis
  - Per-document scatter (pages vs total time) with linear fit
  - Pairwise hexbin plots of per-page times across parsers

Analysis
- `python perf/run_analysis.py <perf_csv> --top 20 --mode typed` extracts detailed stage timings
  for the slowest pages to help identify bottlenecks. Writes `perf/results/analysis_<ts>.csv` by default.
