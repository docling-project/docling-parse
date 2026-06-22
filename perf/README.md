# Performance tools

`docling-parse` ships two benchmark entry points:

- `perf/run_perf.py`: one-shot per-page benchmarking with CSV output
- `perf/run_scaling.py`: threaded scaling and pages/sec sweeps for parse and render

## Install

Core docling runs work with the normal project install. Optional third-party
baselines need the perf extras:

```sh
uv sync --group perf-test
```

or:

```sh
pip install .[perf-tools]
```

## `run_perf.py`

Benchmarks a single backend and writes one CSV row per page:

```sh
python perf/run_perf.py ./dataset -r -p docling
python perf/run_perf.py ./dataset -r -p docling-threaded --threads 8
python perf/run_perf.py ./dataset -r -p pypdfium2
```

Backends:

- `docling`
- `docling-threaded`
- `pdfplumber`
- `pypdfium2` (`pypdfium` alias)
- `pymupdf`

Useful flags:

- `--recursive/-r`: recurse into directories
- `--output/-o`: choose CSV path
- `--limit/-l`: cap number of input documents
- `--bytesio`: sequential `docling` only
- `--threads/-t`: threaded docling only
- `--max-concurrent-results`: threaded docling only

Output:

- main CSV: `perf/results/perf_<parser>_<timestamp>.csv`
- per-document CSV: `perf/results/perf_<parser>_<timestamp>_per_doc.csv`
- terminal summary with totals, percentiles, and timing breakdowns for docling runs

Note: for `docling-threaded`, each row's `elapsed_sec` is only the wait time to
receive that result. Use the printed total wall time for throughput comparisons.

## `run_scaling.py`

Runs threaded docling at multiple thread counts and reports pages/sec plus
speedup versus baselines.

```sh
python perf/run_scaling.py ./dataset --mode parse
python perf/run_scaling.py ./dataset --mode render --threads 1,2,4,8,12,16
python perf/run_scaling.py --mode both --other "pypdfium2;pymupdf"
```

Inputs:

- local PDF file
- local directory of PDFs
- Hugging Face dataset repo id whose `pdf/` subdirectory contains PDFs

Modes:

- `parse`: decode only
- `render`: decode plus raster render
- `both`: run both tables

Important flags:

- `--max-pages/-l`: exact total page cap across the input set
- `--max-concurrent-results`: threaded backpressure limit
- `--scale`: render scale for render mode
- `--other`: semicolon-separated single-threaded reference backends
- `--enable-timing`: write one timing row per threaded page result
- `--timing-csv`: output path for the timing CSV

The scaling script now reflects the v7 config split:

- decode-stage booleans: `--keep-char-cells`, `--create-word-cells`, `--create-line-cells`, `--keep-shapes`, `--keep-bitmaps`
- materialization booleans: `--materialize-char-cells`, `--materialize-word-cells`, `--materialize-line-cells`, `--materialize-shapes`, `--materialize-bitmaps`, `--materialize-bitmap-bytes`

Those flags are compiled into:

- `DecodeConfig` for compute tuning
- `ContentConfig` for `ContentLevel.SKIP`, `COMPUTE`, and `COMPUTE_AND_MATERIALIZE`

## Timing visualization

`run_scaling.py --enable-timing` writes a CSV that
`perf/run_scaling_visualization.py` can plot:

```sh
python perf/run_scaling_visualization.py timing-2026-06-22-12-00-00.csv
python perf/run_scaling_visualization.py timing.csv --mode render --bins 80
```

## Slow-page analysis

`perf/run_analysis.py` replays the slowest pages from a `run_perf.py` CSV and
extracts detailed decode timings:

```sh
python perf/run_analysis.py perf/results/perf_docling_20260622-120000.csv --top 25
python perf/run_analysis.py perf/results/perf_docling_20260622-120000.csv --nth 7
```
