# Performance benchmarking

This page covers the current benchmark tooling in `docling-parse` after the v7
config split.

Use:

- `perf/run_perf.py` for per-page CSV benchmarking
- `perf/run_scaling.py` for threaded scaling, pages/sec, and render sweeps

## Prerequisites

- Python 3.9+
- project installed in your environment
- perf extras only if you want non-docling baselines:

```sh
uv sync --group perf-test
```

or:

```sh
pip install .[perf-tools]
```

## `perf/run_perf.py`

### What it does

- accepts a PDF file or directory of PDFs
- runs one backend
- writes one CSV row per page
- prints overall stats, per-document stats, and docling timing breakdowns

### Backends

- `docling`
- `docling-threaded`
- `pdfplumber`
- `pypdfium2` (`pypdfium` alias)
- `pymupdf`

### CLI

```text
python perf/run_perf.py <input> [options]

positional arguments:
  input                      Path to a PDF file or directory of PDFs

options:
  --parser, -p PARSER        Backend to benchmark
  --recursive, -r            Recurse into subdirectories
  --output, -o PATH          Output CSV path
  --limit, -l N              Maximum number of documents to process
  --bytesio                  Sequential docling only
  --threads, -t N            Threaded docling only
  --max-concurrent-results N Threaded docling only
```

### Examples

```sh
python perf/run_perf.py ./dataset -r -p docling
python perf/run_perf.py ./dataset -r -p docling-threaded --threads 8
python perf/run_perf.py ./dataset -r -p docling-threaded --threads 8 --max-concurrent-results 32
python perf/run_perf.py ./dataset -r -p pdfplumber
python perf/run_perf.py ./dataset -r -p pypdfium2
python perf/run_perf.py ./dataset -r -p pymupdf
```

### Output

The main CSV is written to:

- `perf/results/perf_<parser>_<timestamp>.csv`

For docling runs, extra timing columns are appended per row. A second CSV with
per-document aggregates is also written next to the main one.

### Interpretation

- Sequential `docling`: `elapsed_sec` is page wall time.
- Threaded `docling`: `elapsed_sec` is only the wait time for that emitted page result.
- For threaded comparisons, use the printed total wall time and pages/sec.

## `perf/run_scaling.py`

### What it does

- runs `DoclingThreadedPdfParser` across multiple thread counts
- supports parse-only, render-only, or both
- prints speedup tables and pages/sec
- optionally writes one timing row per threaded page result

### Inputs

`run_scaling.py` accepts:

- a local PDF file
- a local directory of PDFs
- a Hugging Face dataset repo id whose `pdf/` subdirectory contains PDFs

If no input is provided, it defaults to
`docling-project/performance-dataset-bo767`.

### CLI

```text
python perf/run_scaling.py [input] [options]

options:
  --mode {parse,render,both}
  --recursive, -r
  --max-pages, -l N
  --max-concurrent-results N
  --threads 1,2,4,8,12,16
  --scale FLOAT
  --other "pypdfium2;pymupdf"
  --enable-timing / --no-enable-timing
  --timing-csv PATH
```

### v7 content-selection flags

The scaling script still exposes decode-style and materialization-style booleans
on the CLI, but internally compiles them into the new public config split.

Decode-stage booleans:

- `--keep-char-cells`
- `--create-word-cells`
- `--create-line-cells`
- `--keep-shapes`
- `--keep-bitmaps`

Materialization booleans:

- `--materialize-char-cells`
- `--materialize-word-cells`
- `--materialize-line-cells`
- `--materialize-shapes`
- `--materialize-bitmaps`
- `--materialize-bitmap-bytes`

They map to:

- `DecodeConfig` for compute tuning
- `ContentConfig` with `ContentLevel.SKIP`, `COMPUTE`, and `COMPUTE_AND_MATERIALIZE`

### Examples

```sh
python perf/run_scaling.py ./dataset --mode parse
python perf/run_scaling.py ./dataset --mode render --threads 1,2,4,8,12,16
python perf/run_scaling.py ./dataset --mode both --other "pypdfium2;pymupdf"
python perf/run_scaling.py ./dataset --mode render --enable-timing
```

### Reading the tables

- `wall_time (s)`: end-to-end elapsed time for the whole selected page set
- `pages/sec`: total scheduled pages divided by wall time
- `ms/page`: wall time normalized by total scheduled pages
- `vs threaded(1)`: speedup relative to the threaded parser with one worker
- `vs <baseline>`: speedup relative to the named baseline

Parse mode includes a sequential `DoclingPdfParser` baseline. Render mode does
not, because the sequential parser has no render path.

## Timing CSVs and visualization

When `run_scaling.py` is called with `--enable-timing`, it writes a CSV with:

- mode
- threads
- success
- page number
- `timing_total_s`
- `timing_make_page_decoder_s`
- `timing_decode_page_s`
- `timing_create_word_cells_s`
- `timing_create_line_cells_s`
- `timing_render_page_s`

Plot those timings with:

```sh
python perf/run_scaling_visualization.py timing.csv
python perf/run_scaling_visualization.py timing.csv --mode render --bins 80
```

## Slow-page replay

To inspect the slowest pages from a `run_perf.py` CSV:

```sh
python perf/run_analysis.py perf/results/perf_docling_20260622-120000.csv --top 25
python perf/run_analysis.py perf/results/perf_docling_20260622-120000.csv --nth 7
```

This replays those pages through `DoclingPdfParser.get_page_with_timings()` and
extracts detailed decode-stage timings.
