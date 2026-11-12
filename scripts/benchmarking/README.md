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
| `recover_performance_benchmarks.py` | Reuse completed core results and recollect comparisons | Existing bundle; normally invoked through `RECOVER_FROM` | New bundle with original core artifacts and separate comparison provenance |
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

## Optional process memory measurements

Memory collection is opt-in on macOS and Linux. `off` retains the existing
execution path; `peak` adds each backend process's OS RSS high-water mark;
`profile` also samples current RSS from a separate monitor. Both modes work for
native scaling, PDFium/Docling settings replay, and cross-package comparisons.

```sh
# OS peaks, without polling for an RSS timeline.
REPEATS=3 THREADS=1,4,8,12 RUN_COMPARISON=0 DATASET=bo20 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh --memory-mode peak

# OS peaks plus current RSS samples, nominally every 50 ms.
REPEATS=3 THREADS=1,4,8,12 RUN_COMPARISON=0 DATASET=bo20 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh \
    --memory-mode profile --memory-interval-ms 50
```

`MEMORY_MODE=peak|profile|off` and `MEMORY_INTERVAL_MS=50` are equivalent
environment settings; explicit CLI values take precedence. The interval must
be a positive integer. Shorter intervals add monitoring work. Collection is
optional because its overhead should be measured on the actual workload.
Use matching settings and several repetitions for timing comparisons.

Peaks cover one process and all its threads over its entire lifetime, including
imports, PDF loading, result collection and teardown. They exclude the monitor,
`uv`, and log piping. They are RSS, not allocator/heap usage or the sum of a
process tree. macOS uses `wait4.ru_maxrss` in bytes; Linux uses the same field
converted from KiB. Existing backend timing boundaries are preserved, so the
peak's lifetime is broader than the reported native processing timer. The
monitor's elapsed time is recorded separately and includes polling delay.

Current RSS comes from macOS `proc_pidinfo` or Linux `/proc/PID/statm` in the
external monitor, so native code holding Python's GIL does not stop sampling.
Samples use a monotonic process clock; they include startup and are not aligned
to page events. The OS peak and sampled maximum are separate values: polling
can miss short spikes. A sufficiently short run may produce no samples while
still producing an OS peak. Repeated traces are plotted independently.

With memory enabled, comparison backend/thread series each run in a fresh
Python process. With memory off, comparisons retain their historical shared
process. This changes cache isolation as well as enabling meaningful individual
peaks; do not attribute timing differences between these modes solely to the
sampler. The standalone comparison runner accepts the same two flags. Its
optional sequential parse baseline is timing-only; the measured series are
the registered reference backends and threaded Docling configurations.

Outputs include:

- Native/replay `*.memory.json` sidecars with PID, executed command, OS peak,
  measurement scope, completion status, method and sample metadata.
- Profile `*.memory.rss.csv` files with elapsed seconds, RSS bytes and PID.
- Comparison `memory/` artifacts alongside series checkpoints, including
  worker requests/results and the corresponding memory sidecars.
- `report/memory_runs.csv`, `memory_summary.csv`, peak-versus-threads figures,
  and per-series RSS timeline figures. In `report/summary.md`, the `Parallel
  Scaling` section contains native and Python scaling tables, followed by
  `Process Memory` with peak-memory tables and figures. Elapsed-time RSS plots
  are in `report/memory_traces.md`, linked from the summary with a return link.

Timing tables use compact task labels, with full descriptions listed once below
each table. Peak-RSS and throughput axes start at `(0, 0)`. Throughput plots
retain measured points and add dashed ideal-scaling guides: measured one-thread
throughput multiplied by thread count. Guides run from the origin to the largest
measured thread count for backends measured at one thread and additional counts.

Missing observations are reported as unavailable. Older replay results already
contain a process peak; reports expose it as `existing`, even with collection
off. This does not manufacture peaks for native/other historical runs or create
a historical timeline. OS peaks remain available in failed-run artifacts for
diagnosis, but failed measurements cannot become successful report rows.
Checkpoint reuse checks memory settings and hashes of sidecars and timelines.

Recovery accepts the same flags:

```sh
uv run --no-sync python scripts/benchmarking/recover_performance_benchmarks.py \
  /absolute/path/to/completed-or-interrupted-bundle \
  --memory-mode profile --memory-interval-ms 50
```

Recovery retains the original native/replay results and their original memory
settings; it measures only new comparison series with the requested settings.
It cannot add memory observations to earlier core runs. Compatible checkpoints
and their memory artifacts are copied together. Changed settings or missing
artifacts cause affected comparisons to be recollected. Existing strict
core-binary/package provenance checks still apply; use a fresh refresh to
measure a newly rebuilt extension. Report regeneration reads saved artifacts
and does not rerun benchmarks.

## Reference preflight, checkpoints and recovery

The perf dependency group pins **PyMuPDF 1.27.2.3**. Version 1.28.2 aborts in
MuPDF display-list construction on **Bo767 `1293644.pdf`, page 3**. The pinned
version was checked on all 453 pages with text extraction, rasterization and PIL
conversion. See [the matching upstream report](https://github.com/pymupdf/PyMuPDF/issues/5149).
When `RUN_COMPARISON=1`, the launcher probes this page in a disposable subprocess
before any corpus measurements if the PDF is present in the selected dataset.
A native abort or timeout fails the preflight with the document/page and package
version, instead of wasting the preceding scaling runs.

Comparison runs checkpoint each completed backend/thread series with its wall
time, page CSV, CSV hash, source/package provenance and input/settings identity.
Saving happens after that series' timer stops. The launcher supplies
`--checkpoint-dir`; standalone comparison runs can use the same option and add
`--resume` to reuse matching complete checkpoints. A failed backend's partial
pages are not treated as a completed measurement.

To recover a failed refresh without repeating completed native/replay measurements:

```sh
RECOVER_FROM=/absolute/path/to/scratch-performance-refresh-RUN \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

Recovery creates a **new** bundle using the dataset, repetitions and thread
counts from the source bundle. It synchronizes the pinned dependencies while
retaining the installed local project, checks the recorded machine, Python, core
packages, native binary/extension hashes and QPDF version, then validates the
complete core results and dataset hashes. It links the original native/replay
artifacts, so **keep the original bundle**. It does not rebuild native targets.
If scripts were committed during collection, replay provenance is accepted only
when the commit patch exactly matches the recorded build diff and the replay
script hash matches that commit. The report explains this equivalence.

Both comparison modes are recollected under the current reference versions.
Compatible checkpoints from a previous recovery are copied and reused; a
reference-version or source change prevents checkpoint reuse. The original
metadata is retained as `original-metadata.json`. `comparison_provenance` records
the new environment separately, and report tables show package versions. The
completed original parse comparison remains in its original bundle.

If recovery is interrupted, use the newly printed recovery directory as
`RECOVER_FROM` to retain compatible completed comparison checkpoints.
`OUT` may select a new output directory; `BUILD_MODE=clean` is incompatible with
recovery because the recorded core binaries must remain unchanged.

You can also inspect the completed core results independently:

```sh
uv run --no-sync python scripts/benchmarking/run_performance_report.py \
  /absolute/path/to/scratch-performance-refresh-RUN --core-only
```

This validates all requested native/replay profiles and writes `report-core/`,
clearly labeling comparisons as excluded. The normal report still requires the
complete comparison suite and rejects missing profiles or mixed provenance.

For a finished comparison whose report failed, regenerate directly without
recovery or benchmark reruns:

```sh
uv run --no-sync python scripts/benchmarking/run_performance_report.py \
  /absolute/path/to/scratch-performance-recovery-RUN
```

A reference series with page errors is listed under **Excluded reference
comparisons**, with attempted/successful/failed page counts. Its timings remain
in `excluded_comparisons.csv`, and document/page/error details are written to
`comparison_errors.csv`. All repetitions of that series are excluded from
performance tables, speedups and figures. Reporting requires full attempted-page
coverage, matching checkpoint provenance and checksums; missing pages and native
errors still fail validation. Original CSVs, checkpoints and metadata are retained.

Native stage timers and CSV output are enabled; glyph diagnostics and PNG
saving are disabled. Every repetition runs both `render-only` (word/line cell
construction disabled) and `render-with-text` (both enabled), with separate
fresh processes for each profile/thread count. Both decode characters and bitmap
geometry needed for drawing; Python models and separate bitmap-bound outputs
are not materialized. The native wall clock excludes document preload. Replay
wall time sums document totals including construction/load, processing and
unload. Comparison wall time comes from the exact completed-series checkpoint,
including document loading. Older bundles without checkpoints use rounded console
wall times. The report checks console rounding against checkpoint timings,
including older tables rounded to six significant digits. These remain separate
result series.

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
