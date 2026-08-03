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

## Comparison suite (`run_scaling.py --compare`)

`--compare` replaces the scaling tables with the two tables published in
`docs/performance_benchmarks.md`: a per-page time distribution, and a wall-time
speedup grid. Without the flag the script behaves exactly as before.

```sh
# docling-parse vs pypdfium2, the default pairing
python perf/run_scaling.py --mode both --compare

# every backend, and write the markdown for the docs
python perf/run_scaling.py --mode both --compare all --threads 1,2,4,8 \
    --markdown-out docs/_generated/benchmark_tables.md \
    --pages-csv perf/results/pages.csv
```

Backends and the API each one is timed on:

| name            | parse                              | parse+render                 |
| --------------- | ---------------------------------- | ---------------------------- |
| `docling-parse` | `DoclingThreadedPdfParser`         | + `RenderConfig`             |
| `pymupdf`       | `page.get_text("rawdict")`         | + `get_pixmap` → `pil_image` |
| `pypdfium2`     | textpage rects + `get_text_bounded`| + `page.render` → `to_pil`   |
| `pdfplumber`    | `page.chars`                       | + `page.to_image()`          |
| `pdfminer.six`  | `extract_pages` + `LTChar` walk    | not supported                |
| `pypdf`         | `extract_text(visitor_text=...)`   | not supported                |

Each backend uses a *position-bearing* extraction API, so that all rows measure
the same task. `pymupdf` deliberately uses `rawdict` rather than
`get_text("text")`, which returns a bare string and would be a cheaper task.

docling-parse is run once per `--threads` value; every other package is run
single-threaded, because none of them expose a thread-safe multi-page pipeline.

How the numbers are defined:

- `total time` is a wall clock around the whole corpus, including opening
  documents. This is the number that shows the benefit of threading.
- the per-page distribution is the cost of a *single* page, so it stays roughly
  flat as threads increase while `total time` drops. Third-party backends are
  timed with a wall clock around each page's work (document open/close
  excluded); docling-parse reports the C++ page timing, because under
  concurrency no wall-clock interval belongs to one page. The table's
  `per-page source` column states which of the two a row used.

In render mode the rasterised width and height of every page are recorded too,
and a *render size check* table compares each backend against docling-parse
page by page. A timing comparison only means something if all backends produced
the same canvas, so this reports how many pages agree within 2 px and the worst
offender. The sizes are also in the `--pages-csv` output as `image_width` and
`image_height`.

Flags:

- `--compare [list]`: bare flag uses `docling-parse;pypdfium2`; otherwise a
  semicolon-separated list, or `all`
- `--markdown-out`: write both tables as markdown
- `--pages-csv`: one row per (backend, task, threads, doc, page), with the
  rendered image size

Third-party backends that are not installed are skipped, and the skipped rows
are restated just before the tables so they are not mistaken for failures.
Install them all with `uv sync --group perf`.

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
