# Render scaling: short implementation and measurement cycles

Date: 2026-11-04.
Status: v11 LRU optimization validated and accepted on 2026-11-05.
All native optimizations are retained. User approved upgrading QPDF from 12.2.0
to 12.4.2, then repeating scaling against the v11 diagnostics-off baseline.
Python investigation remains pending after the C++ dependency checkpoint.

## Working loop

The user compiles and runs performance experiments. Codex reviews the code, proposes
one bounded patch, and implements that patch only after approval. After each change,
Codex supplies exact commands and expected checks; the user returns results; Codex
analyzes them before proposing the next change. Do not automatically advance to the
next implementation step or launch long benchmarks.

For each cycle:

1. State the hypothesis, affected files, proposed diff, and success criteria.
2. Obtain approval for that implementation step.
3. Apply only that step and perform agreed, focused checks.
4. Hand off build and benchmark commands to the user.
5. Record build identity, measurements, correctness results, and the decision here.
6. Keep, revise, or revert the individual change by agreement before continuing.

Prioritize quick diagnostic selections. Run the full corpus only after a candidate
passes focused correctness and performance checks. Keep changes independently reviewable.

## Starting evidence and scope

The current corpus has 767 PDFs and 54,730 pages. The standalone C++ render run at
scale 1, queue capacity 32, and words/lines enabled measured:

- 1 worker: 1494.200 s.
- 4 workers: 404.826 s.
- 8 workers: 225.652 s; 6.62x speedup and 82.8% efficiency.

The Python backend replay measured 1514.998 / 484.586 / 337.128 s at 1/4/8 workers.
Its construction/load bucket was 35-37 s. That runner creates one parser per
document, consumes models/images/separator geometry, and includes loading in its
timer. C++ preloads documents outside timing and drains native results. These
differences prevent treating the gap as Python language overhead.

Read-only probes from this conversation:

- First 512 pages of 1593260.pdf: standalone C++ 4/8 workers, 1.639 / 1.403 s
  (single runs).
- First 256 pages of 2216123.pdf: standalone C++ 4/8 workers, 7.346 / 5.109 s
  (single runs).
- Three-repetition raw-binding medians showed the same weak text scaling.
- A separate text probe attributed 7.12 of 8.84 summed native render worker-seconds
  to glyph metrics. Worker elapsed time is not CPU time.
- A separate vector probe spent 6.10 consumer-seconds in get_shape_lines() during
  8.71 s of page processing. This combines native queries, binding conversion,
  and Python model/coordinate work; those components were not isolated.

These are diagnostic observations, not controlled baseline/candidate comparisons
or forecasts of corpus-wide improvement. Earlier investigations used different
datasets/settings; see [the earlier investigation](../completed/render-performance-investigation.md).
The current Python report is
[the November 4 report](../../scratch_perf_docling_benchmark_2026_11_04_08_40_14/report.json).

## Step 0: Capture a quick baseline with the user's build

The user supplied [build.sh](../../build.sh) and selected
[build.log.v1](../../build.log.v1) as the baseline. This supersedes the earlier probe
numbers for candidate comparisons. The script builds run_scaling.exe and runs
the text/vector selections below with 1,4,8 threads. Keep that script and the
baseline log unchanged for the first candidate.

| Selection | Pages | 1 thread (s) | 4 threads (s) | 8 threads (s) | Errors |
|---|---:|---:|---:|---:|---:|
| Text: 1593260.pdf | 512 | 3.979 | 1.255 | 1.356 | 0 |
| Vector: 2216123.pdf | 256 | 23.994 | 7.018 | 4.982 | 0 |

These are single-run observations, not medians; no baseline spread was measured.
Use them for the first quick iteration as requested, and collect repetitions if
the candidate difference is small or inconsistent. Timing CSV export was disabled.

Recorded before applying Step 1:

- Source HEAD: `01a430f57823365112fb8f9dded887e806524ca3`; tracked working tree clean.
- Build flags: Release, `-O3 -DNDEBUG`, C++20, arm64; compiler path
  `/Library/Developer/CommandLineTools/usr/bin/c++`.
- Baseline log SHA-256:
  `d705ec653bc0e58b8fb35a3c7288b2aeccbaa893f46670d03b5d6e8f96daa7bb`.
- Build script SHA-256:
  `e8df6a3b7a8de210075b214240b70a23bfea01d0e241cc6477f2f343e62bb4f4`.
- Executable present at handoff, SHA-256:
  `443e83a0c6e26cc8b623084e4acbdb8423cb4667595517f2c9ea4a14adca2562`.
- Exact compiler version and dependency revisions were not captured in the log.

No production edits. Confirm the current Release build and record its source
revision/working diff, compiler, dependencies, and command. Keep those fixed for
baseline/candidate comparisons.

From the repository root, use the existing configured build:
```sh
cmake --build build --target run_scaling.exe --parallel 8

PDF_ROOT=/Users/taa/Documents/projects/_data/bo767/pdf

./build/run_scaling.exe "$PDF_ROOT/1593260.pdf" \
  --mode render --threads 4,8 --max-pages 512 \
  --max-concurrent-results 32 --scale 1 \
  --create-word-cells=true --create-line-cells=true

./build/run_scaling.exe "$PDF_ROOT/2216123.pdf" \
  --mode render --threads 4,8 --max-pages 256 \
  --max-concurrent-results 32 --scale 1 \
  --create-word-cells=true --create-line-cells=true
```

For additional repetitions when needed, run one discarded warm-up, then three
measured invocations per selection. Each
invocation is a fresh process. Alternate --threads 4,8 and --threads 8,4 between
invocations to expose ordering effects. Report median and range for each worker
count, pages, and errors. If a result is close to the run-to-run variation, repeat
only enough to resolve that uncertainty.

Optional existing coarse timing export:
`--enable-timing --timing-csv /private/tmp/render-scaling-baseline-text-r1.csv`.
Use a different filename for each selection, build, and repetition: the current
writer appends. Keep timing enabled identically for baseline/candidate comparisons.
PNG output is for correctness checks and stays outside throughput measurements.

## Step 1: Cache substituted-font glyph bounds

Hypothesis: substituted/system fonts repeatedly extract outlines because
glyph_bbox_face_key stays null. Reusing bounds reduces glyph-metrics work and
allocator contention, improving text throughput and possibly 4-to-8 scaling.

First patch: only src/render/blend2d_renderer.h. Populate an identity string from
the actual selected Blend2D font face for the substituted-font path, and pass its
address through the existing bounds-cache interface. Keep the embedded-font path,
glyph/size key, capacity limit, eviction policy, and worker-local ownership.

Proposed core diff:
```diff
-            const std::string* glyph_bbox_face_key = nullptr;
+            std::string substituted_face_key;
+            const std::string* glyph_bbox_face_key = nullptr;
+            if (not using_embedded_font)
+              {
+                substituted_face_key =
+                  "blend2d-face:" + std::to_string(font.face().unique_id());
+                glyph_bbox_face_key = &substituted_face_key;
+              }
             if (using_embedded_font)
```

Review actual face selection and identity lifetime before applying. Do not combine
this first experiment with a numeric cache-key refactor or unrelated rendering changes.

Correctness:

- Compare decoded pixels with cache capacity 65536 versus 0 on the same host/build.
- Cover repeated substituted glyphs across pages, different faces and sizes,
  embedded fonts, and CJK/FreeType fallback.
- Verify identical dimensions and extraction metadata, and zero new errors.
- Use existing focused render tests where applicable. Rebuild the extension before
  Python render checks; rebuilding run_scaling.exe alone does not update it.
- Do not regenerate groundtruth or relax tolerances to accommodate the change.

Performance: repeat Step 0 commands on the candidate. Capacity 0 is a useful
same-build diagnostic, but the primary performance comparison is baseline versus
candidate at the original capacity. Assess both worker counts and the vector
selection for regressions. Claim a benefit only when it exceeds observed variation.

Checkpoint: user reviews the results before any instrumentation step. If unclear,
propose a small diagnostic for cache hits/misses and outline extractions rather
than adding another optimization. Do not promise corpus gains from this text probe.

### Step 1 implementation and user handoff

Applied the proposed core diff in `src/render/blend2d_renderer.h`. The substituted
face identity is read from `font.face()` after shaping and system/script fallback
selection. The local identity string outlives the bounds-cache call, and the cache
copies its keys. Blend2D assigns face IDs through its unique-ID generator. The
embedded-font key path and the glyph/size key remain unchanged.

The user owns compilation and benchmark execution in this iteration. Run from
the repository root:

```sh
bash build.sh > build.log.v2 2>&1
```

The user compiled and supplied `build.log.v2`. Commands, printed settings, page
counts, and zero error counts match v1. Text times are 3.715 / 1.042 / 0.595 s;
vector times are 25.080 / 7.241 / 5.037 s at 1/4/8 threads. At eight threads the
text selection takes 56.1% less time; the vector selection takes 1.1% more time.
These remain single runs without a measured spread.

The user also reported a 54,730-page corpus run: 1597.433 / 395.515 / 207.843 s,
all with zero errors. Relative to the original corpus run, one-thread time rises
6.9%, four-thread time falls 2.3%, and eight-thread time falls 7.9%. The 4-to-8
speedup improves from 1.79x to 1.90x. Reported eight-thread efficiency rises to
96.1%, partly because the new one-thread baseline is slower. The user approved
proceeding to instrumentation to explain remaining costs and that slowdown.

Subsequent corpus repeat after recompiling the instrumented driver, reported in
chat at 2026-11-04 16:29 (no `--enable-timing`): 1531.978 / 404.323 / 239.116 s,
zero errors, with the same printed decode/render settings and 54,730 pages.

| Threads | Original corpus (s) | First post-cache corpus (s) | Latest corpus (s) |
|---|---:|---:|---:|
| 1 | 1494.200 | 1597.433 | 1531.978 |
| 4 | 404.826 | 395.515 | 404.323 |
| 8 | 225.652 | 207.843 | 239.116 |

Latest versus first post-cache: -4.1% / +2.2% / +15.0% elapsed time at 1/4/8
threads. Latest versus original: +2.5% / -0.1% / +6.0%. The latest 4-to-8 speedup
is 1.69x and reported eight-thread efficiency is 80.1%. The earlier full-corpus
improvement is therefore not established as reproducible. The focused text gain
remains supported separately; do not extrapolate it to a stable corpus-wide gain.

The substituted-font cache patch is still present and build flags remain Release
`-O3 -DNDEBUG`. Detailed collection/export was disabled for this repeat. That
rules out active CSV export as its cause, but does not rule out effects from the
compiled instrumentation changes (result layout/worker code) or changing runtime
conditions. These runs do not isolate either explanation. A fresh-process
eight-thread corpus run with `--enable-timing --timing-csv` would supply missing
stage attribution without another long one-thread run. Establishing whether the
instrumentation patch itself regresses performance needs a matched A/B with the
same renderer and old/new benchmark drivers; do not label this difference ordinary
noise or a confirmed code regression from these observations alone.

Pixel-equivalence checks remain pending; zero errors do not establish equivalent
rendering. The existing Python extension retains its renderer until separately rebuilt.

## Step 2: Attribute remaining native costs in separate patches

Each substep requires its own diff and approval. Instrumentation should preserve
output and scheduling, with bounded overhead and worker-local collection.

2a. Extend app/run_scaling.cpp reporting:

- Retain decoder construction, decoding, word creation, line creation, and rendering.
- Measure canvas extraction separately; current render_page_s excludes it.
- Measure worker result-queue blocking and consumer wait for results separately.
- Report wall time, summed worker elapsed stages, and the slowest document/pages.
- Export useful existing renderer detail timings, including glyph metrics and image
  work; the standalone driver currently does not merge renderer detail timings as
  the production threaded renderer does.
- Keep diagnostics optional; version the extended CSV or require a new output file.

2b. Split QPDF decoder-construction attribution in
src/parse/pdf_decoders/document.h and src/parse/utils/pdf_timings.h:

- Distinguish source-document mutex wait from protected work, including integrity
  checks and page extraction/serialization.
- Distinguish unlocked buffer copying and standalone-page parsing.
- Preserve protection of shared QPDF state, including QPDFWriter::write().
- Publish timing data safely without unnecessarily extending critical sections.
- Label nested timers so parent and child durations are not added together.

Validate each instrumentation patch on the quick selections with diagnostics on/off.
Do not interpret summed elapsed stages as CPU time, queue waits as additional
non-overlapping wall time, or total decoder construction as mutex contention.

After validation, the user runs a 4/8-worker corpus timing pass. Rank operations
and slow pages by measured contribution before selecting Step 3.

### Step 2a implementation and user handoff

Files: `app/run_scaling.cpp`, `build.sh`, and this plan. Step 2b/QPDF internals
are unchanged. Diagnostics are enabled only with `--enable-timing`.

Per-page CSV schema 2 adds canvas extraction, PNG saving, native detail collection,
worker result-queue wait, consumer result wait, and `detail_timings_json`. The
worker total includes diagnostic collection and excludes queue blocking. Both
queue timers include mutex acquisition and condition-variable waiting. Consumer
waits overlap native work; they are not extra wall time or worker CPU time.

Detail export combines existing static decode/render timing keys, including glyph
metrics, codecs, and bitmap drawing. Dynamic per-font/resource keys are omitted
to bound the export vocabulary. Native details retain nested timing semantics.
No extra shared timing mutex or per-glyph diagnostic counters were introduced.

With diagnostics enabled, each run prints aggregate stages, consumer CSV time,
the native detail tree, ten slowest pages, and ten documents with the most summed
page worker time. Document rankings are not per-document wall times. Wall timing
includes collection, aggregation, CSV serialization, and the final CSV flush;
summary printing is outside the wall timer. Detail-collection time does not measure
all diagnostic overhead; compare enabled/disabled runs to assess the total effect.

The CSV writer checks headers before starting workers, rejects incompatible
existing files, and checks the final write/flush. Compatible schema-2 appends are
allowed, so use a fresh prefix for each invocation. `build.sh` stops on build/run
failure and accepts optional `TIMING_PREFIX`; without it the workload and CLI
settings stay as before.

User commands from the repository root:

```sh
# Build and run with diagnostics, using new output names.
TIMING_PREFIX=timing-v3 bash build.sh > build.log.v3 2>&1

# Optional matching run without diagnostics to assess instrumentation overhead.
bash build.sh > build.log.v3.no-timing 2>&1
```

The instrumented invocation creates `timing-v3.text.csv` (1536 page rows) and
`timing-v3.vector.csv` (768 page rows), each covering 1/4/8 threads and one header.
Return both CSVs and the log. Check page/error counts, finite nonnegative timers,
canvas time versus native `render_page.get_canvas`, glyph metrics, image decode
and rendering, queue waits, and ranked slow pages. Preserve diagnostic overhead
labels when interpreting the results. Keep throughput comparisons under the same
diagnostic setting. The user completed the instrumented quick runs; results follow.

Codex validation: C++ syntax checked with the configured target's compiler flags;
the actual CSV/summary helper code was separately compiled and exercised for
quoted/newline paths, JSON round trips, schema/header rejection, compatible
append, disabled output, stage aggregation, and bounded top-ten ranking. The
actual shell argument flow passed with diagnostics off/on and a prefix containing
spaces, using mocked build/run commands. Shell syntax and patch whitespace checks
passed. These checks do not replace the user's native render/queue runtime test.

Before a corpus pass, review quick-selection diagnostics and enabled/disabled
overhead. Do not start Step 2b or another optimization automatically.

### Step 2a results: build.log.v3 and timing-v3 CSVs

Evidence: [log](../../build.log.v3), [text CSV](../../timing-v3.text.csv), and
[vector CSV](../../timing-v3.vector.csv). The optional `build.log.v3.no-timing` was
not provided. Comparing v3 with the earlier v2 run is a sanity check, not a
controlled measurement of instrumentation overhead.

Validation passed: 1536 text rows and 768 vector rows; exactly one successful row
per selected page at each of 1/4/8 threads; schema 2; no errors; finite nonnegative
coarse/detail times. All 66 coarse stage sums agree with the console summaries
within one microsecond. Coarse canvas time agrees with the internal canvas timer
within 0.13 ms per run. Coarse processing stages account for the worker totals
with less than 0.6 ms aggregate residual per run.

| Selection | 1 thread wall (s) | 4 threads wall (s) | 8 threads wall (s) |
|---|---:|---:|---:|
| Text | 3.816365 | 1.028399 | 0.586744 |
| Vector | 24.324593 | 7.062024 | 4.995825 |

At eight threads, summed worker elapsed seconds are:

| Stage | Text | Vector |
|---|---:|---:|
| Decoder construction | 0.076877 | 0.114574 |
| Decode | 2.347884 | 19.219596 |
| Word creation | 0.357343 | 0.153619 |
| Line creation | 0.000821 | 0.000521 |
| Render, excluding canvas | 1.838026 | 6.554795 |
| Canvas extraction | 0.024071 | 0.080472 |
| Detail collection | 0.004942 | 0.004911 |
| Total page processing | 4.650555 | 26.129046 |
| Worker result-queue wait, outside processing total | 0.000051 | 0.000022 |

Text findings:

- Decode accounts for 50.5% of summed worker time, rendering 39.5%, words 7.7%.
- Glyph metrics cost 0.081915 worker-seconds: 1.8% of total processing and 5.4%
  of the 1.526432 seconds spent rendering text. It is no longer the main hotspot.
- From four to eight threads, summed processing rises 4.105129 -> 4.650555 s.
  Rendering accounts for 71.2% of this increase; virtually all of its increase
  falls within the text-rendering parent, rather than bitmap drawing or canvas.
- The ratio sum(page processing)/(threads * wall) is 99.1% at eight workers.
  This indicates little time outside measured page processing, not 99.1% CPU
  utilization: page elapsed time includes internal waits and descheduling.
  Reduced scaling here primarily appears as inflated per-page processing time.

Vector findings:

- Decode accounts for 73.6% of worker time; rendering accounts for 25.1%.
- Decode-content detail includes operator self-time 6.760875 s, image work
  6.366586 s, and content tokenization 5.404598 s. Image sub-buckets include
  decode-array handling 1.626112 s, JPX 1.412223 s, JPEG 0.935830 s, and CCITT
  0.790059 s. Bitmap drawing costs 3.376102 s; shape drawing costs 2.439307 s.
- Pages 55 / 162 / 153 take 3.734150 / 3.715480 / 1.693593 worker-seconds at
  eight threads. Their decode portions are 3.153920 / 3.181033 / 1.626227 s,
  dominated by tokenization and operator execution rather than page construction.
- Those three pages represent 35.0% of worker processing and 69.6% of content
  tokenization. Page 162 is the last completed result at both four/eight threads.
- Measured processing occupancy falls to 65.4% at eight threads. Together with
  negligible queue blocking and the last long-running pages, this supports a
  significant tail/load-imbalance limit in this selection. Start/end timestamps
  were not recorded, so its complete execution timeline cannot be reconstructed.
  A full corpus offers more independent work and may hide such individual tails.

Diagnostic collection costs about 4-5 ms of summed worker time per run; CSV
serialization/writes take about 9-12 ms on the consumer. These do not include all
diagnostic overhead and overlap native work; do not add them to wall time or
claim a precise overhead percentage without the matching disabled run.

Interpretation caveats:

- The console tree's `%branch` denominator is the sum of measured sibling rows,
  not the parent's full duration or the worker total. Use seconds and the coarse
  totals above for hotspot shares; do not add nested detail rows.
- Source documents are reused across the 1/4/8-thread runs. Text page 461 takes
  127.33 ms in decoder construction at one thread, but 0.293 / 0.322 ms at four/
  eight threads. Warmed document state is a plausible explanation; the exact
  operation is not isolated. Fresh-process runs per thread count are needed to
  separate such warm-up effects from parallel scaling. These samples cannot
  explain the full-corpus one-thread variation by themselves.
- Successful processing does not establish pixel equivalence; that check remains
  pending from Step 1.

Recommendation for user review: retain instrumentation and collect a full-corpus
4/8-thread timing pass before selecting the next optimization. Decoder construction
is only 1.7% of text and 0.4% of vector worker time in these selections, so they do
not justify prioritizing QPDF lock optimization. Step 2b remains a possible next
diagnostic if the corpus identifies expensive preparation on other documents.
The measured candidates here are remaining text-render work and heavy vector
content tokenization/operator execution, with image handling another substantial
absolute cost. No further production change is authorized by recording this review.

### Step 2a corpus results: build.log.corpus-8-r1

Evidence: [log](../../build.log.corpus-8-r1) and
[CSV](../../timing-corpus-8-r1.csv). This invocation ran only eight threads in a fresh
process, at scale 1, queue capacity 32, words/lines enabled, and memory release
disabled, with schema-2 diagnostics enabled.

Validation: 54,730 rows, 767 documents, unique complete page ranges, all successful,
no errors, and finite nonnegative coarse/detail timings. All eleven coarse CSV
sums agree with the log within one microsecond.

Wall time: 209.694757 s, 261.0 pages/s. This is 0.9% slower than the first
post-cache corpus run (207.843 s), 12.3% faster than the latest uninstrumented
repeat (239.116 s), and 7.1% faster than the original 225.652 s baseline. The new
binary can reproduce approximately the earlier fast eight-thread throughput with
diagnostics active; these runs still do not isolate the cause of the 239 s result.
This fresh-process execution differs from eight threads following 1/4-thread
trials in the same process, so it is not a controlled overhead or scaling test.

| Coarse stage | Summed worker-seconds | Share of page processing |
|---|---:|---:|
| Decoder preparation | 34.303817 | 2.0% |
| Decode | 960.990884 | 57.3% |
| Word creation | 24.332178 | 1.5% |
| Line creation | 0.082485 | 0.005% |
| Rendering, excluding canvas | 650.684506 | 38.8% |
| Canvas extraction | 5.598266 | 0.3% |
| Detail collection | 0.670723 | 0.04% |
| Total page processing | 1676.747605 | 100% |

Worker result-queue blocking sums to 0.436177 s (0.026% of page processing).
Consumer wait is 196.441936 s; CSV serialization/write/flush takes 1.748026 s.
These consumer durations overlap native work. The ratio of summed page processing
to eight times wall time is 99.95%; this is processing-section occupancy, not CPU
utilization or parallel efficiency. It indicates negligible aggregate scheduling
and end-tail gaps, unlike the isolated vector selection. Inside-stage contention
and descheduling remain included in the worker times.

Largest image buckets:

- Native image preparation/decode: 643.284497 worker-seconds (38.4% of processing).
- Bitmap rendering: 464.017553 s (27.7%). These two disjoint parent buckets total
  66.0% of measured page processing; do not add their nested children again.
- Image phase detail: CCITT 315.426316 s, /Decode mapping 134.691101 s, JPEG
  64.187466 s, JPX 50.266243 s, JBIG2 40.266677 s. Existing image_phase_timer
  subtracts nested phase durations, so /Decode time is excluded from an enclosing
  CCITT/JBIG2 phase. Codec buckets can still include sample expansion and other
  branch work, rather than only the codec function itself.
- The highest-cost documents include 1149133.pdf (73.84 processing s),
  1385012.pdf (71.46 s), 1072248.pdf (54.09 s), and 1495736.pdf (52.71 s).
  Each is dominated by image work. The first is a useful CCITT/grayscale probe:
  CCITT 33.23 s, /Decode mapping 10.49 s, bitmap rendering 29.93 s.

Remaining text/vector observations:

- Text rendering totals 172.634600 s (10.3% of processing), including glyph metrics
  65.731065 s (3.9% of processing) and font resolution 32.032363 s. Glyph metrics
  remain relevant on some full documents, despite being small in the first
  512-page text probe. Full 1593260.pdf contributes 16.16 glyph-metrics seconds;
  the short selection was not representative of every page in that document.
- Content tokenization totals 44.057850 s (2.6%), operator self-time 135.204209 s
  (8.1%), and shape rendering 8.781570 s (0.5%). The three highlighted heavy
  vector pages are expensive individually but do not create a corpus end tail.
- Decoder preparation is 2.0% overall; 1106182.pdf and 2182429.pdf contribute
  about 3.9 s each. This does not make QPDF-lock optimization the first corpus
  priority, though preparation timing still combines waits and protected work.

Proposed at corpus review and subsequently approved as Step 3a: optimize the single-component
/Decode application in src/parse/pdf_states/bitmap.h. The current helper applies
floating-point mapping and lround for every byte. A 256-entry table generated
using the existing mapping function can preserve rounding/clamping exactly while
replacing repeated mapping with byte lookups; identity mappings may be skipped.
Keep multi-component and other decode paths unchanged in the first patch. The
134.69-second aggregate is an opportunity bound, not a promised saving; this
first patch would address only the applicable single-component portion.

Capture a baseline on a short 1149133.pdf selection before applying it, preserve
the existing text/vector probes as controls, compare all 256 byte outputs for
representative Decode pairs, and validate image/mask polarity and pixel equality.
Bitmap conversion/downsampling (inside build_bitmap_image) and CCITT internals are
subsequent candidates; subdivide bitmap preparation versus drawing if needed.
No production changes were made while recording this review.

## Step 3: Optimize one measured C++ bottleneck at a time

Choose the next patch from the corpus measurements; this is not advance approval
for any particular algorithm:

- Image decode, conversion, downsampling, or drawing if image costs dominate.
- Repeated immutable resource work if repetition is demonstrated.
- Scheduling or serialized extraction only if lock waits or worker tails justify it.
- Cache-key/allocation refinements if remaining glyph costs justify them.

For each candidate, name the hotspot, exact files, preserved output contract,
focused correctness cases, and diagnostic selection before requesting approval.
Preserve image scale, words/lines/shapes, fonts, masks, and release/queue settings.

Once focused gains are convincing, confirm on the corpus. Obtain a candidate
1/4/8-worker result before reporting candidate parallel efficiency; an old
one-worker baseline cannot establish the new build's efficiency.

### Step 3a: Single-component Decode lookup implementation

Approved after the user committed the current renderer/benchmark changes at
`e5748a095d740fe52bfaed300a170a6c36fec3a6`. The tracked working tree was clean
before this step. Keep that commit as the source baseline for comparisons.

Implementation in `src/parse/pdf_states/bitmap.h`:

- For one component and an exact [0 1] mapping, leave samples unchanged.
- For other one-component mappings with at least 256 sample bytes, build one
  256-entry stack table using the existing `jpeg::apply_decode_component`, then
  replace each byte by lookup. This preserves the existing rounding/clamping.
- For smaller buffers, retain the scalar implementation to avoid table setup.
- Multi-component mappings and missing/invalid-input guards retain the scalar path.
- Keep existing phase timing around all branches, including table construction.
  No quality, scale, codec, colorspace, stencil, or alpha changes are included.

Added `tests/test_unit_image_decode.py`: 42 raw-sample PDF regression cases across
sizes 1/17/255/256/257/1024, implicit/explicit identity, inversion, a restricted
range, constant half-level rounding, clamping, and an unused extra Decode pair.
Expected bytes use simple independent integer mappings rather than the candidate
lookup implementation. This tests sample bytes independently of image resampling.

Validation completed by Codex:

- Extracted the actual committed scalar helper and patched helper into a native
  harness, compiled with -O3, and compared 20,448 cases / 12,026,832 sample bytes.
  Covered null/empty buffers, missing/short Decode arrays, disabled Decode,
  negative/zero component counts, one-/multi-component controls, table boundaries,
  all 256 byte values, deterministic random finite endpoints, and rounding edges.
  All outputs matched.
- Full run_scaling.cpp syntax passed using the configured Release target flags.
- The 42 PDF cases passed on the installed baseline Python extension. That
  extension has not been rebuilt, so these are baseline/oracle checks, not a
  candidate integration pass. Candidate native render checks remain pending.

Before rebuilding, captured baseline evidence under
`/private/tmp/docling-decode-step3-xhp3zca6/`:

- `baseline.json`, `baseline.log`, `baseline.csv`: first 64 pages of 1149133.pdf,
  threads 4,8, scale 1, queue 32, words/lines on, diagnostics on.
- Baseline wall times: 10.735 / 6.325 s at 4/8 threads (single runs).
- Baseline Decode-mapping worker times: 6.486879 / 7.765721 s at 4/8 threads.
- Existing executable SHA-256:
  `ca11903f707c372bbb5919777eecaf8dbd9781a2c22890d73e443125b7795c3b`.
- `pixel-inputs/`, `baseline-png/`, `baseline-pixels.json`, and
  `baseline-pixels.log`: 50 native-render reference fixtures, comprising the 42
  grayscale cases and 8 CCITT combinations of image/stencil, BlackIs1, and Decode
  polarity. Captured dimensions, modes, and pixel hashes. Compare after rebuilding
  the candidate; successful reference generation is not a candidate parity pass.

The temporary evidence is machine-local and may be removed by the OS. The
committed baseline source remains available if it needs to be reproduced.

User build and benchmark command:

```sh
TIMING_PREFIX=timing-v4 bash build.sh > build.log.v4 2>&1
```

`build.sh` retains the text/vector controls and adds the 64-page 1149133.pdf image
probe at 4/8 threads. With the timing prefix it produces `timing-v4.text.csv`,
`timing-v4.vector.csv`, and `timing-v4.image.csv`. Expected rows are 1536 / 768 /
128 respectively. Use a fresh prefix and return the log and all three CSVs.
Compare image-probe Decode-mapping time, codec/render times, wall time, errors,
and control workloads. Rebuild the Python extension separately before treating
the new pytest cases as candidate integration checks. After the native build,
Codex can render the saved 50 fixtures and check exact pixel equality.

Do not advance to another optimization before reviewing these results. The
single-run baseline and runtime variability remain limits on small speed claims.

### Step 3a results: build.log.v4

Evidence: [log](../../build.log.v4), [text CSV](../../timing-v4.text.csv),
[vector CSV](../../timing-v4.vector.csv), [image CSV](../../timing-v4.image.csv).
All 2432 rows have the expected unique page/thread combinations, successful
processing, empty error messages, and finite nonnegative timers. All 88 coarse
stage sums agree with the eight console summaries within one microsecond.

| Selection | Before wall, 1/4/8 threads (s) | v4 wall, 1/4/8 threads (s) |
|---|---|---|
| Text, v3 control | 3.816365 / 1.028399 / 0.586744 | 3.940015 / 1.052009 / 0.610861 |
| Vector, v3 control | 24.324593 / 7.062024 / 4.995825 | 24.273447 / 7.058102 / 5.049123 |
| Image, tool-captured baseline | Not run / 10.735 / 6.325 | Not run / 3.504592 / 1.817711 |

The text control is 2.3-4.1% slower; vector is between 0.2% faster and 1.1%
slower. These are single runs and do not establish whether the small text
difference is variation or a secondary effect. Decode-array worker time decreases
on both controls: text at eight workers 0.062134 -> 0.040434 s (34.9% reduction),
vector 1.626112 -> 1.403786 s (13.7%). The latter retains its vector-page tail.

The image probe's observed speedups against the tool-captured baseline are 3.06x
and 3.48x at four/eight threads. Do not attribute these whole-probe ratios to the
patch: the baseline was run through the tool runner and v4 through the user's
terminal, and unchanged phase timings changed substantially. At eight threads,
CCITT time fell 19.535502 -> 7.409871 s and bitmap rendering 22.289617 ->
6.223254 s, as well as Decode mapping 7.765721 -> 0.510320 s. Those large changes
outside the targeted operation confound the end-to-end comparison. Future
performance baselines should use the same launch context as candidate runs.

Independent cross-check using the same first 64 pages of 1149133.pdf from the
earlier user-run corpus CSV versus the v4 image probe, both at eight workers:

| Summed worker phase | Earlier corpus prefix (s) | v4 image probe (s) |
|---|---:|---:|
| Decode mapping | 2.327575 | 0.510320 |
| CCITT | 7.558430 | 7.409871 |
| Bitmap rendering | 6.852283 | 6.223254 |
| Total page processing | 16.781549 | 14.188148 |

The mapping decrease is 78.1% (4.56x), while CCITT is within 2.0%; this supports
the targeted benefit without the roughly threefold shifts in untouched phases
seen against the tool-run baseline. Corpus versus isolated-selection context and
remaining phase variation still prevent treating this as a controlled patch-only
wall-time speedup. Full-corpus candidate confirmation is the next performance gate.

Candidate executable SHA-256:
`c69512178e93cf46fc2ae3f8d2dc5dcfa97fd050828e723cd58ac9aa4227e6a0`.
Codex verified that the executable differs from the saved baseline, rendered all
50 saved fixtures with that candidate, and compared dimensions, modes, and decoded
pixel SHA-256 values: all 50 matched exactly. This includes every grayscale
mapping/size case and all CCITT image/stencil, BlackIs1, and Decode-polarity cases.
Results are in the existing temporary evidence directory as `candidate-png/`,
`candidate-pixels.log`, and `candidate-pixels.json`. The installed Python extension
was not rebuilt by build.sh; its separate raw-sample integration run remains
pending, while the new native executable passed the saved rendering comparisons.

Decision: retain the candidate, record the small control-workload changes, and
confirm corpus throughput/phase savings before proceeding to another optimization.
Use a fresh-process eight-thread run with the same diagnostics and settings as
`build.log.corpus-8-r1`, writing new log/CSV names. No production changes were made
while reviewing v4.

### Step 3a full-corpus confirmation: corpus-8-v4-r1

Evidence: [candidate log](../../build.log.corpus-8-v4-r1) and
[candidate CSV](../../timing-corpus-8-v4-r1.csv), compared with
[baseline log](../../build.log.corpus-8-r1) and
[baseline CSV](../../timing-corpus-8-r1.csv).

Both runs use eight threads in a fresh process. CLI arguments and printed native
decode/render settings match except the CSV destination. Each CSV has the same
54,730 unique document/page combinations across 767 documents, all successful,
with no errors and finite nonnegative timers. All eleven coarse CSV sums agree
with the corresponding log within one microsecond.

The current executable still matches the SHA-256 of the candidate that passed
all 50 saved pixel comparisons. This is a single before/after pair, not a measured
distribution, but the stage attribution is substantially cleaner than the earlier
tool-run image baseline.

| Metric | Baseline | Candidate v4 | Change |
|---|---:|---:|---:|
| Wall seconds | 209.694757 | 197.160286 | -5.98% |
| Pages/second | 260.998 | 277.591 | +6.36% |
| Summed worker processing seconds | 1676.747605 | 1576.448868 | -100.298737 s |
| Decode mapping worker-seconds | 134.691101 | 25.313180 | -81.21% |
| Decode worker-seconds, parent | 960.990884 | 845.068297 | -115.922587 s |
| Rendering worker-seconds | 650.684506 | 666.896524 | +2.49% |

Decode mapping saves 109.377921 worker-seconds. Total image preparation/decode
saves 109.839487 s, closely tracking that targeted reduction. Other image phases
are nearly unchanged: CCITT 315.426316 -> 314.853990 s; JPEG 64.187466 ->
64.204625 s; JPX 50.266243 -> 50.389366 s; JBIG2 40.266677 -> 40.421421 s.
All four change by less than 0.5%, unlike the confounded short image baseline.

Several scan documents almost eliminate Decode mapping time, consistent with the
identity shortcut: 1385012.pdf 8.431833 -> 0.000158 s, 1495736.pdf 7.545059 ->
0.000073 s, and 1072248.pdf 7.439466 -> 0.000182 s. 1149133.pdf decreases
10.491883 -> 2.379630 s. No new component counters were added, so do not assign
the complete corpus saving precisely between identity skipping and lookup use.

Rendering rises 16.212018 worker-seconds: bitmap rendering 464.017553 ->
475.826453 s, text rendering 172.634600 -> 177.173420 s. This offsets part of
the mapping benefit. Keep this 2-3% change visible rather than automatically
labeling it noise or a code regression; the pair does not isolate its cause.
Word creation and canvas extraction remain effectively unchanged.

Net worker saving divided by eight is 12.537342 s, close to the observed
12.534471 wall-second saving. Measured processing occupancy is 99.95% in both
runs; producer queue blocking remains negligible at 0.498038 worker-seconds in
v4. Occupancy is not CPU utilization or 1-to-8 parallel efficiency.

Decision for review: retain/commit Step 3a. The next major measured opportunity
is bitmap rendering (475.83 worker-seconds, 30.2% of processing), followed by
CCITT processing (314.85 s, 20.0%). Remaining Decode mapping is 25.31 s (1.6%).
Recommend splitting bitmap image construction/conversion/downsampling from
drawing before choosing the next bounded production patch. Do not automatically
extend this lookup optimization to additional component counts. No production
changes were made during this evaluation.

### Step 3b1: Subdivide bitmap rendering timings

User approved starting bitmap work after committing the preceding changes at
`2b219ff` (tracked working tree clean at the start of this step). The v4 corpus
attributes 475.826453 worker-seconds, 30.2% of processing, to bitmap rendering.
First distinguish preparation from painting before selecting an algorithm change.

Patch: only `src/render/blend2d_renderer.h` and
`src/parse/utils/pdf_timings.h`, adding scoped timers and four registered keys.
No bitmap arithmetic, resampling, clipping, or rendering-policy changes.

| Detail key | Timing boundary |
|---|---|
| `render_page.bitmap.prepare` | Entire build_bitmap_image call: allocation, conversion, and optional box averaging |
| `render_page.bitmap.prepare.convert` | Preparation when output dimensions equal decoded source dimensions; includes allocation |
| `render_page.bitmap.prepare.downsample` | Preparation when dimensions differ; includes allocation, conversion, and existing premultiplied box averaging |
| `render_page.bitmap.paint` | Work after preparation: alpha/blend setup, clipping, masks, compositing, drawing, and context-state restoration |

The prepare parent contains exactly one convert/downsample child per image; both
children can appear on a page containing different images. Do not add the prepare
parent to its children. Bitmap parent time minus prepare/paint includes geometry,
validation, sizing, source-image destruction, and timer bookkeeping. Paint does
not include source-image destruction, because that image outlives the paint timer.
All ordinary, affine, mask, and early-return branches retain their previous logic.

The keys are registered as static and in the timing hierarchy so the benchmark's
existing collector includes them in its detail JSON and console tree. Schema 2
and coarse CSV columns are unchanged; old files simply lack these new detail keys.
Renderer scoped timers use the existing native timing collector, including when
benchmark export is disabled; `--enable-timing` controls collection into the
benchmark's CSV/summary. Review overhead rather than assuming these timers free.

Codex checks passed: configured Release-target C++ syntax, static-key registration,
parent/child hierarchy, aggregation, tree formatting, JSON round trip, shell syntax,
and patch whitespace. No full executable rebuild or new native render pass was
performed: the user owns that next step. The current executable still matches v4's
pixel-validated SHA-256, so the saved 50 fixtures can be reused after rebuilding.

User command from the repository root:

```sh
TIMING_PREFIX=timing-v5 bash build.sh > build.log.v5 2>&1
```

Return the log and `timing-v5.text.csv`, `timing-v5.vector.csv`, and
`timing-v5.image.csv`. The script and probe selection remain unchanged. Compare
coarse bitmap time against v4, confirm new details survive export, and rank
prepare.convert / prepare.downsample / paint. Expect no algorithmic speedup from
this step; it selects the next bounded optimization. Check pixel equality using
the saved fixtures after the native build. Do not start another production edit
until the user results are reviewed. A full-corpus timing pass can follow if the
quick image/vector probes do not establish the relevant split clearly.

### Step 3b1 results: build.log.v5

Evidence: [log](../../build.log.v5), [text CSV](../../timing-v5.text.csv),
[vector CSV](../../timing-v5.vector.csv), [image CSV](../../timing-v5.image.csv).
All 2432 expected page/thread rows succeeded, with finite nonnegative timing
values and valid hierarchy bounds: convert + downsample <= prepare, and
prepare + paint <= bitmap parent. All 88 coarse CSV sums agree with the printed
summaries within one microsecond. New timing keys survive collection/export.

At eight threads, summed bitmap worker-seconds are:

| Probe | Bitmap total | Prepare/convert | Prepare/downsample | Paint |
|---|---:|---:|---:|---:|
| Text | 0.282018 | 0.046135 | 0.171806 | 0.063842 |
| Vector | 3.393653 | 0.076643 | 3.084250 | 0.231570 |
| Image | 6.086520 | 0 | 5.982449 | 0.103805 |

The two prepare children include allocation and conversion; downsample also
includes the existing box averaging. These are not pure filtering-only timings.
Reduced-size preparation accounts for 60.9% / 90.9% / 98.3% of bitmap time in
the text/vector/image probes respectively. Painting accounts for 22.6% / 6.8% /
1.7%. Remaining geometry/setup/cleanup time is below 0.1% of each bitmap total.
The four-thread split is consistent with the eight-thread split.

| Probe | v4 wall 1/4/8 threads (s) | v5 wall 1/4/8 threads (s) |
|---|---|---|
| Text | 3.940015 / 1.052009 / 0.610861 | 3.914988 / 1.064102 / 0.625805 |
| Vector | 24.273447 / 7.058102 / 5.049123 | 24.657404 / 7.148703 / 5.163125 |
| Image | Not run / 3.504592 / 1.817711 | Not run / 3.564035 / 1.784009 |

All changes are between -1.9% and +2.5%; these single pairs do not isolate
instrumentation overhead from run variation. No algorithmic gain is claimed.

Codex ran the current native executable on the 50 saved fixtures. Dimensions,
modes, and decoded pixels match every baseline image exactly, covering grayscale
mapping/size cases and CCITT image/stencil polarity. Candidate SHA-256:
`fb33c3066e6241ce4fa6752c015ac5baec31b8c0a700e8ae977996489122557e`.
Artifacts are in the existing temporary evidence directory as
`bitmap-timing-v5-png/`, `bitmap-timing-v5-pixels.log`, and
`bitmap-timing-v5-pixels.json`. No implementation changes were made in this review.

Recommended next patch for review: specialize reduced-size preparation for
opaque, one-channel grayscale data in build_bitmap_image. Read source bytes
directly after the existing buffer validation; accumulate one grayscale sum per
output box instead of converting each source sample to PRGB32 and accumulating
three identical channels. Replicate the averaged gray value into RGB afterward.
Preserve source-box boundaries, integer division/rounding, and existing unsigned
accumulation behavior, including alpha arithmetic. Keep RGB/CMYK, image stencils,
soft-mask alpha, and same-size conversion on the generic path in the first patch.

Validate against the committed generic implementation across random grayscale
samples, odd sizes, one-/two-axis reduction and varied ratios, then compare native
rendered pixels and measured downsample time. The image probe provides a relevant
target and the existing text/vector probes remain controls. Corpus-wide gains
must be measured; do not extrapolate the image probe's 98.3% share to every bitmap.
The user subsequently approved this algorithm change; implementation follows.

### Step 3b2: Opaque-grayscale downsample specialization

Implemented in `src/render/blend2d_renderer.h`, inside `build_bitmap_image`.
The fast path applies only to one-channel GRAY data without an image stencil or
soft-mask alpha, with at least one reduced dimension and neither dimension enlarged.
It reads the validated source bytes directly and accumulates one grayscale sum
per output box. It then replicates the averaged value into RGB. The generic path
continues handling other formats, masks, alpha, same-size conversion, and upsampling.
The existing v5 timing scopes cover the new branch without additional timers.

Source-box boundaries and integer division match the generic implementation.
Unsigned accumulation behavior is preserved, including alpha overflow for unusually
large boxes: alpha uses `255u * n / n`, rather than assuming the result is always 255.
This patch does not change the filtering algorithm or rounding.

Focused candidate validation passed:

- Extracted the actual committed generic builder from `2b219ff` and the candidate
  builder, compiled both against the existing real Blend2D library/image buffers,
  and compared 6,018 cases / 5,930,558 output pixels exactly. An instruction-state
  fixture supplies the getters; this is a builder comparison, not full integration.
  Coverage includes 442 fast-path calls, random/black/white/gradient samples,
  odd ratios, one-/two-axis reductions, same-size/upsampling controls, RGB/CMYK,
  masks/alpha, and two large boxes that overflow the generic alpha accumulator.
- Configured Release-target C++ syntax check passed without rebuilding the executable.
- Added 30 PDF regression cases in `tests/test_unit_gray_downsample.py`, comparing
  opaque grayscale against the generic fully opaque soft-mask path. All 30 pass
  on the installed baseline Python extension; candidate integration awaits rebuild.
- Full `uv run pre-commit run --all-files` passed: Ruff formatting/lint, MyPy,
  and uv-lock. Patch whitespace checks also passed.

Native comparison artifacts and `validation.json` are saved in
`/private/tmp/docling-gray-step3b2-ddyqx068/`. Before rebuilding, the v5 executable
rendered 60 new PDF fixtures into that directory's `baseline-png/`; all 30 paired
opaque/soft-mask reference images match exactly. `baseline-pixels.json` records
the command, binary identity, dimensions, modes, and decoded-pixel hashes.
These references plus the 50 existing Decode/CCITT fixtures permit 110 native
pixel comparisons after the user's rebuild.

No executable or Python extension was rebuilt by Codex. The current executable
still has v5 SHA-256
`fb33c3066e6241ce4fa6752c015ac5baec31b8c0a700e8ae977996489122557e`.
The performance baseline remains the user's v5 run above; correctness-only runs
from the tool environment are not performance comparisons.

User command from the repository root:

```sh
TIMING_PREFIX=timing-v6 bash build.sh > build.log.v6 2>&1
```

The script and probe selections are unchanged. Return the log and
`timing-v6.text.csv`, `timing-v6.vector.csv`, and `timing-v6.image.csv`.
Expected row counts remain 1,536 / 768 / 128 respectively. Review wall time and
bitmap downsample worker-time against v5, using text/vector as controls, then
perform the 110 saved native pixel comparisons. No speedup is claimed before
those measurements. Do not advance to another optimization automatically.

### Step 3b2 results: v6 quick probes and full corpus

Evidence: `build.log.v6`, its three `timing-v6.*.csv` files,
`build.log.corpus-8-v6-r1`, and `timing-corpus-8-v6-r1.csv`.

| Probe | V6 wall 1/4/8 threads (s) | Change from v5 |
|---|---|---|
| Text | 3.689791 / 1.015750 / 0.581512 | 5.8% / 4.5% / 7.1% less time |
| Vector | 24.130153 / 7.284516 / 4.969706 | 2.1% less / 1.9% more / 3.7% less time |
| Image | Not run / 2.150533 / 1.099307 | 39.7% / 38.4% less time at 4/8 threads |

At eight threads, downsample worker-time fell from v5's 0.171806 / 3.084250 /
5.982449 s to 0.111239 / 2.573312 / 0.514626 s for text/vector/image. The image
downsample stage is 11.6x faster, with painting and decoding close to baseline.
All 2,432 quick-probe rows passed identity, success, timing/hierarchy, and summary
consistency checks. The four-thread vector total worker-time fell despite its
wall-time increase; long-page scheduling is a possible explanation, not proven
without task start/end timestamps. These are single-run comparisons.

All 110 saved native reference images match the v6 executable exactly in decoded
pixels, dimensions, and modes. Binary SHA-256:
`25358112a89eda2501b57582a364666f5f82ef93c98fb0d4c714707e562cfb0b`.
Evidence is saved as `gray-downsample-v6-pixels.json` and corresponding PNG/log
files in both existing temporary evidence directories. User committed production
changes at 9a7d950 and the grayscale test at 7ae0c4c.

| Corpus threads | V6 wall (s) | Pages/s | Speedup | Efficiency |
|---|---:|---:|---:|---:|
| 1 | 1139.083658 | 48.0 | 1.00x | 100% |
| 4 | 302.749877 | 180.8 | 3.76x | 94.1% |
| 8 | 163.574713 | 334.6 | 6.96x | 87.0% |

Compared with v4's eight-thread corpus, bitmap worker-time fell from 475.826453
to 219.549718 s; all rendering from 666.896524 to 414.012364 s; decoding stayed
close at 845.068297 versus 840.638072 s. Bitmap savings of 256.276736 worker-s
(32.034592 s divided by eight) closely explain the 33.585573 s wall-time gain.
V4 ran eight threads alone while v6 followed one/four-thread passes; repetitions
would tighten the percentage estimate and document/cache lifecycle comparison.

The corpus CSV had 8,222 complete old eight-thread rows plus a truncated fragment
joined to the new run's first row. Validation excluded old rows and parsed the
intact new first row from its schema/mode/thread prefix, entirely in memory.
The original CSV is unchanged. The recovered run contains exactly 54,730 unique
pages / 767 documents per thread count, matching v4's page identities and config;
all succeeded. All 33 coarse stage sums match console summaries within 1 us,
and all new bitmap hierarchy checks pass. Always use a fresh CSV filename.

V6 worker elapsed rises from 1138.865417 s at one thread to 1308.243239 s at
eight. Text rendering accounts for 88.180191 s of that 169.377822 s increase:
text 92.513164 -> 180.693355 s, font resolution 9.649726 -> 41.563036 s, glyph
metrics 25.597275 -> 64.264774 s. Queue starvation/final-tail idle capacity cannot
explain most of the efficiency loss; elapsed work occupies 99.97% of eight-worker
wall capacity. This does not distinguish CPU work, scheduling, and mutex waits.

### Step 3c: Conditional CCITT reference scans

User approved the exact bounded patch after reviewing the proposal. In
`src/parse/utils/ccitt/ccitt_utils.h`, `decode_g4_row` now skips `find_b1` for
horizontal mode and calls `find_b2` only in pass mode. Vertical modes still
compute b1; pass mode computes both. Both helpers only read reference pixels.
Bit consumption, row filling, polarity, Group 3 handling, EOF/error semantics,
and logging remain unchanged. No new timers or benchmark changes.

Inspection of the v6 optimized ARM64 executable confirmed both scans were still
executed before mode dispatch; the compiler had not removed the unused scans.
The v6 corpus has 309.779325 CCITT worker-s across 10,690 positive-time pages
in 117 documents. The existing image probe's 1149133.pdf is the largest document
contributor at 31.248514 worker-s for all 288 pages. The quick 64-page selection
remains the first performance check; the effect is not yet measured.

Validation passed:

- Compiled the complete committed header (7ae0c4c) and candidate header under
  separate namespaces, linked to the existing real loguru library. Test-copy
  counters track mode/scanner coverage; production code has no counters.
- Compared 4,117 full decodes and 12,000 row decodes, totaling 3,843,524 output
  bytes, exactly. Row checks also compare success, EOF flag, bit position, and
  exhaustion state; include 1,097 error returns and 36 EOFB cases.
- Generated 62 independent Pillow Group 3/4 or existing hand-written aligned
  streams: blank/solid/checker/banded/shifted/random patterns, odd/narrow widths,
  crop/pad widths, both polarities, truncation, invalid dimensions and columns.
  All nine G4 modes were exercised. Baseline b1/b2 counts were both 350,027;
  candidate b1 was 320,950 and b2 was 17,337, exactly matching mode requirements.
  Counts demonstrate avoided calls in these fixtures, not a corpus speed estimate.
- Configured Release-target C++ syntax and full pre-commit checks passed
  (Ruff formatter/linter, MyPy, uv-lock); patch whitespace check passed.

Evidence: `/private/tmp/docling-ccitt-step3c-8x_s0_zv/`, including original/candidate
header copies, native harness/source, compressed fixtures, and `validation.json`.
Before rebuilding, the unchanged v6 executable rendered 124 new CCITT PDF
references (62 streams, both polarities). `baseline-pixels.json` records binary
identity, command, dimensions/modes, and decoded-pixel hashes. Together with the
110 existing fixtures, 234 native pixel comparisons are available after rebuild.
This native builder/header validation does not replace full renderer integration.

No full executable or Python extension was rebuilt by Codex. The user compiles
and runs the unchanged script from the repository root:

```sh
TIMING_PREFIX=timing-v7 bash build.sh > build.log.v7 2>&1
```

Return `build.log.v7`, `timing-v7.text.csv`, `timing-v7.vector.csv`, and
`timing-v7.image.csv`. Use this fresh prefix once: the writer appends to existing
compatible CSV files. Expected row counts: 1,536 / 768 / 128. Compare v7 wall
and `do_image_total.ccitt` worker-time with v6 while checking unchanged stages;
then perform 234 native pixel comparisons. No speedup is claimed before those
measurements. Do not automatically advance to a larger CCITT rewrite.

### Step 3c results: v7 quick probes and full corpus

All 2,432 quick-probe rows passed identity/config, success, finite timing,
hierarchy, and summary consistency checks. Image wall time fell from v6's
2.150533 / 1.099307 s to 1.827245 / 0.929393 s at four/eight threads (15.0% /
15.5% less time). CCITT worker-time fell 14.6% / 15.0%, accounting for nearly
all image worker-time savings. The vector CCITT bucket fell 14-16%; total vector
wall time changed -1.6% / -2.7% / +1.4% at 1/4/8 threads. Text contains no CCITT
work and changed +6.4% / +5.1% / +0.3%. Single pairs cannot distinguish run
variation and indirect code-layout effects in the controls.

All 234 saved native reference images are pixel-identical, including 124 CCITT
fixtures. Evidence: `ccitt-v7-pixels.json` and corresponding PNG/log files in
the three previous temporary evidence directories. Binary SHA-256:
`707f2750bc305b4ce0159056ec76521363ad30317c678c3ecd0192a033139e30`.

`build.log.corpus-8-v7-r1` and its CSV actually contain all three thread counts:

| Threads | V6 wall (s) | V7 wall (s) | Less wall time | Less CCITT worker-time |
|---|---:|---:|---:|---:|
| 1 | 1139.083658 | 1124.333564 | 1.3% | 7.6% |
| 4 | 302.749877 | 295.416800 | 2.4% | 8.1% |
| 8 | 163.574713 | 161.746933 | 1.1% | 7.5% |

At eight threads, CCITT worker-time fell 309.779325 -> 286.557440 s; decoding
840.638072 -> 816.714181 s; rendering rose 414.012364 -> 423.668005 s. Net worker
savings of 14.961218 s divided by eight (1.870152 s) closely explain the observed
1.827780 s wall-time saving. CCITT improved in 102/117 affected documents;
the complete 1149133.pdf improved 13.1%, with heterogeneous gains elsewhere.
Scaling is essentially unchanged at 6.95x / 86.9% efficiency. Candidate retained
and committed at aa6486c.

The clean v7 corpus CSV contains 164,190 successful rows: 54,730 unique pages /
767 documents per thread count. Page identities and configuration match v6;
all 33 coarse stage totals match the log within 1 us and bitmap hierarchy checks
pass. The executable matches the pixel-validated v7 binary. The previous v6 CSV
was read with its old partial-run prefix excluded, as recorded above.

### Step 3d investigation and approved renderer-local embedded-face cache

Read-only investigation examined complete 1593260.pdf (2,227 pages), 2354163.pdf
(952), and 2410699.pdf (254), source ownership, corpus page details, and short
native samples at one/eight threads. Samples were captured through stdout;
no production files or benchmark scripts were changed during investigation.
Sampling perturbs execution and the tool environment differs from the user's;
these runs are not controlled performance baselines.

Confirmed: on 2354163.pdf, multiple render workers wait inside
`blend2d_embedded_font_cache::resolve -> shared_mutex::lock_shared/unlock_shared
-> mutex wait`. The shared embedded-face cache is consulted per text instruction,
including hits. On 1593260.pdf, separate waits occur in document integrity checks
(`ensure_page_content_present`); these are not attributed to the font cache.

Independent unresolved issue: v7's first 512 pages of 1593260.pdf report glyph
metrics of 0.063893 / 0.092281 worker-s in the standalone quick probe versus
0.255068 / 1.700240 s inside the corpus at one/eight threads. The worker-local
glyph cache survives document boundaries, includes exact font size in its keys,
copies a string key on lookup, and updates an LFU heap on hits. Cache-history,
miss/eviction behavior, and lookup work remain hypotheses until counters are
collected. Embedded-face lock waits occupy a different timer and do not explain
this glyph-metrics increase by themselves. Font resolution also includes FreeType
fallback rendering, so its whole bucket cannot be treated as lock wait.

User reviewed and approved a separate bounded production patch in
`src/render/blend2d_renderer.h`: add `local_embedded_face_cache_`, keyed by the
same owned content key, to each renderer. Reuse the shared cache's BLFontFace
result after its first lookup, including invalid faces. Existing fallback logic
continues for each instruction. Shared cache ownership of font bytes and selected
faces is unchanged. Local state is destroyed with the renderer and introduces no
shared mutable map, new locks, timers, flags, or scheduling policy changes.

Validation passed:

- Compiled the actual candidate selection block with real shared-cache logic and
  real Blend2D font faces. A resolve-call counter exists only in a temporary copy
  of the shared-cache header; production has no diagnostic counters.
- Compared 288,000 baseline/candidate selections across 144 renderer instances
  per variant at one/eight threads. Validity and exact face identity match for
  valid TrueType, content-identical aliases, a distinct valid font blob, malformed
  and truncated font data, and null/empty guards. Invalid faces remain cached;
  renderer resets and returned face lifetime were checked.
- Shared resolves fell from 102,858 to exactly 576 in those fixtures: four
  distinct nonempty content keys per renderer. This establishes the mechanism,
  not a measured corpus speedup.
- Configured Release-target C++ syntax and full pre-commit checks passed
  (Ruff format/lint, MyPy, uv-lock); patch whitespace passed.

Evidence: `/private/tmp/docling-local-face-step3d-nk0k_lha/`, including the native
harness/source, temporary instrumented cache, and `validation.json`. The unchanged
v7 executable rendered 22 additional embedded-font regression pages from
`tests/data/regression/2508.13113v2.pdf`, covering native TrueType and Type1
FreeType fallback. `baseline-pixels.json` records dimensions/modes/pixel hashes
and binary identity. Together with the 234 previous fixtures, 256 native image
comparisons are available after rebuild. Full candidate integration is pending;
neither the executable nor Python extension was rebuilt by Codex.

Capture a focused user-run baseline before rebuilding, then run the unchanged
script and repeat the focused probe. The following function uses fresh CSV names;
do not repeat a prefix, because the writer appends to compatible files:

```sh
font_probe() {
  ./build/run_scaling.exe /Users/taa/Documents/projects/_data/bo767/pdf/2354163.pdf \
    --mode render --threads 1,4,8 --max-concurrent-results 32 --scale 1 \
    --create-word-cells=true --create-line-cells=true \
    --enable-timing --timing-csv "timing-$1.font.csv" \
    > "build.log.$1.font" 2>&1
}

font_probe v7 &&
TIMING_PREFIX=timing-v8 bash build.sh > build.log.v8 2>&1 &&
font_probe v8
```

The focused probe covers all 952 pages (2,856 CSV rows). Compare v7/v8 focused
wall time and font-resolution bucket; review other stages as controls and perform
the saved 256 pixel comparisons after rebuilding. Return `build.log.v7.font`,
`build.log.v8.font`, their `timing-v7.font.csv` / `timing-v8.font.csv`, and the
ordinary `build.log.v8` with its three CSVs. The ordinary quick-probe baselines
remain v7. Do not attribute independent glyph-cache history effects to this patch
or advance to another optimization before results are reviewed.

### Step 3d results: v8 focused probe and corpus

The user captured the full 952-page 2354163.pdf baseline with the unchanged v7
executable, rebuilt v8 via the normal script, and repeated the same focused
probe. Source committed at c552796. Focused wall times at 1/4/8 workers:

| Build | 1 thread (s) | 4 threads (s) | 8 threads (s) |
|---|---:|---:|---:|
| V7 | 13.124505 | 3.620083 | 2.353494 |
| V8 | 13.120844 | 3.566910 | 1.926891 |

Eight-thread wall time fell 18.1%; efficiency rose 69.7% -> 85.1%. Font-resolution
worker-time fell 3.498369 -> 0.203785 s (94.2%); its 3.294584 worker-s saving
divided by eight (0.411823 s) closely explains the 0.426603 s wall saving.
Decoding/bitmap work stayed close. Standard probe changes were mixed and small:
text -3.9% / -3.4% / +2.7%, vector -0.4% / -0.4% / +0.8%, image +1.5% / +0.9%
at four/eight threads. These are single comparisons, not repetitions.

All 5,288 candidate rows (2,432 ordinary + 2,856 focused) passed identity,
configuration/command, success, timing/hierarchy, and summary consistency checks.
All 256 saved reference PNGs match decoded pixels/modes/dimensions exactly.
Artifacts are `local-face-v8-pixels.json` with corresponding logs/PNGs in all four
temporary evidence directories. V8 executable SHA-256:
`e7a130daa46d4ae5db8d152770a1e6101ac4baf0a5b45ad05443ecd05ef1fb90`.

The user additionally ran the full corpus at 1/4/8/12 workers in
`build.log.corpus-8-v8-r1` / `timing-corpus-8-v8-r1.csv`:

| Threads | Wall (s) | Change from v7 | Pages/s | Efficiency within v8 |
|---|---:|---:|---:|---:|
| 1 | 1141.670113 | +1.5% | 47.9 | 100% |
| 4 | 300.800121 | +1.8% | 181.9 | 94.9% |
| 8 | 160.438139 | -0.8% | 341.1 | 88.9% |
| 12 | 113.465881 | No v7 baseline | 482.3 | 83.8% |

Eight-thread font resolution fell 42.492660 -> 11.152522 worker-s (73.8%);
glyph metrics rose 68.372612 -> 74.589298 s; all rendering fell 423.668005 ->
400.187815 s; decoding rose 816.714181 -> 828.542489 s. Net worker savings
10.163788 s / eight = 1.270474 s, close to the observed 1.308794 s wall saving.
Twelve threads give 41.4% more throughput than eight within this build; there is
no matched v7 twelve-thread result. A slower single-thread baseline also affects
reported efficiency comparisons between builds.

All 218,920 corpus rows succeeded, with 54,730 unique pages / 767 documents per
thread count, matching v7 identities/configs. All 44 coarse stage sums agree
with console summaries within 1 us; bitmap hierarchy checks pass. The binary
matches the pixel-validated v8 executable.

The remaining glyph-metrics anomaly is non-monotonic. V8 worker-s at 1/8/12
threads are 2.771648 / 15.886670 / 5.085015 for 1593260.pdf, 1.519516 / 13.919483 /
3.080316 for 2354163.pdf, and 0.727003 / 7.308551 / 12.696342 for 2410699.pdf.
This motivates investigating cache history/key diversity; it does not measure
misses or prove an eviction cause. The local-face patch was retained.

### Step 3e: Key glyph bounds by the actual Blend2D font size

The next read-only investigation used the existing instruction-export API,
without rebuilding the extension or changing source. A raw parser with explicit
font-program extraction inspected four selected pages per document. Geometry
was calculated using the renderer's scale-1 coordinate/size formula.

| PDF / selected pages | Text instructions examined | Exact double font/size pairs | Float font/size pairs |
|---|---:|---:|---:|
| 1593260: 1,1025,2017,2022 | 12,101 | 150 | 8 |
| 2354163: 1,214,333,915 | 12,545 | 34 | 27 |
| 2410699: 1,6,61,77 | 21,481 | 249 | 13 |

For example, nominal 7.02-point Arial produced 35 exact doubles between
7.019999999999606 and 7.020000000000197, all becoming float 7.019999980926514.
Nominal 8.5728-point Arial produced 49 variants that also share one float.
These are font/size counts from selected decoded instructions, not native
glyph-cache hit/miss counters or corpus speed estimates.

Source confirms primary, system-fallback, and script-fallback BLFont construction
all convert geom.size to float; Blend2D exposes that effective size as font.size().
The cache instead keyed raw double size bits. User reviewed and approved changing
only the four lookup/insertion calls in `src/render/blend2d_renderer.h` to use
font.size(). Font creation, double draw geometry, bounding-box adjustment, and
implausible-outline thresholds keep their existing values. Cache implementation,
capacity, eviction policy, scheduling, and timing/CSV schemas are unchanged.

Focused validation passed:

- Extracted the actual committed (c552796) and candidate bodies of the outline
  plausibility guard and glyph-bbox adjustment; compiled them with real Blend2D
  fonts/glyph buffers and the production cache. An instruction-state fixture
  supplies the unchanged geometry getters. Outline counters exist only in the
  temporary harness copies.
- 153,633 comparisons match exactly, including 107,520 adjustments with bounds
  and 26,880 true implausibility decisions. Checked 320 double sizes / 32 native
  float sizes, double perturbations and midpoint boundaries between adjacent
  floats, multiple glyphs/empty outlines, embedded/substituted/null cache keys,
  bbox fitting/top alignment, and cache capacities 0/1/16/65,536.
- In a focused 33-size reuse case with one actual float font size, baseline
  extracted 33 outlines and candidate extracted one; adjustments remain bit-exact.
- Configured Release-target C++ syntax and full pre-commit checks passed
  (Ruff format/lint, MyPy, uv-lock), plus patch whitespace.

Evidence: `/private/tmp/docling-font-size-step3e-7ad19cov/`, including native
harness/source and `validation.json`. Full candidate renderer integration remains
pending the user's build. Codex has not rebuilt the executable or extension;
the executable still has the pixel-validated v8 SHA-256 above. Reuse all 256 saved
native reference images after rebuilding.

First run the unchanged quick script with a fresh prefix:

```sh
TIMING_PREFIX=timing-v9 bash build.sh > build.log.v9 2>&1
```

Return the log and the three `timing-v9.*.csv` files for checks and pixel
comparisons. The key follow-up is an eight-thread corpus run, since the short
standalone probes do not reproduce inherited worker-cache state. After focused
checks, use a fresh CSV filename and compare against v8's 160.438139 s:

```sh
./build/run_scaling.exe /Users/taa/Documents/projects/_data/bo767/pdf \
  --mode render --threads 8 --max-concurrent-results 32 --scale 1 \
  --create-word-cells=true --create-line-cells=true \
  --enable-timing --timing-csv timing-corpus-8-v9-r1.csv \
  > build.log.corpus-8-v9-r1 2>&1
```

The corpus comparison uses a fresh eight-thread process, whereas v8's eight-thread
pass followed 1/4-thread passes; record that lifecycle difference. No performance
gain or cause of the historical slowdown is claimed before measurement. Broad
hit/miss/eviction instrumentation remains a separate potential next step.

### Step 3e results: v9 effective-font-size keys

The quick probes retain identical pixels on all 256 saved native reference
images. All 2,432 candidate CSV rows passed identity/config, success, timing and
hierarchy checks; 88 coarse sums match the log within 1 us. Glyph worker-time
fell 15-21% across text/vector thread counts, although the buckets are small in
these short selections. Vector wall improvements also include decoding changes
and cannot all be attributed to the patch from one comparison. V9 binary SHA-256:
`3c017e9595c08dc07b03879144cff7f7ecf1161ca6d72f6f00d402c1bd7fb70c`.
Pixel artifacts are `font-size-v9-pixels.json` plus corresponding logs/PNGs in
the four existing evidence directories.

The user's corpus run used all four thread counts, matching v8's lifecycle:

| Threads | V8 wall (s) | V9 wall (s) | Less wall time | V9 efficiency |
|---|---:|---:|---:|---:|
| 1 | 1141.670113 | 1135.209471 | 0.6% | 100% |
| 4 | 300.800121 | 299.088472 | 0.6% | 94.9% |
| 8 | 160.438139 | 154.937313 | 3.4% | 91.6% |
| 12 | 113.465881 | 108.805460 | 4.1% | 86.9% |

Eight-thread glyph metrics fell 74.589298 -> 28.091011 worker-s (62.3%); all
rendering 400.187815 -> 351.875983 s. Bitmap work and font resolution stayed
close; decoding rose 828.542489 -> 831.184817 s. Glyph savings / eight = 5.812286 s,
close to the 5.500826 s observed wall saving. Twelve-thread glyph metrics fell
65.062243 -> 29.906536 s (54.0%). The four-call patch was retained and committed.

At eight threads, glyph metrics on 1593260.pdf fell 15.886670 -> 1.068615 s and
2354163.pdf 13.919483 -> 1.295864 s, while 2410699.pdf stayed 7.308551 -> 7.239596 s.
At twelve threads the last document stayed 12.696342 -> 12.898127 s, accounting
for about 43% of the remaining glyph bucket. This remains the next target.

The corpus CSV had six complete rows from an old four-thread attempt and a
truncated row joined to the new run's first record. Excluding that prefix and
parsing the intact new first row in memory recovered all 218,920 successful
rows. Each thread count has 54,730 unique pages / 767 documents, matching v8's
identities/config/command. All 44 coarse sums match the log within 1 us and
hierarchy checks pass. Original input logs/CSVs were not modified.

### Step 3f: Opt-in worker-local glyph-cache diagnostics

User reviewed and approved diagnostics in three source files and this plan.
No change to cache keys, capacity, LFU policy, drawing output, or scheduling
policy. The benchmark now accepts `--glyph-cache-diagnostics PATH` only with
`--mode render`. It writes one JSON object per completed page, from the single
consumer thread. One output stream covers all requested thread counts in the
invocation. Existing schema-2 timing CSVs retain their original layout.

The diagnostic path must be new. An existing destination is rejected before
opening; a path equal to the enabled timing CSV (including normalized aliases)
is also rejected. The ordinary timing CSV still appends compatible files, so
both paths need fresh names for each invocation.

JSONL schema 1 fields:

- `threads`, zero-based `worker_id`, and one-based `worker_page_index` identify
  each worker's chronological history within a thread-count pass.
- `doc_key`, one-based `page_number`, and `success` identify the page and partial
  measurements if it failed.
- `capacity`, `entries_before`, and `entries_after` capture inherited occupancy.
- `delta` contains per-page counter/timing differences from cumulative worker
  cache statistics. Cache/statistics are not reset at document boundaries.

| Delta field | Meaning |
|---|---|
| `lookups`, `hits`, `misses` | Public find calls and their outcomes; capacity zero counts as misses |
| `insertions`, `updates`, `evictions` | New entries, replacements of existing values, and LFU removals |
| `outline_attempts`, `outline_successes` | Bounds extraction attempts and successful nonempty bounds at the two glyph-metrics sites; the difference includes empty/failed outlines |
| `lookup_s` | Key construction/destruction, hashing, and map lookup; excludes hit-touch work |
| `hit_touch_s` | LFU updates on successful public find calls |
| `insert_s` | Whole insertion/update, including lookup, eviction, and heap maintenance |
| `outline_s` | Native outline extraction plus bounds calculation; excludes cache insertion and BLPath object construction/destruction |

The four timing fields cover disjoint work here. An existing-entry insert's
touch is included only in `insert_s`. Timers/counters are worker-local and
introduce no new mutexes. Defaults preserve disabled diagnostics: no sidecar,
counter updates, or clock reads from these new cache/outline scopes. Added
branches/object layout still warrant a diagnostics-off control run.

Enabled diagnostics add clock/counter, per-page JSON, and output work; they can
change task assignment/cache history. Treat these as explanatory measurements,
not a new native throughput baseline or uninstrumented absolute-cost estimate.
No per-glyph events are retained; only fixed-size cumulative worker stats and
one bounded-queue page record are held in memory.

Read-only exploratory capacity checks on the standalone 254-page 2410699.pdf
used the unchanged v9 executable, no timing export and no files. Eight-thread
single runs were 0.493 / 1.223 / 0.488 s at capacities 65,536 / 0 / 262,144;
twelve-thread runs 0.401 / 1.392 / 0.417 s. Disabling caching hurts this selection;
larger capacity does not clearly help it. Tool environment and lack of repetitions
prevent performance claims; standalone runs do not reproduce corpus cache history.

Validation passed:

- 200,000 cache trace steps against the actual committed cache (69aa16b), with
  140,606 public lookups and 180,134 insert calls. Results and LFU behavior match
  with diagnostics both off/on at capacities 0/1/2/17/65,536. Counts, occupancy,
  and 2,000 page-delta/JSON round-trip checks pass; early-exit scope accounting
  passes. A test-copy clock-call counter confirms zero new clock reads when off.
- Extracted production delta serializer, output-path checks, and render-mode
  validation pass native tests, including preserving existing output contents
  and rejecting timing/diagnostic path aliases before creating a file.
- Actual committed and candidate guard/adjustment bodies match in 153,633 native
  comparisons with diagnostics off and again with them on (real Blend2D fonts,
  glyph buffers and cache, instruction-state fixture).
- Configured Release-target C++ syntax and full pre-commit checks pass
  (Ruff format/lint, MyPy, uv-lock), plus patch whitespace.

Evidence: `/private/tmp/docling-glyph-diagnostics-jsx6qujd/validation.json` and
`/private/tmp/docling-font-size-step3e-0hqtk1jd/validation.json`, with harnesses,
test-copy headers, and binaries alongside. Full JSONL worker/consumer integration
and rendered pixels remain pending the user build. No full executable/extension
was rebuilt by Codex; the executable is still the pixel-validated v9 SHA above.
The 256 existing native image references can be reused with diagnostics off/on.

User commands, keeping build.sh unchanged and using the v10 prefix once:

```sh
glyph_probe() {
  ./build/run_scaling.exe "$1" \
    --mode render --threads 8,12 --max-concurrent-results 32 --scale 1 \
    --create-word-cells=true --create-line-cells=true \
    --enable-timing --timing-csv "timing-v10.$2.csv" \
    --glyph-cache-diagnostics "glyph-v10.$2.jsonl" \
    > "build.log.v10.$2" 2>&1
}

TIMING_PREFIX=timing-v10 bash build.sh > build.log.v10 2>&1 &&
glyph_probe /Users/taa/Documents/projects/_data/bo767/pdf/2410699.pdf standalone &&
glyph_probe /Users/taa/Documents/projects/_data/bo767/pdf corpus
```

The ordinary script is the diagnostics-off control (2,432 timing rows), compared
with v9. Standalone diagnostics should contain 508 page records, corpus 109,460,
with the same counts in their timing CSVs. Return all three logs, five timing
CSVs, and both JSONL files. Validate complete page sets, worker sequence and
occupancy continuity, lookup = hits + misses, entries-after = entries-before +
insertions - evictions on successful pages, and outline-successes <= attempts.
Join sidecar to timing rows by thread count/document/page. Then compare per-worker
2410699.pdf costs in isolation and following prior corpus documents: hit rate,
eviction rate, occupancy, lookup/hit-update cost, and repeated outlines.
Choose any optimization only after these results are reviewed.

### Step 3f results: v10 standalone versus inherited cache state

Evidence: `build.log.v10`, `build.log.v10.standalone`, `build.log.v10.corpus`,
their five timing CSVs, and the two `glyph-v10.*.jsonl` sidecars. All 109,968
diagnostic records join exactly to their timing rows: 508 standalone and 109,460
corpus. Worker sequence/occupancy continuity, counter identities and deltas,
complete page sets, success, finite values, timing hierarchy, and console stage
sums pass. The 2,432 diagnostics-off control rows also pass checks.

For all 254 pages of 2410699.pdf:

| Measurement | Standalone 8 | Corpus 8 | Standalone 12 | Corpus 12 |
|---|---:|---:|---:|---:|
| Lookups | 979,268 | 979,268 | 979,268 | 979,268 |
| Hits | 811,778 | 32,135 | 810,715 | 31,568 |
| Hit rate | 82.9% | 3.3% | 82.8% | 3.2% |
| Successful bounds / insertions | 3,538 | 783,181 | 4,601 | 783,748 |
| Evictions | 0 | 783,181 | 0 | 783,748 |
| Unsuccessful/empty bounds | 163,952 | 163,952 | 163,952 | 163,952 |
| Lookup worker-s | 0.033875 | 0.031960 | 0.034413 | 0.041959 |
| Hit-update worker-s | 0.013673 | 0.000573 | 0.015977 | 0.000508 |
| Insert worker-s | 0.000907 | 0.151817 | 0.001279 | 0.167659 |
| Outline worker-s | 0.011769 | 3.923862 | 0.021589 | 6.715568 |
| Parent glyph-metrics worker-s | 0.124245 | 7.249861 | 0.144548 | 12.822195 |

All corpus workers enter this document at capacity (65,536 entries), whereas
standalone workers end with only 294-527 entries each. The lookup and heap-hit
costs remain small. Repeated successful outline extraction is the main observed
increase; untimed BLPath construction/destruction, adjustment logic, and timer
overhead also contribute to parent glyph time. The 163,952 unsuccessful bounds
are invariant across contexts and do not explain the extra successful work.

The LFU policy initializes every new entry at frequency one without aging old
frequencies. Once older entries are frequently used, newly inserted glyphs can
become the next victims before reuse. The actual document has almost one eviction
per successful extraction despite its small standalone working set. This supports
testing recency-based eviction before optimizing hash lookups or heap updates.

The instrumented corpus measured 156.653749 / 110.006459 s at eight/twelve
threads. These include diagnostic overhead and altered task assignment; keep v9
as the uninstrumented throughput reference. Diagnostics-off controls versus v9
were mixed: text -0.4% / +0.9% / -2.0%, vector +2.4% / +3.4% / +4.3%, and image
-0.2% / +3.5% at four/eight threads. Vector slowdown includes decoding and bitmap
work; one pair does not isolate default instrumentation/code-layout overhead.

All 256 native reference images match with diagnostics off and again with them
on (512 comparisons), with one JSONL row per rendered page when enabled.
Evidence: `glyph-diagnostics-v10-off/on-pixels.json` and matching PNG/log/JSONL
files in the four existing fixture directories. V10 binary SHA-256:
`6a75bbae3bc20a74843a3025144ed4dba3237b513d1a7648552a62cdf8d177d1`.

### Step 3g: LRU eviction using the existing heap

User approved changing the comparator in `src/render/glyph_bbox_cache.h` to
compare `last_used` alone, plus updating the eviction comment. No other source
edits belong to this step. Capacity, cache keys, storage, logical clock, hit and
insert paths, worker lifecycle, and diagnostics remain fixed. Existing frequency
metadata is deliberately retained for this first experiment, isolating policy
from entry-layout/bookkeeping cleanup. LRU preserves the most recently used
working set rather than allowing old high-frequency glyphs to dominate forever.

The uncommitted v10 instrumentation remains in the working tree; source HEAD is
69aa16b. The exact v10 cache header was saved before this policy change in
`/private/tmp/docling-glyph-lru-0nppmufi/v10-cache.h` (SHA-256
`29c899e7f42db7e1af520aa14cdf5e4871abf3ae0c3c485818d080a0f1641df4`).

Validation passed:

- 431,112 trace steps match an independent map/list LRU oracle, with 3,120 state
  and diagnostic-counter checks, at capacities 0/1/2/17/65,536 and diagnostics
  both off/on. Largest-capacity cases prefill all 65,536 slots. Hits return exact
  stored boxes; inserts/updates, eviction and recency behavior match the oracle.
- A separate trace warms 64 old entries, then cycles eight new keys 8,000 times.
  Actual v10 LFU has zero hits and 8,000 evictions; candidate LRU has 7,992 hits
  and eight evictions. This tests the proposed mechanism, not corpus performance.
- With the LRU cache, actual guard/adjustment bodies match committed formulas in
  153,633 native comparisons with diagnostics off and again with them on. Real
  Blend2D fonts/glyph buffers and the production cache are used; instruction
  geometry comes from the existing focused fixture.
- Configured Release-target C++ syntax and full pre-commit checks passed
  (Ruff formatter/linter, MyPy, uv-lock), plus patch whitespace checks.

Evidence: `/private/tmp/docling-glyph-lru-0nppmufi/validation.json` plus its
harness/source, and `/private/tmp/docling-font-size-step3e-d40qia1i/validation.json`
for renderer checks. Candidate header SHA-256:
`743a4dc658476a986bf0ae4e62dcfa647741c58c07d4c66ecd4acb7accfda655`.
At implementation handoff, no full executable/extension rebuild was performed
by Codex; the executable still matched v10. The user subsequently rebuilt v11,
and all 256 native references were checked in both diagnostic modes below.

Run the same selections/flags with fresh v11 filenames; redefine the previous
helper because its old paths contain v10:

```sh
glyph_probe() {
  ./build/run_scaling.exe "$1" \
    --mode render --threads 8,12 --max-concurrent-results 32 --scale 1 \
    --create-word-cells=true --create-line-cells=true \
    --enable-timing --timing-csv "timing-v11.$2.csv" \
    --glyph-cache-diagnostics "glyph-v11.$2.jsonl" \
    > "build.log.v11.$2" 2>&1
}

TIMING_PREFIX=timing-v11 bash build.sh > build.log.v11 2>&1 &&
glyph_probe /Users/taa/Documents/projects/_data/bo767/pdf/2410699.pdf standalone &&
glyph_probe /Users/taa/Documents/projects/_data/bo767/pdf corpus
```

Expect 2,432 ordinary control CSV rows, 508 standalone diagnostic/timing rows,
and 109,460 corpus diagnostic/timing rows. Compare directly against v10's matched
instrumented runs. Evaluate hit rate, repeated successful outlines, evictions and
glyph worker-time for 2410699.pdf, plus corpus-wide totals and other documents
to detect policy tradeoffs. Compare diagnostics-off controls against v10 and v9;
do not infer uninstrumented corpus gains from the instrumented pair alone.
Keep/revise/revert by agreement after measurements and pixel checks. Do not
automatically remove frequency metadata or replace the heap with another layout.

#### V11 results and acceptance (2026-11-05)

The user rebuilt and ran the quick controls, standalone font probe, instrumented
corpus, and finally the full corpus with glyph diagnostics disabled. The v11
executable used for validation has SHA-256
`baac57042de5344f0b0fea9e38a1bf89d47a82397693a88ccbf4cabb1386f2d4`.

All 109,968 diagnostic records join their timing rows; worker sequence,
occupancy, counters, page identities, success and timing checks pass. All 2,432
quick-control timing rows pass. All 256 saved native reference PNGs match in
dimensions, modes and decoded pixels with glyph diagnostics off and again on
(512 comparisons). Evidence: `glyph-lru-v11-off-pixels.json` and
`glyph-lru-v11-on-pixels.json` in the four existing native fixture directories.

Instrumented corpus wall time fell from v10's 156.653749 / 110.006459 s to
153.373555 / 106.065629 s at eight/twelve threads. For 2410699.pdf, corpus hit
rate rose from about 3.3% to 82.9% at eight threads. Successful outline
extractions fell from 783,181 to 3,562 (eight threads) and 783,748 to 4,578
(twelve threads). Corpus-wide successful extractions fell from 6,447,686 to
1,311,470 and 4,454,467 to 1,485,614 respectively. These paired diagnostics
support the eviction-policy mechanism; their timings include diagnostic costs.

The final comparison keeps glyph diagnostics disabled and normal stage timing
and schema-2 CSV enabled in both versions:

| Threads | V9 wall (s) | V11 wall (s) | Less wall time | V11 efficiency |
|---|---:|---:|---:|---:|
| 1 | 1135.209471 | 1105.222198 | 2.64% | 100% |
| 4 | 299.088472 | 291.623013 | 2.50% | 94.7% |
| 8 | 154.937313 | 151.496934 | 2.22% | 91.2% |
| 12 | 108.805460 | 106.174378 | 2.42% | 86.7% |

Glyph-metrics worker elapsed fell from 28.091011 to 7.153535 s at eight threads
and 29.906536 to 8.090282 s at twelve. Eight-thread decoding changed by -0.27%
and bitmap rendering by +0.13%. This is less native work with essentially
unchanged parallel efficiency. These are single runs, not repeated estimates.

Evidence: [final log](../../build.log.corpus-v11-off-r1) and
[CSV](../../timing-corpus-v11-off-r1.csv). All 218,920 rows pass finite-value,
success, hierarchy and summary checks; each thread count covers the same 54,730
unique pages across 767 documents. Console sums match within one microsecond.
The old v9 CSV's interrupted prefix was excluded in memory as documented above;
source logs/CSVs remain unchanged. The user accepted retaining the optimizations.

### Step 3h: QPDF 12.4.2 dependency checkpoint

User approved changing `QPDF_TAG` from `v12.2.0` to `v12.4.2` in
`cmake/extlib_qpdf_v12.cmake`, with `PREFIX extlib_qpdf-${QPDF_TAG}`. The versioned
ExternalProject directory avoids reusing the old checkout when `UPDATE_COMMAND`
is disabled. Existing v11 evidence and old QPDF build directories are preserved.
System-dependency builds continue to use their installed QPDF.

The user rebuilds and runs the usual quick controls first:

```sh
TIMING_PREFIX=timing-v12 bash build.sh > build.log.v12 2>&1
```

Verify that `externals/include/qpdf/DLL.h` reports 12.4.2 after the rebuild,
then compare quick-control timing, success and native reference pixels. If those
checks pass, repeat the matched corpus run with fresh output filenames:

```sh
./build/run_scaling.exe /Users/taa/Documents/projects/_data/bo767/pdf \
  --mode render --threads 1,4,8,12 \
  --max-concurrent-results 32 --scale 1 \
  --create-word-cells=true --create-line-cells=true \
  --enable-timing --timing-csv timing-corpus-v12-off-r1.csv \
  > build.log.corpus-v12-off-r1 2>&1
```

Expected: 218,920 successful timing rows and the same document/page identities
and configurations as v11. Compare wall time, decoding, decoder construction,
image codecs and rendering separately. Do not assume a dependency upgrade
improves this workload. Build, pixel comparisons and timings are pending.

## Step 4: Python investigation after the C++ checkpoints

First add a matched workload ladder: standalone C++, raw bindings, result wrappers,
materialized page contents, then the complete backend replay. Match document
lifecycle, memory-release policy, outputs, and timing boundary within each comparison.

Split parser construction from PDF loading and attribute model materialization,
normalization, geometry queries/conversion, image copies, output accounting, and
JSONL writes. Then prioritize one measured change at a time:

- Bulk word/line materialization and coordinate normalization.
- Bulk separator geometry export/conversion.
- Parser/resource reuse or bounded document overlap, with explicit memory limits.
- Avoidable image copies if their measured contribution warrants it.

Keep changes to execution policy separately labeled from per-operation speedups.
Do not assume Docling document-concurrency settings parallelize backend loading.

## Iteration log

| Step/build | Change | Correctness | Text 1/4/8 s | Vector 1/4/8 s | Decision |
|---|---|---|---|---|---|
| Initial probes | Existing executable; single runs | Zero reported errors; pixel parity not tested | Not measured / 1.639 / 1.403 | Not measured / 7.346 / 5.109 | Superseded for candidate comparisons |
| Step 0: build.log.v1 | User-selected baseline; single runs | Zero reported errors; pixel parity not tested | 3.979 / 1.255 / 1.356 | 23.994 / 7.018 / 4.982 | Accepted as baseline by user |
| Step 1: build.log.v2 | Substituted-font bounds caching | User build/run passed; zero errors; pixel parity pending | 3.715 / 1.042 / 0.595 | 25.080 / 7.241 / 5.037 | Retained; user approved Step 2a |
| Step 2a: build.log.v3 | Optional stage/detail/queue diagnostics and schema-2 CSV | User build/run passed; zero errors; CSV checks passed; pixel parity and overhead control pending | 3.816 / 1.028 / 0.587 | 24.325 / 7.062 / 4.996 | Results reviewed; next step awaits user decision |
| Step 2a: corpus-8-r1 | Fresh-process eight-thread corpus with diagnostics | 54,730 successful pages; CSV consistency passed; pixel parity not tested | Full corpus, 8 threads: 209.695 s | Not a short-selection run | Images dominate; user approved Step 3a |
| Step 3a: build.log.v4 | Single-component Decode identity/table path | User build/run and CSV checks passed; 50/50 native PNGs pixel-identical | 3.940 / 1.052 / 0.611 | 24.273 / 7.058 / 5.049 | Image 4/8: 3.505 / 1.818 s; baseline confounded; retain pending corpus confirmation |
| Step 3a: corpus-8-v4-r1 | Candidate eight-thread corpus with diagnostics | 54,730 successful pages; CSV/config checks passed; same pixel-validated executable | Full corpus, 8 threads: 197.160 s | Not a short-selection run | 5.98% less wall time; mapping down 81.2%; retain candidate |
| Step 3b1: build.log.v5 | Bitmap prepare/convert/downsample/paint detail timers | User build/run, CSV/hierarchy checks, and 50/50 pixel comparisons passed | 3.915 / 1.064 / 0.626 | 24.657 / 7.149 / 5.163 | Image 4/8: 3.564 / 1.784 s; reduced-size preparation dominates; propose opaque-gray specialization |
| Step 3b2: build.log.v6 | Opaque-grayscale downsample specialization | 2,432 quick-probe rows and 110/110 native PNG comparisons passed | 3.690 / 1.016 / 0.582 | 24.130 / 7.285 / 4.970 | Image 4/8: 2.151 / 1.099 s; retained and committed |
| Step 3b2: corpus-8-v6-r1 | V6 full corpus, 1/4/8 threads | 54,730 successful unique pages per thread count; recovered CSV matches log/config | Full corpus: 1139.084 / 302.750 / 163.575 s | Not a short-selection run | 17.0% less eight-thread time than v4; bitmap worker-time down 53.9% |
| Step 3c: build.log.v7 | Conditional CCITT reference scans | Quick-probe CSV checks and 234/234 pixel comparisons passed | 3.926 / 1.068 / 0.583 | 23.739 / 7.089 / 5.041 | Image 4/8: 1.827 / 0.929 s; retained pending corpus |
| Step 3c: corpus-8-v7-r1 | V7 full corpus, 1/4/8 threads | 54,730 successful unique pages per thread; CSV/config checks passed | Full corpus: 1124.334 / 295.417 / 161.747 s | Not a short-selection run | 1.1-2.4% less wall time; CCITT down 7.5-8.1%; committed aa6486c |
| Step 3d: build.log.v8 | Renderer-local embedded-face results | 5,288 probe rows and 256/256 pixel comparisons passed | 3.773 / 1.031 / 0.599 | 23.644 / 7.063 / 5.079 | Full font probe 8 threads: 2.353 -> 1.927 s; 18.1% less time |
| Step 3d: corpus-8-v8-r1 | V8 corpus, 1/4/8/12 threads | 218,920 rows passed CSV/config checks; pixel-validated binary | Full corpus: 1141.670 / 300.800 / 160.438 / 113.466 s | Not a short-selection run | Eight-thread font bucket down 73.8%; wall down 0.8%; committed c552796 |
| Step 3e: build.log.v9 | Key glyph bounds by actual font size | 2,432 probe rows passed; 256/256 native PNGs pixel-identical | 3.781 / 1.022 / 0.594 | 23.237 / 6.949 / 4.895 | Glyph bucket down 15-21%; retained pending corpus |
| Step 3e: corpus-8-v9-r1 | V9 corpus, 1/4/8/12 threads | 218,920 recovered completed-run rows pass CSV/config checks; pixel-validated binary | Full corpus: 1135.209 / 299.088 / 154.937 / 108.805 s | Not a short-selection run | Eight/twelve-thread wall down 3.4%/4.1%; glyph bucket down 62.3%/54.0%; committed 69aa16b |
| Step 3f: v10 diagnostics | Opt-in glyph-cache counters/timers and worker JSONL | 109,968 diagnostic records join timing rows; 2,432 controls checked; 256/256 pixels match in each diagnostic mode | 3.765 / 1.031 / 0.582 | 23.785 / 7.187 / 5.107 | Target corpus hit rate 3.3% vs 82.9% alone; LFU retention identified; user approved LRU experiment |
| Step 3g: v11 | Evict by recency using existing heap | 431,112 LRU oracle steps; all diagnostic/control rows checked; 256/256 pixels match in each diagnostic mode | 3.721 / 1.022 / 0.582 | 24.145 / 7.258 / 5.024 | LRU retained after corpus confirmation |
| Step 3g: corpus-v11-off-r1 | V11 corpus, glyph diagnostics disabled | 218,920 rows pass CSV/config/page-set checks; zero errors | Full corpus: 1105.222 / 291.623 / 151.497 / 106.174 s | Not a short-selection run | 2.2-2.6% less wall time than v9; optimizations accepted |
| Step 3h: QPDF 12.4.2 | Dependency pin and versioned ExternalProject directory | Pending user rebuild and correctness checks | Pending | Pending | Approved; compare against accepted v11 baseline |
