# Reduce the native parser's memory footprint

Date: 2026-11-11.
Status: step 4a Form streaming validated by native experiments and 153 focused tests.
Step 2 native recheck and carried-forward semantic coverage passed.
Step 3 native smoke/scaling, Form timing recheck and focused semantic coverage passed.
Step 1 core scaling is reviewed. Remaining step-4 entry points and step 5 have not started.
The preliminary state-reset audit is in progress.
Initial C++ reproduction and structural investigation are complete.

Related issues:

- [docling-parse#448](https://github.com/docling-project/docling-parse/issues/448):
  quadratic dash-array growth, fixed by
  [#452](https://github.com/docling-project/docling-parse/pull/452).
- [docling-parse#345](https://github.com/docling-project/docling-parse/issues/345):
  high native memory consumption on vector-dense pages.
- [docling#4058](https://github.com/docling-project/docling/issues/4058):
  original dense-grid reproduction and downstream operational impact.

## Working agreement: Codex implements; the user compiles and experiments

The user explicitly wants **Codex to write the code, the user to compile and run
the experiments, and both to strategize together from the intermediate results**.

This is an iterative collaboration, not authorization to implement all five
steps in one pass.

For each implementation cycle:

1. Codex reads the current code and the most recent results.
2. Codex states the hypothesis, proposes one bounded change, identifies affected
   files, and shows the proposed diff whenever possible.
3. The user approves that change before Codex modifies the code. Approval already
   given for a specific change does not need to be requested again.
4. Codex implements the approved change and reviews the diff and ownership,
   state, error-handling, and timing implications.
5. Codex supplies exact build, test, and experiment commands, identifying which
   commands already work and which require the new probe features.
6. **The user compiles, runs the correctness checks and experiments, and returns
   the build output and measurement artifacts. Codex does not launch builds,
   tests, or benchmark runs unless the user explicitly delegates them.**
7. Codex analyzes the returned data, explains what it establishes and what remains
   uncertain, and records the result in this plan.
8. Together, choose to keep, revise, or revert that individual change and agree
   on the next implementation cycle.

A compilation failure, an output difference, or an unexpected memory curve is a
result to investigate together. Do not automatically advance to the next step
just because code has been written. Do not update regression ground truth to
make a behavior difference disappear.

Writing this plan does not start step 1 or authorize production-code changes.

## Objective and scope

Remove the unnecessary retention and copying of complete content-stream token
lists in docling-parse while preserving extracted page content, graphics-state
semantics, damaged-document recovery, and rendered pixels.

The first investigation and measurements remain in native C++ to isolate this
work from Python model construction and bindings. The existing Python-facing
tests still provide important correctness coverage once implementation begins.

The primary scope is:

- Audit reusable state for unintended accumulation and missing reset/replacement operations, separately from the streaming refactor.
- Distinguish QPDF's own parsing cost from our decision to collect every token.
- Interpret instructions as QPDF delivers them instead of waiting for a complete
  vector of parsed objects.
- Remove unnecessary token-vector copies in nested content interpretation.
- Keep intentional caches useful without copying their contents on every use.
- Demonstrate improvements on synthetic fixtures and real documents.
- Verify that the gains survive the actual threaded parsing path.

Separate follow-up decisions, informed by the measurements:

- A per-page segment, decoded-byte, or memory budget with a degraded-page result.
- Scheduling heavy pages to limit concurrent working memory.
- Reducing the separate page-shape and render-instruction representations.
- QPDF changes, if QPDF-only measurements reveal a remaining problem at that layer.

These follow-ups must not be folded into the initial streaming change. The
existing dash-array fix must remain intact.

## Terminology

| Term | Meaning in this plan |
| --- | --- |
| QPDF parsing with a discard callback | QPDF decodes the content bytes and delivers parsed objects; the callback counts and releases them without accumulating a token list. This has not yet been measured. |
| Collecting tokens | Our callback retains each QPDF object handle and its type/text representation in a vector of qpdf_stream_instruction records. This is what the initial probe called "tokens" or "tokenization only." |
| Interpretation | Execute PDF operators, maintaining graphics/text state and constructing page elements and render instructions. |
| Full native parsing / decode | Content decoding and object parsing, interpretation, and the usual native page processing; no Python model materialization. |
| Rendering | Execute render instructions to produce a raster canvas, at the specified scale. |
| Peak RSS | A process-lifetime resident-memory high-water mark, including temporary allocations. |
| Current RSS | Resident memory at a particular observation point; it may include allocations retained by the allocator after objects have been freed. |
| Retained payload | Bytes or objects we can count in live structures. This is not interchangeable with RSS. |

QPDF already delivers parsed objects individually through ParserCallbacks.
In the version inspected, it first buffers the decompressed content-stream bytes.
Consuming callbacks immediately therefore removes our accumulated object list;
it does not make the whole parser constant-memory.

## Starting evidence

### Code and experiment identity

The initial investigation used HEAD
511c57a87e4cc72a25d6ac613a095839288ba34e, macOS arm64, a Release build with
-O3, and QPDF v12.4.2. Future cycles must record their own identities; these
numbers are not universal memory guarantees.

The native probe is
[tests/cpp/test_memory_on_lines.cpp](../../tests/cpp/test_memory_on_lines.cpp).
Its optional CMake target is test_memory_on_lines, enabled with
DOCLING_PARSE_BUILD_MEMORY_PROBE=ON. It belongs under tests/cpp, not app.

Initial artifacts, local to the investigation machine:

- [Detailed report](/private/tmp/docling-line-memory/report.txt).
- [Summary CSV](/private/tmp/docling-line-memory/summary.csv).
- [Raw measurements](/private/tmp/docling-line-memory/measurements.jsonl).
- [Build and dependency identity](/private/tmp/docling-line-memory/metadata.json).
- [Scaling plot](/private/tmp/docling-line-memory/line_memory.png).

The /private/tmp paths are temporary artifacts, not durable repository fixtures.
The initial experiment generated 16 PDF variant/size combinations and ran 108
measurements: three fresh processes for each of 36 configurations. Generated
PDFs were produced in separate processes, outside the measured parsing process.

### Dash-array finding: already fixed

"Old dash handler" means current code with only grph.h substituted from
immediately before #452. The relevant header differs only by the added
dash_array.clear(). It does not mean an entire historical release.

"Current code" in the initial report means the checkout above, already including
#452.

Median peak RSS through rendering, in MiB:

| Dashed lines | Without the clear | With the clear |
| ---: | ---: | ---: |
| 1,000 | 82.06 | 24.19 |
| 2,000 | 263.75 | 29.72 |
| 4,000 | 884.09 | 33.94 |
| 8,000 | 3,245.97 | 49.09 |

Both versions produce exactly N shape render instructions and N page-shape
records. Before the fix, shape i receives 4i dash entries. Two retained copies
of those double arrays occupy 32 × N × (N + 1) bytes, excluding overhead and
temporary allocations. With the fix, the same payload is 64 × N bytes.

The quadratic behavior is in each instruction's growing payload, not in the
number of render instructions. This finding is established; it does not need
to be rediscovered during the new memory work. Use the fixed code as the normal
baseline and retain dash controls as correctness coverage.

### Dense-grid finding: large linear cost

The C++ port of the published path-storm generator rounds its grid dimensions,
so the actual line count is slightly smaller than the requested count.

Median peak RSS, in MiB:

| Actual lines | Collect tokens only | Parse with shapes disabled | Full parse | Full parse + render |
| ---: | ---: | ---: | ---: | ---: |
| 124,861 | 226.47 | 229.98 | 303.64 | 303.23 |
| 249,652 | 441.20 | 445.22 | 570.78 | 573.67 |
| 499,204 | 873.06 | 878.38 | 1,109.42 | 1,119.55 |
| 999,108 | 1,734.98 | 1,742.52 | 2,202.89 | 2,218.86 |

These are separate process measurements, not additive memory buckets. Small
differences between full parse and full parse + render can reflect run-to-run
variation. RSS differences are not an exact breakdown of live allocations.

At 999,108 lines:

- 999,108 shape render instructions and page-shape records.
- 1,998,216 path points, with no stored dash entries.
- 6,993,758 top-level tokens: 7 × N + 2 for this generator.
- Token-vector capacity of 8,388,608.
- sizeof(qpdf_stream_instruction) of 64 bytes on this build, giving 512 MiB for
  the vector's inline allocation alone; the referenced QPDF objects and other
  allocations are additional.

The measurements show approximately linear growth on these inputs. They
establish a substantial cost before page elements are created, but do not yet
separate QPDF's own working memory from our retained token objects. No QPDF leak
has been established.

The first measurements use one page, one process, do_thread_safe=false, default
sanitization, and render scale 1.0. They do not reproduce the real large Form
XObject mentioned in #345 or the threaded backend's aggregate memory.

## Source map and current structure

Line numbers can move; use the named methods when implementing.

| File | Relevant responsibility |
| --- | --- |
| [src/parse/qpdf/stream_decoder.h](../../src/parse/qpdf/stream_decoder.h) | QPDF ParserCallbacks, token normalization, per-token type/text construction, stream.push_back, damaged-content recovery. |
| [src/parse/qpdf/stream_instruction.h](../../src/parse/qpdf/stream_instruction.h) | QPDF handle plus key and val strings retained for each token. |
| [src/parse/pdf_decoders/stream.h](../../src/parse/pdf_decoders/stream.h) | Stored stream vector, interpretation loop, graphics state, inline images, nested content, and stream = stream_ copy in the vector-taking interpretation overload. |
| [src/parse/pdf_decoders/page.h](../../src/parse/pdf_decoders/page.h) | Page content decoding followed by interpretation; annotation appearance decoding; timing attribution. |
| [src/parse/pdf_resources/page_xobject_form.h](../../src/parse/pdf_resources/page_xobject_form.h) | parse_stream() materializes and returns a local Form token vector. |
| [src/parse/pdf_resources/page_pattern.h](../../src/parse/pdf_resources/page_pattern.h) | Cached parsed tiling-pattern content; requires a deliberate cache/replay policy. |
| [src/parse/pdf_states/shape.h](../../src/parse/pdf_states/shape.h) | Constructs page-shape records and render subpaths. |
| [src/parse/page_items/render_instructions.h](../../src/parse/page_items/render_instructions.h) | Retained render instructions and geometry. |
| [src/parse/utils/pdf_timings.h](../../src/parse/utils/pdf_timings.h) | Existing timing keys and attribution hierarchy. |

Current sequence for page content:

1. Read /Contents, including a possible array of streams.
2. QPDF buffers the decompressed bytes and parses objects.
3. Our callback appends every normalized object to a token vector.
4. The interpreter traverses that vector and creates page output.
5. The stream decoder is destroyed when content decoding finishes.
6. Page output remains available for extraction and rendering.

On the Form path, parse_stream() creates a local vector; the receiving
interpreter then copies it with stream = stream_. This is an additional
linear allocation. It is not a copy from a Form token cache. Tiling-pattern
caching is a separate case.

## Measurement protocol for every cycle

### Keep comparisons controlled

- Use the same generated PDF bytes for baseline and candidate. Record input
  hashes and actual emitted line counts.
- Use matching compiler, optimization mode, architecture, dependency versions,
  and parser/render settings. Avoid dependency upgrades within a comparison.
- Record source revision, relevant uncommitted diff, binary hash, build flags,
  QPDF version, and the exact command.
- Run each measured case in a fresh process. Generate the PDFs in a separate
  process and keep generation outside the measured run.
- Begin with a small diagnostic selection. Expand only after the user returns
  a successful build and the first results.
- Use three repetitions for reported comparisons, preserving each observation
  and reporting median plus range. Keep baseline and candidate run conditions
  comparable; investigate noisy results instead of drawing a conclusion from
  one convenient run.
- Keep single-process, single-page native measurements first. Add threaded and
  Python integration measurements at the relevant later gates.
- Preserve raw logs, settings, failures, and output counts. Give each iteration
  its own output directory so the previous result is still available.

### Record the quantities that explain memory

Collect scalar counters without accumulating an event log per token:

- Decompressed content bytes, token and operator counts, and executed operators.
- Peak RSS and current RSS at defined stages; normalize units to bytes in data.
- Existing token-vector size/capacity and any nested-vector copies.
- Maximum pending operand count and, where practical, large operand payload size.
- Active nested content depth and the kind of stream being interpreted.
- Page-shape, render-instruction, subpath, segment, and point counts.
- Dash-array entry counts, decoded bitmap sizes, and retained output sizes where
  practical.
- Total process wall time, full parse time, render time, and relevant stage times.
- Parse/load failures, skipped operators, degraded output, and unexpected warnings.

The existing probe reports elapsed time at snapshots before process teardown;
do not silently treat this as the entire subprocess lifetime. If end-to-end time
is needed, record it separately around the child process. External peak-memory
measurement can corroborate the in-process high-water readings.

A top-level token can be a large array, dictionary, string, or inline-image
payload. A small token count does not prove a small byte footprint. The constant
pending-operand expectation applies to the simple line generator, not every PDF.

### Compare slopes and output, not just one large-page number

For the grid, use the current requested sizes 125k, 250k, 500k, and 1M and compare
actual counts. Retain smaller cases for quick feedback and larger cases only as
the user finds practical.

Compare memory versus actual lines, memory per line after accounting for fixed
overhead, and the number of retained token objects. Do not claim constant total
memory: QPDF's decompressed bytes and the page outputs still scale with input.

For repeated and nested Forms, record both unique stream size and total
invocation/output count. Replaying a Form R times can legitimately create R times
the output; distinguish that from unnecessarily retaining R token-list copies.

## Preliminary audit — Find unintended accumulation and missing resets

The user requested an additional investigation for bugs similar to the missing
dash_array.clear() fixed by
[commit 9b09a05](https://github.com/docling-project/docling-parse/commit/9b09a057722edb56ad55bd9a6767a3ae4960eebd).

Run this audit before the interpreter refactor and revisit relevant containers
as steps 2–4 change their lifetimes. Keep the five main implementation stages
in scope; this audit is an additional preliminary investigation, not a
replacement for isolating the token-list cost in step 1.

The same working agreement applies: Codex investigates and proposes the code;
the user approves changes, compiles and runs reproductions; both review the
intermediate results before deciding what to fix or investigate next.

### Hypothesis and distinction to preserve

A setter or reset operation may accidentally append to a reusable container
instead of replacing its previous contents. If subsequent page elements copy
that state, a small mistake can produce:

- Wrong PDF semantics, such as an empty dash array failing to restore solid lines.
- Retained state proportional to the number of prior setter calls.
- Total copied payload proportional to the number of paints multiplied by the
  accumulated state size, potentially giving quadratic memory growth.

There are also legitimate accumulations with expensive copying. Those are a
different category. Correct clipping history, page output, resource scopes and
intentional caches must not be cleared simply because their containers grow.

A search result without a nearby clear is a lead, not a confirmed bug.
Assignment, construction of a fresh object, swap, replacement of a smart
pointer, or an owner with a shorter lifetime may already provide the reset.

### Audit procedure

1. Inventory member containers, strings and owned buffers in reusable parser
   state, graphics/text state, path handling, resources and decode helpers.
   Distinguish them from local scratch objects and page-result collections.
2. Locate their mutations: push_back, emplace_back, insert, append, operator+=,
   resize and indexed assignment, as well as clear/assign/swap/replacement.
3. For each candidate, identify the semantic operation: replace a setting,
   append a path segment, intersect a clip, emit a page element, merge a resource
   scope, replay cached content, or finish a pending operation.
4. Trace actual callers and object lifetimes. Establish whether the same
   instance can reach that operation repeatedly. Check constructor, reset,
   copy/assignment, q/Q restoration, nested-stream entry/exit and page unloading.
5. Check empty values, explicit defaults and malformed operands. An empty value
   that should reset a setting is especially useful for exposing stale state.
   Also verify partial-update semantics: an ExtGState dictionary changes the
   properties it supplies, so missing fields must not blindly reset other state.
6. Identify downstream snapshots/copies and their lifetimes. Record both current
   state size and the total state payload copied into emitted elements.
7. Classify each finding as confirmed incorrect accumulation, suspected pending
   reproduction, intentional accumulation with potential copy amplification,
   safe because replacement/fresh construction was verified, or unresolved.
8. For a confirmed candidate, propose a minimal semantic fix and a focused
   regression fixture. Fix and measure it separately from streaming changes so
   its effect remains attributable.

### Initial areas to inspect

| Area | Reset/reuse questions | Evidence required |
| --- | --- | --- |
| Graphics-state settings and ExtGState application | Does each supplied setter replace its own value? Do empty patterns/default values work? Are unspecified properties preserved? | Repeated and alternating settings followed by visible paint, including explicit empty values and q/Q restoration. |
| Current paths and temporary subpaths | Are painted/discarded paths released at the correct operator? Are retained current-path portions required by PDF semantics? | Repeated m/l/S and m/l/n cases, multiple subpaths, closing operators, clip-plus-paint and nested state scopes. |
| Clipping state and snapshots | Which history must accumulate, and is the entire history copied into every render instruction? | Repeated clip-plus-paint with and without q/Q, clip counts/points per instruction, total copied clip bytes, unchanged pixels. |
| Text state and pending Type 3 procedures | Are pending procedures drained after each show operation, and are appropriate per-operation fields reset without destroying persistent text state? | Repeated text and Type 3 painting, nested content and recoverable failures, with emitted glyph/procedure counts. |
| Color-space, font and other resource parsing | Is the object reused, replaced, or freshly constructed? Can repeated initialization append to old arrays/maps or leave stale flags? | Actual call-path evidence plus a supported repeated-use reproduction; do not treat an unreachable reuse pattern as an established page-memory bug. |
| Inline images and decoding scratch buffers | Are headers/payloads replaced between images and after failure? Are reused buffers holding content or only capacity? | Multiple images with different settings, a damaged image followed by a valid image, and buffer/payload counters. |
| Marked content, operands and other stacks | Are scopes popped/drained at the proper logical boundary? Does recovery leave stale pending work? | Balanced and malformed streams, nested Forms, split /Contents, usable suffixes after a bad operator. |
| Page/document and cache lifetimes | Does repeated processing retain output or resources beyond their intended owner? | Repeated processing/unloading in one process, separated from fresh-process scaling. |

### Initial source observations: not additional confirmed bugs

The first targeted source inspection established the following leads and
non-findings. It was not a complete audit.

- The direct d-operator bug is already fixed in pdf_states/grph.h.
- ExtGState /D parsing in pdf_resources/page_grph.h uses a fresh local
  std::vector<double> and assigns it to dash_array_. The local push_back loop
  is not itself the same missing-clear bug.
- Resource collection setters examined in page_grphs.h and page_colorspaces.h
  construct a fresh individual resource before assigning it into the map. Any
  claim about a missing reset must account for that construction and the map's
  intended scope.
- pdf_state<SHAPE>::capture_pending_clip() intentionally appends clip paths:
  successive clipping operations intersect the current clip.
  pdf_state<SHAPE>::get_clip_state() builds a snapshot from that history, which
  painting can retain in a render instruction. This is a candidate for
  legitimate-state copy amplification, not evidence that the history should
  be cleared. Its scaling has not yet been measured.
- No second missing-clear defect has been confirmed by this preliminary pass.

### Reproduction and measurement strategy

For each selected candidate, Codex should supply a small deterministic PDF
generator/fixture and exact commands for the user. Prefer the native probe for
memory measurements and focused semantic tests for correctness.

Compare:

- Repeating the same setting before every paint.
- Setting it once, followed by the same number of paints.
- Alternating two distinct values, to expose stale contents rather than only
  memory growth.
- Setting a nonempty value followed by its defined empty/reset value.
- A save/restore control using q/Q, where appropriate for that state.
- A fresh resource/object versus a supported repeated-use path.

Use increasing operation counts. Keep the number of settings and emitted paint
operations explicit; they may vary independently.

Record:

- Final and maximum current-state entry/byte counts.
- Number of emitted page elements and render instructions.
- Per-element snapshot sizes and total retained/copied state payload.
- Peak RSS and runtime in fresh processes.
- Output values and pixels that prove the reset/replacement semantics.
- For cross-page/document retention, a separate repeated-use process experiment.

If current state grows with repeated replacement operations, investigate a
missing reset. If current state must grow by definition but snapshots multiply
it across outputs, consider immutable sharing or another correctness-preserving
representation instead. Do not label allocator-retained capacity as retained
logical state: clear() normally preserves capacity, while shrink_to_fit() is
not a universal fix and should not be added to hot paths without evidence.

### Acceptance and integration with the five stages

The audit deliverable is a candidate table with semantic expectations,
owner/lifetime evidence, reproduction status, measured scaling and a proposed
decision.

A missing-reset fix needs both a semantic regression test and a before/after
measurement on its reproduction. Passing a memory check alone is insufficient
if the proposed clear removes required PDF state.

Keep each confirmed fix independently reviewable and obtain approval before
editing production code. The user compiles/runs each candidate, and we decide
together whether to retain it. Refresh the memory baseline after accepting such
a fix before attributing later gains to callback-driven interpretation.

If the audit finds no additional missing resets, record the inspected scope and
move on with the five-step plan. Do not make speculative clears to produce a
smaller graph.

## Step 1 — Complete the baseline and isolate QPDF

**Purpose:** determine how much memory belongs to QPDF's own parsing path and
how much comes from retaining its delivered objects in docling-parse.

### Proposed code changes

Extend tests/cpp/test_memory_on_lines.cpp, retaining the existing optional CMake
target and current modes.

Add a clearly named QPDF-only/discard mode:

- Open the same file and select the content stream under test.
- Supply ParserCallbacks that count delivered objects/operators but retain no
  object handles, per-token strings, or growing containers.
- Release each delivered object when its callback returns.
- Record decompressed byte count through the supported callback, and call counts.
- Use the same QPDF object-level parsing API as the collecting path. A lower-level
  lexical tokenizer would answer a different question.
- Label this mode unambiguously. Keep the old tokens mode identifiable as the
  collecting callback, not as bare QPDF.

Add a Form-based generator:

- Put the same line operators in a Form XObject invoked from the page.
- Match page size, geometry, paint settings, and compression to the direct case.
- Initially invoke the Form once; add repeated and nested variants separately.
- In a QPDF-only Form measurement, inspect the actual Form content, not just the
  page's small /Fm0 Do wrapper.
- Preserve deterministic PDF generation and report actual output counts.

Avoid changing production interpreter behavior in this step. If result
aggregation needs a helper, Codex should provide it with exact usage; any Python
helper is orchestration/reporting only, never the parser under measurement.

### Experiments for the user

1. Compile the probe using the existing target.
2. Run a small direct-page case with discard, collect, full-parse, shapes-disabled,
   and render modes to verify all commands and counts.
3. Repeat at increasing line counts for the direct page.
4. Compare direct and single-Form full parsing at matched line counts.
5. Run repeated/nested Forms at modest sizes to expose copy count and lifetime.
6. Keep the fixed-dash controls; do not routinely rebuild the pre-#452 handler.

### Expected evidence and decision gate

- Discard and collect modes deliver matching token/operator counts for matching
  logical content.
- Discard mode has no growing container of callback objects.
- Direct and equivalent Form cases produce matching geometry and rendered output.
- Explain the memory gap between discard and collect modes, rather than assigning
  the entire initial 1.69 GiB to QPDF.
- Quantify the added Form cost before choosing the order of nested-path changes.

If discard mode itself consumes unexpectedly large memory, investigate its
source and object lifetimes before assuming the rest of the plan will provide
the predicted gain. Decide together whether a QPDF-side investigation is needed.
Do not set an absolute memory target until this baseline is available.

## Step 2 — Extract instruction execution from storage

**Purpose:** make the interpreter consume one instruction at a time while
initially keeping the existing buffered delivery path.

### Proposed code changes

Refactor pdf_decoder<STREAM> so stream lifecycle and dispatch are explicit:

- Begin a logical content stream: establish the initial state and capture its
  base transformation before executing its first instruction.
- Consume one normalized instruction: preserve existing inline-image handling,
  operand accumulation, null handling, operator execution, and operand cleanup.
- Finish a logical content stream: handle trailing operands, unresolved marked
  content, and current end-of-stream diagnostics at the same semantic boundary.

The existing vector loop should call these operations. This patch changes the
organization of execution, not whether QPDF tokens are retained.

Preserve:

- Graphics/text state and current transformation behavior.
- Resource resolution, including Form-local resources and inherited fallbacks.
- Operand order and current validation/normalization semantics.
- Recoverable per-operator errors and propagation of resource-exhaustion errors.
- Inline-image header/data state transitions.
- /ActualText, artifact and MCID linkage, and end-of-stream cleanup.
- Existing output order and unknown-operator reporting.

Do not silently add eager object-to-string conversions or extra copies in the
new helper. Treat ownership and reference lifetimes explicitly.

### Experiments and correctness checks for the user

- Compile and run the small native comparison matrix from step 1.
- Compare parsed output and rendered pixels before/after the refactor.
- Run focused split-content-stream, damaged-content, inline-image, graphics-state,
  text, clipping, and actual-text coverage.
- Review warning/error changes as well as successful outputs.

### Expected evidence and decision gate

Memory and runtime should remain within the measured baseline variation; this
is a behavior-preserving preparation step, not the main memory optimization.
Investigate any systematic difference before proceeding. Accept the refactor
only after output equivalence and error-handling behavior are established.

## Step 3 — Interpret page instructions as QPDF delivers them

**Purpose:** remove the complete collected token list from ordinary page
content interpretation.

### Proposed code changes

Allow the QPDF adapter to deliver normalized instructions to the execution
operations from step 2 instead of always appending them to a vector.

For ordinary page content:

1. Set up stream state before the first delivered instruction.
2. Consume operand objects as they arrive.
3. When an operator arrives, execute it and release its operands.
4. Release the delivered instruction when its callback returns unless execution
   explicitly needs ownership of its payload.
5. Finalize once at the end of the logical content stream.

Keep buffered delivery temporarily as an internal test/comparison path. Prefer
a narrow internal selector or test facility over a permanent public parser option.
The public Python API and normal decode configuration should not need to change
just to compare implementations.

Retain normalization in one place so buffered and callback delivery cannot drift.
Do not replace the QPDF parser with a new PDF tokenizer as part of this work.

### Error recovery and stream boundaries: required design work

The existing recovery code uses stream.empty() to decide whether parsing
delivered any tokens before failing. That check becomes invalid if tokens are
consumed without being stored.

Replace it with explicit delivery progress:

- Track whether/how many objects have been delivered for the relevant attempt.
- Distinguish failure before any callback from failure after a prefix was consumed.
- Retry a damaged stream or its intact component streams only under conditions
  that cannot execute the same prefix twice.
- Preserve the existing behavior of retaining usable content before corruption.
- Keep operator-execution errors separate from QPDF parser/decompression errors.
- Do not let a broad exception handler reinterpret allocation failures or
  interpreter failures as a reason to retry the source stream.

A /Contents array is one logical content stream. Continue to support operands
and compound tokens split across its physical streams. Do not reset graphics,
text, pending operands, or marked-content state at each physical boundary.

Specify when finishing occurs after a recoverable error versus an unrecoverable
failure. A cleanup routine must not accidentally emit or replay content on a
failed attempt.

### Timing and instrumentation

Callback execution interleaves QPDF parsing and interpretation. An outer parsing
timer will now include callback work.

Define the new accounting before comparing old stage keys:

- Keep end-to-end full parse wall time as the primary runtime comparison.
- Separate QPDF-exclusive time from callback interpretation where feasible.
- Account for nested interpretation consistently.
- Avoid double counting and negative exclusive timings.
- Document any changed interpretation of existing timing fields.

Do not add per-token logging to the measured fast path. Diagnostic counters
should be bounded and disabled or inexpensive in ordinary operation.

### Experiments and correctness checks for the user

Run buffered and callback delivery on identical input bytes:

- Direct solid-line grids at increasing sizes.
- Fixed repeated dashes and q/Q controls.
- Shapes-disabled parsing to expose the remaining QPDF/input-buffer cost.
- Text, bitmaps, inline images, clipping and large operand-array cases.
- /Contents arrays with a token spanning physical streams.
- Damaged streams that fail before any object and after a usable prefix.
- A bad operator followed by valid content, ensuring recovery does not end the
  page and does not duplicate earlier content.

### Expected evidence and decision gate

For the simple line fixtures:

- Maximum pending numeric operands stays bounded as N increases; it should track
  the operands needed by the operator, not the entire content-stream history.
- No complete vector of all page tokens remains live on the callback path.
- Geometry, page elements, and render instructions match the buffered path.
- Peak memory falls substantially relative to the collecting baseline.
- Shapes-disabled measurements move toward the QPDF-discard baseline plus the
  remaining interpreter state; do not assume equality or promise a fixed MiB cap.
- Full-parse memory still includes legitimate page output.

If memory does not fall as expected, inspect retained handles, large operands,
allocator effects, and remaining container lifetimes before moving on.

## Step 4 — Remove unnecessary retention in nested content

**Purpose:** carry the improvement into the Form-heavy inputs relevant to #345
and the other nested content entry points.

This step may require several separately reviewed patches. Agree on their order
from step 1 and step 3 results; do not combine all nested mechanisms at once.

### Form XObjects first

Replace the materialize-local-vector-then-copy sequence with callback-driven
interpretation where the Form is interpreted once.

Preserve:

- Caller graphics state and save/restore semantics.
- Form /Matrix, /BBox, clipping, and base transformation capture.
- Local fonts, graphics state, colorspaces, shadings, patterns, and XObjects,
  including inherited resource fallback.
- Marked-content linkage and text replacement behavior.
- Transparency-group and soft-mask behavior.
- The lifetime of the caller's callback, operands, QPDF object and source bytes
  while a nested parse is active.

Review QPDF reentrancy and the lifetime of every borrowed object. Verify nested
parsing against the actual supported API rather than assuming that a callback
can safely retain references into a parent parser's temporary objects.

### Other entry points

Audit and convert in bounded groups:

- Annotation/widget appearance streams in pdf_decoder<PAGE>.
- Type 3 character procedures.
- Soft-mask group streams.
- Tiling-pattern parsing and replay.

Ordinary Form parse_stream() currently returns a newly built vector. Tiling
patterns intentionally cache content and must be treated differently.

For intentional replay/caching:

- Identify whether the existing parsed-token cache or decoded render-instruction
  cache is needed for repeated painting.
- Preserve useful reuse without repeatedly copying cached token vectors.
- If a token representation remains necessary, evaluate an immutable borrowed
  view with a clearly proven owner/lifetime.
- Do not move from a cache that another invocation needs, or invalidate a view
  during nested resource resolution.
- Measure both retained cache bytes and repeated execution time.
- Avoid reparsing a frequently used pattern on every paint just to lower one
  snapshot's retained memory.

Review uses of stream = stream_, copies of pattern.parse_stream() results, and
other vector-taking interpretation entry points. A byte-saving local fix must
not change state inheritance or introduce repeated decoding.

### Experiments and correctness checks for the user

- Direct page versus equivalent single Form at matching sizes and settings.
- One Form invoked repeatedly, and nested Forms at controlled depth.
- Different transforms and local resources on repeated invocations.
- Widget appearances and inherited AcroForm resources.
- Type 3 glyphs painted repeatedly and at different transforms.
- Soft-mask groups and transparency.
- Reused colored and uncolored tiling patterns, including nested content.
- The existing focused tests and selected real regression pages covering each
  mechanism touched by the individual patch.

### Expected evidence and decision gate

- Equivalent page elements and pixels.
- Unnecessary local/interpreter token copies disappear on the converted paths.
- Memory differences between direct and Form input become explainable by actual
  source buffers, state, nesting, resources, and retained output.
- Repeated use does not cause unintended cache growth or an unexplained runtime
  regression.
- No claim of constant total memory with nesting: active QPDF content buffers
  and genuine output can still accumulate.

Record an entry-point audit showing which paths now use callbacks, which retain
an intentional immutable representation, and why.

## Step 5 — Validate broadly and retire the comparison path

**Purpose:** establish correctness and memory improvements beyond the diagnostic
fixtures, then remove the temporary old execution path.

### Validation sequence for the user

1. Complete the synthetic direct/nested matrix and focused correctness checks.
2. Run the existing self-contained parser and renderer unit tests.
3. Select representative real vector-heavy and Form-heavy pages from the
   regression corpus. Record document names and page numbers.
4. Compare baseline/candidate extracted content, render instructions where
   meaningful, and rendered output.
5. Run the broader relevant parser and renderer regression suites.
6. Exercise the actual threaded path with one, two and four parser threads,
   using a multi-page fixture that contains several heavy pages.
7. Run a bounded set through the Python-facing API to confirm integration and
   preserve public behavior, after the native comparison has passed.

Use the repository's existing build/test workflow. Codex supplies commands for
the user's environment after the code for that cycle is ready; the commands
below are reference starting points, not a request for Codex to execute them.

Real-document evidence is required for a contribution under CONTRIBUTING.md.
Use the existing dataset first. The real 5.6M-segment Form page mentioned in #345
has not been supplied or reproduced, so do not label a synthetic Form fixture
as validation of that exact file.

### Threaded measurements

Use matching thread counts and settings for baseline/candidate:

- Per-process peak memory across active workers and retained results.
- Throughput, wall time, failures, and output counts.
- Peak behavior with several heavy pages eligible to decode simultaneously.
- Results queue settings and consumer behavior, recorded explicitly.
- Repeated document/page processing to distinguish intended caches from
  cumulative retention.

A shared content stream in a PDF does not imply that concurrent page decoding
will share its working buffers. Measure the actual backend. Do not claim that
max_concurrent_results limits active page decoding.

### Retirement and acceptance

After the user returns successful validation and we agree to accept the result:

- Remove the temporary buffered comparison selector and unused production paths.
- Keep the useful QPDF-discard and collecting diagnostics in the optional native
  probe if they remain valuable for future investigations.
- Remove dead token-vector APIs and copies only after the entry-point audit.
- Keep targeted regression tests for streaming-specific failure modes.
- Recheck timing semantics, public configuration and API compatibility.
- Provide the user a final bounded verification command set for the cleaned-up
  implementation.

Acceptance requires:

- Equivalent expected parsed content, graphics-state semantics, and output order.
- Matching rendered output for the diagnostic fixtures and compliance with the
  existing regression tolerances for the real corpus.
- No silent increase in failed, skipped, or degraded pages.
- A documented, reproducible reduction in peak memory on the relevant inputs.
- The user's explicit priorities are lower memory and no significant slowdown.
  The user now explicitly accepts up to +5% slower. Use a configurable +5%
  review threshold with repetition spread; this supersedes the provisional +3%.
- No complete per-page token list on ordinary streamed interpretation paths.
- An explanation for intentional retained representations and remaining memory.
- No material unexplained runtime regression; interpret changes against the
  observed repetition spread and agree on any practical tradeoff together.
- No recurrence of quadratic dash payload growth.
- A documented result for both the native and threaded/integration paths.

## Correctness coverage to preserve

Use existing tests where they already express the relevant behavior. Add focused
fixtures only for behavior that the streaming change newly risks breaking.

| Behavior | Existing starting points / additional checks |
| --- | --- |
| Split /Contents and compound tokens | tests/test_unit_split_content_streams.py; arrays/operands spanning streams. |
| Corrupted streams and operator recovery | tests/test_unit_damaged_content_streams.py; before-first-token and after-prefix failures; no replayed output. |
| Malformed numbers and null handling | Existing normalization in qpdf/stream_decoder.h; preserve equivalent results through both delivery paths during development. |
| Inline images and regular image XObjects | tests/test_unit_image_decode.py and relevant codec/image tests; BI/ID/EI transitions and payload lifetime. |
| ActualText, MCIDs and artifacts | tests/test_unit_actual_text.py, tests/test_unit_structure_tree.py. |
| Graphics state, clipping and coordinate systems | tests/test_unit_clipping.py, page-geometry and shape-frame tests; q/Q and Form inheritance. |
| Dash semantics | Existing dash coverage in tests/test_unit_patterns.py and tests/test_unit_dash_sanitization.py; fixed repeated-pattern and empty-array controls in the native probe. |
| Form and tiling-pattern resources | tests/test_unit_patterns.py and representative Form-based regression pages. |
| Widgets and appearance streams | tests/test_unit_widget_appearances.py, tests/test_unit_acroform_widgets.py. |
| Type 3 fonts | tests/test_unit_type3_fonts.py. |
| Transparency and masks | tests/test_unit_transparency.py, tests/test_unit_soft_masks.py. |
| Threaded behavior and output equivalence | tests/test_regression_threaded_parse.py, tests/test_regression_threaded_render.py, tests/test_unit_threaded_page_range.py. |

Do not use changed ground truth or weakened tolerances as a substitute for
understanding an output difference.

## User-run command starting points

These commands describe the probe that already exists. The new discard/Form/
callback-comparison options will be specified with the step 1–3 patches; they
must not be presented as already implemented.

Configure and compile:

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON
cmake --build build --target test_memory_on_lines --parallel 2
~~~

Create one deterministic grid, outside every measured process:

~~~sh
mkdir -p /private/tmp/docling-memory-baseline
./build/test_memory_on_lines generate pathstorm 1000000 \
  /private/tmp/docling-memory-baseline/grid.pdf
~~~

Run each mode in a separate process, repeating each three times for reported
results and preserving stdout/stderr under distinct names:

~~~sh
./build/test_memory_on_lines measure tokens \
  /private/tmp/docling-memory-baseline/grid.pdf
./build/test_memory_on_lines measure decode \
  /private/tmp/docling-memory-baseline/grid.pdf
./build/test_memory_on_lines measure skip-shapes \
  /private/tmp/docling-memory-baseline/grid.pdf
./build/test_memory_on_lines measure render \
  /private/tmp/docling-memory-baseline/grid.pdf
~~~

The current tokens mode uses our collecting callback. It is not the proposed
QPDF-only/discard mode.

The optional pause_seconds argument is for external inspection only. Keep it
zero for timing comparisons. The optional PNG output is useful for correctness
comparison; PNG encoding happens after the probe's rendered snapshot.

After the user rebuilds the Python extension using the project's established
workflow, focused tests can start with:

~~~sh
uv run pytest tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_patterns.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_soft_masks.py -q
~~~

Broaden the selection according to the files changed and the stage's acceptance
gate. Running against an old extension is not validation of newly edited C++.

## Results and joint decision record

For every cycle, record:

| Field | Required content |
| --- | --- |
| Cycle / step | Smallest implemented change and its hypothesis. |
| Approval | User instruction approving that bounded change. |
| Build identity | Source revision/diff, compiler/flags, dependency versions, binary hash. |
| Inputs | PDF hashes, document/page selection, actual counts and generator settings. |
| Configuration | Delivery mode, parser flags, render scale, thread count, consumer/queue settings. |
| User commands | Exact build, correctness and measurement commands. |
| Correctness | Output comparisons, tests, warnings/failures, and any differences needing explanation. |
| Memory | Individual observations, median/range, relevant structural counts, and scaling. |
| Runtime | Individual observations and median/range; distinguish stage and process timings. |
| Interpretation | What the evidence establishes, alternative explanations and remaining uncertainty. |
| Joint decision | Keep/revise/revert, and the agreed next experiment or patch. |

Suggested comparison rows:

| Cycle | Input / actual lines | Mode | Threads | Peak RSS baseline → candidate | Parse/runtime baseline → candidate | Max pending operands | Output comparison | Decision |
| --- | --- | --- | ---: | --- | --- | ---: | --- | --- |
| Step 3 | Direct, 999,108 lines | Decode | 1 | 2,200.23 → 1,148.80 MiB | 3.178 → 2.963 process s | 2 | Counts and PNGs matched | Retain page streaming; semantic suite pending |
| Step 3 | Direct, 999,108 lines | Shapes disabled | 1 | 1,742.58 → 108.47 MiB | 2.366 → 2.049 process s | 2 | Expected zero shapes | Native memory/runtime gain verified |
| Step 3 | Direct, 999,108 lines | Render | 1 | 2,219.50 → 1,169.59 MiB | 3.563 → 3.197 process s | 2 | Byte-identical PNG | Native memory/runtime gain verified |

### Progress

- [x] Reproduce the original dash-payload growth in native C++.
- [x] Measure current direct-page dense-grid memory and inspect structural origins.
- [x] Agree on the five-step strategy and document the user/Codex working loop.
- [x] Write the first measurement patch and user-run launcher; native smoke/scaling passed.
- [ ] Preliminary audit: classify state accumulation and reset lifetimes; reproduce suspected defects; jointly review measured candidates.
- [x] Step 1 core checkpoint: QPDF-discard/collect scaling and single-Form overhead measured and reviewed; broader Form reuse sweeps remain optional follow-ups.
- [x] Step 2 checkpoint: buffered refactor, matching grid outputs, scaling and targeted timing recheck accepted; carried-forward semantic suite passed.
- [x] Write step 2's buffered refactor and baseline comparison.
- [x] Write step 3 page callback implementation, boundedness diagnostics and recovery regressions; native user runs passed.
- [x] Step 3 checkpoint: ordinary page memory gain verified; fresh Form callback timing within +5%; focused semantic suite passed.
- [x] Step 4a checkpoint: Form-only callback implementation, native smoke/scaling/reuse and 153 focused semantic tests passed.
- [ ] Step 4: Nested content and intentional-cache audit; results verified.
- [ ] Step 5: Real-document, corpus and threaded validation; comparison path retired.

## Cycle 1 handoff — Step 1 measurement implementation

Date: 2026-11-11.
Authorization: the user said "good, then let's start implementing."
Status: the user subsequently ran the small and core scaling matrices successfully;
see the result records below. Step 2 is proposed, not implemented.

### Implemented scope

- tests/cpp/test_memory_on_lines.cpp:
  - qpdf-discard: parse objects with QPDF and count/release callback objects.
  - collect: parse through the same QPDF entry point, forwarding delivered
    objects to our existing collecting adapter.
  - tokens remains an alias for collect.
  - Both raw modes report delivered objects/operators, decompressed bytes,
    content-size notifications, EOF, retained tokens and token-vector capacity.
  - Record the runtime QPDF version in baseline snapshots.
  - pathstorm-form adds a Form-based grid generator, with optional invocation
    count and nesting depth. The grid leaf has the same content bytes and
    compression as the direct pathstorm case.
  - Generated Form chains are explicitly marked in the page dictionary. Raw
    token modes follow that marked chain and parse the leaf once; they do not
    pretend to execute the page's Do operators.
  - Full parse and render modes still use the production interpreter, executing
    all Form invocations and nested content.
  - Report copied clip paths, points and payload bytes in shape instructions
    as additional bounded audit counters.
- tests/cpp/run_memory_on_lines.py:
  - Launch an already-built native probe in separate subprocesses.
  - Generate PDFs outside measured processes.
  - Cover direct, single-Form, repeated-Form and nested-Form layouts.
  - Check raw leaf counts, generated shape/segment counts, absence of dash
    payload on solid grids, and the direct versus single/nested Form PNG bytes.
  - Record each subprocess lifetime separately from the probe's snapshot timer.
  - Save input/binary/source/dependency hashes, source snapshots, available
    CMake flags/link command, raw JSON/logs, PNGs, and summary.csv.
  - Refuse to overwrite an existing output directory.
  - Use the Python standard library for launching/reporting and tabulate for
    the console summary; no PDF bindings, compilation, dependency installation
    or plotting dependencies.

Production parser code has not been modified. The existing optional CMake
target from the initial reproduction remains sufficient.

For repeated-Form fixtures, unique_lines is the line count inside the leaf,
while lines is the expected total painted output after all invocations.
Raw parsing counts the leaf once even when the full page invokes it repeatedly.
Do not compare that raw token count with total emitted page shapes as if the
two measurements performed the same amount of interpretation.

Clipping byte counters describe the retained instruction snapshots. They are
not an exhaustive heap profiler and do not demonstrate a missing clear.

### Preliminary reset audit: current candidate record

This is a targeted source audit, not a completed repository-wide review.

| Container / operation | Observed lifetime or reset | Classification / next action |
| --- | --- | --- |
| Direct d-operator dash array | clear before replacement, with null/error branches assigning an empty array. | Existing #452 fix retained; no new defect here. |
| ExtGState /D local dash array | Fresh local vector, then assignment to the resource member. | Not the same missing-clear pattern. |
| Current shape paths | register_paths clears after painting; n/discard_paths clear and restore the appropriate scratch state. | Required reset is present in inspected paths; retain semantic coverage. |
| Pending Type 3 procedures | paint_type3_glyphs swaps pending work into a local vector before nested execution/state copies. | Queue drain is present; malformed/reentrant cases still belong to later correctness validation. |
| Inline-image state | begin_inline_image replaces the entry; successful and incomplete-image endings replace it again. | Reset is present in the inspected transitions. |
| Graphics-state and color-space resource setters | Resource collections construct a fresh individual resource before assigning it into their map. | Do not infer a missing member clear without proving a reused instance on a supported path. |
| Page-shape init_from optional graphics fields | Geometry vectors are assigned; optional graphics fields are changed only when their JSON keys are present. | Reuse candidate for stale optional state, not a confirmed memory-growth defect. Trace callers and reproduce before proposing a change. |
| Clip history and snapshots | History intentionally accumulates intersections; snapshots can copy it into paint instructions. | Potential copy amplification; no missing-clear fix proposed and no scaling measurement yet. |
| Tiling-pattern token cache | One parsed stream is intentionally cached and returned for reuse. | Intentional retention; inspect copying and lifetime in step 4. |

No second missing-clear defect has been confirmed. Further resource/font,
malformed-input and cross-page lifetime inspection is pending. A confirmed
independent reset fix would be proposed and measured as a separate cycle.

### First user action: compile and run the small matrix

Use the repository root. The target was already configured in the initial
investigation; rebuild it after these source changes:

~~~sh
cmake --build build --target test_memory_on_lines --parallel 2
~~~

If the build directory has not been configured for this optional target:

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON
cmake --build build --target test_memory_on_lines --parallel 2
~~~

Then run the small matrix, using a fresh output directory:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step1-smoke \
  --sizes 1000 \
  --repeats 1
~~~

This runs 20 measurements: five modes across four layouts. The default repeated
Form is invoked four times; the nested Form has depth three. Generated direct,
single-Form and nested-Form images should match. Repeated painting is not
required to match one painting at antialiased boundaries.

The runner stops and records a failure if compilation was skipped and the
binary does not support the new modes, if a child fails/times out, if a count
check fails, or if the compared PNG bytes differ. PNG-byte differences require
inspection of pixels/encoding rather than automatic ground-truth updates.

Return:

- Build output, especially any compiler error.
- status.json.
- summary.csv if the matrix completed.
- measurements.jsonl and metadata.json.
- image-comparisons.json if present.
- For a failure, its named .log and any generated PNGs.

Codex has reviewed source changes and whitespace only. No build, Python script,
correctness test, fixture generation or benchmark has been run by Codex for
this cycle, in accordance with the working agreement.

### After the smoke results: decide the scaling experiment together

The next suggested run, after we jointly review the smoke result, is three
repetitions for direct and single-Form cases at the previous grid sizes:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step1-scaling \
  --sizes 125000,250000,500000,1000000 \
  --layouts direct,form \
  --repeats 3
~~~

Keep repeated/nested exploration at smaller sizes initially:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step1-form-reuse \
  --sizes 1000,8000 \
  --layouts direct,form,form-repeat,form-nested \
  --repeats 3
~~~

The larger commands are proposed follow-ups, not completed experiments or a
reason to proceed automatically. We first review the user's build and smoke
output, then agree on the next run. Step 2 remains unstarted.

### Pending joint decision

| Item | Status |
| --- | --- |
| Source patch | Implemented for review. |
| Compilation | The user's updated native probe ran successfully; compiler log not supplied. |
| Small-matrix counts and images | Passed: 20 measurements and both direct/Form PNG comparisons. |
| QPDF-discard versus collecting memory | Scaling reviewed: 100.73 versus 1,734.95 MiB at the 1M target. |
| Direct versus Form copy overhead | Scaling reviewed: +427.13 MiB with shapes disabled; identified vector copy is 426.87 MiB. |
| Keep/revise this patch | Pending joint review. |
| Advance to the refactor | Subsequently approved by the user; cycle 2 implementation is awaiting validation. |

### User smoke results and console-format follow-up

The user ran the documented --sizes 1000 --repeats 1 matrix. Local artifacts in
/private/tmp/docling-memory-step1-smoke confirm status=passed, 20 measurements,
and identical PNG bytes for direct versus single-Form and nested-Form output.
The generator emitted 980 unique lines, 6,862 leaf tokens and 28,866
decompressed leaf bytes. Repeated-Form full parsing emits 3,920 lines.

Peak RSS from this single repetition, in MiB:

| Layout | QPDF discard | Collect | Full parse | Shapes disabled | Parse + render |
| --- | ---: | ---: | ---: | ---: | ---: |
| Direct | 7.53 | 9.25 | 12.72 | 12.03 | 19.17 |
| Single Form | 7.55 | 9.30 | 13.28 | 12.22 | 19.58 |
| Repeated Form | 7.59 | 9.22 | 17.58 | 12.27 | 23.66 |
| Nested Form | 7.53 | 9.27 | 13.36 | 12.20 | 19.58 |

These results establish that the first native fixtures, count checks and image
comparisons work on the user's build. They do not establish large-page memory
scaling or a useful runtime comparison: this is one small run, and startup/
first-use costs are visible, especially in rendering.

At the user's request, the console summary now uses tabulate with two decimal
places and grouped integer counts. CSV fields and measurement behavior are
unchanged. tabulate is already declared in the project's dev/perf dependency
groups. This formatting change needs no C++ rebuild; Codex has not rerun the
experiments or the changed launcher.

### User scaling results — QPDF retention and Form-copy attribution

Date: 2026-11-11.
Artifact directory: /private/tmp/docling-memory-step1-scaling.
User-run matrix: requested sizes 125k, 250k, 500k, 1M; layouts direct and form;
five modes; three fresh subprocesses per configuration.
Status: all 120 measurements passed. Direct and single-Form PNG bytes matched
at every size. Runtime QPDF version was 12.4.2.

Build identity and individual observations are preserved in metadata.json,
measurements.jsonl, summary.csv and the per-run logs in that directory.
The native binary SHA-256 was
e5c75a7c7eba3434c419bf47b5a1b71acd77ccddbd5e8c7868bb0527c742b57b.
HEAD remained 511c57a87e4cc72a25d6ac613a095839288ba34e with the measurement
working-tree changes. This was not a production interpreter optimization.

Median peak RSS for the direct-page raw parsing comparison:

| Actual lines | QPDF discard MiB | Collect MiB |
| ---: | ---: | ---: |
| 124,861 | 19.31 | 226.50 |
| 249,652 | 31.03 | 441.22 |
| 499,204 | 54.36 | 873.14 |
| 999,108 | 100.73 | 1,734.95 |

At the largest size, QPDF delivers the same 6,993,758 top-level objects from
29,473,719 decompressed leaf bytes in both modes. The discard callback retains
no token vector; the collector retains all 6,993,758 records.

Collecting peaks 1,634.22 MiB higher, approximately 17.22 times the discard
peak. The discard run has approximately 94.2% lower peak RSS in this isolated
comparison. This is measured evidence about the two raw-parsing modes, not
a claim that full parsing will eventually use only 100.73 MiB or improve by
94.2%. Full parsing still needs page output and interpreter/resource state.
The QPDF-discard curve itself grows approximately linearly with input size.

The Form results provide evidence for the independently identified token-vector
copy. It is most clearly visible with shapes disabled:

| Actual lines | Direct shapes-disabled MiB | Form shapes-disabled MiB | Form minus direct MiB | Token count × 64 bytes, MiB |
| ---: | ---: | ---: | ---: | ---: |
| 124,861 | 229.95 | 283.41 | 53.45 | 53.35 |
| 249,652 | 446.67 | 552.08 | 105.41 | 106.66 |
| 499,204 | 878.50 | 1,091.89 | 213.39 | 213.28 |
| 999,108 | 1,742.45 | 2,169.58 | 427.13 | 426.87 |

In pdf_decoders/stream.h, do_form materializes the local vector returned by
xobj.parse_stream(), and the vector-taking interprete overload copies it with
stream = stream_. At 999,108 lines, the extra inline record allocation is
6,993,758 × 64 = 447,600,512 bytes, or 426.865 MiB. Its size closely matches the
observed 427.125 MiB shapes-disabled Form overhead. This copy shares the QPDF
objects through their handles; it does not duplicate the complete QPDF object
graph. Differences of independent peak-RSS observations still are not an exact
heap accounting.

With full shape output enabled at the largest size:

- Direct full parse: 2,200.27 MiB; Form full parse: 2,399.69 MiB.
- Direct parse + render: 2,218.33 MiB; Form parse + render: 2,401.11 MiB.

The full-parse Form/direct peak difference is smaller than the isolated
shapes-disabled copy difference. Different allocation lifetimes, vector growth,
allocator reuse and peak timing prevent treating these RSS buckets as additive.
Do not infer that a particular local allocation always adds its entire size to
the final process high-water mark.

At the largest size, median full subprocess times were:

| Layout | QPDF discard s | Collect s | Full parse s | Shapes disabled s | Parse + render s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Direct | 0.642 | 1.045 | 3.136 | 2.279 | 3.560 |
| Form | 0.651 | 1.110 | 3.249 | 2.439 | 3.530 |

These provide runtime baselines. The probe also records earlier in-process
snapshot times separately; do not substitute those for subprocess lifetime.

#### Interpretation and next proposed checkpoint

The evidence supports fixing docling-parse's retained token list and nested
vector copies first. It does not establish a QPDF memory bug. QPDF's remaining
buffer/object-working-memory behavior can be investigated separately if it
becomes the dominant cost after these allocations are removed.

Step 1's core comparison is now measured and reviewed. Larger repeated/nested
Form sweeps remain unperformed; the small matrix establishes their functional
fixture checks, and their broader investigation can be scheduled jointly.

The next recommended patch is step 2 only:

- Extract explicit begin/consume-one-instruction/finish operations.
- Continue feeding them from the existing stored vector.
- Preserve operand validation, inline images, state capture, per-operator
  recovery and logical end-of-stream cleanup.
- Hand off focused correctness and buffered-baseline comparison commands.
- Expect approximately unchanged memory; the substantial retention reduction
  belongs to step 3.

No step 2 production edits, builds or experiments were performed while
analyzing these results. The user's decision on starting that patch is pending.
The broader missing-reset audit remains in progress; this data does not confirm
another missing clear.

## Cycle 2 handoff — Step 2 buffered execution refactor

Date: 2026-11-11.
Authorization: the user explicitly requested implementing step 2 and clarified
the priorities: "(1) the memory is lower and (2) we do not get significantly
slower (up to a few % is ok)".
Status: native smoke/scaling comparisons passed; see the result record below.
Focused semantic-test results and a targeted timing review remain pending.

### Acceptance criteria clarified by the user

The eventual optimized parser must reduce peak memory and preserve throughput
within a few percent of the comparable baseline. A large memory reduction alone
is not sufficient if it introduces a substantial slowdown.

For reporting, start with a configurable +3% wall-time review threshold. This
is a working interpretation of "a few percent", not an exact threshold the user
specified or a statistical significance test. Values above the threshold require
review/repetition; they do not automatically make the run's correctness fail.
Change the threshold explicitly with --slowdown-limit-pct if we jointly choose
a different working budget.

For step 2 specifically, memory is expected to remain approximately unchanged:
the complete token vector and Form vector copy still exist. This checkpoint
establishes that the execution refactor preserves output and does not add an
unacceptable runtime cost. The substantial token-retention reduction belongs
to step 3 and must be measured after that change.

### Production change

Only src/parse/pdf_decoders/stream.h changes production behavior organization:

- begin_interpretation() contains the existing base-CTM capture.
- consume_instruction() contains the existing dispatch for inline-image data,
  operand accumulation, null filtering, operator execution, recoverable
  per-operator errors and operand cleanup.
- finish_interpretation() contains the existing marked-content end cleanup.
- interprete_stream() remains a buffered index loop and calls those operations.

The instruction consumer is inline and accepts the instruction and operands by
reference. No new per-token callback abstraction, std::function, counter,
container allocation or record copy is added. The compiler's inlining choice
and its actual runtime effect still need measurement; inline is not a guarantee
of zero call overhead.

Caller-owned pending operands, the public ensure_stack() call, nested-stack
rebasing, caller-specific trailing-operand diagnostics, exception types and
timing boundaries are preserved. There is no new unconditional cleanup on an
exception and no altered damaged-stream retry policy in this patch.

The only remaining source changes for this cycle are the benchmark launcher
and this plan. Step 3 streaming delivery has not been implemented.

### Launcher comparison

The existing launcher adds:

- --baseline RESULT_DIRECTORY: read the user's existing observations without
  executing or rebuilding the baseline.
- --slowdown-limit-pct 3: configurable timing review threshold.
- Preflight check that the baseline passed and contains the requested cases.
- Matching generated input hashes, probe configuration, dependency hashes and
  runtime QPDF version.
- Matching semantic shape/segment/dash/clip counters.
- Baseline/candidate PNG-byte comparison, in addition to the direct/Form checks
  already performed within a run.
- comparison.csv with before/after peak MiB, peak-memory percentage change,
  median process wall seconds, timing percentage change, timing ranges and
  review flags.
- baseline-comparison.json and a tabulate comparison table.
- An explicit identical-binary flag for repeat/control comparisons.
- The comparison report embedded in status.json, so status=passed cannot be
  mistaken for evidence that no timing cases require review.

Negative memory/time percentage changes are improvements. Overlapping observed
timing ranges are labeled when the median exceeds the review threshold.
Comparisons with fewer than three repetitions are labeled diagnostic.

The comparison uses process_wall_s from the same launch protocol for both
versions, including process teardown. The individual in-process snapshot
elapsed times remain available separately. Raw QPDF-discard and collect modes
also serve as controls: their parsing path was not changed by this refactor.

### User commands: preserve baseline, rebuild, run sanity comparison

Before rebuilding the current native binary, optionally preserve it for a fresh
baseline timing run if the historical measurements later look noisy:

~~~sh
cp -n build/test_memory_on_lines /private/tmp/docling-memory-before-step2
~~~

The intended baseline binary is the one from the accepted step-1 scaling
experiment, hash
e5c75a7c7eba3434c419bf47b5a1b71acd77ccddbd5e8c7868bb0527c742b57b.
Copying an already-rebuilt step-2 binary would not preserve that baseline.
No binary copy or compilation has been performed by Codex for this cycle.

Rebuild the native target:

~~~sh
cmake --build build --target test_memory_on_lines --parallel 2
~~~

Run the small existing fixture matrix and compare it with the saved smoke run:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step2-smoke \
  --baseline /private/tmp/docling-memory-step1-smoke \
  --sizes 1000 \
  --repeats 1
~~~

This is a build/count/image check across all four layouts. Its timing flags
are diagnostic because there is only one repetition. Use a new output directory;
the launcher refuses to overwrite existing results.

### User commands: compare memory and runtime at scale

Once the native sanity comparison passes, run the same scaling matrix as step 1:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step2-scaling \
  --baseline /private/tmp/docling-memory-step1-scaling \
  --sizes 125000,250000,500000,1000000 \
  --layouts direct,form \
  --repeats 3 \
  --slowdown-limit-pct 3
~~~

This performs 120 candidate measurements and compares them with the 120 saved
baseline measurements. It does not rerun the old binary. Review:

- Peak-memory changes: approximately flat is expected at this preparation step.
- Full decode and render process wall-time changes, especially at larger sizes.
- Timing range and repetition spread.
- Raw QPDF-mode controls for general machine/load differences.
- Exact input, output-counter and PNG checks.

A flag above +3% calls for investigation, not an immediate conclusion that the
refactor is slower. If necessary, agree on a fresh, interleaved baseline/candidate
comparison using the preserved baseline executable and extra repetitions. Do
not introduce compiler-specific forced-inlining or other optimizations before
the returned data shows a need.

### User correctness checks beyond line fixtures

The synthetic grids do not exercise text, damaged operands, split compound
tokens, inline images, nested text state or ActualText. Before accepting this
production refactor, rebuild the extension in the established project environment
and run focused existing tests.

The existing documented build workflow is:

~~~sh
uv run --no-sync python local_build.py
~~~

Then:

~~~sh
uv run --no-sync pytest \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_soft_masks.py -q
~~~

Keep build mode and dependencies aligned with the baseline; no dependency upgrade
is part of this patch. A stale extension does not validate the edited C++.
The broader real-document/corpus gate in step 5 remains necessary; these checks
do not replace it.

No additional implementation-mirroring unit tests were introduced for the
mechanical extraction: existing content-semantic tests and direct baseline output
comparisons cover this checkpoint.

### Evidence to return and pending decision

Return compilation errors if any; otherwise status.json, comparison.csv,
baseline-comparison.json and the focused test result. Preserve measurements.jsonl,
metadata.json and logs for further analysis.

Codex performed read-only source review and whitespace checks. Codex did not
compile, generate PDFs, run the launcher, run correctness tests or collect
candidate measurements for this cycle.

| Item | Status |
| --- | --- |
| User acceptance priorities | Lower memory and up to +5% slowdown; explicitly confirmed after the recheck. |
| Step 2 implementation | Written; buffered delivery retained. |
| Native/extension builds | Native candidate ran successfully; rebuilt-extension semantic results not yet reported. |
| Small-fixture baseline comparison | Passed 20 measurements and all four baseline image comparisons. |
| Scaling memory/runtime comparison | Passed 120 measurements; memory approximately flat; three timing flags reviewed below. |
| Focused semantic tests | Awaiting user. |
| Keep/revise refactor | User authorized proceeding after the seven-repetition timing recheck; semantic checks carry forward. |
| Step 3 callback delivery | Source written; see Cycle 3 for validation and pending measurements. |

### User step-2 results — Buffered refactor comparison

Date: 2026-11-11.
Source of evidence: the user's comparison table and saved artifacts under
/private/tmp/docling-memory-step2-smoke and
/private/tmp/docling-memory-step2-scaling.

The small 20-measurement matrix passed its input/counter/image comparisons
against step 1. The scaling matrix passed all 120 measurements, comparing 40
three-repetition configurations against the saved baseline. All eight
direct/Form render outputs at the four larger sizes matched the corresponding
baseline PNG bytes. Baseline and candidate binaries were different, as expected.

Candidate native binary SHA-256:
1f037251bd0b8938449d3eb2e86d0b934afc241b442a0c1df579f5db26bbb551.

#### Memory conclusion

Peak-memory changes across the scaling comparison ranged from -0.785% to
+0.106%. Memory is effectively unchanged at this checkpoint, as expected for the
buffered refactor. The retained token list and Form copy have not been removed.
Do not count this as the project's required memory reduction.

#### Main decode/render timing conclusion

All 16 full-decode and render configurations are within the +3% review budget.
The largest observed slowdown among those modes is +1.342%.

At the largest requested size:

| Layout | Mode | Memory change % | Process-time change % |
| --- | --- | ---: | ---: |
| Direct | Decode | -0.001 | +1.342 |
| Direct | Render | +0.053 | +0.080 |
| Form | Decode | +0.021 | -0.530 |
| Form | Render | -0.034 | -0.819 |

Many smaller cases measured faster, but unchanged QPDF control paths also
shifted. Do not attribute all apparent speedups to this mechanical refactor.

#### Three timing review flags

| Layout / mode at 1M target | Median change % | Baseline process range s | Candidate process range s | Interpretation |
| --- | ---: | --- | --- | --- |
| Direct collect | +4.266 | 1.0446–1.0776 | 1.0671–1.0914 | Ranges overlap; collecting does not execute the refactored interpreter. |
| Direct shapes disabled | +3.815 | 2.2650–2.2977 | 2.3167–2.4263 | Ranges do not overlap; this path executes the refactor and needs a targeted repeat. |
| Form QPDF discard | +4.375 | 0.6506–0.6590 | 0.6603–0.6832 | Does not execute the refactored interpreter; demonstrates that an unchanged control can also shift between these runs. |

The control shifts make machine/load variation a plausible explanation, not
proof that the shapes-disabled flag is harmless. A small per-instruction cost
could be more visible with shape construction disabled. Resolve that specific
question with a fresh comparison rather than forcing inlining or rerunning
the entire scaling matrix immediately.

#### Next proposed user experiment: targeted fresh baseline and candidate

The preserved binary at /private/tmp/docling-memory-before-step2 exists.
Codex verified its file hash by read-only inspection: it matches the original
step-1 binary hash e5c75a7c7eba3434c419bf47b5a1b71acd77ccddbd5e8c7868bb0527c742b57b.

Run the old binary on the direct 1M case, with QPDF-discard as a control and
shapes-disabled parsing as the case under review:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --exe /private/tmp/docling-memory-before-step2 \
  --output /private/tmp/docling-memory-step2-recheck-baseline \
  --sizes 1000000 \
  --layouts direct \
  --modes qpdf-discard,skip-shapes \
  --repeats 7
~~~

Immediately follow with the current binary and compare the fresh results:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step2-recheck \
  --baseline /private/tmp/docling-memory-step2-recheck-baseline \
  --sizes 1000000 \
  --layouts direct \
  --modes qpdf-discard,skip-shapes \
  --repeats 7
~~~

These are two short back-to-back batches, not a fully interleaved statistical
experiment. They refresh the machine conditions and increase the repetition
count without changing code. If they still leave the question unresolved,
decide jointly whether alternating the executables or profiling their generated
code is warranted.

The native source snapshot saved while running the old executable describes
the current working tree, not the historical source compiled into that old
binary. Its executable hash, the original step-1 metadata and known step-2 diff
establish the baseline identity.

#### Correctness and next implementation gate

Focused semantic-test results after rebuilding the extension have not yet been
reported in this conversation. Grid counters and matching pixels do not
substitute for the damaged-stream, split-content, text/ActualText, inline-image,
pattern and nested-content coverage already listed for step 2.

Recommendation: provisionally retain the refactor, obtain the focused semantic
test result, and resolve the direct shapes-disabled timing flag before accepting
the step and starting callback delivery. This is a recommendation, not recorded
joint approval for step 3.

No candidate builds, tests or benchmark executions were run by Codex while
analyzing these results. Only saved artifacts and hashes were inspected.
No production code changes were made in response to this result.

## Cycle 3 handoff — Step 3 callback-driven page content

Date: 2026-11-11.
Authorization: after the seven-repetition recheck, the user said:
"Up to 5% slower is totally acceptable. I would go to step 3 now."
Status: step 3 source code written; user compilation and all new validation pending.

### Step 2 recheck and updated runtime agreement

The user's fresh recheck compared the preserved original baseline with the
step-2 binary on the direct 1M case, using seven repetitions per mode:

| Mode | Baseline peak MiB | Candidate peak MiB | Baseline process s | Candidate process s | Time change % |
| --- | ---: | ---: | ---: | ---: | ---: |
| QPDF discard | 100.77 | 100.72 | 0.643 | 0.639 | -0.68 |
| Shapes disabled | 1,742.58 | 1,742.58 | 2.230 | 2.251 | +0.96 |

The earlier shapes-disabled timing flag did not reproduce at the same size
under this refreshed comparison. The native checkpoint supports proceeding
with the mechanical refactor. No forced-inlining change was made.

The current runtime allowance is explicitly **up to +5% slower** for the
optimized implementation, together with lower peak memory and equivalent
output. This supersedes the earlier provisional +3% review threshold.
The launcher's default is now 5%; the threshold remains configurable.

Focused semantic-test results for the rebuilt extension have not been reported.
The user explicitly authorized step 3; carry those checks forward into step-3
validation rather than treating them as already passed.

### Production changes

1. qpdf/stream_decoder.h supports either its existing vector collector or a
   borrowed function-pointer consumer and context.
2. Both delivery paths use the same normalization logic for QPDF objects, null
   operands and malformed numeric tokens.
3. pdf_decoder<STREAM>::decode_and_interprete() establishes stream state,
   consumes normalized objects immediately and finalizes the logical stream
   once when decoding/recovery completes.
4. pdf_decoder<PAGE>::decode_contents() uses immediate consumption by default.
5. The temporary DOCLING_PARSE_BUFFERED_PAGE_CONTENTS compile definition keeps
   the buffered page path only for a standalone comparison executable. No public
   Python configuration switch or environment variable is introduced.

QPDF parsing still buffers its decompressed input. The new path removes our
whole-page vector of rich token records; output geometry, text, bitmaps and
required current state still use memory.

Nested Forms, widget appearances, Type 3 character procedures, soft masks and
cached tiling-pattern content retain their existing buffered entry points in
this step. Parent page callbacks now execute their nested operations before
the parent QPDF parse returns, so Form/pattern/image tests are important even
though conversion of their own source streams remains step 4.

Source inspection of QPDF v12.4.2 shows that each parseContentStream invocation
has its own content buffer, OffsetBuffer and Tokenizer. This supports the
reentrant design; it is not a substitute for executing the nested fixtures and
semantic tests on the user's build.

### Recovery and exception ownership

- Delivered-object progress replaces stream.empty() / stream.size() as the
  recovery decision signal.
- A source failure before any delivered objects can still recover component
  streams or a usable decompressed prefix.
- A prefix that has already been delivered is not replayed.
- A consumer exception is retained as an exception_ptr and rethrown instead
  of being mistaken for damaged PDF data.
- std::bad_alloc propagates through all three decoder recovery catch sites.
  Buffered tokenization also receives this allocation-failure correction.
- Callback/context references are borrowed only during synchronous decoding.
  Operands and output that must outlive a callback hold their own values or
  QPDF handles; they do not retain a reference to its temporary token record.
- QPDF's component EOF callbacks do not perform interpreter cleanup. Logical
  stream finishing happens once after the entire page /Contents operation.
- Fatal interpretation/allocation errors leave through the exception path;
  finishing is not invoked through an unconditional destructor that could
  emit/replay content during failure.

### Timing semantics

Callback delivery interleaves parsing and execution. The streaming path reports
content_parse_interprete_total: combined parsing/operator self-time after
subtracting already-attributed nested work. This key is registered in the native
timing tree and the performance-analysis breakdown.

The buffered comparison keeps content_decode_total and interprete_ops_total.
Do not compare one of those old component keys directly with the new combined
key. Overall parse/process time is the acceptance metric.

No clock calls were added to the per-object/per-operator callback to reconstruct
the old split. Pending-operand high-water counting is enabled only for the
optional native probe targets, using DOCLING_PARSE_CONTENT_DIAGNOSTICS. Production
extension builds do not pay for that high-water update.

### New diagnostics and their scope

The probes report:

- page_content_delivery: callback or buffered.
- page_source_objects and page_source_bytes_observed.
- page_buffered_tokens and page_buffered_token_capacity.
- page_operand_diagnostics and page_max_pending_operands.

These page_* values cover the page's /Contents only, not a total over nested
Form sources.

For direct grid fixtures:

- Both implementations consume 7 × unique_lines + 2 page-source objects.
- Buffered delivery retains that many source records.
- Callback delivery retains zero source records and zero token-vector capacity.
- The pending operand high-water mark is two, independent of line count.

For Form fixtures:

- The page /Contents wrapper consumes four objects per Form invocation and has
  a pending operand high-water mark of one.
- Raw QPDF-discard/collect modes continue to inspect the Form's leaf once.
- Large Form-source vectors remain live in the existing nested path. A zero
  page_buffered_tokens reading must not be described as zero nested retention.

The launcher validates these structural claims on generated fixtures. Its CSV
now contains the page-source/delivery/operand fields. The CMake flags saved with
each run come from the actual executable target's directory, so buffered and
callback builds can be distinguished.

### Native comparison and regression targets

With DOCLING_PARSE_BUILD_MEMORY_PROBE=ON, build:

- test_memory_on_lines: normal callback page delivery.
- test_memory_on_lines_buffered: the same source/dependencies with buffered
  page delivery, useful for controlled comparison and diagnosing failures.
- stream_callback_regression: focused native consumer/recovery checks.

The native regression covers:

- Delivery order and normalization equivalence with buffered collection.
- Inline-image object delivery.
- Consumer failures and std::bad_alloc before/after a delivered prefix.
- Those failures on valid Flate, damaged stream arrays and recovered Flate
  prefixes, covering different recovery contexts.
- Decoder reuse after a consumer failure.
- Preserving intact streams before and after a damaged array component exactly
  once.

tests/test_unit_split_content_streams.py additionally checks pending operands
crossing physical streams and a damaged middle stream between two text-bearing
streams without duplicate prefix output.

All checks are written but have not been compiled or executed by Codex.

### First user commands: configure, compile and run native recovery checks

The additional targets require CMake regeneration:

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines \
  test_memory_on_lines_buffered \
  stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'
~~~

Do not infer success from the existence of an older native binary. The new
targets, delivery labels and diagnostics identify the new build.

### First user experiment: compare the two builds on small fixtures

Create a fresh same-source buffered reference:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered \
  --output /private/tmp/docling-memory-step3-buffered-smoke \
  --sizes 1000 \
  --repeats 1
~~~

Then run callback delivery and compare:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step3-smoke \
  --baseline /private/tmp/docling-memory-step3-buffered-smoke \
  --sizes 1000 \
  --repeats 1
~~~

Each matrix contains 20 runs across direct, single-Form, repeated-Form and
nested-Form layouts. The expected first gate is matching inputs/counters/PNG
bytes, zero buffered page tokens on the callback path and the expected pending
operand bounds. One repetition at this size is not a runtime-budget test.

Return any compilation/native regression failure first. If successful, return
status.json, comparison.csv, baseline-comparison.json, summary.csv and relevant
logs for the two small runs.

### Scaling after the small gate

The primary end-to-end comparison should remain against the user's step-2
baseline, so shared changes in a newly built buffered reference do not hide an
overall regression relative to the prior implementation:

~~~sh
python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step3-scaling \
  --baseline /private/tmp/docling-memory-step2-scaling \
  --sizes 125000,250000,500000,1000000 \
  --layouts direct,form \
  --repeats 3 \
  --slowdown-limit-pct 5
~~~

Expected hypothesis, not a measured result:

- Direct-page peak memory falls substantially, especially with shapes disabled.
- Full output memory remains approximately linear in emitted elements.
- Form-heavy pages retain most of their nested-source cost until step 4.
- Full parse/render runtime remains within the user's +5% allowance, or improves.
- Counts/pixels remain equivalent.

If the historical comparison is noisy or a change needs attribution, collect
the matching larger buffered matrix with --exe build/test_memory_on_lines_buffered
and compare callback delivery against that fresh reference as well. Both probe
builds have operand diagnostics enabled, whereas the historical step-2 build did
not; account for instrumentation and machine variation when interpreting a small
timing difference. Do not credit shared buffered-path changes as a callback-only
benefit.

Agree on extra measurements from the returned results; do not launch them
automatically.

### Semantic validation before accepting step 3

Rebuild the extension using the established workflow, then run the focused
existing tests plus the new split/recovery cases:

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py -q
~~~

These must exercise the new extension, not an older installed binary. In
particular, verify operator-error recovery, the usable prefix of a corrupted
Flate stream, physical stream splits, inline images, ActualText finalization and
nested content invoked while a parent QPDF callback is active.

Do not update ground truth, weaken tolerances or accept a success count that
silently drops content. The larger corpus and real-document checks remain in
the final validation stage.

### Pending decision

| Item | Status |
| --- | --- |
| Step-2 targeted native recheck | Reviewed; +0.96% on shapes-disabled parsing, unchanged peak memory. |
| User runtime allowance | Explicit maximum +5% slowdown; default review threshold updated. |
| Step-3 source implementation | Written for review. |
| New native builds/regressions | Awaiting user. |
| Small buffered/callback comparison | Awaiting user. |
| Scaling memory/runtime comparison | Awaiting user after the small gate. |
| Rebuilt-extension semantic tests | Awaiting user; earlier results were not reported. |
| Accept/revise callback delivery | Pending joint review. |
| Step 4 nested-content conversion | Not started. |

No build, test, fixture generation or benchmark was run by Codex for this cycle.
Only source inspection and whitespace checks were performed. No memory-reduction
or speedup measurement is claimed for the written implementation.

## Cycle 4 handoff — Step 4a: stream ordinary Form XObjects

Date: 2026-11-11.
Authorization: after reviewing the step-3 Form recheck, the user said:
"Yes, please go ahead with step 4."
The immediately proposed next patch was Form XObjects only.
Status: Form source conversion written; user compilation and experiments pending.
Step 4 remains open for its other entry points and intentional-cache audit.

### Step-3 evidence reviewed before this patch

The user ran the two 20-measurement smoke matrices, the 120-measurement scaling
matrix, and two 56-measurement Form rechecks. Saved artifacts were inspected:

- /private/tmp/docling-memory-step3-buffered-smoke
- /private/tmp/docling-memory-step3-smoke
- /private/tmp/docling-memory-step3-scaling
- /private/tmp/docling-memory-step3-form-recheck-buffered
- /private/tmp/docling-memory-step3-form-recheck

Both smoke matrices passed; their four baseline/candidate PNG pairs were
byte-identical. The 120 scaling measurements passed their fixture assertions
and all eight scaling PNG comparisons matched byte-for-byte. All 72 decoded
scaling measurements reported callback page delivery, zero buffered page token
size/capacity, and pending operand maxima of two (direct) or one (Form wrapper).
The saved CTest log also reports stream_callback_regression passed.

At the 1M direct-page target:

| Mode | Step-2 peak MiB | Step-3 peak MiB | Memory change % | Process-time change % |
| --- | ---: | ---: | ---: | ---: |
| Decode | 2,200.23 | 1,148.80 | -47.79 | -6.79 |
| Shapes disabled | 1,742.58 | 108.47 | -93.78 | -13.38 |
| Render | 2,219.50 | 1,169.59 | -47.30 | -10.28 |

Shapes-disabled memory is close to the QPDF-discard result of 100.73 MiB.
Direct decode/render/skip-shapes process times remain within +5% at all four
sizes. Form source memory remains approximately unchanged, as expected because
step 3 converts only the page /Contents.

The historical comparison flagged smaller Form cases. A fresh comparison of
step-3 buffered-page and callback-page executables, seven repetitions per case,
gave:

| Form target | Decode time change % | Shapes-disabled time change % |
| --- | ---: | ---: |
| 125,000 | +0.83 | +0.49 |
| 250,000 | +1.41 | +2.70 |

Both fresh runs passed all 56 measurements. All eight comparisons are within
+5%, with overlapping repetition ranges; no PNG comparison was requested in
that recheck. Raw controls differed by +2.87% to +3.70%. This supports a small
callback-specific cost on these Form inputs. It does not attribute the earlier
historical gap to either machine variation or shared code overhead.

The user authorized moving on. Focused rebuilt-extension semantic test results
have still not been reported; carry those tests into this cycle. Do not mark
the complete semantic validation or step 5 as passed from grid evidence.

### Hypothesis and production change

For an ordinary Form Do, the old path does both:

1. xobj.parse_stream() builds a local vector of normalized source records.
2. new_stream.interprete(insts, parameters) copies that vector into the child
   interpreter and then executes it.

The new path copies a QPDF stream handle from the resource, sets up the child
interpreter exactly as before, and calls decode_and_interprete(content, parameters).
It reuses the step-3 normalizer, callback ownership, recovery and logical-stream
lifecycle rather than duplicating the operator dispatcher.

This should remove both Form token vectors. It is a hypothesis about peak
memory and performance until the user runs the new binaries.

### State and ownership review

The Form still creates parent-linked resource sets for fonts, ExtGState,
colorspaces, shadings, patterns and XObjects. Local resources are populated in
the same order before source interpretation.

The existing parent q(), transparency-group entry, /Matrix application,
update_stack() resource rebase, inherited MCID/artifact linkage, shared soft-mask
cache/depth, child Q() and parent Q() remain in place. The begin hook captures
the Form base CTM after its matrix and inherited stack have been established.

The returned QPDFObjectHandle is a value held until synchronous decoding returns.
The owning QPDF document must remain alive; that owner is unchanged. No token
reference or consumer/context is cached after decode returns. Nested Form
parsing leaves each parent callback and its operand vector alive on its own
stack. The nested tests must verify that behavior in the supported build.

Existing clipping and /BBox behavior is retained. This patch does not implement
the preexisting /BBox/page-boundary TODO in do_form().
The ordinary Form path has no new token cache. Repeated Do invocations parse
the source again just as before; measure that cost rather than assuming reuse
or removing useful caches elsewhere.

Consumer failures and allocation failures use the step-3 propagation rules.
The legacy buffered resource helper now also rethrows std::bad_alloc explicitly,
rather than wrapping it as a logic_error that operator recovery could swallow.
Recoverable operator errors, damaged-source prefix repair and trailing operand
reporting continue through their existing mechanisms.

### Timing semantics

The streaming Form path records form_parse_interprete_total, a combined Form
parsing/operator self-time after subtracting work already attributed during
nested execution. It notes that self-time as attributed so enclosing Forms and
the page do not count it again.

Form machinery remains total time minus resource setup and complete child
source/execution wall time. The buffered comparison retains parse_stream_total
and its old interpretation path.

The new key is registered in pdf_timings and the performance-analysis breakdown.
No per-token timer is introduced. Use full process/parse runtime, not an old
tokenization-only bucket, for the +5% acceptance decision.

### Comparison targets

With DOCLING_PARSE_BUILD_MEMORY_PROBE=ON:

| Executable | Page source | Ordinary Form source |
| --- | --- | --- |
| test_memory_on_lines | Callback | Callback |
| test_memory_on_lines_buffered_forms | Callback | Buffered, including the old vector copy |
| test_memory_on_lines_buffered | Buffered | Buffered, including the old vector copy |

The Form-only reference isolates this patch while keeping page streaming.
Both comparison definitions are restricted to standalone native targets.
Normal production builds use streamed pages and streamed ordinary Forms.

Codex preserved the original step-3 executable at:

- /private/tmp/docling-memory-before-step4
- SHA-256: 4222c234f884d09389da15e523f39911a193bd5700ae658144029580f2a3711b

Its hash matches both the step-3 scaling and final Form-recheck metadata.
Copying/checking this binary did not execute it. Use it for fresh historical
rechecks if needed; the primary scaling baseline can remain the saved step-3
results. A source snapshot collected while executing this historical binary
would describe today's working tree, so retain the original metadata/hash too.

### Form diagnostics

Probe targets enable scalar aggregation over Do calls reached from the page's
ordinary Form tree:

- form_content_delivery: callback or buffered.
- form_diagnostics: whether probe aggregation is enabled.
- form_executions: total Form invocations, including nested/repeated calls.
- form_source_objects: normalized source objects consumed over those calls.
- form_materialized_tokens: cumulative records materialized across the legacy
  local and interpreter vectors. This is not peak live tokens or allocated bytes.
- form_max_pending_operands and form_max_depth.

Existing page_* fields still describe only the page /Contents.
These Form counters do not aggregate across separate buffered appearance,
Type 3, soft-mask or pattern interpreter entry points. Generated fixtures use
the ordinary Form tree and therefore have exact expectations.

Let L be unique grid lines, D the Form depth and R page-level repetitions:

- Direct pages: zero Form executions, objects, materializations and depth.
- Form fixtures: D * R executions and maximum depth D.
- Form source objects: R * (7 * L + 2 + 4 * (D - 1)).
- Maximum pending operands inside Forms: two.
- Callback Form materializations: zero.
- Buffered Form materializations: twice the consumed Form source-object count.

The launcher validates these fields on every decoded measurement and writes
them to summary.csv. Existing tabulate memory/runtime summaries remain.
It also snapshots the relevant production headers alongside probe sources,
flags and hashes. Older saved baselines without these diagnostic fields remain
readable, so the step-3 data can be compared without regeneration.

Aggregation and operand high-water updates are enabled only for native probes;
production does not perform the aggregation after each Do.

### New semantic regression coverage

tests/test_unit_form_streaming.py adds four cases:

1. Repeated, nested Forms with distinct matrices and shadowed ExtGState names:
   verify local opacity, restored outer opacity, continued outer painting and
   restored page color/transform.
2. A damaged Flate Form with a recoverable prefix, invoked twice, followed by
   parent text: verify both executions and the suffix appear exactly once.
3. A Form's closed ActualText scope applies its replacement before parent text.
4. An unclosed Form ActualText scope preserves literal text and cannot capture
   the parent suffix.

These are meaningful behavior checks for nested callback execution; they have
not been compiled/run by Codex. Existing transparency, soft-mask, pattern,
widget and Type 3 tests remain important because they may invoke ordinary Forms
even while their own source entry points stay buffered.

### First user commands: configure and build

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines \
  test_memory_on_lines_buffered_forms \
  stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'
~~~

This CTest target covers generic callback normalization/recovery. It does not
replace the generated Form comparison or extension semantic tests.

### First user experiment: small Form matrix

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_forms \
  --output /private/tmp/docling-memory-step4-buffered-forms-smoke \
  --sizes 1000 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4-smoke \
  --baseline /private/tmp/docling-memory-step4-buffered-forms-smoke \
  --sizes 1000 --repeats 1
~~~

Each run has 20 measurements across direct, single-Form, repeated-Form and
nested-Form fixtures. Require matching counters and all four baseline PNGs,
expected execution/depth counts, zero callback Form materialization, and
two pending Form operands independent of line count.

One repetition at 1,000 lines is a correctness/structural smoke check, not a
runtime-budget measurement. Return compilation or assertion failures first.

### Scaling after the small gate

Keep the historical step-3 comparison to assess the complete change:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4-scaling \
  --baseline /private/tmp/docling-memory-step3-scaling \
  --sizes 125000,250000,500000,1000000 \
  --layouts direct,form --repeats 3 --slowdown-limit-pct 5
~~~

Expected hypothesis:

- Direct results remain close to step 3.
- Single-Form peak memory falls substantially, approaching direct-page memory
  plus explainable Form state/resource/source-buffer costs.
- Shapes-disabled Forms lose both large token vectors.
- Runtime stays within +5% or improves.
- Counts, order and rendered pixels remain equivalent.

If needed, compare against freshly run test_memory_on_lines_buffered_forms or
the preserved original step-3 binary. The former isolates callback Form delivery;
the latter detects shared overhead relative to the historical implementation.

After reviewing that scaling result, a bounded repeated/nested sweep is available:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_forms \
  --output /private/tmp/docling-memory-step4-buffered-forms-reuse \
  --sizes 125000,250000 --layouts form-repeat,form-nested \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4-reuse \
  --baseline /private/tmp/docling-memory-step4-buffered-forms-reuse \
  --sizes 125000,250000 --layouts form-repeat,form-nested \
  --modes decode,skip-shapes,render --repeats 3
~~~

The default repeated fixture paints four times as many lines as the target;
the nested fixture has depth three. Each reuse matrix has 36 measurements.
Account for emitted-output growth and active QPDF buffers when interpreting
depth/repetition results. Agree on these additional runs from the first results;
do not execute them automatically.

### User semantic validation

Rebuild the extension, then run the new Form cases together with the pending
step-3 suite:

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_form_streaming.py \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py -q
~~~

The tests must use the rebuilt extension. Report failures without updating
ground truth or weakening assertions. The native grid alone is not semantic
acceptance for resource shadowing, ActualText or transparency.

### Entry-point audit and next decision

| Entry point | Current implementation | This cycle |
| --- | --- | --- |
| Ordinary page /Contents | Callback | Retained from step 3 |
| Ordinary Form Do | Callback | Native smoke/scaling/reuse and focused semantic coverage passed |
| Appearance/widget source | Buffered | Later bounded step-4 patch |
| Type 3 CharProc source | Buffered | Later bounded step-4 patch |
| Soft-mask group source | Buffered | Later bounded step-4 patch |
| Tiling-pattern source/replay | Intentional cached tokens and copies | Audit/optimize separately; preserve repeated-use benefit |

A Form Do encountered inside one of those other interpreters uses the converted
do_form() path, but that does not convert the caller's own source storage.

Return status.json, comparison.csv, baseline-comparison.json, summary.csv and
the focused test result. Keep metadata, measurements.jsonl, source snapshots and
logs for analysis. Decide jointly whether to retain/revise this Form patch and
which remaining step-4 entry point to tackle next.

Codex performed source review, preserved/hashed the old executable, and checked
whitespace. Codex did not compile, execute the native regression, generate PDFs,
run the launcher or run pytest. No step-4 memory or runtime result is claimed.

### User results — Step 4a native and focused semantic checkpoint

Date: 2026-11-11.
The user returned the smoke, single-Form scaling and repeated/nested scaling
results, then reported 153 focused tests passing in 6.91 seconds. These results
complete the current page/Form implementation checkpoint. They do not complete
all step-4 conversions or the broad step-5 validation.

Evidence directories inspected:

- /private/tmp/docling-memory-step4-buffered-forms-smoke: 20 measurements passed.
- /private/tmp/docling-memory-step4-smoke: 20 measurements passed; all four
  baseline/candidate PNG comparisons are byte-identical.
- /private/tmp/docling-memory-step4-scaling: 120 measurements passed; all eight
  baseline/candidate PNG comparisons are byte-identical. Direct/single-Form
  PNGs also match at all four sizes.
- /private/tmp/docling-memory-step4-buffered-forms-reuse: 36 measurements passed.
- /private/tmp/docling-memory-step4-reuse: 36 measurements passed; all four
  baseline/candidate PNG comparisons are byte-identical; no timing flags.
- The saved CTest log reports stream_callback_regression passed.

#### Single-Form scaling

At the 1M target (999,108 painted lines):

| Mode | Step-3 peak MiB | Step-4a peak MiB | Memory change % | Process-time change % |
| --- | ---: | ---: | ---: | ---: |
| Decode | 2,402.66 | 1,148.91 | -52.18 | -10.23 |
| Shapes disabled | 2,169.59 | 108.73 | -94.99 | -10.28 |
| Render | 2,402.62 | 1,168.91 | -51.35 | -6.89 |

Direct-page memory at the same target is 1,148.73 MiB for decode, 108.52 MiB
with shapes disabled and 1,168.83 MiB for render. The extra memory of this
single-Form fixture is consequently about 0.08–0.21 MiB rather than the large
token-vector gap present before conversion.

All direct and Form decode/render/skip-shapes comparisons satisfy the +5%
allowance. The sole flag is raw Form-leaf collect at 1M, +6.10%; that measurement
bypasses do_form() and therefore does not identify a callback Form regression.
Do not attribute all observed speed differences to the patch: raw controls also
vary between batches.

All 72 decoded scaling measurements report zero Form materializations.
The 1M Form consumes 6,993,758 normalized source objects and emits 999,108 shape
instructions. Pending operands inside Forms remain bounded at two.

#### Repeated and nested Forms

At the 250k leaf target:

| Layout | Mode | Buffered peak MiB | Callback peak MiB | Memory change % | Process-time change % |
| --- | --- | ---: | ---: | ---: | ---: |
| Four Form invocations | Decode | 1,785.33 | 1,136.33 | -36.35 | -7.73 |
| Four Form invocations | Shapes disabled | 552.66 | 38.97 | -92.95 | -8.50 |
| Four Form invocations | Render | 1,788.84 | 1,142.70 | -36.12 | -4.29 |
| Three Form levels | Decode | 673.73 | 300.08 | -55.46 | -6.88 |
| Three Form levels | Shapes disabled | 552.34 | 35.50 | -93.57 | -17.09 |
| Three Form levels | Render | 677.70 | 308.45 | -54.49 | -10.27 |

All 12 reuse comparisons are faster, by 4.29–17.09%, with matching output
counters and PNGs. All 36 decoded callback measurements have zero materialized
Form tokens and a maximum of two pending Form operands.

The repeated layout has four Form executions at depth one and paints 998,608
lines at the 250k target. It consumes 6,990,264 source objects. The nested
layout has three Form executions at depth three, consumes 1,747,574 source
objects and paints 249,652 lines. Their output sizes differ by a factor of four;
do not compare their total RSS as though their emitted output were identical.

Doubling the repeated fixture's leaf target from 125k to 250k approximately
doubles retained-output memory (574.97 to 1,136.33 MiB for decode). This is
consistent with linear scaling for the measured cases. It does not establish a
universal bound for arbitrary PDFs, deeper nesting or other entry points.

#### Focused semantic validation

The user reported all 153 tests passing in 6.91 seconds:

| Test module | Passed |
| --- | ---: |
| test_unit_form_streaming.py | 4 |
| test_unit_split_content_streams.py | 4 |
| test_unit_damaged_content_streams.py | 6 |
| test_unit_actual_text.py | 8 |
| test_unit_image_decode.py | 42 |
| test_unit_clipping.py | 12 |
| test_unit_patterns.py | 35 |
| test_unit_widget_appearances.py | 3 |
| test_unit_type3_fonts.py | 10 |
| test_unit_soft_masks.py | 22 |
| test_unit_transparency.py | 7 |

This covers the four new Form cases and the focused coverage carried forward
from steps 2–3. Read-only inspection of the local compiled Python extension
found the new form_parse_interprete_total key, consistent with a step-4 build.
Codex did not rerun pytest or import/execute the extension during this review.

#### Joint checkpoint and remaining work

Retain page and ordinary Form callback delivery. The current patch meets its
native memory/runtime goals, its fixture counts/images match, and the focused
semantic gate passes. No further synthetic performance sweeps are required for
this Form checkpoint unless a new change or failure justifies them.

Step 4 remains open for separately reviewed appearance/widget, Type 3 and
soft-mask source conversions, plus the intentional tiling-pattern cache/copy
audit. A bounded next patch can address appearance/widget source delivery first.
Decide on each patch's scope before editing; retain useful pattern replay
caching rather than reparsing it on every paint.

Step 5 remains open for real-document, corpus and threaded/integration coverage
and eventual removal of the temporary buffered comparison paths. The missing-
reset audit also remains open for unexamined lifetimes. Do not infer a second
missing-clear defect from legitimate clipping history or remaining output cost.

## Questions to answer from the intermediate results

1. How much of the initial token-stage memory remains with a callback that
   releases every object?
2. How much extra memory is due to the Form's local vector and interpreter copy?
3. Does page callback delivery remove the predicted retention, and what remains?
4. Do damaged-document and split-stream outputs remain equivalent?
5. Which intentional caches improve repeated-use runtime enough to retain?
6. Is the remaining dominant cost the input bytes, page outputs, bitmap data,
   clipping state, cache retention, or concurrent pages?
7. After the avoidable token cost is removed, is a budget, weighted scheduling,
   output-representation optimization, or QPDF change the next justified step?
8. Are there additional missing-reset defects, or legitimate state histories whose repeated copying amplifies memory? Which can be reproduced and fixed independently?

Answer these together as evidence arrives. Do not preselect later optimizations
or promise memory limits that the measurements have not established.
