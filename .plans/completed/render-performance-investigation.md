# Render performance investigation and improvement plan

Date: 2026-11-01.
Branch: `fix/pr-337-improvement`, based on `7a920a5`, with the existing uncommitted PR #337 improvements.

## Scope and measurement limits

Investigate the render benchmark's PDFium workload, absolute performance, and scaling. No production changes were made during this investigation. Fixes below are proposed work.

The full run used Apple M3 Max, 64 GB RAM, Python 3.12.11, scale 2.0, queue capacity 64, and dataset `docling-project/performance-dataset-bo767` at revision `c684df54de2081a6de1192cee12713b54759c6bb`: 753 documents and 54,584 pages. Assess scaling through 12 performance cores; exclude 16-thread efficiency from conclusions.

New native experiments used only 4 and 8 threads. The mixed probe contains 300 deliberately hotspot-enriched pages across 13 PDFs. The text probe contains all 3,764 pages of `1593260.pdf` and `1781090.pdf`. These are diagnostic single runs, not repeated whole-corpus performance estimates. Summed worker elapsed times include waits and are neither wall time nor CPU time. Detailed timers are nested and must not all be added together.

## 2026-11-02: Standalone Docling-settings replay baseline

User-approved Step 1 adjustment: the new runner is standalone, with comments linking to the inspected Docling source and no `docling` imports. No dedicated test file was added. Settings are replayed from the inspected revision rather than resolved through imported Docling option classes. Actual-backend parity and full-pipeline conditional-call coverage remain unverified; the original Step 1 acceptance criteria must not be treated as fully satisfied.

Report: [2026-11-02 report](../../scratch_perf_docling_benchmark_2026_11_02_09_24_32/report.json), produced by `scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py`. This is a separate workload from the legacy scale-2 diagnostic results below, not evidence of native optimization gains.

Dataset: `docling-project/performance-dataset-bo767`, revision `c684df54de2081a6de1192cee12713b54759c6bb`; 753 documents, 54,584 pages. Requested image scale 1.0; PDFium renders at 1.5 then resizes; native rendering at 1.0 with RGB conversion, words/lines materialized, shapes computed and separator geometry consumed, bitmap bounds without byte export, font boundaries enforced, queue capacity 32, native-memory release every 128 pages. Native 1/4/8-thread runs are explicit scaling overrides. No recorded environment overrides. Python 3.12.11, docling-parse 7.24.2, docling-core 2.103.1, pypdfium2 5.14.0, Pillow 12.3.0, rtree 1.4.1. Parser revision `7a920a5ad5347aa8ed72c5dd2dff1f2d9e3cb20e` with working-tree changes recorded in the report; inspected Docling revision `07154cdc82500058ad9377bac92beed3de820458`.

| Backend | Console elapsed | Console pages/s | Summed document seconds | Native speedup vs 1 thread | Native parallel efficiency |
|---|---:|---:|---:|---:|---:|
| PDFium, synchronized single worker | 24:20 | 37.4 | 1,460.85 | — | — |
| docling-parse, 1 thread | 25:05 | 36.3 | 1,505.19 | 1.00× | 100% |
| docling-parse, 4 threads | 8:01 | 113.3 | 481.57 | 3.13× | 78.1% |
| docling-parse, 8 threads | 5:49 | 156.1 | 349.47 | 4.31× | 53.8% |

Console elapsed and rates are preserved as displayed; derived ratios use summed document timings. Native 4→8 speedup is 1.38×. All native per-document output-count dictionaries match at 1/4/8 threads, including totals of 2,299,638 text lines, 12,390,139 words, 343,177 bitmap bounds, 4,046,910 shape lines and 271,095 connected-shape bounds. Count equality does not establish coordinate, text or pixel equality. PDFium uses different segmentation and geometry semantics and does not emit word cells or shape lines.

### Parallel-efficiency analysis of this report

| Native threads | Construction/load seconds | Page-processing seconds | Unload seconds | Total seconds |
|---:|---:|---:|---:|---:|
| 1 | 36.24 | 1,468.41 | 0.53 | 1,505.19 |
| 4 | 36.44 | 444.53 | 0.60 | 481.57 |
| 8 | 37.03 | 311.80 | 0.63 | 349.47 |

- Construction/load is a measured, nearly constant cost: 37.03 seconds, or 10.6% of the 8-thread total. The runner constructs a parser and loads one document at a time. This bucket does not distinguish parser setup from PDF loading. Eliminating the entire measured bucket, an optimistic bound rather than a forecast, would reduce 349.47 to 312.44 seconds (1.12× faster). Removing just unload cannot matter materially.
- Page-processing-only speedup at 8 threads is 4.71× (58.9% efficiency), so setup alone does not explain the loss. This bucket combines worker waits, lazy getters, Python model construction, normalization/coordinate conversion, image conversion, shape queries, output accounting and JSONL writes; it is not a native-render-only measurement.
- Document length exposes a scheduling limit: the 215 documents with fewer than 8 pages occupy 18.92 seconds at 8 threads versus 24.89 at one thread, only 1.32× speedup. The 105 documents with at least 128 pages occupy 204.79 versus 981.75 seconds, a 4.79× speedup. A per-document worker pool cannot keep eight workers busy on short documents, and document boundaries prevent overlap in this profile.
- Long-document scaling also stalls. `1593260.pdf` and `1781090.pdf` together take 34.06/16.17/16.49 seconds at 1/4/8 threads. They emit 1,434,357 word models and are an appropriate probe for the serial consumer and the already-confirmed legacy glyph-outline hotspot. This report cannot attribute the plateau to either cause.
- Vector-heavy probes are relevant to the newly included shape work. `2216123.pdf` takes 36.48/13.57/12.23 seconds and emits 1,440,700 shape lines; `1393538.pdf` takes 18.14/7.57/6.65 seconds and emits 924,759 shape lines. Together they emit 58.5% of all shape lines. Shape queries and coordinate conversion are consumed serially in Python, making bulk materialization/conversion worth measuring. Correlation with shape count does not isolate query, conversion, rendering or allocation cost.
- Define the gap to ideal 8-thread scaling as `T8 - T1/8`: 161.32 seconds. The five largest document contributions sum to 27.63 seconds (17.1% of that gap); the ten largest sum to 40.09 seconds (24.9%). Targeted probes are useful, but fixing one document class cannot be assumed to recover all lost efficiency.

Priority follow-ups, preserving the output contract and keeping optimization claims separate from measurement changes:

1. Add attribution for parser construction, document load, waiting for results, image/RGB conversion, shape queries and box conversion, segmented-page/model materialization, normalization, bitmap bounds and JSONL export. Export existing native stage/detail timers separately from consumer wall timings. Use this to distinguish a serial consumer from worker contention before choosing an optimization.
2. Profile the text pair and the two vector-heavy PDFs above at 4/8 threads. If consumer work dominates, investigate bulk word/line model construction and coordinate normalization, and avoid intermediate per-shape BoundingBox conversions where the public result can be preserved. Retain Step 2's worker-local actual-face glyph caching as an independently evidenced candidate, but do not infer its share of this workload from legacy timings.
3. Measure bounded document concurrency as a separately labeled execution-policy experiment, e.g. two independent 4-thread parsers in separate processes with at most eight native workers in total, each retaining its per-document configuration. This could overlap loading, short-document processing and independent-document work; it also changes scheduling and memory use and is not the current sequential backend-default profile. Parser/pool reuse is another separate lifecycle experiment, conditional on safe reset and cache behavior.
4. Repeat focused measurements in fresh processes, alternating order. Only then compare candidate and baseline under the same settings. Do not change image scale, disable words/shapes, alter release cadence, or tune queue capacity on the assumption that this report identifies those as bottlenecks.

Limits: one pass per configuration, in fixed order PDFium→native 1→4→8 in one process. Cross-profile warm-up and filesystem-cache effects are uncontrolled. Reported peak RSS is cumulative process high-water memory (about 2.49/8.21/10.00/10.06 GiB across the four runs), not independent per-profile peaks. No CPU utilization, GIL/queue-wait attribution, allocation counts, native detailed timers or release-event counters are in this report. QPDF lock contention, glyph allocation, image decoding and Python consumer saturation remain hypotheses for this profile. OCR/layout/table inference remains excluded.

## Findings (original scale-2 diagnostic workload)

### 1. PDFium uses the same scale but performs less output work

The active `cmp_pypdfium2` in [the benchmark runner](../../scripts/benchmarking/run_performance_benchmarking.py) passes `page.render(scale=scale)`. The older helper with a hardcoded scale is not the active reference.

Across all 54,584 pages, 54,494 rendered dimensions match exactly and 90 differ by one pixel. There is no evidence of a material scale mismatch.

The reference extracts text and a bounding rectangle, then discards them. It does not construct Docling-equivalent lines or models, and it does not enumerate bitmap bounds. It does not extract bitmap bytes.

PDFium rectangles are not guaranteed to be lines: 1,800 of 45,467 rectangle strings in the mixed probe contain newlines. PDFium does not provide word/line/paragraph layout analysis; see its [API documentation](https://pypdfium2.readthedocs.io/en/stable/python_api.html).

| PDFium mixed probe | Wall seconds |
|---|---:|
| Existing reference | 23.79 |
| Retain text/bbox tuples and enumerate image bounds | 24.88 |
| Same, with RGBA rendering | 24.47 |

The added enumeration found 764 image bounds and cost about 1.11 seconds. Existing reference output was RGB; Docling output was RGBA. The amended probe still does not provide exact Docling line/model semantics. Do not extrapolate its overhead to the full corpus.

### 2. Rendering accounts for most scaling-related inflation

Full-corpus summed worker elapsed seconds:

| Threads | Page-decoder construction | Decode | Render |
|---:|---:|---:|---:|
| 4 | 21.83 | 945.65 | 843.41 |
| 8 | 33.68 | 970.25 | 1,017.79 |
| 12 | 48.69 | 990.03 | 1,247.93 |

Rendering accounts for approximately 82% of the increase in total worker time from 4 to 8 threads, and 86% from 8 to 12. Decoder construction accounts for approximately 1.2%, 1.6%, and 2.1% of worker elapsed time respectively.

### 3. Repeated glyph-outline allocation is a confirmed scaling hotspot

| Text probe | 4 threads, wall seconds | 8 threads, wall seconds |
|---|---:|---:|
| Current branch, normal rendering | 13.08 | 12.83 |
| Pre-feature build, `e90fee2` | 12.64 | 12.96 |
| Current branch, text drawing disabled | 6.76 | 4.27 |

Disabling text drawing is diagnostic only and changes the rendered output.

The `render_page.text.glyph_metrics` timer increases from 14.78 to 61.01 worker-seconds when doubling workers. Native sampling shows:

```text
calculate_glyph_bbox_adjustment
  -> get_glyph_outlines
  -> BLPath allocation/destruction
  -> macOS allocator locks
  -> __ulock_wait2
```

In [the renderer](../../src/render/blend2d_renderer.h), `glyph_bbox_face_key` is null for system/substituted faces. `calculate_glyph_bbox_adjustment` consequently bypasses [the glyph bbox cache](../../src/render/glyph_bbox_cache.h) and builds a fresh outline repeatedly.

Disabling the existing cache did not improve the text probe. The renderer source is byte-identical to `e90fee2`, whose build reproduces the scaling failure. This establishes a pre-existing bottleneck; it does not establish that PR #337 caused the reported regression.

### 4. QPDF serialization is real, but current timers cannot isolate mutex waiting

The per-document mutex protects shared-QPDF operations through `QPDFWriter::write()` in [document.h](../../src/parse/pdf_decoders/document.h). Do not remove that protection.

Individual documents show increased decoder-construction worker time:

| Document | 4 threads | 8 threads |
|---|---:|---:|
| `1106182.pdf`, 32 pages | 1.74 s | 3.91 s |
| `2182429.pdf`, 341 pages | 1.61 s | 4.61 s |

Construction includes lock waiting, integrity checks, serialization, copying, and standalone-page parsing. These totals are not measurements of lock-held time.

The current benchmark never requests structure extraction. Threaded workers also bypass the direct-document decode path containing additional decode/timing guards. The `qpdf_build_thread_safe_buffer` timing key exists but is never recorded.

### 5. Image work dominates the mixed probe; JSON is a lower priority

At four threads, detailed worker timers report:

| Operation | Seconds |
|---|---:|
| Image decoding/preparation | 35.76 |
| JPEG2000 subset of image work | 20.63 |
| CCITT subset of image work | 7.13 |
| Bitmap rendering | 22.05 |
| Font decoding | 0.31 |

Full page and annotation JSON conversion are disabled by current defaults. Python page materialization costs approximately 0.12 seconds on this probe. Omitting materialization or reducing the result queue from 64 to 4 did not improve the run.

[Font-program population](../../src/parse/pdf_resources/page_font.h) still creates descriptor and stream JSON with no apparent internal reader. Making this metadata lazy is useful cleanup, but the measurements do not identify it as the principal bottleneck.

## Ordered implementation steps

### Step 1: Correct the reference and preserve comparable baselines

Measure backend performance with the default settings and backend operations used by Docling. Keep `scripts/benchmarking/run_performance_benchmarking.py` as is throughout this plan. Create `scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py` for the Docling-default workload; keep the existing raw-library scale-2 results separately labeled as diagnostic measurements.

Reference implementations:

- `.references/docling/docling/backend/pypdfium2_backend.py`
- `.references/docling/docling/backend/docling_parse_backend.py`
- `.references/docling/docling/datamodel/backend_options.py`
- `.references/docling/docling/datamodel/pipeline_options.py`
- `.references/docling/docling/models/stages/page_preprocessing/page_preprocessing_model.py`

Inspected Docling revision: `07154cdc82500058ad9377bac92beed3de820458`.

Implementation files: the new `scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py` and focused backend-integration verification. Put the new benchmark logic, settings resolution, and result export in the new script; do not add a profile to or otherwise modify the existing benchmark script.

1. Create the new script to benchmark exactly two backends: pypdfium through `PyPdfiumDocumentBackend` and threaded docling-parse through `ThreadedDoclingParseDocumentBackend`. Prefer instantiating these classes and consuming their public page APIs. This includes wrapper overhead, synchronization, model construction, coordinate conversion, and caching. Verify any standalone replay against the actual backends before using its results. Do not include the non-threaded docling-parse parser or other PDF backends.

2. Resolve settings through Docling's default `PdfFormatOption`, `PdfPipelineOptions`, `PdfBackendOptions`, and `ThreadedDoclingParseBackendOptions`. Select each backend explicitly. Record resolved settings and environment overrides.

   Add a prominent header comment block in the new Python script that reproduces the complete settings table below as `#` comments. Include the reference file paths, inspected Docling revision, measurement boundary, default API-call sequence, and distinction between defaults and explicit scaling overrides. Explain PDFium's oversampling/resize and docling-parse's word/shape work, RGB conversion, caching, and native-memory release. Keep these comments aligned with the resolved settings printed by the script; document any differences caused by dependency revisions or environment overrides.

   The inspected defaults are:

   | Setting | Docling-default workload |
   |---|---|
   | Requested page-image scale | 1.0 |
   | PDFium rendering | Render at requested scale × 1.5, copy the PIL image, then resize to the requested dimensions |
   | docling-parse rendering | Enabled; pre-render scale 1.0; preserve native render defaults and include backend RGB conversion |
   | docling-parse character cells | Compute, without materializing character models |
   | docling-parse word and line cells | Compute and materialize both |
   | docling-parse shapes | Compute while rendering; do not materialize through the page-content config |
   | docling-parse bitmaps | Materialize bounds; `include_bitmap_images=False` |
   | Font-boundary enforcement | `enforce_same_font=True` |
   | Parser threads | Resolve accelerator settings; default 4 unless overridden |
   | Result queue | Inherit the parser default, currently 32 |
   | Native-memory release | Every 128 pages |
   | Reading-order separator geometry | Enabled; consume shape-line and connected-shape queries |
   | Outline/structure requests | Preserve default call conditions; heading hierarchy is disabled by default |

   Image decoding required for rendering remains inside the timed workload. Disabling embedded-image-byte export does not remove that work. Preserve each backend's default pixel format and internal raster scale.

3. Replay the default preprocessing sequence: page acquisition and size, cached `Page.get_image(scale=1.0)`, shape geometry for reading-order separators, and `get_segmented_page()`. Preserve the image cache so repeated requests at the same scale do not trigger additional backend renders. Consume bitmap bounds through `get_bitmap_rects()` for this benchmark's output contract; identify that explicit request where it extends the default pipeline call sequence.

4. Include the actual extraction work performed by each backend:

   - PDFium: `_PageChars` character scanning, text selection, horizontal merging, `TextCell` and `SegmentedPdfPage` construction, coordinate conversion, image-object indexing, and `pypdfium2_lock`.
   - docling-parse: word/line materialization, text normalization, coordinate conversion, bitmap bounds, and RGB image conversion.

   Preserve each backend's segmentation algorithm. Raw PDFium rectangles do not reproduce Docling's PDFium backend.

5. Preserve per-document backend/parser creation, page iteration, lazy getters, cache lifetime, and unload behavior. Consume results with bounded retention. Report construction/load, page processing, and unload separately, plus their end-to-end total. Distinguish cold initialization from steady-state measurements.

6. Define the measurement boundary explicitly. This profile measures backend work with Docling defaults. OCR, layout, table, and other model inference remain outside parser throughput. Inventory conditional backend calls from those stages, including OCR crops, content-intersection queries, and repeated text getters. Measure those backend costs with a recorded call trace before claiming full-pipeline backend coverage.

7. Measure the default profile and explicitly labeled 4/8-thread scaling variants. For scaling variants, change only the docling-parse thread count. Preserve PDFium's backend synchronization and all remaining defaults.

   Keep the previous scale-2, queue-64, memory-release-disabled, word-disabled, shape-disabled workload separately labeled. Its existing findings and the Step 6 reproduction command describe that diagnostic workload.

8. Record actual imported backend paths, Docling/docling-parse revisions and working-tree changes, dependency/compiler versions, environment, dataset revision, complete resolved settings, API-call counts, output counts, image modes/dimensions, timings, and peak RSS. Compare candidate and baseline under the same profile. Report changes to the reference workload separately from native optimization gains.

Acceptance:

- `scripts/benchmarking/run_performance_benchmarking.py` is unchanged. The new script benchmarks only pypdfium and threaded docling-parse, and its header comments include the complete settings table and workload contract above.
- Integration checks confirm settings and public API behavior against the actual Docling backends.
- Fixtures cover text, vector content, images, rotated/cropped pages, and documents longer than 128 pages.
- Verify word/line materialization, bitmap bounds without embedded-image-byte export, shape geometry, PDFium oversampling/resize, docling-parse RGB conversion, cache behavior, queue defaults, and native-memory release.
- Use the same page selection and requested output scale. Document expected segmentation and pixel differences.
- Label backend-default, scaling-override, legacy diagnostic, and full-conversion measurements distinctly.

### Step 2: Fix glyph-bound caching for actual font faces

Files: `src/render/glyph_bbox_cache.h`, `src/render/blend2d_renderer.h`; preserve worker ownership in `src/pybind/docling_threaded_renderer.h`.

1. Replace the embedded-only string identity with an identity for the actual Blend2D face used for drawing. Evaluate `font.face().unique_id()`; verify lifetime and reuse across pages.
2. Key bounds by actual face identity, glyph ID, and font size. Include any additional font state that can change the computed outline if that state is supported.
3. Use the same identity in outline plausibility checks and bbox adjustment so both paths reuse bounds. Keep embedded-face distrust tracking separate.
4. Preserve bounded capacity, capacity-zero behavior, worker-local ownership, and fallback selection. Add no shared cache mutex.
5. Add worker-local diagnostic counts for hits, misses, outline extractions, and evictions. Inspect remaining allocation costs after fixing misses.
6. Consider skipping outline work only after checking all consumers of the bounds. `fit_glyph_bbox_to_target=false` alone is insufficient: alignment and clipping also consume them.

Acceptance: repeated substituted/system glyphs hit the cache; face/size changes cannot reuse incorrect bounds; embedded and fallback rendering remain correct; cache-on/off output matches on deterministic fixtures. Repeated 4/8-thread text probes must show lower outline counts, lower glyph-metrics time, and a reproducible throughput improvement without relying on disabled text or altered quality.

### Step 3: Add attribution timers before further optimization

Files: `src/parse/pdf_decoders/document.h`, `src/parse/utils/pdf_timings.h`, `src/render/blend2d_renderer.h`, and benchmark export.

1. Separate QPDF mutex wait, locked integrity checking, locked page extraction/serialization, unlocked buffer copy, and standalone-page parse.
2. Record locked durations while locked, but publish timing data after releasing the source-document mutex. Preserve timing-data synchronization without extending the critical section.
3. Separate bitmap conversion/downsampling from drawing/blitting. Account explicitly for any image re-decode performed before the current bitmap timer starts.
4. Export worker-local counts and correctly nested timers. Avoid per-glyph shared counters or expensive diagnostic logging.

Acceptance: distinguish wait time from protected work; no double-counting in reported totals; no measurable instrumentation regression beyond run-to-run spread. Keep shared-QPDF accesses protected.

### Step 4: Optimize measured image paths

Files: `src/render/blend2d_renderer.h`, `src/parse/pdf_states/bitmap.h`, and relevant codec helpers.

1. Use the new timers to rank codec decode, decode-array handling, conversion, downsampling, and blitting.
2. Preserve existing JPEG/JPEG2000 reduced-resolution decode and the render-scale hint. Verify when reduction is suppressed by masks or indexed color and whether avoidable re-decodes occur.
3. Add specialized opaque RGB and gray conversion loops with validation outside inner loops. Preserve CMYK, alpha, stencil, and decode-array behavior.
4. Optimize downsampling only with the existing premultiplication and rounding behavior preserved, unless a quality change is separately reviewed.
5. Investigate reuse of immutable decoded samples for repeated images only if timers show repeated decoding; include mask, colorspace, and decode parameters in correctness decisions.

Acceptance: identical dimensions and extraction metadata; no unintended pixel differences on color/mask fixtures; reproducible mixed-probe improvement at unchanged scale and quality; no unexplained memory increase.

### Step 5: Remove unused eager JSON

Files: `src/parse/pdf_resources/page_font.h` and `src/parse/pdf_resources/page_font/embedded_font_program.h`.

1. Audit all readers and external/native API expectations for descriptor and stream JSON.
2. Remove unused snapshots or populate them only for an explicit diagnostic request. Preserve any required API behavior.
3. Leave page/annotation debug JSON disabled by default. Measure remaining CMap JSON separately before considering a typed representation.

Acceptance: embedded-font extraction and font fallback tests pass; requested diagnostic output remains available where required; report the measured benefit without promising a large corpus speedup.

### Step 6: Validate improvements and report limits

1. Preserve the current branch as a reproducible baseline, including its uncommitted changes. Build baseline and candidate with identical Release settings and dependencies.
2. Run one discarded warm-up and at least three fresh-process measurements per build at 4 and 8 threads. Alternate build order; report medians and spread.
3. Start with the 3,764-page text selection, then the exact 300-page mixed selection, then the full corpus after focused improvements are established.
4. Compare wall time, worker timers, cache counts, output counts, peak RSS, and time to first result. Do not infer end-to-end gains from nested timers alone.
5. Exercise substituted fonts, embedded fonts, CJK/FreeType fallback, font-size changes, masks, transparency, CMYK/gray images, rotations, and repeated image objects. Run existing relevant renderer/parser regression checks.
6. Do not claim a 12-thread improvement until it is measured. Keep exploratory reruns at 4/8 threads unless broader measurement is authorized.

Use `scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py` for the new Docling-default measurements. Preserve the existing runner unchanged; the following command reproduces the legacy diagnostic text selection without the PDFium reference:

```sh
uv run python scripts/benchmarking/run_performance_benchmarking.py \
  --only-threaded --mode render --threads 4,8 \
  --3rd-party-backends none --scale 2 \
  --pdf-selection '1593260.pdf;1781090.pdf'
```

## Evidence locations

- Full-corpus input: `scratch-performance-benchmarks-2026-11-01-13-21/apple_m3_max_performance-dataset-bo767_render.csv`.
- Temporary native driver: `/private/tmp/pr337_render_probe.py`.
- Temporary PDFium driver: `/private/tmp/pr337_pdfium_probe.py`.
- Exact selections: `/private/tmp/pr337-probe-selection.json` and `/private/tmp/pr337-text-selection.json`.
- Native sample: `/private/tmp/pr337-render8.sample.txt`.
- Probe result JSON files: `/private/tmp/pr337-probe-*.json`, `/private/tmp/pr337-text-*.json`, and `/private/tmp/pr337-pdfium-*.json`.
- Related implementation context: [PR #337 improvement results](pr-337-improvement-results.md).

Temporary evidence can be deleted by the OS. Before implementation, archive the drivers, exact selections, result JSON, and sampled-stack interpretation under `.plans/render-performance-evidence/`; normalize machine-specific dataset paths when making a portable replay driver.
