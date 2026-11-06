# Performance benchmarks

Results refresh: **pending**. Timing, throughput and efficiency values below are
placeholders until the new runs have completed and their reports have been reviewed.
The earlier scale-2 charts and measurements are not used for this refresh.

## Workloads and timing boundaries

Three experiments answer different questions:

| Experiment | Runner | What the reported wall time includes |
|---|---|---|
| Native C++ scaling | `build/run_scaling.exe` | Page decoder construction, decoding, word/line computation, rasterization, canvas extraction, result collection and timing CSV output. Document loading is outside the timer. |
| Python backend replay | `run_performance_benchmarking_with_docling_settings.py` | Per-document parser construction/loading, processing, model and image consumption, separator queries, JSONL output and unloading. |
| Optional cross-package comparison | `run_performance_benchmarking.py` | Each selected package's complete corpus workload, including document loading. The Docling row uses the public threaded Python API. |

Native C++ produces native results. The replay also materializes Python models and
RGB images, uses a different document lifecycle, and releases native memory every
128 pages. Their runtime difference is not an isolated measurement of Python
language overhead.

The replay follows a pinned inspection of Docling revision
`07154cdc82500058ad9377bac92beed3de820458`. It excludes OCR, layout/table models,
inference and Docling framework construction. Exact parity with the actual
Docling backend has not been independently established.

## Dataset and configuration

Within each run, all experiments use the selected pinned dataset and file order.
The default is Bo767:

| Parameter | Value |
|---|---|
| Dataset | `docling-project/performance-dataset-bo767` |
| Revision | `221c19e716d6ede5d18fd1560bc938ab19cc1a70` |
| Documents / pages | 767 / 54,730 |
| Render scale | 1.0: requested output at 72 dpi |
| Result queue capacity | 32 |
| Thread counts | 1, 4, 8, 12 by default |
| QPDF | Bundled version recorded after the build; current pin: 12.4.2 |
| Machine, compiler, package versions and source revision | **TBD from the new run metadata** |
| Repetitions | **TBD**; launcher default: one; use three for median/range reporting |

Set `DATASET=bo20` for 20 PDFs / 496 pages, or `DATASET=bo10k` for 10,000 PDFs /
213,538 pages. Each corpus has its own pinned revision, listed in
[the benchmarking tools README](../scripts/benchmarking/README.md#publication-bundle-and-plots).
`DATASET_REVISION` can override the selected corpus's pin; the requested revision
and resolved commit are saved in the run metadata.

The earlier Bo767 snapshot `c684df54de2081a6de1192cee12713b54759c6bb` contained
753 PDFs / 54,584 pages. Restoring 14 PDFs added 146 pages. Existing reports from
that snapshot keep their original inputs and must not be mixed with measurements
of the complete corpus. The launcher records ordered filenames, page counts and
SHA-256 hashes so the exact inputs can be checked.

Native rendering is measured twice: `render-only` disables word/line cell
construction, while `render-with-text` enables both. Both decode characters and
bitmap geometry needed for drawing and extract native RGBA canvases. Neither
materializes Python models or separate bitmap bounding-box outputs.
Python replay materializes word/line models and bitmap bounds, computes shapes
without materializing their models, and excludes bitmap image-byte export.
Both native profiles and replay use crop-box boundaries and enable same-font
enforcement.

The replay requests scale-1 images from both backends. PDFium renders at 1.5 times
the requested scale and resizes; docling-parse renders at scale 1 and converts to
RGB. Equal output dimensions do not establish equal pixels or text segmentation.

## Run the refresh

From the repository root, launch the native and replay experiments:

```sh
bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

To refresh every section of this page, including the slower cross-package suite,
and collect three repetitions:

```sh
REPEATS=3 RUN_COMPARISON=1 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh
```

These are full-corpus measurements and can take hours. The script installs the
build/dev/perf dependency groups, checks/builds the Python extension and standalone
executable incrementally, runs the damaged-page unit tests, resolves
the pinned snapshot, and records provenance before timing. It does not edit this
page or publish results.

`BUILD_MODE=incremental` is the default. Existing `build/` and `externals/`
directories are reused; CMake checks dependencies and recompiles affected
targets. Unchanged targets are reused. Cached repository, Python, SDK and
compiler paths are checked before and after dependency synchronization.
An incompatible cache stops the run and requests `BUILD_MODE=clean`.

Use `BUILD_MODE=clean` after SDK/toolchain changes or bundled dependency pin/patch
upgrades. Existing directories are then moved to `previous-build/` and
`previous-externals/` under the new output directory before rebuilding from
scratch. Build work remains outside all benchmark timers; each bundle records
the selected mode and native binary/extension hashes.
Setup stages and experiments are labeled by repetition, backend and thread
count. Runner output, including page progress bars, is streamed to the terminal
and saved in the run log. `BENCHMARK_OUTPUT=compact` is the launcher default:
repeated config tables and native timing tables are hidden, while progress and
result tables remain visible. Native timing summaries are saved beside each CSV
as `<timing-csv>.summary.log` and used by the report. Set
`BENCHMARK_OUTPUT=full` to print all benchmark tables. Standalone runners keep
their full output unless `--suppress-config` and, for C++, `--suppress-timings`
are requested. Completion messages report command elapsed time;
the report still uses each benchmark's own documented wall-time boundary.
On macOS, the launcher selects the current SDK with `xcrun`. A failed phase
prints its exit status, log path and final 40 log lines, then stops.

Each native thread profile and each replay backend/thread profile runs in its
own process. Runs execute sequentially. Each cross-package mode runs in one
process per repetition and measures its selected packages sequentially.

Run on an otherwise idle machine. Dataset verification reads the files before
measurement, so these are not cold-cache benchmarks. Repeat ranges expose
observed variation; they are not confidence intervals. Thread counts above the
machine's available physical/performance cores need separate interpretation.

Optional launcher settings:

```sh
# Small corpus for quick benchmark iterations; incremental build by default.
DATASET=bo20 THREADS=1,4 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

# Shorter thread sweep; output directory must not already exist.
THREADS=1,4,8 REPEATS=1 OUT=scratch-performance-my-run \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

# Force a fresh build, for example after a bundled dependency upgrade.
BUILD_MODE=clean DATASET=bo20 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

bash scripts/benchmarking/refresh_performance_benchmarks.sh --help
```

A successful launch prints the path to `report/summary.md`. Raw logs and page data
stay in that unique output directory. Inspect an active phase by tailing its log;
a nonzero exit stops the launcher and preserves completed measurements.

## Native C++ scaling

**Results pending.** Stage timing and CSV collection are enabled; glyph-cache
diagnostics and PNG saving are disabled. All durations are seconds.

| Native profile | Threads | Wall median (s) | Observed min–max (s) | Pages/s | Speedup | Efficiency |
|---|---:|---:|---:|---:|---:|---:|
| Render images | 1 | TBD | TBD | TBD | TBD | TBD |
| Render images | 4 | TBD | TBD | TBD | TBD | TBD |
| Render images | 8 | TBD | TBD | TBD | TBD | TBD |
| Render images | 12 | TBD | TBD | TBD | TBD | TBD |
| Render images + word/line text geometry | 1 | TBD | TBD | TBD | TBD | TBD |
| Render images + word/line text geometry | 4 | TBD | TBD | TBD | TBD | TBD |
| Render images + word/line text geometry | 8 | TBD | TBD | TBD | TBD | TBD |
| Render images + word/line text geometry | 12 | TBD | TBD | TBD | TBD | TBD |

Figures to insert after review:

- `native_render_only_runtime.png` and `native_render_with_text_runtime.png`: corpus wall time and throughput.
- `native_render_only_scaling.png` and `native_render_with_text_scaling.png`: speedup and parallel efficiency, each relative to its own one-thread run.
- `native_render_only_phases.png` and `native_render_with_text_phases.png`: summed page worker elapsed by non-overlapping stage.
- `native_render_only_distribution.png` and `native_render_with_text_distribution.png`: separate page worker elapsed distributions.

The generated `Figures` section groups C++ plots first, followed by Python with
Docling settings and optional Python API comparisons. Workload subtitles explain
whether images, text geometry and bitmap bounding boxes are produced. Captions
explain the axes and timing boundaries, and each group has a reproduction block
using the recorded dataset path, repetitions and thread counts with fresh output
files. The native and replay blocks retain a fresh process per thread count.

Worker elapsed includes waits within a stage and is not CPU time. Worker totals
overlap across threads and must not be added to wall time. The phase plot uses
the measured run nearest each profile's median wall time.

## Python backend replay

**Results pending.** The wall-time basis is the sum of measured document
`total_s`, including construction/load, page processing and unload. Imports,
dataset verification and final report serialization are outside that boundary.

| Backend | Threads | Wall median (s) | Observed min–max (s) | Pages/s | Speedup | Efficiency |
|---|---:|---:|---:|---:|---:|---:|
| pypdfium2 | 1 | TBD | TBD | TBD | — | — |
| docling-parse | 1 | TBD | TBD | TBD | TBD | TBD |
| docling-parse | 4 | TBD | TBD | TBD | TBD | TBD |
| docling-parse | 8 | TBD | TBD | TBD | TBD | TBD |
| docling-parse | 12 | TBD | TBD | TBD | TBD | TBD |

Figures to insert after review:

- `replay_runtime.png`: backend wall time and throughput.
- `replay_scaling.png`: docling-parse speedup and efficiency.
- `replay_phases.png`: construction/load, processing and unload.
- `replay_distribution.png`: **document** total-time distributions.

The current replay processes documents serially, constructing a parser for each
document; native page work inside that document is threaded. Construction/load
therefore remains a serial contribution. This describes this benchmark's
execution policy, not every possible Docling deployment.

Replay JSONL files contain page output metadata, not individual page timings.
They support output-count and image-dimension validation, but cannot supply
per-page latency histograms.

## Optional cross-package comparison

Set `RUN_COMPARISON=1` to measure these API workloads on the same corpus.
The experiment uses character geometry and, for rendering, a PIL image. Word and
line creation are disabled in its Docling profile; character models are
materialized. It is a separate workload from the backend replay.

| Package | Position-bearing extraction API | Rendering API |
|---|---|---|
| docling-parse | Threaded parser with materialized character cells | Native renderer and `get_image()` |
| PyMuPDF | `page.get_text("rawdict")` | `get_pixmap()` and `pil_image()` |
| pypdfium2 | Text-page rectangles and bounded text | `page.render()` and `to_pil()` |
| pdfplumber | `page.chars` | `page.to_image()`, using PDFium |
| pdfminer.six | `extract_pages()` and an `LTChar` walk | Unsupported |
| pypdf | `extract_text(visitor_text=...)` | Unsupported |

These APIs expose different geometry granularity and output structures. The
comparison measures the stated API workloads, not identical output fidelity.
Third-party packages run with one worker in this harness; Docling uses the
requested page-thread sweep. No general claim about other packages' concurrency
capabilities follows from that choice.

**Results pending:**

| Package | Threads | Parse wall median / range (s) | Parse + render wall median / range (s) |
|---|---:|---:|---:|
| docling-parse | 1 | TBD | TBD |
| docling-parse | 4 | TBD | TBD |
| docling-parse | 8 | TBD | TBD |
| docling-parse | 12 | TBD | TBD |
| PyMuPDF | 1 | TBD | TBD |
| pypdfium2 | 1 | TBD | TBD |
| pdfplumber | 1 | TBD | TBD |
| pdfminer.six | 1 | TBD | — |
| pypdf | 1 | TBD | — |

Generated figures are `comparison_parse_runtime.png`,
`comparison_parse_scaling.png`, `comparison_parse_render_runtime.png` and
`comparison_parse_render_scaling.png`. The report also includes a per-page
canvas-size check against PDFium with a two-pixel tolerance. Review mismatches
before making render-throughput comparisons.

The older `run_performance_eval.py` still supports comparison CSV histograms and
correlations. Its scaling plots use arrival gaps, whose interval begins after
document submission for Docling and omits loading for reference packages. Use
the new report's console-wall-based figures for the tables above.

## Artifacts, metrics and publication

The output bundle contains:

```text
metadata.json                 dataset manifest, source/build/package identities
commands.log                  exact timed/build commands and log destinations
build*.log, tests.log          build and preflight evidence
native/<profile>/r*/t*.log     native console output, separated by rendering profile
native/<profile>/r*/t*.csv     schema-2 page timings; adjacent .csv.summary.log files
replay/r*/*/report.json        replay document timing and provenance
replay/r*/*/*.pages.jsonl      replay page output metadata
comparison/r*/*/              optional comparison logs and page CSVs
report/summary.md              generated tables, methodology and figure links
report/summary.csv, runs.csv   aggregate values and individual corpus runs
report/*_phases.csv            native worker stages and replay document phases
report/canvas_checks.csv       optional comparison image-size checks
report/*.png                  generated figures
```

Regenerate the report without rebuilding or rerunning benchmarks:

```sh
uv run --no-sync python scripts/benchmarking/run_performance_report.py \
  /absolute/path/to/scratch-performance-refresh-RUN
```

The report rejects failed pages, duplicate or incomplete page sets, missing
profiles/repetitions, and native CSV totals that disagree with their logs.
Older bundles containing only `native/r*/t*.csv` remain readable as the original
single native profile; they do not acquire unmeasured render-only results.
It does not infer missing timings or repair contaminated/appended CSVs.

Throughput is pages divided by median corpus wall time. Amortized milliseconds
per page are `1000 * wall / pages`; this is not individual-page latency. Speedup
is `median_wall(1) / median_wall(N)` within the same workload/backend/task.
Efficiency is `100 * speedup / N`. Each measured range is minimum–maximum across
repetitions; with one run it contains no information about variability.

After reviewing the generated report, replace the TBD cells, record the machine,
source revision and repetition count, copy the selected figures to a named
subdirectory under `docs/performance_benchmarks/`, and insert their image links.
Keep the raw bundle available as evidence. Do not combine rows from different
datasets, scales, machines, lifecycle policies or timing boundaries.

Performance measurements are separate from rendering quality:
see [quality benchmarks](quality_benchmarks.md). Older published measurements
are available in [legacy performance benchmarks](legacy/legacy_performance_benchmarks.md).
