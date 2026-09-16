# Benchmarking tools

Four entry points:

| Script | Role | Reads | Writes |
| --- | --- | --- | --- |
| `run_performance_benchmarking.py` | **Measure.** Thread-scaling sweeps and cross-package comparison | PDFs (local or a Hugging Face dataset) | terminal tables, per-page CSV, markdown |
| `run_performance_eval.py` | **Plot.** Histograms, scatters, hexbins, per-document stats | per-page CSV | PNGs + CSV in a directory named after the CSV |
| `run_performance_analysis.py` | **Drill down.** Slowest pages, C++ stage timings | per-page CSV | analysis CSV / detailed table |
| `run_quality_benchmarking.py` | **Compare quality.** Text and render agreement across PDF engines | PDFs (local or a Hugging Face dataset) | quality CSVs, render visualizations, histograms |

They are chained by one file format: the per-page CSV that `run_performance_benchmarking.py
--pages-csv` writes. Every row carries its own `backend`, `task` and `threads`,
so one file can hold several packages at several thread counts, and the other
two scripts split it into series themselves rather than guessing from the
filename.

`_common.py` owns `PageRow`, the performance CSV schema, and shared helpers
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

## `run_performance_benchmarking.py`

### Thread-scaling sweep (default)

Runs docling-parse at several thread counts and reports pages/sec plus speedup
against single-threaded baselines.

```sh
python scripts/benchmarking/run_performance_benchmarking.py ./dataset --mode parse
python scripts/benchmarking/run_performance_benchmarking.py ./dataset --mode render --threads 1,2,4,8,12,16
python scripts/benchmarking/run_performance_benchmarking.py --mode both --other "pypdfium2;pymupdf"
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
- `--only-threaded`: run nothing but `DoclingThreadedPdfParser` at each
  `--threads` value; skips the sequential `DoclingPdfParser` baseline and every
  3rd-party backend (overrides `--other`), and narrows `--compare` to
  docling-parse. The `vs <baseline>` columns disappear with them
- `--output-dir`: where the outputs land (default
  `./scratch-performance-benchmarks-YYYY-MM-DD-HH-MM`, computed from the
  current datetime, so a before/after pair of runs never collides)

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
python scripts/benchmarking/run_performance_benchmarking.py --mode both --compare

# every backend, writing the report into the docs
python scripts/benchmarking/run_performance_benchmarking.py --threads 1,4,8,12 --compare all --mode render \
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
- `--output-dir`: where the report and CSV land (default
  `./scratch-performance-benchmarks-YYYY-MM-DD-HH-MM`)

## Outputs

The threaded rows also carry an `efficiency` column: `vs threaded(1)` divided
by the thread count, i.e. the fraction of a perfect linear speedup that thread
count reaches. It is blank for the single-threaded reference backends, where it
has no meaning.

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

## `run_performance_eval.py`

```sh
python scripts/benchmarking/run_performance_eval.py docs/performance_benchmarks/apple_m3_max_bo767_render.csv
python scripts/benchmarking/run_performance_eval.py docs/performance_benchmarks   # scan a directory
python scripts/benchmarking/run_performance_eval.py pages.csv --task parse --threads 1
python scripts/benchmarking/run_performance_eval.py                               # defaults to scripts/benchmarking/results
```

Without `--viz-dir`, plots go to the input CSV path with `.csv` dropped —
`apple_m3_max_bo767_render.csv` produces `apple_m3_max_bo767_render/` beside
it, so the plots inherit the run's name and sit next to the markdown report
they belong to. Several inputs land in a `viz/` directory next to them.

Produces:

- `hist_stacked.png` — per-page time histograms, one panel per series on a
  shared log-log x-axis. Filter to a single backend and task and the panels
  become the thread sweep.
- `hist_pages_per_document.png` — corpus shape
- `scaling_<task>.png` — docling-parse against its thread count: seconds/page
  on the left axis (black `-o`) and pages/second on the right (red `s-`), both
  log-scaled
- `hex_loglog_<reference>_vs_<package>.png` — per-page time of docling-parse at
  one thread against each other package, log-log, with the `x=y` diagonal
- a per-document statistics table and `per_document.csv`

The scaling plot reconstructs total time as the sum of `wall_gap_s`, which
tiles the interval from the end of loading to the last result. Summing
`elapsed_s` would be wrong: that is per-page cost, which by design stays flat
as threads increase.

Hexbins never cross tasks (a `parse` time against a `parse+render` time is not
a like-for-like page), and other docling-parse thread counts are excluded
because per-page cost is the same quantity at any thread count — those plots
would just be the diagonal.

Flags: `--backend`, `--task`, `--threads` to filter, `--bins`, `--viz-dir`,
`--top-documents`.

## `run_quality_benchmarking.py`

Compares output quality rather than speed. Render comparison reuses the same
image metrics and three-panel visualizations as
`tests/test_regression_pypdfium_render.py`; text comparison reports per-page string
similarity and edit counts after optional whitespace normalization.

```sh
python scripts/benchmarking/run_quality_benchmarking.py ./dataset
python scripts/benchmarking/run_quality_benchmarking.py ./dataset --compare render
python scripts/benchmarking/run_quality_benchmarking.py ./dataset \
    --reference-renderer pypdfium2 --renderers docling-parse,pymupdf
python scripts/benchmarking/run_quality_benchmarking.py ./dataset \
    --reference-parser pypdfium2 --parsers docling-parse,pymupdf,pypdf
```

Inputs: a local PDF file, a local directory of PDFs, or a Hugging Face dataset
repo id whose `pdf/` subdirectory contains PDFs. The default is
`docling-project/regression-dataset-for-docling-parse`, using the pinned
regression snapshot from `tests.constants` and its `regression/` subdirectory.

Supported renderers: `docling-parse`, `pypdfium2`, `pymupdf`.

Supported text parsers: `docling-parse`, `pypdfium2`, `pymupdf`,
`pdfplumber`, `pdfminer.six`, `pypdf`.

Outputs under `--output-dir` (default
`./scratch-quality-benchmarks-YYYY-MM-DD-MM`, computed from the current
datetime):

- `render_quality.csv`: one row per page and renderer pair, with dimensions,
  `normalized_delta`, `mean_abs_error`, changed-pixel ratio and tolerance flag
- `text_quality.csv`: one row per page and parser pair, with similarity ratio,
  character counts and insertion/deletion/replacement counts
- `current_page.jsonl`: flushed breadcrumbs for the page being processed, useful
  when native code aborts before Python can write normal CSV output
- `visualizations/`: three-panel render comparisons and corpus histograms,
  controlled by `--render-visualizations=all|above-tolerance|none`; default is
  `all`

Important flags:

- `--compare`: `parse`, `render`, or `both` (default)
- `--max-pages/-l`: exact total page cap across the input set
- `--recursive`: search local input directories recursively
- `--scale`: render scale (1.0 = 72 dpi)
- `--text-normalization`: `whitespace` or `none`
- `--progress-log`: override the JSONL breadcrumb path

## `run_performance_analysis.py`

Replays the slowest pages from a per-page CSV and extracts detailed decode
timings:

```sh
python scripts/benchmarking/run_performance_analysis.py scripts/benchmarking/results/pages.csv --top 25
python scripts/benchmarking/run_performance_analysis.py scripts/benchmarking/results/pages.csv --nth 7
python scripts/benchmarking/run_performance_analysis.py pages.csv --top 25 --task parse --threads 1
```

- `--top N`: per-page CSV of static timings for the N slowest pages, plus an
  aggregate breakdown of where the time went
- `--nth N`: full static and dynamic timing table for one page
- `--backend` / `--task` / `--threads`: narrow a mixed CSV to one series
- `--min-sec`: ignore pages faster than this
