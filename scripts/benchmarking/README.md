# Benchmarking tools

For the current publication workflow, use one launcher:

```sh
bash scripts/benchmarking/refresh_performance_benchmarks.sh
# Include cross-package parsing/rendering and repeated measurements:
REPEATS=3 RUN_COMPARISON=1 bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

See [the performance page](../../docs/performance_benchmarks.md) for workload
contracts, timing boundaries and publication placeholders. The launcher rebuilds
the native executable and Python extension, verifies the pinned corpus, runs
each native/replay profile in a fresh process, and writes a unique output bundle.
It does not update the documentation. Full runs can take hours.

| Entry point | Role | Inputs | Outputs |
|---|---|---|---|
| `refresh_performance_benchmarks.sh` | Build and run the publication workflow | Pinned HF dataset; environment settings shown by `--help` | Raw measurements, provenance, generated report and PNGs |
| `run_performance_report.py` | Validate and summarize a launcher bundle | Bundle containing `metadata.json` | `report/summary.md`, CSV tables, PNGs |
| `run_performance_benchmarking_with_docling_settings.py` | Standalone backend replay | PDFs or pinned HF revision | `report.json` and page metadata JSONL |
| `run_performance_benchmarking.py` | Python API scaling and cross-package measurement | PDFs or HF dataset | Console tables and per-page CSV |
| `run_performance_eval.py` | Explore Python comparison page timings | Comparison per-page CSV | Histograms, scaling/correlation PNGs, document statistics |
| `run_performance_analysis.py` | Replay slow pages for detailed native timings | Comparison per-page CSV and original PDFs | Analysis CSV or detailed timing table |
| `run_quality_benchmarking.py` | Compare text and raster quality | PDFs or HF dataset | Quality CSVs and comparison visualizations |

## Install and rebuild

For individual tools:

```sh
uv sync --group build --group dev --group perf
# Rebuild the extension after C++ changes:
uv run --no-sync python local_build.py
uv run --no-sync cmake --build build --target run_scaling.exe --parallel 8
```

The launcher defaults to `BUILD_MODE=incremental`: existing `build/` and
`externals/` are retained. It configures/builds/installs the Python extension and
checks/builds the standalone executable; CMake recompiles affected targets and
reuses unchanged ones. Both checks run before any measurements.

The launcher checks cached repository, Python, SDK and compiler paths before
dependency synchronization, and again afterwards in case Python changed. An
incompatible cache stops the run with a `BUILD_MODE=clean` instruction.
Use clean mode after SDK/toolchain changes or bundled dependency pin/patch
upgrades: it archives existing directories as `OUT/previous-build` and
`OUT/previous-externals`, then builds from scratch. It selects the current macOS
SDK and preserves old outputs rather than deleting them. Failed
commands print their exit status, log path and final 40 log lines before stopping.
Each experiment displays its number, repetition, backend and thread count. Live
runner output includes its existing page progress bar and is also saved in full.
Completion messages show command elapsed time, separate from benchmark wall time.
The launcher defaults to `BENCHMARK_OUTPUT=compact`: live progress, short run
headers, result tables and error messages remain visible, while repeated config
tables and native timing tables are suppressed. Use `BENCHMARK_OUTPUT=full` to
restore the full benchmark output. Setup/build output is unaffected.

Standalone runners retain their full output by default. All three accept
`--suppress-config`; the native executable also accepts `--suppress-timings`.
With `--enable-timing --suppress-timings`, native timing summaries go to
`<timing-csv>.summary.log`, including exact wall time, stage totals, the timing
tree and slowest pages/documents. The report reads that file when present and
also supports older console-only logs. Timing collection and CSV data are
unchanged. Config suppression retains the replay's untimed parser initialization.

```sh
# Force a fresh dependency and native build when needed.
BUILD_MODE=clean DATASET=bo20 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

BENCHMARK_OUTPUT=full DATASET=bo20 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

The `perf` group includes the five
reference packages, Matplotlib, PDFium and R-tree support. Selected comparison
backends that cannot import are skipped by the old measurement runner; the new
bundle report rejects the resulting incomplete suite.

## Publication bundle and plots

```sh
bash scripts/benchmarking/refresh_performance_benchmarks.sh --help
# Use a new output path; existing paths are rejected to prevent CSV appends.
THREADS=1,4,8 REPEATS=1 OUT=scratch-performance-new-run \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

Choose the corpus with `DATASET=bo20`, `DATASET=bo767` (default), or
`DATASET=bo10k`. Each selects the matching
`docling-project/performance-dataset-<name>` repository and a pinned revision:

| Dataset | Default revision | PDFs | Pages |
|---|---|---:|---:|
| `bo20` | `65b9971b3fa939fe4ef89be678237e6928049b67` | 20 | 496 |
| `bo767` | `221c19e716d6ede5d18fd1560bc938ab19cc1a70` | 767 | 54,730 |
| `bo10k` | `f85e203f95879e2f5d77059a6bfeffe667cd359e` | 10,000 | 213,538 |

```sh
# Smaller corpus and thread sweep; builds incrementally by default.
DATASET=bo20 THREADS=1,4 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

DATASET=bo10k \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

`DATASET_REVISION` overrides the selected repository's default pin. The requested
revision, resolved commit, repository, file hashes and counts are recorded in
`metadata.json`. Default snapshots are checked against the counts above.
The historical Bo767 snapshot `c684df54de2081a6de1192cee12713b54759c6bb`
contained 753 PDFs / 54,584 pages; its existing reports retain those inputs.
Compare runs with matching corpus revisions and file identities.

Native stage timers and CSV output are enabled; glyph diagnostics and PNG
saving are disabled. Every repetition runs both `render-only` (word/line cell
construction disabled) and `render-with-text` (both enabled), with separate
fresh processes for each profile/thread count. Both decode characters and bitmap
geometry needed for drawing; Python models and separate bitmap-bound outputs
are not materialized. The native wall clock excludes document preload. Replay
wall time sums document totals including construction/load, processing and
unload. Comparison wall time comes from the console corpus table, which includes
loading and is rounded to milliseconds. These remain separate result series.

Every output bundle preserves `metadata.json`, exact commands, build logs,
tests, raw native CSV/log pairs, replay JSON/JSONL and optional comparison CSVs.
To regenerate tables and figures:

```sh
uv run --no-sync python scripts/benchmarking/run_performance_report.py \
  scratch-performance-new-run
```

Results go to `INPUT/report`, or the directory supplied by `--output-dir`.
The report validates complete page sets, repetitions and native log/CSV sums.
It writes `summary.md`, `summary.csv`, `runs.csv`, phase CSVs, optional canvas
checks, and runtime/scaling/phase/distribution PNGs.
Native CSVs are stored under `native/<profile>/r*/t*.csv`. Profiles are recorded
in metadata, and native baselines, stage totals and histograms stay separate.
New native figures use `native_render_only_*` and `native_render_with_text_*`;
legacy bundles keep their original single profile and figure names.

The generated `Figures` section places C++ before Python, with explicit workload
subtitles for images, text geometry and bitmap bounding boxes. Each plot has a
caption, and each profile includes shell commands that reproduce its measurements
with fresh output paths. A complete launcher command regenerates the whole bundle.

Native histograms use page worker elapsed. Replay histograms use document
totals: its page JSONL stores outputs, not per-page timings. Phase bars show the
run nearest each median total. Error bars show observed minimum–maximum wall
time, not confidence intervals.

## Individual Python API comparison

The currently supported modes are `parse-without-bitmap`, `parse-with-bitmap`,
`render`, `default` (parse without bitmap bytes plus render), and `all`.

```sh
uv run --group perf python scripts/benchmarking/run_performance_benchmarking.py \
  ./pdfs --mode render --threads 1,4,8,12 --scale 1 \
  --max-concurrent-results 32 --3rd-party-backends all --only-threaded \
  --output-dir scratch-comparison-new-run
```

Inputs may be a local PDF/directory or an HF dataset ID. This runner has no
revision flag: for a pinned comparison, pass the same snapshot directory used by
the other runners, as the launcher does. Add `--recursive` for nested local PDFs.

Supported flags include:

- `--3rd-party-backends`: `all`, `none`, or a semicolon-separated list of
  `pymupdf;pypdfium2;pdfplumber;pdfminer.six;pypdf`.
- `--only-threaded`: skip the sequential Docling parse baseline. Reference
  packages remain controlled by `--3rd-party-backends`.
- `--threads`, `--scale`, `--max-concurrent-results`, `--max-pages`,
  `--pdf-selection`, `--bytesio`, `--output-dir`.
- `--keep-char-cells`, `--create-word-cells`, `--create-line-cells`,
  `--keep-shapes`, `--keep-bitmaps`.
- `--materialize-char-cells`, `--materialize-word-cells`,
  `--materialize-line-cells`, `--materialize-shapes`,
  `--materialize-bitmaps`.

Boolean flags take `true` or `false`. Bitmap-byte export is selected by the
experiment mode; it has no separate CLI switch. Third-party packages are
excluded from the bitmap-byte-export mode.

The runner writes `<cpu>_<input-name>_<mode>.csv` under the supplied output
directory and prints the result/configuration tables to stdout. Capture stdout
as a log to retain the corpus wall times. It currently does not write a
Markdown report. There are no `--compare`, `--other`, `--pages-csv` or
`--mode both` options.

## Backend-settings replay

```sh
uv run --group perf python \
  scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py \
  docling-project/performance-dataset-bo767 \
  --dataset-revision 221c19e716d6ede5d18fd1560bc938ab19cc1a70 \
  --backend threaded --threads 8 --output-dir scratch-replay-new-run
```

Choose `--backend pdfium`, `threaded` or `both`. A comma-separated thread
list is supported, but use separate invocations for independent process/RSS
measurements. `--threads default` uses accelerator environment resolution;
explicit counts are labeled scaling overrides.

Outputs contain source/package/configuration provenance, per-document phase
timings, API/output counters and streamed page metadata. The replay includes
Python model materialization, separators and RGB images and excludes model
inference and Docling framework construction. See the script header for the
pinned source inspection and limitations. Native CSV, replay JSONL and comparison
CSV are different formats; they must not be passed interchangeably to plotting
tools.

## Existing page-level exploration tools

`_common.py` defines the comparison `PageRow` schema:

```text
backend,task,threads,doc_key,page_number,success,elapsed_s,wall_gap_s,
image_width,image_height,make_page_decoder_s,decode_page_s,
create_word_cells_s,create_line_cells_s,render_page_s,error_message
```

```sh
uv run --group perf python scripts/benchmarking/run_performance_eval.py \
  scratch-comparison-new-run/COMPARISON.csv --viz-dir scratch-comparison-plots

uv run --group perf python scripts/benchmarking/run_performance_analysis.py \
  scratch-comparison-new-run/COMPARISON.csv --backend docling-parse \
  --task 'parse+render' --threads 8 --top 20 --render 1.0
```

Replace `COMPARISON.csv` with the filename printed by the measurement runner.
Evaluation generates histograms, hexbins, per-document statistics and scaling
plots. Its scaling plots sum arrival gaps, which omit document loading and may
omit native processing already performed during submission. They do not share
the corpus-wall boundary of the publication tables. Use
`run_performance_report.py` for publication runtime and efficiency figures.

Docling page durations are native worker elapsed; reference page durations are
wall measurements around page operations. Neither distribution is the
amortized `corpus wall / page count`. Equal-size renders are not automatically
pixel-equivalent.

Analysis replays pages and requires the original files. Pass the measured scale
and content/decode flags explicitly; defaults may differ from the original run.

## Quality benchmarking

```sh
uv run --group perf python scripts/benchmarking/run_quality_benchmarking.py ./pdfs
uv run --group perf python scripts/benchmarking/run_quality_benchmarking.py ./pdfs \
  --compare render --reference-renderer pypdfium2 --renderers docling-parse,pymupdf
```

The default input is the pinned regression dataset in `tests.constants`.
Outputs include `render_quality.csv`, `text_quality.csv`, render comparison
panels and quality-distribution plots. Quality evidence is separate from the
performance refresh; the launcher's damaged-page preflight is not a complete
render-quality regression run.
