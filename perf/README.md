# Performance tools

Three entry points, on a shared `_common.py`:

| Script | Role | Reads | Writes |
| --- | --- | --- | --- |
| `run_scaling.py` | **Measure.** Thread-scaling sweeps and cross-package comparison | PDFs (local or a Hugging Face dataset) | terminal tables, per-page CSV, markdown |
| `run_eval.py` | **Plot.** Histograms, scatters, hexbins, per-document stats | per-page CSV | PNGs + CSV in a directory named after the CSV |
| `run_analysis.py` | **Drill down.** Slowest pages, C++ stage timings | per-page CSV | analysis CSV / detailed table |

They are chained by one file format: the per-page CSV that `run_scaling.py
--pages-csv` writes. Every row carries its own `backend`, `task` and `threads`,
so one file can hold several packages at several thread counts, and the other
two scripts split it into series themselves rather than guessing from the
filename.

`_common.py` owns `PageRow`, that CSV's schema, and the shared helpers
(`find_pdfs`, `percentile`, ...). It is a library, not an entry point.

## Install

Core docling runs work with the normal project install. The third-party
backends need the perf group:

```sh
uv sync --group perf
```

That installs pymupdf, pypdfium2, pdfplumber, pdfminer.six, pypdf and
matplotlib. Backends that are missing are skipped with a message rather than
failing the run.

## `run_scaling.py`

### Thread-scaling sweep (default)

Runs docling-parse at several thread counts and reports pages/sec plus speedup
against single-threaded baselines.

```sh
python perf/run_scaling.py ./dataset --mode parse
python perf/run_scaling.py ./dataset --mode render --threads 1,2,4,8,12,16
python perf/run_scaling.py --mode both --other "pypdfium2;pymupdf"
```

Inputs: a local PDF file, a local directory of PDFs, or a Hugging Face dataset
repo id whose `pdf/` subdirectory contains PDFs (the default is
`docling-project/performance-dataset-bo767`).

Modes: `parse` (decode only), `render` (decode plus raster), `both`.

Important flags:

- `--max-pages/-l`: exact total page cap across the input set
- `--max-concurrent-results`: threaded backpressure limit
- `--scale`: render scale (1.0 = 72 dpi)
- `--other`: semicolon-separated single-threaded reference backends
- `--bytesio`: load PDFs from memory instead of by path (docling-parse only)
- `--output-dir`: where the outputs land (default `./scratch`)

The config split is exposed as:

- decode-stage booleans: `--keep-char-cells`, `--create-word-cells`,
  `--create-line-cells`, `--keep-shapes`, `--keep-bitmaps`
- materialization booleans: `--materialize-char-cells`,
  `--materialize-word-cells`, `--materialize-line-cells`,
  `--materialize-shapes`, `--materialize-bitmaps`,
  `--materialize-bitmap-bytes`

compiled into `DecodeConfig` and into `ContentConfig`'s `ContentLevel.SKIP`,
`COMPUTE` and `COMPUTE_AND_MATERIALIZE`.

### Comparison suite (`--compare`)

`--compare` replaces the scaling tables with the ones published in
`docs/performance_benchmarks.md`: a per-page time distribution, a wall-time
speedup grid, and a render size check. Without the flag the script behaves as
it always did.

```sh
# docling-parse vs pypdfium2, the default pairing
python perf/run_scaling.py --mode both --compare

# every backend, writing the report into the docs
python perf/run_scaling.py --threads 1,4,8,12 --compare all --mode render \
    --output-dir ./docs/performance_benchmarks/
```

Backends and the API each one is timed on:

| name | `parse` | `parse+render` |
| --- | --- | --- |
| `docling-parse` | `DoclingThreadedPdfParser` | + `RenderConfig` |
| `pymupdf` | `page.get_text("rawdict")` | + `get_pixmap` → `pil_image` |
| `pypdfium2` | textpage rects + `get_text_bounded` | + `page.render` → `to_pil` |
| `pdfplumber` | `page.chars` | + `page.to_image()` |
| `pdfminer.six` | `extract_pages` + `LTChar` walk | not supported |
| `pypdf` | `extract_text(visitor_text=...)` | not supported |

Each backend uses a *position-bearing* extraction API, so every row measures
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

In render mode the rasterised width and height of every page are recorded, and
a *render size check* table compares each backend page by page against
pypdfium2, which is run first and treated as ground truth (PDFium is the
rasteriser three of the six packages ultimately rely on; docling-parse and then
pymupdf are the fallbacks when pypdfium2 is not in the run). A timing
comparison only means something if all backends produced the same canvas, so
this reports how many pages agree within 2 px and the worst offender.

Flags:

- `--compare [list]`: bare flag uses `docling-parse;pypdfium2`; otherwise a
  semicolon-separated list, or `all`
- `--output-dir`: where the report and CSV land (default `./scratch`)

## Outputs

You only ever name a directory. Both files are named
`<cpu>_<dataset>_<mode>`, derived from the machine and the run, so results
from different machines never overwrite each other:

```
docs/performance_benchmarks/
  apple_m3_max_performance-dataset-bo767_render.md
  apple_m3_max_performance-dataset-bo767_render.csv
```

The markdown report is self-contained, so a published number never has to be
traced through terminal scrollback. It holds:

- the **exact command** that produced it, and a timestamp
- a **Benchmark** table: dataset, source, revision, document and page counts,
  mode, thread counts, backends, render scale
- a **System** table: cpu, cores, memory, platform, python, and the version of
  every package that was benchmarked
- **Decode config**, **Content config** and **Render config** tables, showing
  the parameters the run was actually driven with
- the result, speedup and render-size-check tables

The scaling sweep (no `--compare`) writes the per-page CSV only; the markdown
report is a rendering of the comparison tables.

## Per-page CSV format

One row per timed page:

```
backend,task,threads,doc_key,page_number,success,elapsed_s,wall_gap_s,
image_width,image_height,make_page_decoder_s,decode_page_s,
create_word_cells_s,create_line_cells_s,render_page_s,error_message
```

- `elapsed_s` — the per-page cost used for all statistics
- `wall_gap_s` — observed arrival gap; equals `elapsed_s` when unthreaded
- `image_width` / `image_height` — rasterised size, zero outside `parse+render`
- the `*_s` stage columns — C++ breakdown, zero for third-party backends

This is the only format the tools read. A CSV without a `backend` column is
skipped, so scanning a directory of unrelated CSVs is harmless.

## `run_eval.py`

```sh
python perf/run_eval.py docs/performance_benchmarks/apple_m3_max_bo767_render.csv
python perf/run_eval.py docs/performance_benchmarks   # scan a directory
python perf/run_eval.py pages.csv --task parse --threads 1
python perf/run_eval.py                               # defaults to perf/results
```

Without `--viz-dir`, plots go to the input CSV path with `.csv` dropped —
`apple_m3_max_bo767_render.csv` produces `apple_m3_max_bo767_render/` beside
it, so the plots inherit the run's name and sit next to the markdown report
they belong to. Several inputs land in a `viz/` directory next to them.

Produces:

- per-series page-time histograms, plus stacked and overlaid versions
- a pages-per-document histogram for the corpus
- per-series scatter of document page-count vs total time, with a linear fit
- pairwise hexbins of per-page times, linear and log-log
- a per-document statistics table and `per_document.csv`

Hexbins only pair series of the *same* task, and by default compare each series
against one reference per task (docling-parse at its lowest thread count).
`--all-pairs` gives the full grid, which is quadratic in the number of series.

Flags: `--backend`, `--task`, `--threads` to filter, `--bins`, `--viz-dir`,
`--top-documents`, `--all-pairs`.

## `run_analysis.py`

Replays the slowest pages from a per-page CSV and extracts detailed decode
timings:

```sh
python perf/run_analysis.py perf/results/pages.csv --top 25
python perf/run_analysis.py perf/results/pages.csv --nth 7
python perf/run_analysis.py pages.csv --top 25 --task parse --threads 1
```

- `--top N`: per-page CSV of static timings for the N slowest pages, plus an
  aggregate breakdown of where the time went
- `--nth N`: full static and dynamic timing table for one page
- `--backend` / `--task` / `--threads`: narrow a mixed CSV to one series
- `--min-sec`: ignore pages faster than this
