# Performance benchmarks

Performance results are published separately for each dataset and machine setup.
Choose a result page below for measured tables, plots, package versions and provenance.
A publication updates only its dataset/setup; results from other setups remain available.

<!-- performance:availability:start -->
| Dataset | Setup | Run | Workloads |
|---|---|---|---|
| bo20 | [Apple M3 Max / 64 GiB / macOS-26.6.2-arm64-arm-64bit](performance_benchmarks/bo20/apple-m3-max-64gib-macos-26-6-2-arm64-arm-64bit/index.md) | scratch-performance-refresh-20261112-123829-Q21HfP | native, replay |
| bo767 | [Apple M3 Max / 64 GiB / macOS-26.6.2-arm64-arm-64bit](performance_benchmarks/bo767/apple-m3-max-64gib-macos-26-6-2-arm64-arm-64bit/index.md) | scratch-performance-recovery-c2rz8gq0 | native, replay, comparison |
<!-- performance:availability:end -->

## Workloads and timing boundaries

| Experiment | What the reported wall time includes |
|---|---|
| Native C++ scaling | Decoder construction, decoding, optional word/line computation, rasterization, canvas extraction, result collection and timing CSV output. Document loading is outside the timer. |
| Python scaling with Docling settings | Per-document parser construction/loading, processing, text-model and page-image consumption, separator queries, JSONL output and unloading. |
| Cross-package comparison, when collected | Each selected package's corpus workload, including document loading. The Docling row uses the public threaded Python API. |

Native profiles produce page-images in native RGBA canvases. `render-only` disables
word/line cell construction; `render-with-text` enables both. Both decode character
and bitmap geometry needed for drawing. Neither materializes Python models.

The Python replay creates RGB page-images, materializes word/line models and bitmap
bounds, and queries separator geometry. It follows selected backend operations
from pinned Docling revision `07154cdc82500058ad9377bac92beed3de820458`.
It excludes OCR, layout/table inference and Docling framework construction.
Exact parity with the full Docling backend has not been independently established.
PDFium renders at 1.5 times the requested scale and resizes; docling-parse renders
at the requested scale and converts to RGB. Equal dimensions do not establish
equal pixels or text segmentation.

Cross-package comparisons exercise package-specific text/geometry APIs, optionally
with rendering. Docling materializes character models; other packages expose
different geometry granularity. These experiments do not produce identical output
models. Subtracting wall times across workloads does not isolate Python overhead.

A page-image is a raster of the entire PDF page, distinct from an embedded bitmap.
The published profiles do not export original embedded bitmap bytes separately.
Performance and extraction/rendering quality are separate: see
[quality benchmarks](quality_benchmarks.md).

## Datasets and setups

The launcher supports Bo20, Bo767 and Bo10K. Each publication records the exact
dataset revision, ordered input identity, document/page counts, CPU, architecture,
RAM, operating system, source/build identities, package versions, thread sweep,
render scale and queue capacity. Custom revisions retain their recorded identity;
a dataset name alone does not establish equivalent inputs.

Bo767's earlier snapshot contained 753 PDFs; the complete snapshot contains 767.
Those measurements must not be mixed. Likewise, Bo20 results are not substitutes
for Bo767 or Bo10K measurements.

Results from different CPUs appear on separate pages. Treat OS, RAM, dataset
revision, workload settings, package/build versions and memory instrumentation as
part of the experimental setup when comparing numbers. A setup page presents one
published run; it never combines missing sections with measurements from an older run.

## Reading the results

Throughput is pages divided by corpus wall time. Amortized milliseconds/page are
`1000 * wall / pages`, not individual-page latency. Speedup uses the one-thread
baseline from the same workload/backend/task; efficiency is `100 * speedup / threads`.
Missing baselines produce unavailable ratios. Repeated runs report median wall
time and observed min–max ranges; these ranges are not confidence intervals.
A single observation provides no empirical variability estimate.

Worker-stage totals include overlapping work and waits; they are neither corpus
wall time nor CPU time. Native page-worker durations and replay whole-document
durations describe different units. Thread counts above available physical or
performance cores need separate interpretation.

Memory summaries use OS process high-water marks including startup, loading and
teardown, excluding the monitor/logger. Missing observations are unavailable,
never zero. Compare timings under matching memory instrumentation and process
isolation. Detailed sampled RSS timelines remain in local reports.

## Collect and publish results

From the repository root:

```sh
DATASET=bo20 REPEATS=3 THREADS=1,4,8,12 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

# Include the slower optional cross-package suite:
DATASET=bo767 REPEATS=3 RUN_COMPARISON=1 \
  bash scripts/benchmarking/refresh_performance_benchmarks.sh

# Preview publication from an existing run; no benchmarks are rerun:
uv run python scripts/tools/update_performance_docs.py --performance-run-dir scratch-performance-refresh-RUN

# Apply after reviewing the preview:
uv run python scripts/tools/update_performance_docs.py --performance-run-dir scratch-performance-refresh-RUN --apply

# Check published file sizes, manifests and links:
python3 scripts/tools/update_performance_docs.py --check
```

Run on an otherwise idle machine. Dataset verification reads files before timing,
so these are not cold-cache benchmarks. Native/replay profiles run in fresh
processes. Memory-off cross-package suites retain their shared-process execution;
peak/profile comparison series use fresh processes.

The publisher validates raw measurements using the report generator. Omitted
optional suites are labeled “not collected”; missing expected measurements fail
validation. An absent documentation placeholder is skipped and reported.
Custom `--setup-id` values support older bundles with incomplete machine metadata.
An older run requires `--allow-older` before replacing a newer publication.

Only referenced summary PNGs, Markdown, compact provenance and a publication
manifest enter the repository. Raw logs, CSV/JSONL and detailed RSS timelines stay
local. Publication is preview-only by default and includes byte sizes and net size
change. Size limits are enforced before docs are written and checked in CI:
100 KiB per Markdown/JSON file, 500 KiB per image, 5 MiB per dataset/setup publication,
and 25 MiB for the performance documentation tree, including historical assets.
Limits live in [the publication configuration](../scripts/tools/update_performance_docs_artifacts/performance_docs_limits.json).
Replacing a publication removes obsolete files owned by its manifest; Git retains
history. Publish selected refreshes because replacing binary assets still grows Git history.

See [the benchmarking tools README](../scripts/benchmarking/README.md) for build,
pinned datasets, standalone report generation and failed-run recovery. Earlier
V1/V2 measurements remain in [legacy performance benchmarks](legacy/legacy_performance_benchmarks.md).
