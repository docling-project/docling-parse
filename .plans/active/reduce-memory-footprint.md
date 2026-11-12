# Reduce the native parser's memory footprint

Date: 2026-11-11.
Status: pattern replay and the state/reset/shared-clip changes passed focused validation: 131 semantic tests passed, the native clip-history regression passed, and ordinary-page controls passed with the user accepting the small overlapping timing flag as noise.
Shared immutable clip history removes the measured quadratic retained geometry. The renderer cursor resolves the chain's render-only slowdown; at 1,000 clips, rendering is 33.85% faster than the shared-history implementation without the cursor. Broader corpus/threaded validation and final comparison-path retirement remain open.
The legacy Type3 cleanup regression passed in that suite; its controlled performance comparison remains pending.
The Type3-cleanup smoke completed 20 measurements but its comparison was rejected because nine dependency archives changed; no controlled cleanup result is established.
MyPy/Ruff pass; step 4b appearance/widget streaming validated by native experiments and 171 focused tests.
Step 4a Form streaming is validated by native experiments and 153 focused tests.
Step 2 native recheck and carried-forward semantic coverage passed.
Step 3 native smoke/scaling, Form timing recheck and focused semantic coverage passed.
Step 1 core scaling is reviewed. Full unit/corpus/threaded validation and final cleanup remain pending; the new focused acceptance checkpoint and next user commands are recorded below. Earlier bo20 observations do not validate the later patches.
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

The user reaffirmed "Keep builds and experiments with me" during the six-item
completion pass. The earlier delegated 174-test run does not change that rule.

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

The Type3 audit also found eager init_charprocs() description-vector retention.
Remove unnecessary storage/copies while preserving eager validation and damaged-
stream repair before glyph analysis; see the ordered continuation below.

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
- [x] Step 4b checkpoint: appearance/widget callback implementation, native smoke/scaling and 171 focused semantic tests passed.
- [x] Repair launcher typing; full MyPy (110 sources) and Ruff checks pass.
- [x] Step 4c procedural Type3 checkpoint: native smoke/scaling and 174 focused semantic tests passed.
- [x] Write legacy Type3 font-token cleanup and eager-repair bounds regression; user build/measurements/tests pending.
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

## Cycle 5 handoff — Step 4b: stream annotation appearances

Date: 2026-11-11.
Authorization: after the Form checkpoint passed, Codex proposed appearance/widget
streams as the next bounded patch and the user said "continue".
Status: appearance-source conversion and validation harness written; compilation,
semantic tests and measurements await the user.

### Production change and ordering

decode_ap_stream() formerly decoded its entire source into the STREAM decoder's
token vector, installed the appearance matrix, and interpreted the vector.
It now installs that same matrix first and calls decode_and_interprete().

The matrix must be installed before decoding because a QPDF callback can execute
the first drawing/text operator immediately. set_base_matrix() also establishes
the existing pattern-space anchor; the callback lifecycle preserves that state.

The existing shared decode_ap_stream() path covers widget and rendered markup
annotation appearances. The patch preserves:

- /AP/N stream selection and /AS lookup for appearance-state dictionaries.
- AP-local font lookup followed by AcroForm /DR and page-font fallback.
- Existing graphics-state, colorspace, shading, pattern and XObject resolution.
- The composed /Matrix and transformed /BBox-to-/Rect fit.
- Temporary appearance cells/shapes/images/render instructions and existing
  merge order, widget cell tagging and widget-frame/background filtering.
- Step-3 logical-stream finalization, damaged-source recovery and consumer/
  allocation exception ownership.
- The ordinary Form callback path when an appearance invokes a Form.

No user-facing configuration switch is added. The buffered appearance path is
retained only behind DOCLING_PARSE_BUFFERED_AP_CONTENTS in native comparison
targets. This is one buffered vector in the old appearance decoder; unlike the
old ordinary Form path, it does not add a second interpreter vector copy.

The existing appearance resource/merge behavior is kept; this conversion does
not expand supported annotation rendering or fix unrelated resource/merge gaps.
Type 3 and soft-mask source entry points and cached pattern replay remain later
step-4 work.

### Ownership, output memory and timing

decode_ap_stream() already receives its QPDF handle by value. The owning document,
handle, local resources, interpreter and operand vector remain alive throughout
synchronous callback decoding. The child output objects retain their values as
before; no temporary source-token reference is cached.

Appearance execution still builds temporary output containers and copies selected
results into the page. Their lifetimes can overlap, and render instructions and
page elements still consume memory. Removing the token list does not imply that
appearance memory will equal direct-page memory or become independent of output.

The existing decode_annots timing includes source decoding, execution and merging.
No additional per-token clocks or artificial split between parsing and execution
are introduced. Native process wall time and peak RSS remain the acceptance
measurements, with a +5% slowdown allowance.

### Native comparison targets

| Executable | Page source | Ordinary Form source | Appearance source |
| --- | --- | --- | --- |
| test_memory_on_lines | Callback | Callback | Callback |
| test_memory_on_lines_buffered_appearances | Callback | Callback | Buffered |
| test_memory_on_lines_buffered_forms | Callback | Buffered | Callback |
| test_memory_on_lines_buffered | Buffered | Buffered | Buffered |

All probe targets enable the same diagnostic definition. Normal production
builds use callback appearances without probe aggregation.

The original step-4a executable is preserved at:

- /private/tmp/docling-memory-before-step4b
- SHA-256: 20193d8cace138734c51c32efe8488104f97131e8bf955717d845e17f76f6d72

The hash matches the saved step-4 scaling metadata. It is useful for old-layout
regression rechecks. That old executable cannot generate/select the new
appearance fixtures, so the new same-source buffered-appearance target is the
primary reference for this cycle. A snapshot of today's source while running
an old executable must not be mistaken for that executable's compiled source.

### Probe diagnostics and scope

PAGE exposes scalar appearance statistics through a native-only getter.
Aggregation is compiled only into diagnostic probes, after each appearance
source returns. Counters reset when annotation decoding begins.

The probe reports:

- appearance_content_delivery and appearance_diagnostics.
- appearance_executions and appearance_widget_executions.
- appearance_source_objects and appearance_source_bytes_observed.
- appearance_materialized_tokens: cumulative token records stored by all
  top-level appearance decoders, not simultaneous live tokens or allocated bytes.
- appearance_max_buffered_token_capacity: maximum final source-vector capacity
  across those decoders; this is not a peak measure of every parser allocation.
- appearance_max_pending_operands.

These counters cover selected top-level annotation appearance sources. They do
not roll Forms invoked inside an AP into its own source-object count. The
existing page_* fields still cover page /Contents; form_* fields cover the
ordinary page/Form invocation tree, excluding separate AP interpreter trees.

For valid generated AP grids with L unique lines and R selected appearances:

- Page /Contents is an empty stream: zero page source objects/bytes/operands.
- AP executions = R; widget executions = R only for the widget layout.
- AP source objects = R * (7 * L + 2).
- AP observed source bytes = R * leaf content_bytes.
- Callback AP materialization and final buffered capacity = zero.
- Buffered AP materialization = the AP source-object count, with enough vector
  capacity for one leaf's token list.
- Pending AP operands peak at two; page-tree Form execution counters are zero.
- keep_shapes=false yields no emitted shape output but still consumes the AP.

The byte-count equality is for valid generated fixtures. Recovery can report
source bytes across more than one attempt; do not treat it as unique bytes for
arbitrary damaged PDFs.

### New native fixtures and launcher behavior

The C++ generator adds:

- pathstorm-appearance N PDF [appearances=1]: a /Stamp AP with the grid.
- pathstorm-widget N PDF [appearances=1]: a /Btn widget AP with the grid.

The AP /BBox and annotation /Rect match the direct grid page, and its matrix is
identity. The page source is empty. Repeated annotations share one AP stream
but each selected appearance executes; generation stays in a separate process.

The launcher offers these opt-in layouts:

| Layout | Selected APs | Role |
| --- | ---: | --- |
| appearance | 1 | Markup annotation |
| widget | 1 | Widget selection and frame-filter path |
| appearance-repeat | 4 by default | Shared AP, repeatedly interpreted |

--appearance-repeats controls the last layout. Existing default layouts remain
direct/form/form-repeat/form-nested, so earlier commands retain their scope.

Raw qpdf-discard/collect modes use the generated marker to select the first AP
source once. They do not execute annotations or multiply token counts by AP
invocations. All raw layouts should expose the same grid source signature.

The launcher validates the new counters on every decoded run, stores them in
summary.csv and keeps the tabulate memory/runtime tables. Metadata records the
appearance repeat setting and snapshots the new regression source too. Older
saved direct/Form baselines remain readable.

The image checker compares direct versus single appearance/widget output when
those layouts are in one matrix, as well as baseline/candidate images. Repeated
appearance pixels are compared to the corresponding repeated baseline.

### New focused semantic tests

tests/test_unit_appearance_streaming.py adds six cases:

1. A /Stamp AP with a rotated, nonzero BBox and an inherited page Form: the
   fitted matrix must be active from the first callback, and the nested Form
   must see the AP's local half-opacity resource.
2. The corresponding /Widget case exercises the same transform and local-state
   requirements through widget handling.
3–5. Two widgets select the same /Yes appearance at distinct rectangles; only
   the selected content is emitted twice. The font is supplied respectively by
   AP /Resources, AcroForm /DR, or page resources.
6. A recoverable Flate prefix with an unclosed ActualText scope keeps literal
   text, then a following annotation still emits its own text exactly once.

These supplement existing widget frame/background/mark tests and AcroForm
metadata tests. They are written and syntax-checked, not executed by Codex.

### First user commands: configure and build

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines \
  test_memory_on_lines_buffered_appearances \
  stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'
~~~

The generic callback regression is still useful but does not replace the new
AP fixtures and semantic tests.

### First user experiment: small appearance matrix

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_appearances \
  --output /private/tmp/docling-memory-step4b-buffered-appearances-smoke \
  --layouts direct,form,appearance,widget,appearance-repeat \
  --sizes 1000 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4b-smoke \
  --baseline /private/tmp/docling-memory-step4b-buffered-appearances-smoke \
  --layouts direct,form,appearance,widget,appearance-repeat \
  --sizes 1000 --repeats 1
~~~

Each run has 25 measurements. Require matched counters and all five baseline
PNGs, equivalent direct/single-Form/single-AP/widget images, correct AP selection
counts, zero callback AP vector materialization/capacity, and bounded operands.

One repetition at 1,000 lines is a structural/correctness gate. Return compile,
counter or image failures first; do not interpret these short process times as
a runtime regression or established speedup.

### User semantic validation

Rebuild the extension, then run the new cases and the established focused suite,
adding AcroForm metadata coverage:

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_appearance_streaming.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_acroform_widgets.py \
  tests/test_unit_form_streaming.py \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py -q
~~~

Do not count the previous 153 passing tests as validation of this newly compiled
appearance path. Return new failures without changing ground truth or weakening
assertions.

### Scaling after the small gate

Use a fresh buffered-appearance reference with the same current source, since
the earlier result matrices do not contain the new AP layouts:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_appearances \
  --output /private/tmp/docling-memory-step4b-buffered-appearances-scaling \
  --sizes 125000,250000 --layouts direct,appearance,widget \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4b-scaling \
  --baseline /private/tmp/docling-memory-step4b-buffered-appearances-scaling \
  --sizes 125000,250000 --layouts direct,appearance,widget \
  --modes decode,skip-shapes,render --repeats 3 --slowdown-limit-pct 5
~~~

Each scaling matrix has 54 measurements. Start at these sizes because AP output
temporaries and page-output copies may make its memory larger than a direct or
ordinary Form case even after source token retention disappears.

Hypotheses to measure, not claimed results:

- Callback APs materialize no complete source-token vector.
- Shapes-disabled APs show a clear memory reduction.
- Full output peak memory falls, with remaining temporary/page copies explained.
- Direct controls remain similar and interpretation/render time stays within +5%.
- Counters, output order and PNGs remain equivalent.

After reviewing those results, a bounded shared-AP reuse check can use
--sizes 125000 --layouts appearance-repeat --modes decode,skip-shapes,render
--repeats 3 in fresh baseline/candidate directories. Four APs then paint roughly
500k lines. Agree on larger/reuse runs from the intermediate evidence.

### Pending decision and remaining entry points

| Item | Status |
| --- | --- |
| Page/Form checkpoint from earlier cycles | Native measurements and 153 focused tests passed |
| Appearance/widget source implementation | Written for this cycle |
| New native/AP smoke and semantic checks | Passed; six new AP cases and 171 focused tests total |
| AP memory/runtime scaling | Passed two 54-measurement matrices; matching PNGs and no timing flags |
| Accept/revise AP patch | Retain; native and focused semantic gates passed |
| Type 3 and soft-mask source conversion | Not started |
| Tiling-pattern cache/copy audit | Not started |
| Broad corpus/threaded validation and fallback retirement | Step 5, not started |

Codex performed source review, Python AST parsing and whitespace checks, and
preserved/hashed the old native executable. Codex did not compile, run CTest or
pytest, generate PDFs or collect new performance measurements in this cycle.

### User results — Step 4b native and focused semantic checkpoint

Date: 2026-11-11.
The user reported both AP smoke matrices, both scaling matrices and the rebuilt
extension's focused suite: 171 tests passed in 6.86 seconds. This completes the
appearance/widget implementation checkpoint; retain the callback path.

#### Native evidence

Saved artifacts were inspected under:

- /private/tmp/docling-memory-step4b-buffered-appearances-smoke
- /private/tmp/docling-memory-step4b-smoke
- /private/tmp/docling-memory-step4b-buffered-appearances-scaling
- /private/tmp/docling-memory-step4b-scaling

Both smoke matrices passed 25 measurements. All five baseline PNG pairs matched
byte-for-byte, including four selected appearances sharing one source. Direct,
single-Form, single-appearance and widget output PNGs also matched each other.
The saved native callback/recovery regression passed.

Both scaling matrices passed 54 measurements. All six baseline PNG pairs matched
byte-for-byte, and direct/single-appearance/widget PNGs matched at both sizes.
All 18 scaling comparisons satisfied the +5% runtime allowance; no timing flags
were reported.

At the 250k target (249,652 painted lines):

| Layout | Mode | Buffered peak MiB | Callback peak MiB | Memory change % | Process-time change % |
| --- | --- | ---: | ---: | ---: | ---: |
| Appearance | Decode | 730.44 | 437.38 | -40.12 | -4.09 |
| Appearance | Shapes disabled | 445.38 | 35.36 | -92.06 | -5.40 |
| Appearance | Render | 730.52 | 438.12 | -40.03 | -1.83 |
| Widget | Decode | 730.61 | 437.62 | -40.10 | -4.68 |
| Widget | Shapes disabled | 445.48 | 35.44 | -92.05 | -7.70 |
| Widget | Render | 730.48 | 437.59 | -40.10 | -3.27 |

Across both sizes, appearance/widget full-output peak memory fell approximately
38–40%, shapes-disabled memory fell approximately 90–92%, and those process
timings improved by 1.83–8.04%. The single-repetition smoke times remain
diagnostic rather than runtime evidence.

All 36 decoded appearance/widget scaling measurements had zero materialized
appearance tokens, zero final source-vector capacity and a pending operand
maximum of two. At 250k, each source consumed 1,747,566 normalized objects and
emitted 249,652 shape instructions.

Direct-page decode memory at 250k was 298.66 MiB, below the AP's 437.38 MiB.
With shapes disabled the corresponding results were 35.25 and 35.36 MiB.
This is consistent with remaining temporary appearance output containers and
page-output copies contributing to the full-output gap. No allocation profile
was collected to assign that gap exactly to individual representations.

A large shared-AP reuse sweep was not run. The smoke fixture and shared selected
appearance semantic cases cover repeated execution for this checkpoint. Further
single-AP scaling is unnecessary unless a new change or failure justifies it.

#### Focused semantic evidence

The user reported 171 tests passing in 6.86 seconds:

| Test module | Passed |
| --- | ---: |
| test_unit_appearance_streaming.py | 6 |
| test_unit_widget_appearances.py | 3 |
| test_unit_acroform_widgets.py | 12 |
| test_unit_form_streaming.py | 4 |
| test_unit_split_content_streams.py | 4 |
| test_unit_damaged_content_streams.py | 6 |
| test_unit_actual_text.py | 8 |
| test_unit_image_decode.py | 42 |
| test_unit_clipping.py | 12 |
| test_unit_patterns.py | 35 |
| test_unit_type3_fonts.py | 10 |
| test_unit_soft_masks.py | 22 |
| test_unit_transparency.py | 7 |

This includes the six new AP cases and the previous 153-test coverage, plus
12 existing AcroForm tests. It covers transformed appearance execution, AP-local
state during nested Form execution, all three font fallback scopes, shared
selected appearances, damaged-prefix recovery and isolation from the following
annotation. Widget chrome filtering and AcroForm metadata coverage also pass.

Codex reviewed the user-provided pytest report and saved native artifacts.
Codex did not compile, rerun pytest or execute new measurements in this review.

#### Decision and next bounded patch

Retain streamed page, ordinary Form and annotation appearance sources.
Their respective native checkpoints and the current 171-test focused suite
pass. Overall step 4 remains open.

The next proposed bounded conversion is Type 3 CharProc source delivery, with
glyph transforms, font resources, procedure draining and recursion limits
preserved. Soft-mask group sources follow separately. Pattern cache/copy
handling requires a separate audit that preserves efficient replay.

Real-document/corpus/threaded validation, the remaining state-lifetime audit and
retirement of the native buffered comparison paths remain outstanding. Do not
treat the focused suite as completion of step 5.

## Cycle 6 handoff — MyPy repair and step 4c Type 3 CharProc streaming

Date: 2026-11-11.
Authorization: the user asked to correct the reported MyPy failures and then
implement Type 3 CharProc streaming.
Status: MyPy/Ruff checks pass; procedural Type 3 source conversion, probe and
regressions are written. User compilation and runtime validation are pending.

### MyPy repair and completed static checks

The launcher had eight reported errors from three causes:

1. Timeout stdout/stderr variables were inferred as bytes and then assigned
   decoded strings, making string concatenation invalid to MyPy.
2. A raw-source string expectation and a later numeric shape-count expectation
   reused the same variable name.
3. Empty fallback dictionaries and the raw-signature cache needed annotations.

Timeout output now normalizes directly into string variables, handling bytes,
text and missing output. Raw source selection uses expected_stream. Summary
dictionaries and measurement/image/signature collections have explicit types.
The signature cache also distinguishes ordinary grids from the metric-prefixed
Type 3 source family.

No type ignores were added and no MyPy settings were weakened. Existing
formatter changes from the user's pre-commit run were preserved.

After adding the Type 3 code and new typed regression module, Codex ran the
project's installed tools directly from .venv/bin:

| Check | Result |
| --- | --- |
| mypy docling_parse tests scripts | Success; 110 source files |
| ruff check docling_parse tests scripts | All checks passed |
| ruff format --check docling_parse tests scripts | 115 files already formatted |
| git diff --check | Passed |

Existing annotation-unchecked notes about other untyped functions remain
informational. The complete pre-commit wrapper/uv-lock hook was not executed;
the Ruff/MyPy checks above use the installed project tools and normal config.
No build, CTest, pytest or benchmark was executed by Codex.

### Production source conversion

do_type3_glyph() previously decoded the CharProc into a local rich instruction
vector and copied that vector into a child interpreter. The normal path now
holds a QPDF handle and calls new_stream.decode_and_interprete(charproc, parameters)
after establishing the same glyph resources and graphics state.

Preserved behavior:

- paint_type3_glyphs() swaps the pending queue into a local vector before glyph
  execution. Each queued procedure is consumed once; inherited states do not
  carry pending work back into themselves.
- CharProc lookup uses the font's resources with existing parent fallback.
- Glyph placement combines FontMatrix, font size/rise, text placement and the
  inherited page CTM in the existing order.
- The child stack is rebound to glyph resources before callbacks execute.
- Glyph cells, shapes and images stay in private temporary containers; only
  render instructions reach the page, alongside the already-created text cell.
- Bitmap glyph classification, unknown-operator handling and the thread-local
  recursion limit of eight remain in place.
- The recursion counter is decremented on normal and exceptional source exits.
- The parent q()/Q() boundary is now also restored if child setup or execution
  throws, so operator-level recovery cannot continue with the glyph transform.
- The old buffered source reference explicitly propagates std::bad_alloc rather
  than warning/returning as though a glyph were merely unreadable.

Cached image-mask/vector glyph rendering and font extraction are unchanged.
Soft-mask source conversion and pattern cache/copy work remain separate patches.

### Important independent Type 3 memory stage

get_type3_glyph() first examines the decoded CharProc to decide whether it can
cache a rasterized glyph. Its lightweight vector analyzer currently builds a
temporary vector of strings and scans the source even when it eventually
chooses procedural fallback.

That first-use tokenization, source-buffer copies and any useful cached masks
remain part of process-lifetime peak RSS. The new interpreter diagnostics do
not measure that analyzer's temporary tokens.

Therefore a streamed CharProc can report zero rich instruction materialization
while peak RSS still scales with source size, particularly with shapes disabled.
Do not promise a QPDF-discard-level memory floor or describe this patch as
removing every Type 3 token list. Use returned measurements to decide whether
the independent analyzer deserves a later bounded optimization.

### Comparison targets and old binary

The new test_memory_on_lines_buffered_type3 target streams page, Form and
appearance sources but keeps the old two-vector procedural CharProc path.
test_memory_on_lines streams all four converted source paths. The fully
buffered native target now defines the Type 3 comparison macro as well.

DOCLING_PARSE_BUFFERED_TYPE3_CONTENTS is restricted to standalone native
comparison targets. There is no new public runtime/Python switch.

The exact earlier executable is preserved at:

- /private/tmp/docling-memory-before-step4c
- SHA-256: 19bc36ff06cb57a931c8a5730bdc1ee8744fe5c05560b2d6efa4d5725ec84203

Its hash matches the step-4b scaling metadata. It can recheck historical
direct/Form/AP fixtures, but cannot generate or select the new Type 3 layouts.
Use the new buffered-Type3 reference for the new source family.

### Native Type 3 fixtures

The C++ generator adds pathstorm-type3 N PDF [glyphs=1]. It embeds a Type 3
font with identity FontMatrix and displays it at font size one with a fresh
identity text matrix for each glyph invocation, mapping grid ink into the same
page coordinates as the direct fixture.

The CharProc contains a 595 0 d0 metrics prefix followed by the normal stroked
grid. Strokes are unsupported by the lightweight cached-mask rasterizer, so the
probe requires actual procedural executions. This detects accidentally
benchmarking a cached glyph mask instead of the converted interpreter.

New opt-in launcher layouts:

- type3: one procedural glyph.
- type3-repeat: four invocations by default, controlled by --type3-repeats.

Repeated invocations reset their text placement and paint the same grid again.
They produce repeated render geometry, while glyph outlines remain excluded
from the public page-shape collection. This is intentional Type 3 semantics,
not silently dropped output.

Raw qpdf-discard/collect modes select the marked CharProc once and do not execute
Tj or multiply source counts by glyph invocations. Their source has three extra
objects and one extra operator for d0, so its raw signature is grouped separately
from ordinary grids. Default layouts remain the existing four page/Form layouts.

### Diagnostics and exact expectations

The probes report type3_content_delivery, type3_diagnostics, type3_executions,
type3_source_objects, type3_materialized_tokens, type3_max_pending_operands,
type3_max_depth and type3_recursion_skips, plus text_instructions.

Counters aggregate procedural fallback execution through the page/Form/Type3
interpreter tree, including CharProcs invoked through Forms. They do not include
cached mask draws or separate AP/soft-mask interpreter trees. Aggregation is
compiled only for diagnostic native probes.

For L unique grid lines and R procedural glyph invocations:

- One CharProc source contains 7 * L + 5 objects and 3 * L + 2 operators.
- Executed CharProc objects total R * (7 * L + 5).
- Type 3 executions = R, maximum procedural depth = 1, recursion skips = 0.
- CharProc pending operands peak at two.
- The page source has 5 + 9 * R objects, known generated page_content_bytes,
  and an operand maximum of six due to Tm.
- Normal decode/render emits R * L shape instructions/subpaths/segments and
  twice that many points, with R Type 3 text instructions.
- Public page_shapes remains zero: the path marks are glyph ink.
- Shapes-disabled runs emit zero shape instructions but still execute CharProcs.
- Streamed interpreter materialization = zero.
- Buffered interpreter materialization = twice the executed CharProc objects,
  counting both legacy vectors cumulatively, not peak live bytes.

These do not count the independent first-use glyph analyzer's token strings.
The launcher validates counters, preserves matching-input/output checks, writes
the fields to summary.csv and retains tabulate memory/runtime summaries.

### New focused semantic coverage

tests/test_unit_type3_streaming.py adds three cases:

1. Two glyphs queued by one Tj share the font-local half-opacity graphics
   resource. Pixel checks verify each stroke paints once, glyph transforms stay
   local and the parent page retains its own blue/quarter-opacity stroke.
2. The same behavior with a recoverable damaged Flate CharProc checks prefix
   recovery and the second queued execution of the shared, repaired source.
3. A self-recursive glyph paints eight quarter-opacity strokes, then a plain
   glyph and parent suffix still paint. Pixel opacity tests the depth bound;
   later marks check that the recursion counter and graphics state were restored.

Strokes and recursive text force these cases through the procedural fallback.
Existing Type 3 tests still cover cached vector/image-mask paths, image-XObject
resources, orientation and artifact classification.

These cases are formatted and type-checked but have not been run by Codex.

### First user commands: configure/build and generic native recovery

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines \
  test_memory_on_lines_buffered_type3 \
  stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'
~~~

The generic callback regression does not replace Type 3 fixture/semantic checks.

### First user experiment: small Type 3 matrix

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_type3 \
  --output /private/tmp/docling-memory-step4c-buffered-type3-smoke \
  --layouts direct,form,type3,type3-repeat --sizes 1000 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4c-smoke \
  --baseline /private/tmp/docling-memory-step4c-buffered-type3-smoke \
  --layouts direct,form,type3,type3-repeat --sizes 1000 --repeats 1
~~~

Each matrix has 20 measurements. Require baseline/candidate PNG equality for
all four layouts, equivalent direct/Form/single-Type3 pixels, correct glyph
execution and ink counts, zero streamed rich-token materialization and bounded
operands. A small single repetition is a structural gate, not runtime evidence.

Return compilation, counter, image or runtime errors first. Do not relax the
assertions or change ground truth to make a mismatch pass.

### User semantic validation

Rebuild the extension, then run the three new cases plus the established focused
suite:

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_type3_streaming.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_appearance_streaming.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_acroform_widgets.py \
  tests/test_unit_form_streaming.py \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py -q
~~~

The 171 earlier passing tests describe the previous compiled extension; they do
not count as executing this Type 3 change. Report the new result explicitly.

### Scaling after the small gate

Start with a controlled same-source comparison at two sizes:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_type3 \
  --output /private/tmp/docling-memory-step4c-buffered-type3-scaling \
  --sizes 125000,250000 --layouts direct,type3 \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-step4c-scaling \
  --baseline /private/tmp/docling-memory-step4c-buffered-type3-scaling \
  --sizes 125000,250000 --layouts direct,type3 \
  --modes decode,skip-shapes,render --repeats 3 --slowdown-limit-pct 5
~~~

Each scaling matrix has 36 measurements. Measure the combined first-use glyph
analysis, procedural interpretation and retained output; do not infer the whole
process peak from the interpreter counters alone.

Expected hypotheses are lower peak RSS, equivalent output and process time
within +5%. A shared analysis/output cost may limit observed gains. If that cost
dominates, decide jointly whether to profile/optimize it later. Repeated-glyph
scaling is available with --layouts type3-repeat at a bounded target after the
first scaling result; it is not automatically launched.

### Pending decision

| Item | Status |
| --- | --- |
| Reported MyPy errors | Fixed; full 110-file MyPy check passes |
| Ruff lint/format | Pass |
| Procedural Type 3 conversion and three regressions | Written |
| User native build/smoke and rebuilt-extension tests | Native smoke/scaling passed; 174 focused tests passed in 5.93s at the user's request |
| Type 3 memory/runtime scaling | Two 36-measurement matrices passed; lower peak memory and all times within +5% |
| Cached glyph analyzer/mask pipeline | Preserved; separate first-use memory cost documented |
| Accept/revise Type 3 patch | Joint review after user results |
| Soft-mask source conversion and pattern cache/copy audit | Not started |
| Broad corpus/threaded validation and comparison retirement | Step 5, not started |

Codex ran only source/static checks and copied/hashed the historical executable.
No native build, PDF generation, benchmark, CTest or pytest was run by Codex.

## Type 3 native results and remaining font-initialization retention

Date: 2026-11-11.
The user ran both step-4c smoke matrices and both scaling matrices.

- Both smoke runs passed 20 measurements, all four baseline PNG comparisons
  matched byte-for-byte, and direct/Form/single-Type3 PNGs matched.
- Both scaling runs passed 36 measurements and all four baseline PNG comparisons
  matched. Direct and Type3 PNGs matched at both sizes.
- All 12 scaling comparisons meet the +5% allowance. The six Type3 comparisons
  improve process time by 0.58–3.50%.
- All 18 decoded Type3 scaling measurements have zero interpreter token
  materialization, a maximum of two CharProc operands and no recursion skips.
- The native callback/recovery regression also passed.

At the 250k leaf target:

| Type3 mode | Buffered peak MiB | Callback peak MiB | Memory change % | Process-time change % |
| --- | ---: | ---: | ---: | ---: |
| Decode | 980.14 | 791.47 | -19.25 | -0.58 |
| Shapes disabled | 860.12 | 676.72 | -21.32 | -1.12 |
| Render | 978.14 | 790.47 | -19.19 | -2.84 |

The procedural interpreter's native goal is met, but focused rebuilt-extension
test results for this patch have not yet been supplied. The bo20 benchmark
refresh discussed below runs damaged-page tests, not the new Type3 semantic
suite, so it does not close that gate.

### Additional structural finding in font initialization

The remaining shapes-disabled footprint prompted inspection beyond the
lightweight first-use glyph analyzer:

1. PAGE_FONT::set() calls init_charprocs().
2. init_charprocs() eagerly decodes every CharProc into a complete vector of
   qpdf_stream_instruction records.
3. char_processor::parse(stream) copies that vector into char_description.
   It is called twice: the first return value is discarded; the second is
   assigned into name_to_descr.
4. The only reader of name_to_descr found under src/, app/ and tests/ logs its
   size. The retained instructions have no functional reader in this repository.

At 250k the CharProc has 1,747,569 objects. With 64-byte instruction records,
one size-only copied token array is about 106.66 MiB before referenced objects,
strings and excess capacity. Additional eager vector/copy/cache work is thus a
strong remaining memory candidate. Exact attribution of the 676.72 MiB peak
still needs phase measurements or a controlled follow-up comparison.

These vectors are outside the procedural interpreter counters. Zero
type3_materialized_tokens does not mean the complete font pipeline has no
retained token list.

The eager decoder also has observable side effects: damaged CharProcs may be
repaired in place before first-use glyph analysis, and invalid entries are
checked during font initialization. Do not simply delete the eager call based
on the unused map alone.

The next bounded Type3 cleanup should preserve eager validation/recovery through
the existing normalized discard-consumer path while removing the description
vector copies/cache and its size-only debug dependency. Keep the independent
cached glyph-mask analyzer separate. Preserve a pre-cleanup executable and
compare the complete process footprint rather than only the callback counter.
This belongs to step 4's existing unnecessary-retention audit.

## Ordered continuation — finish parser plan, then benchmark memory reporting

Date: 2026-11-11.
The user requested this order:

1. Finish the steps in this parser-memory plan.
2. Update scripts/benchmarking/refresh_performance_benchmarks.sh so the benchmark
   report shows memory evolution, especially peak RSS, with optional time-based
   memory profiles.

The second item is an explicitly requested follow-up. It does not authorize
skipping the parser's remaining semantic/corpus gates. The user still compiles
and runs experiments; Codex implements and analyzes.

### Remaining parser sequence

1. Obtain the focused Type3 semantic result for the rebuilt extension.
2. Remove unnecessary eager Type3 description retention while preserving its
   validation/recovery behavior; measure and validate that bounded patch.
3. Convert soft-mask group source delivery, preserving group state, mask
   semantics, recursion/budgets and useful decoded-mask reuse.
4. Audit tiling-pattern token/cache copies and change only the representation
   needed to remove redundant copies while preserving efficient replay.
5. Finish the missing-reset/lifetime audit and record confirmed/non-defect
   findings; do not clear legitimate clipping state to reduce one snapshot.
6. Complete the existing step-5 self-contained tests, real-document output
   comparisons, broader corpus and actual threaded-path validation.
7. Retire temporary production comparison selectors/dead source paths after
   acceptance, keeping useful native diagnostics and preserved measurement
   artifacts. Supply final user-run checks for the cleaned-up result.

The existing step-5 threaded-memory evidence can use the Python replay peaks
already captured and bounded manual/native OS measurements. The permanent
launcher/reporting extension follows parser acceptance; do not create a circular
dependency that requires that extension before the parser plan can be finished.

### New bo20 bundle: what it establishes now

User command:

~~~sh
REPEATS=1 THREADS=1,4,8,12 RUN_COMPARISON=0 DATASET=bo20 \
  ./scripts/benchmarking/refresh_performance_benchmarks.sh
~~~

Bundle: scratch-performance-refresh-20261111-161912-r7M86J.
Report: report/summary.md.
Dataset: performance-dataset-bo20 at
65b9971b3fa939fe4ef89be678237e6928049b67, 20 PDFs / 496 pages.
Report source revision: 04e31ded07866d6741efb99a059056f94ee8759b.
The bundle also records working-tree and binary provenance; a revision label
alone does not identify uncommitted source compiled into the binaries.

This provides real-workload scaling observations for native render-only and
render-with-text profiles and the Python backend-operation replay. It is one
observation per case, not a variability estimate or a before/after comparison.
The refresh's pre-test is test_unit_damaged_pages.py. Runtime/count validation
does not substitute for pixel/content regression comparisons or the new Type3
focused tests.

### Peak RSS is already available for the Python replay

run_performance_benchmarking_with_docling_settings.py already calls
resource.getrusage(RUSAGE_SELF).ru_maxrss and records peak_rss_mib plus a
process-lifetime scope in each runs[] entry.

The launcher runs each backend/thread profile in a fresh process, making the
following existing values usable for those individual runs:

| Backend replay | Threads | Existing peak RSS MiB | Raw file under the bundle |
| --- | ---: | ---: | --- |
| pypdfium2 | 1 | 623.08 | replay/r1/pdfium/report.json |
| docling-parse | 1 | 710.33 | replay/r1/t1/report.json |
| docling-parse | 4 | 861.80 | replay/r1/t4/report.json |
| docling-parse | 8 | 1,023.56 | replay/r1/t8/report.json |
| docling-parse | 12 | 1,176.50 | replay/r1/t12/report.json |

These are whole-process peaks, including Python/native libraries, startup,
loading, processing and retained output. They are not parser-only heap sizes
or sums of per-page peaks.

run_performance_report.py currently omits these fields from its summary/runs
view. app/run_scaling.cpp does not currently emit a corresponding peak RSS.
There are no saved sampled RSS timelines to recover retroactively.

### Benchmark follow-up A: default per-run peak RSS

Implement only after the parser plan's acceptance gate:

- Carry existing replay peak_rss_mib through report ingestion, runs.csv,
  summary tables and machine-readable output.
- Add native per-process OS peak capture, reusing the native probe's verified
  platform/unit handling where appropriate.
- Capture every repetition/backend/profile/thread combination separately.
  Do not wrap the entire launcher and report its peak: build, downloads and
  reporting processes are outside the requested benchmark measurement.
- Track the actual benchmark process, including all its threads. Do not report
  the RSS of uv, the logger or tee as the backend's memory.
- Normalize native OS values to canonical bytes at collection boundaries and
  display MiB. Darwin ru_maxrss is bytes; the Linux value is KiB, matching the
  existing replay normalization.
- Record method, units, process identity, scope, mode and configuration with
  the existing source/binary/dataset provenance.
- Label whole-process lifetime peaks explicitly. Native wall time excludes
  document preload, while a lifetime peak includes it; do not imply matching
  timing/memory boundaries or subtract two high-water values as phase memory.
- Preserve current workload/profile separation and recorded result-queue,
  output-consumption and native-memory-release policies.
- For multiple repetitions show the median and observed min/max of per-run
  peaks, plus the largest observed run peak for capacity planning.
- Add peak-RSS-versus-thread-count figures alongside throughput/scaling figures.
- Keep old bundles readable: missing native peak data displays as unavailable,
  never zero or an estimate. Existing Python replay peaks can be surfaced in
  a newly generated report without rerunning those measurements.
- Avoid cumulative RUSAGE_CHILDREN or shared-process high-water reuse across
  runs. Optional cross-package comparison currently runs multiple series in a
  process; isolate them before claiming per-series peaks, or label memory
  unavailable/shared-process there. The initial target is the native/replay
  profiles used by RUN_COMPARISON=0.

Peak capture should be enabled by default with low overhead; verify that
overhead instead of promising it is free.

### Benchmark follow-up B: optional sampled RSS timeline

Proposed interface, preserving environment-based invocation and adding explicit
CLI equivalents:

~~~sh
# Peak RSS in every normal report.
MEMORY_MODE=peak REPEATS=3 THREADS=1,4,8,12 DATASET=bo20 \
  ./scripts/benchmarking/refresh_performance_benchmarks.sh

# Optional timeline; also retains the OS peak.
REPEATS=3 THREADS=1,4,8,12 DATASET=bo20 \
  ./scripts/benchmarking/refresh_performance_benchmarks.sh \
  --memory-mode profile --memory-interval-ms 50
~~~

MEMORY_MODE values: off, peak (default), profile.
MEMORY_INTERVAL_MS sets a positive sampling interval, proposed default 50 ms.
CLI values override environment values. These options are proposed and not yet
implemented; existing launcher commands still reject extra arguments.

Requirements:

- Sample current RSS against monotonic elapsed time for each actual benchmark
  process. An external sampler should continue observing while native work or
  the Python GIL is busy.
- Write raw timestamp/RSS/PID samples and a per-run summary beside its existing
  measurement artifacts; include interval/method/identity metadata.
- Keep OS peak RSS and sampled maximum as distinct fields. Sampling may miss
  brief spikes and cannot replace the OS high-water mark.
- Record lifecycle/measurement boundaries when available, so plots distinguish
  initialization/preload, measured processing and teardown.
- Produce RSS-versus-elapsed-time plots by backend, profile, threads and
  repetition without confusing independent clocks or averaging unrelated runs.
- Record sampling as instrumentation. Check its effect on runtime and avoid
  mixing sampled and unsampled runs in a performance claim.
- Bound the monitor's own memory with streamed sample output. Ensure cleanup
  and useful artifacts on child failure, timeout or interruption.
- Preserve completed-series checkpoints and recovery provenance. A resumed
  series must not silently claim a missing timeline or inherit another
  process's high-water value.

### Benchmark follow-up implementation and validation order

1. Add a small collection/schema layer and meaningful unit coverage for unit
   conversion, process isolation, missing/legacy data and invalid options.
2. Surface existing replay peaks and add native per-run peaks; integrate the
   normal launcher, report tables/CSV and memory-scaling plots.
3. Add optional sampling and timeline figures after default peaks are correct.
4. Validate recovery/backward compatibility and scoped failure artifacts.
5. User runs a bounded bo20 smoke at one/four threads, then paired off/peak/
   profile runs to assess capture overhead and peak agreement.
6. User runs the requested one/four/eight/twelve-thread sweep with repeated
   observations, preserving dataset/source/settings for comparison.

Codex has not modified the benchmark launcher/report implementation or rerun the
benchmark in this review. The immediate existing peak-RSS table above was read
from saved JSON reports.

## Completion pass — item 1: discard eager Type 3 validation tokens

Date: 2026-11-11.
Authorization: the user said "ok, finish step 1 to 6", referring to the six
remaining parser items, then explicitly confirmed:
"Keep builds and experiments with me."

All six items are authorized for continuation. Do not ask for source-change
approval again for those listed items. Keep the established measurement and
acceptance gates: Codex writes/reviews code and runs static checks; the user
compiles and runs experiments. Retire comparison paths after broad validation,
not merely because their replacement code has been written.

### Prior Type 3 semantic gate is complete

At the user's explicit request, Codex ran the full focused suite against the
then-current extension: 174 tests passed in 5.93 seconds. That includes the
three procedural Type 3 cases and closes the semantic gate for step 4c.
This one delegated test run does not authorize subsequent native builds or
experiments; the user has reaffirmed the original division of work.

### Item 1 source change

PAGE_FONT now calls validate_charprocs() at the same point in font setup.
It still visits every CharProc, checks that it is a stream, and uses the same
qpdf_stream_decoder normalization and damaged-stream recovery, but delivers
objects to a no-op synchronous consumer.

The unused name_to_descr map, discarded char_processor::parse() call, second
description copy and logging of the map size are removed. Validation retains
no complete rich-token vector.

The independent lazy glyph-mask/vector analyzer remains unchanged. It still
has its own first-use temporary storage, so total RSS is not expected to become
constant or equal to QPDF-discard memory.

The old char_description/char_processor helper definitions remain temporarily
included but have no functional callers. Retire their dead includes/files with
the final source-path cleanup after validation; they do not retain objects now.

### Recovery regression added

test_eager_repair_preserves_damaged_cached_glyph_bounds() supplies a damaged
Flate CharProc with a usable filled-rectangle prefix and a deliberately loose
FontBBox. It expects actual ink dimensions (60 by 100 points).

This checks an observable side effect of the old eager decoder: repair must
happen before lazy glyph analysis computes the text-cell bounds. Simply
removing eager decoding could preserve rendered ink while changing extraction
geometry, so the regression checks the parsed cell rather than only pixels.

The previously passing focused suite gains one case. The new case and updated
source have not been built or executed by Codex.

### Exact before-change reference

Preserved executable:

- /private/tmp/docling-memory-before-type3-font-cleanup
- SHA-256: 24459b9fb6b9450e7d60ae774412d36cea15e03ebf49510b151672aa8b3aa2aa

Its hash matches the user's step-4c scaling metadata. Both the preserved binary
and existing step-4c data have callback CharProc interpretation plus the old
eager description retention. This isolates the new cleanup from the earlier
streaming patch.

The launcher now also snapshots page_font.h, so subsequent result bundles
capture the changed source explicitly.

### Static checks

- MyPy: no issues in 110 source files.
- Ruff lint: passed.
- Ruff format check: 115 files already formatted.
- git diff --check: passed.

These are source/static checks, not C++ compilation or runtime validation.

### User native build and recovery check

The optional targets already exist; an incremental build suffices:

~~~sh
cmake --build build --parallel 2 --target \
  test_memory_on_lines stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'
~~~

If using a new build directory, configure first with
`cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON`.

### User small comparison

Compare to the saved callback-Type3 smoke, not the buffered-Type3 reference:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-type3-font-cleanup-smoke \
  --baseline /private/tmp/docling-memory-step4c-smoke \
  --layouts direct,form,type3,type3-repeat \
  --sizes 1000 --repeats 1
~~~

Require matching source hashes, geometry counters and all four PNG pairs.
The existing Type3 interpreter materialization counter is already zero on both
sides; it does not count eager font initialization. Source inspection plus
process peak measurements establish this cleanup's effect.

### User scaling comparison after the small gate

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-type3-font-cleanup-scaling \
  --baseline /private/tmp/docling-memory-step4c-scaling \
  --sizes 125000,250000 --layouts direct,type3 \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

This has 36 measurements. Assess memory and runtime against the old eager
retention, with direct pages as controls. The earlier 250k Type3 callback
baseline was 791.47 MiB decode and 676.72 MiB with shapes disabled.

If dependency hashes/configuration differ, the historical binary cannot provide
a controlled reference against newly rebuilt dependencies. Rebuild the
pre-cleanup source against the same current dependencies and compiler settings
as the candidate, in an isolated reference build, before collecting a new pair.
Do not bypass comparison safeguards or relabel an old executable with current
archive hashes. Preserve its known hash and original metadata.

### User rebuilt-extension semantic checks

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_type3_streaming.py \
  tests/test_unit_type3_fonts.py \
  tests/test_unit_appearance_streaming.py \
  tests/test_unit_widget_appearances.py \
  tests/test_unit_acroform_widgets.py \
  tests/test_unit_form_streaming.py \
  tests/test_unit_split_content_streams.py \
  tests/test_unit_damaged_content_streams.py \
  tests/test_unit_actual_text.py \
  tests/test_unit_image_decode.py \
  tests/test_unit_clipping.py \
  tests/test_unit_patterns.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py -q
~~~

The added bounds regression must run against the rebuilt extension. Keep
existing assertions and ground truth unchanged when reporting failures.

### Remaining six-item completion status

| Item | Status / next gate |
| --- | --- |
| 1. Legacy Type3 token cleanup | Semantic regression passed in the user's 86-test suite; historical performance comparison rejected on changed dependencies; controlled comparison pending |
| 2. Soft-mask group source streaming | Complete: native smoke/scaling, cache reuse, PNG equivalence and 86 focused semantic tests passed |
| 3. Pattern cache/replay copies | Focused validation complete: native smoke/scaling, PNG/cache checks, control recheck and the user's 131-test semantic suite passed |
| 4. State-lifetime/reset audit | Focused validation complete: native exception/clip-history checks, shared-clip memory/pixel scaling, renderer cursor comparison, ordinary-page controls and 131 semantic tests passed; small timing variations remain recorded for broad validation |
| 5. Broader corpus and threaded validation | Pending; user runs builds/tests/experiments |
| 6. Comparison-path/dead-API retirement | Pending acceptance of broader validation |

The subsequent permanent benchmark peak-RSS/timeline work remains after these
parser items, as requested. This checkpoint does not mark all six complete.

## Completion pass — item 2: stream soft-mask group sources

Date: 2026-11-11.
The user explicitly requested starting item 2 after the Type3-cleanup smoke.
The unresolved item-1 comparison does not block this authorized implementation;
it remains a separate acceptance gate.

### Item-1 intermediate result and comparison limitation

The bundle `/private/tmp/docling-memory-type3-font-cleanup-smoke` contains all
20 requested measurements, but status.json reports `failed`: the baseline and
candidate dependency_sha256 maps differ. The comparison stops before accepting
cross-run output or memory equivalence. Single-repeat timing remains diagnostic.

Changed archives relative to the step-4c smoke are:

- libblend2d.a, libloguru.a and libpdfium_jbig2.a;
- libfreetype.a, libjpeg.a, liblcms2.a and libopenjp2.a;
- libqpdf.a and libturbojpeg.a.

The numbers are useful as a smoke observation but do not isolate the Type3
cleanup's memory or runtime effect. Do not mark item 1 accepted, bypass hashes,
or use this failed comparison bundle as a baseline. Its native executable has
been rebuilt, but the new Type3 bounds regression still needs user execution.
The reference-build instruction above supersedes the earlier suggestion that
simply rerunning the preserved historical executable would suffice.

### Source change and retained ownership

decode_soft_mask() establishes local resources, its own output containers, the
shared page mask cache, recursion depth and the composed mask-space/gs matrix
before invoking decode_and_interprete(). It no longer accumulates a complete
group-source token vector in the normal path.

The useful decoded soft_mask_instruction remains cached by spec and CTM. Its
private render group is retained for reuse by page paint instructions. This
change removes source-token retention, not the geometry needed to render a
mask. Alpha/luminosity selection, backdrop/transfer handling, intersections,
the nested-mask guard and the existing 512-entry limit stay in force.

std::bad_alloc propagates rather than being reported as an unreadable mask and
silently continuing unmasked. Existing malformed-source recovery still uses
the same qpdf_stream_decoder normalization and prefix-repair path.

### Native reference and diagnostics

New temporary target: `test_memory_on_lines_buffered_soft_masks`.
It enables only DOCLING_PARSE_BUFFERED_SOFT_MASK_CONTENTS; pages, Forms,
appearances, Type3 interpretation and font validation match the candidate.
Build this reference and the candidate together against the current archives.
The existing fully buffered target also enables the new mask selector.

New opt-in layouts:

- `soft-mask`: one N-line stroked grid used as an alpha-mask group; the page
  paints one filled rectangle under it.
- `soft-mask-repeat`: four installations/paints of the same entry at the same
  CTM by default (`--soft-mask-repeats` changes the count). The mask source must
  decode once, with three cache hits. Page output has four rectangles, while
  the private decoded mask contains N strokes, not 4N.

The native generator marks the selected group, so QPDF-discard/collect visit
that source once. It has exactly 7N+2 objects and 3N+1 operators, matching the
direct/Form grid controls. `unique_lines` is N; the generic `lines` field is
N times the number of mask applications and does not describe stored geometry.

Probe-only counters follow the shared /Contents/ordinary-Form mask cache:
executions, cache hits, source objects/bytes, materialized tokens, maximum
buffered capacity, pending operands and private mask shape instructions.
They do not claim to cover separate appearance-decoder caches or replace a
process RSS measurement. Source counts describe each decoded group's own
source, not token counts in nested Form sources.

For these fixtures, normal delivery must report zero mask materialized tokens
and capacity, with at most two pending operands. The reference reports 7N+2
materialized tokens for its one decode. Shapes-disabled still executes gs and
decodes the source, but emits no mask shapes. Both paths must preserve source
counts, geometry, cache reuse and PNG bytes.

### Semantic regression

`tests/test_unit_soft_mask_streaming.py` adds four cases: alpha/luminosity,
each with intact/damaged Flate group content. A group matrix composes with the
CTM at gs; a nested Form uses group-local resources; two gs applications share
coverage; parent paint resumes with its original color and transform.

Half-opacity mask content makes duplicate delivery during damaged-prefix
recovery observable in the rendered pixels. Points outside the group box and
inside its unpainted portion must retain the red background. The existing
soft-mask suite supplies transfer, clipping, cache-limit and nested-mask cases.

### User build and native smoke

Static validation of this implementation passed: MyPy (111 source files),
Ruff lint, Ruff formatting (116 files) and git diff --check. These do not
establish C++ compilation, rendered equivalence or a memory/runtime improvement.

Codex has not compiled or executed these paths. Keep builds and experiments
with the user. Regenerate the existing build directory to register the new
reference target, retaining its other cached compiler/dependency options:

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines test_memory_on_lines_buffered_soft_masks \
  stream_callback_regression

ctest --test-dir build --output-on-failure -R '^stream_callback_regression$'

uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_soft_masks \
  --output /private/tmp/docling-memory-soft-mask-buffered-smoke \
  --layouts direct,form,soft-mask,soft-mask-repeat \
  --sizes 1000 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-soft-mask-smoke \
  --baseline /private/tmp/docling-memory-soft-mask-buffered-smoke \
  --layouts direct,form,soft-mask,soft-mask-repeat \
  --sizes 1000 --repeats 1
~~~

Each smoke run has 20 measurements. Use fresh directories, and require both
status.json files to pass. Preserve the compiler flags, exact binary hashes,
archives, inputs and source snapshots in these bundles. Do not reuse the old
Type3 reference for this experiment.

### User scaling after the small gate

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_buffered_soft_masks \
  --output /private/tmp/docling-memory-soft-mask-buffered-scaling \
  --sizes 125000,250000 --layouts direct,soft-mask,soft-mask-repeat \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-soft-mask-scaling \
  --baseline /private/tmp/docling-memory-soft-mask-buffered-scaling \
  --sizes 125000,250000 --layouts direct,soft-mask,soft-mask-repeat \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

Each side has 54 measurements. Seek reduced mask memory and no meaningful
slowdown beyond the accepted 5%. Direct pages are controls. Repeated mask
applications must preserve cache hits and avoid multiplying private geometry.
Do not predict the RSS reduction from token-record sizes alone: temporary
page-shape containers, output geometry, allocator behavior and rasterization
also contribute to the peak.

### User rebuilt-extension semantic gate

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_soft_mask_streaming.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py \
  tests/test_unit_form_streaming.py \
  tests/test_unit_patterns.py \
  tests/test_unit_type3_streaming.py \
  tests/test_unit_type3_fonts.py -q
~~~

This also exercises the pending Type3 bounds regression against the rebuilt
extension. Broad corpus/threaded validation and comparison-path retirement
remain later gates. Await these results before accepting item 2 or changing
the next pattern-cache ownership path.

## Completion pass — item 2 accepted results

The user completed the paired soft-mask smoke (20 measurements per executable)
and scaling (54 measurements per executable). Both candidate bundles passed
the dependency/input checks, geometry assertions, cache diagnostics and PNG
comparisons. All six scaling PNG pairs were byte-identical; no timing case
exceeded the accepted 5% review threshold.

At a 250,000-line target:

| Layout / mode | Before MiB | After MiB | Memory change | Process-time change |
| --- | ---: | ---: | ---: | ---: |
| soft-mask / decode | 566.91 | 245.84 | -56.63% | -4.65% |
| soft-mask / skip-shapes | 445.55 | 35.36 | -92.06% | -7.43% |
| soft-mask / render | 566.91 | 245.75 | -56.65% | -5.36% |
| soft-mask-repeat / decode | 566.86 | 245.88 | -56.63% | -4.98% |
| soft-mask-repeat / skip-shapes | 445.47 | 35.38 | -92.06% | -11.33% |
| soft-mask-repeat / render | 566.86 | 245.89 | -56.62% | -6.10% |

At 125,000, decode/render memory fell about 46–49%, and shapes-disabled memory
about 90%. The direct-page controls stayed within the runtime budget.

The repeated layouts decoded once with three cache hits, retained zero source
tokens/capacity and reached only two pending operands. Private mask geometry
was 124,861 and 249,652 strokes at the two targets, shared by four page paints.
The corresponding source counts were 874,029 and 1,747,566 objects.

The user then rebuilt the extension and ran the seven-file focused semantic
suite: **86 passed in 4.55 seconds**, including all four soft-mask callback
cases and the Type3 eager-repair bounds regression. This closes item 2 and the
item-1 semantic gate; item-1 performance attribution remains unresolved.

## Completion pass — item 3: borrow cached pattern source during replay

The user approved this bounded change with "do it" after reviewing the
two-copy finding. Builds and experiments remain with the user.

### Source and ownership change

Both the direct lattice path and cached tile creation previously copied the
entire pattern source into a local `insts` vector, then copied it again into
the child decoder's `stream`. The normal path now calls interprete_cached(),
which borrows the resource-owned const vector for the synchronous call.

The helper runs the same begin/consume/finish lifecycle. It copies one current
instruction because QPDF accessors and the consumer take mutable objects;
cached instruction records remain untouched. Pending operands still use the
existing bounded-per-operator vector. It does not retain a view after return.

The resource's parsed-token cache remains owned by PAGE_PATTERN, including
its referenced QPDF objects. Its decoded render-cell cache and uncoloured
paint-colour keys also remain. No token cache is cleared or reparsed on each
tile, and no decoded cell is intentionally duplicated by this change.

The pattern resource remains alive through the parent-linked resource maps
of these synchronous child decoders. Nested replay receives an already parsed
cache; normal interpretation does not replace or resize its source vector.
Normal replay also drops any previous child-owned source buffer before use.

### Native reference and instrumentation

New temporary target: `test_memory_on_lines_copied_patterns`, enabling only
DOCLING_PARSE_COPY_PATTERN_CONTENTS. Its two original vector copies and their
call-site lifetimes are preserved. The candidate and reference otherwise use
the same current code and dependencies, including accepted soft-mask streaming
and the Type3 font cleanup. The fully buffered reference also enables this
selector until final comparison-path retirement.

Probe-only shared counters cover the /Contents decoder tree, including its
ordinary Forms, procedural CharProcs, mask groups and pattern child decoders.
They do not aggregate separate appearance decoder roots. The shared statistics
allocation and updates are absent in production builds.

Counters distinguish:

- Source parses/cache hits and retained cached token count/capacity.
- Decoded-cell construction/cache hits and its private shape count.
- Direct lattice executions and cumulative source objects interpreted.
- Cumulative tokens materialized in complete replay vectors, maximum capacity
  of a child replay vector, and maximum pending operands.

The vector materialization count excludes the useful resource cache, the
single working instruction and pending operands. It measures copying work,
not simultaneous peak live memory. The intentional cache remains linear in
source size in both executables. Source/copy counters cannot substitute for
measured process RSS or runtime.

### Native fixtures and invariants

All fixtures reuse the same generated grid source, with N actual strokes,
7N+2 QPDF objects and 3N+1 operators. Raw modes select the marked pattern stream
once. New opt-in layouts are:

- `pattern`: one fill using a page-sized tile. One source parse, one decoded
  cell containing N strokes, and one page rectangle.
- `pattern-repeat`: four fills by default (`--pattern-repeats` changes this).
  One source parse and one decoded cell, three source-cache hits and three
  decoded-cell-cache hits. The private geometry stays N; page output is four
  rectangles.
- `pattern-lattice`: both cell steps are twice the page extent, selecting
  direct replay at [0,1] by [0,1]. Four executions share one parsed source.
  Render output contains 4N stroke instructions, although only the first
  placement contributes on-page ink. The outline is retained as an additional
  page shape and does not become an unresolved solid-fill render instruction.

For E actual cell interpretations and T=7N+2 cached objects, the reference
reports 2ET materialized replay tokens; the candidate reports zero. Cached
tokens/capacity, source/replay counts, page/private geometry and PNG bytes
must match. These grid cells reach at most two pending operands.

With shapes disabled, all three layouts must skip pattern parsing and replay:
zero caches, executions, replay copies and pattern geometry. This preserves
the existing CAD/text-extraction performance behavior.

### Added semantic coverage

`tests/test_unit_pattern_replay.py` has four cases: cached tile/direct lattice,
each using intact/damaged Flate source. An uncoloured stencil paints successive
regions red, blue and red, exercising colour-specific decoded-cell ownership
and reuse. Local half-opacity resources shadow an opaque parent resource.

The source leaves a translation active; each subsequent replay and the parent
suffix must retain their own placement/state. Half-opacity pixels expose
duplicate recovered-prefix execution. The existing pattern suite continues
to cover pattern/base matrices, clipping, nested resources, shape skipping,
overlapping cells and image reporting.

### Static review and checks

- MyPy: no issues in 112 source files.
- Ruff lint: passed.
- Ruff format check: 117 files already formatted.
- git diff --check: passed.

Codex has not compiled these C++ paths or run the new semantic cases/native
experiments. Four source cases are added to the previously passing 86-test
suite; no 90-test result is claimed.

The lifetime audit still needs to address exception restoration in existing
pattern code: lattice depth/q-Q scopes and cached-cell decoding flags are
restored manually, and an allocation or nonstandard exception can bypass
restoration. This source finding is carried to item 4; it is not evidence of
a new measured quadratic-memory defect. This patch isolates token borrowing
from that separate state-restoration change.

### User build and smoke gate

Regenerate the existing build directory to register the reference target while
retaining its other cached compiler/dependency settings:

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines test_memory_on_lines_copied_patterns

uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_copied_patterns \
  --output /private/tmp/docling-memory-pattern-copied-smoke \
  --layouts direct,pattern,pattern-repeat,pattern-lattice \
  --sizes 1000 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-pattern-smoke \
  --baseline /private/tmp/docling-memory-pattern-copied-smoke \
  --layouts direct,pattern,pattern-repeat,pattern-lattice \
  --sizes 1000 --repeats 1
~~~

Each run has 20 measurements. Require passing status.json, unchanged cached
source counts, expected reuse, zero candidate replay vectors and four identical
PNG pairs. Single-repeat timing remains diagnostic.

### User cached-tile scaling after the small gate

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_copied_patterns \
  --output /private/tmp/docling-memory-pattern-copied-scaling \
  --sizes 125000,250000 --layouts direct,pattern,pattern-repeat \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-pattern-scaling \
  --baseline /private/tmp/docling-memory-pattern-copied-scaling \
  --sizes 125000,250000 --layouts direct,pattern,pattern-repeat \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

Each executable contributes 54 measurements. Seek lower RSS for cell decoding
and no meaningful slowdown beyond 5%; decoded-cell reuse must remain intact.
The source cache remains, so do not expect page/soft-mask streaming percentages.

### User bounded direct-lattice scaling

This path emits four placements of every grid stroke. Use smaller source
sizes to keep output geometry and runtime bounded:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_copied_patterns \
  --output /private/tmp/docling-memory-pattern-lattice-copied-scaling \
  --sizes 32000,64000 --layouts direct,pattern-lattice \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-pattern-lattice-scaling \
  --baseline /private/tmp/docling-memory-pattern-lattice-copied-scaling \
  --sizes 32000,64000 --layouts direct,pattern-lattice \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

Each side has 36 measurements. Compare cumulative copying, RSS and process wall
time together; the four executions are sequential and their allocation totals
must not be reported as simultaneous live memory.

### User rebuilt-extension semantic gate

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_pattern_replay.py \
  tests/test_unit_patterns.py \
  tests/test_unit_soft_mask_streaming.py \
  tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py \
  tests/test_unit_form_streaming.py \
  tests/test_unit_type3_streaming.py \
  tests/test_unit_type3_fonts.py
~~~

Review the native and semantic results together before accepting item 3.
Item 4 then closes the state/reset audit and any separately reproduced fixes;
items 5–6 cover broader corpus/threaded validation and source-path retirement.
The permanent benchmark peak-RSS/timeline work still follows parser acceptance.

## Completion pass — item 3 native scaling accepted

The user completed cached-tile scaling (54 measurements per executable) and
direct-lattice scaling (36 per executable). All dependency/input and structural
checks passed. All ten scaling PNG pairs were byte-identical.

Cached-tile decode/render memory fell 26–28%, with 3–7% faster process times.
At 250k, pattern decode fell 780.30 to 566.94 MiB, and repeated-pattern decode
782.27 to 566.97 MiB. The source contains 1,747,566 64-byte token records:
two size-only vector copies predict 213.33 MiB, closely matching the observed
213.36 MiB reduction for one fill. This does not account for every allocator
effect or generalize that exact percentage to other content.

Lattice decode/render memory fell 11–12%, with 3–6% faster process times.
At 64k, decode fell 467.44 to 413.06 MiB and rendering 476.48 to 420.36 MiB.
Both executables retained the same source cache. The candidate materialized
zero replay vectors; repeated cached tiles decoded once with three cache hits,
and the lattice still executed four placements.

The cached-tile matrix flagged direct/125k/skip-shapes at +6.26% and the roughly
20 ms pattern-repeat/250k/skip-shapes case at +7.82% with overlapping ranges.
Both bypass pattern replay. The user rechecked the direct control seven times
per executable: 0.274 to 0.276 seconds (+0.77%), with 23.02 to 23.08 MiB
(+0.27%). The earlier direct slowdown did not reproduce. The very short
skipped-pattern timing remains inconclusive; no replay-path slowdown was found.

The user then said "continue to item 4". This authorizes the audit without
asserting that the pending 90-case semantic suite has passed.

## Completion pass — item 4: state/reset lifetime audit

This is a targeted source audit of the parser state, nested content lifetimes,
resource reuse and output ownership relevant to this memory investigation.
It is not a claim that every C++ subsystem has been exhaustively verified.
Codex has not compiled or executed this iteration.

Static checks passed: MyPy (112 source files), Ruff lint, Ruff formatting
(117 files) and git diff --check. These do not establish native compilation,
exception-test success, output equivalence or runtime overhead.

### Audit findings and classification

| Area | Verified reset/ownership or finding | Classification / action |
| --- | --- | --- |
| Direct dash replacement | d clears the array before replacement; null/error assigns an empty array. | Existing #452 fix is retained; no new normal-path append defect. |
| ExtGState dash parsing | A fresh local array is assigned to the supplied /D field. Missing ExtGState fields preserve existing properties. | Correct replacement/partial-update behavior in inspected paths. |
| Current paths | register_paths, n and discard_paths clear scratch paths after their prescribed painting/clipping work. | Existing resets retained. |
| Clip history | capture_pending_clip appends successive intersections; get_clip_state copies the current history into each painted instruction. | Intentional state with a structural quadratic snapshot case; bounded native fixtures added below. No history clear introduced. |
| Graphics-state copies | GLOBAL copy construction rebinds state references to the new frame; assignment preserves destination resources. SHAPE copies current paths/clip history. | Required scoped state copying; retain resource and q/Q semantics. |
| Internal graphics saves | Form and pattern/shading paths restored manually only on normal return. | Confirmed exception-restoration gaps; scope guards added. |
| Pattern recursion bookkeeping | Lattice depth had no exceptional cleanup. Cached-cell flags/depth could survive a nonstandard exception or a failure while logging/allocating fallback output. | Confirmed exceptional state lifetime defects; scope guards added. |
| Pattern source-cache completion | stream_parsed_ was set before parsing. Allocation failure was caught as unreadable input and could leave a failed attempt treated as complete. | Commit the flag after completion; release partial tokens and propagate allocation/nonstandard failures. |
| Type3 recursion and placement | Existing catch-all cleanup already restored the glyph depth and parent frame. | Moved to the same scope guards; no newly discovered normal-path defect claimed. Native regression preserves that guarantee. |
| Pending Type3 procedures | paint_type3_glyphs swaps work into a local vector before nested state copies. TEXT assignment omits the transient queue; actual child rebase starts with a fresh state. | No independent successful-show accumulation found. Partial text-generation failures are not claimed exhaustively covered. |
| Inline-image scratch | BI replaces the entry; completed/incomplete EI replaces it again. A throwing bitmap decode can retain one entry until the next BI/decoder destruction. | Bounded scratch retention, without a confirmed per-image accumulation or quadratic copy defect. |
| Marked content and operands | EMC pops scopes; logical-stream finish clears unbalanced marked content. Handled operators clear pending operands. | Existing logical stream boundaries retained. Large explicit operands still require their own storage. |
| Resource setters | Inspected collection setters construct fresh resources before map replacement; parent-linked maps preserve inherited ownership. | A missing reset in a hypothetical reused individual resource is not established on these production callers. |
| Shape JSON initialization | PAGE_SHAPES clears/resizes fresh destination entries before optional fields are read. | Optional member reuse was not reproduced on this inspected path. |
| stack_count | Historical cumulative q counter is also emitted as text-cell metadata; Q does not decrement it. | Not treated as live stack depth; unchanged to preserve extracted metadata. Scope guards restore actual frames. |
| Page/document ownership | Page decoders are intentionally cached; unload_page/unload_pages erase those owners. Threaded document unload removes document/schedule owners; held results can retain instructions. | No missing-clear defect established by these ownership paths. Multi-page/threaded RSS remains item 5 validation. |
| Font, mask and pattern caches | Glyph data, decoded masks, parsed pattern tokens and decoded tiles have explicit reuse owners. Unused eager Type3 descriptions were removed in item 1. | Intentional caches retained; token-copy costs are measured separately from cache residency. |

No second normal-path setter bug equivalent to dash_array's old missing clear
was confirmed. The exception defects above and the clip snapshot structure are
separate findings with separate evidence/validation requirements.

### Implemented exception-restoration patch

An internal graphics_state_scope records the current frame count, calls q,
and removes the frames it owns on normal or exceptional exit. Its destructor
performs no allocation, logging, state copying or Q call. Explicit restore()
retains the old normal-return lifetime at each site. The historical stack_count
behavior and actual PDF q/Q operator implementation remain unchanged.

The guard now covers ordinary Forms, lattice parent/placement scopes, shading
and Coons pattern scopes, and procedural Type3 parent placement. Child decoder
stacks remain independently owned and die with their decoder.

A nesting_scope saves/restores the prior thread-local depth and, for cached
tiles, the prior resource decoding flag. It protects lattice, cached-tile and
Type3 interpretation even if a later catch block itself throws.

Cached tile interpretation now propagates std::bad_alloc. Pattern source
parsing commits stream_parsed_ after completion or handled unreadable input;
allocation/nonstandard failures discard partial cache records/capacity and
leave parsing retryable. Ordinary unreadable-input behavior is retained.

These fixes protect recovery/state ownership. They do not promise lower peak
RSS for normal documents or an additional normal-path quadratic-memory fix.

### Native failure-injection regression

New optional target/CTest: `stream_state_lifetime_regression`, source
`tests/cpp/test_stream_state_lifetimes.cpp`.

It has 14 scenario groups, covering recoverable runtime errors, allocation
failure and a nonstandard exception during cached tiles, direct lattice,
ordinary Forms and Type3 procedures, plus failed source-cache parsing/retry.
Glyph scenarios repeat failures beyond the recursion cap before trying again.

Assertions check parent frame count, matrix, clip isolation, bitmap glyph mode,
rendered suffix coordinates, pattern depth/flags and preserved exception types.
Source parse failures seed a partial record, then require an empty/uncommitted
cache and a successful retry with the original source.

Private fault checkpoints are compiled only with
DOCLING_PARSE_STATE_LIFETIME_TESTING in this dedicated target. They are absent
from production and every memory-probe executable. Failures are deterministic;
the tests do not exhaust actual memory. Assertions remain active in Release.
No runtime pass is claimed until the user compiles and executes it.

### Clip snapshot experiment

New opt-in layouts `clip-chain` and `clip-scoped` use N independent strokes and
the same full-page rectangular clip before every stroke:

- clip-chain leaves all intersections active. The instruction count is N,
  but retained clip-path occurrences total N(N+1)/2, with 5N(N+1)/2 points.
- clip-scoped saves/restores each clip-and-paint with q/Q. Each instruction
  retains one clip, so totals are N paths and 5N points.

Both layouts should have identical pixels. The harness checks those PNG pairs,
the exact geometry/source counters and the clip totals. Source object counts
are 14N versus 16N (the scoped version adds q/Q); these are linear. The fixture
target is capped at 4000 lines to keep a diagnostic run bounded.

This demonstrates the distinction between a linear instruction count and a
quadratic copied payload. Generic clipping intersections cannot simply be
cleared. A future shared/persistent clip representation or semantic clip
canonicalization would be a separate optimization, informed by the measured
importance of this case in real documents. No clip representation change is
included here. Actual RSS/timing curves remain to be measured by the user.

### Preserved reference and normal-path comparison

Before modifying the executable, Codex preserved the existing probe:

- `/private/tmp/docling-memory-before-state-lifetimes`
- SHA-256: 959dbf38567d8d6873f50e27111ec2e6f088d4ee54db7876d35bc21fbe0c1994

It exactly matches the executable in the successful pattern control-after
bundle. Current dependency archives were also checked against that bundle:
none had changed. The original bundle retains its compiled-source provenance;
the current source snapshots recorded by a run of the preserved executable
are not a claim about that old binary's source.

Before rebuilding, collect the normal-path reference while those dependencies
still match:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe /private/tmp/docling-memory-before-state-lifetimes \
  --output /private/tmp/docling-memory-state-lifetimes-before \
  --sizes 125000 --layouts direct,form,type3,pattern \
  --modes decode,skip-shapes,render --repeats 3
~~~

This has 36 measurements. The existing dependency checks remain mandatory.
If rebuilding changes archives, do not relabel the preserved binary or bypass
the hashes: rebuild an isolated pre-audit reference against the candidate's
dependencies before comparing again.

### User build and focused native gate

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines stream_callback_regression stream_state_lifetime_regression

ctest --test-dir build --output-on-failure \
  -R '^stream_(callback|state_lifetime)_regression$'
~~~

After the tests pass, compare normal-path memory/runtime and images:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-state-lifetimes-after \
  --baseline /private/tmp/docling-memory-state-lifetimes-before \
  --sizes 125000 --layouts direct,form,type3,pattern \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

Require unchanged geometry/PNGs, ordinary memory behavior and runtime within
the accepted budget. This comparison isolates item 4, not the older Type3
eager-cache cleanup whose historical dependency mismatch remains unresolved.

### User clipping smoke and bounded scaling

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-clip-history-smoke \
  --layouts clip-chain,clip-scoped --sizes 100 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-clip-history-scaling \
  --layouts clip-chain,clip-scoped --sizes 250,500,1000 \
  --modes decode,skip-shapes,render --repeats 3
~~~

The smoke has ten measurements and one equivalent-layout PNG pair. Scaling
has 54 measurements and three equivalent-layout PNG pairs. Inspect retained
clip paths/points/bytes alongside process peak RSS, not instruction count alone.
The shapes-disabled cases must retain no render-instruction clip snapshots.

### User rebuilt-extension semantic gate

The item-3 semantic result was not supplied before item 4 was authorized.
Run its existing eight-file suite after rebuilding this version, and include
clipping, damaged-stream and pattern-related coverage:

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_pattern_replay.py tests/test_unit_patterns.py \
  tests/test_unit_soft_mask_streaming.py tests/test_unit_soft_masks.py \
  tests/test_unit_transparency.py tests/test_unit_form_streaming.py \
  tests/test_unit_type3_streaming.py tests/test_unit_type3_fonts.py \
  tests/test_unit_clipping.py tests/test_unit_damaged_content_streams.py \
  tests/test_unit_split_content_streams.py tests/test_unit_actual_text.py \
  tests/test_unit_appearance_streaming.py tests/test_unit_widget_appearances.py
~~~

This closes the pending item-3 semantics and checks the revised state lifetimes
against existing outputs. Native compilation/tests, clipping measurements and
these semantic results are user gates before accepting item 4. Broader corpus
and actual threaded ownership/memory validation remain item 5.

## Item-4 native results and clipping growth confirmation

The user supplied passing CTest results for stream_callback_regression and
stream_state_lifetime_regression (0.81 seconds total). The paired normal-path
comparison completed 36 measurements per executable and passed all structural,
dependency and PNG checks. All four PNG pairs were identical, with no timing
review flags. The largest reported positive runtime change was +3.94%; positive
peak changes were below 0.8 MiB. This accepts native overhead for the scope
guards, without claiming a rebuilt-extension semantic result.

The clip-history scaling bundle completed 54 measurements and passed all three
equivalent-layout PNG comparisons. Its results confirm the structural case:

| N | Instructions | Chain clip occurrences | Scoped occurrences | Chain minus scoped decode MiB |
| ---: | ---: | ---: | ---: | ---: |
| 250 | 250 | 31,375 | 250 | 5.89 |
| 500 | 500 | 125,250 | 500 | 23.15 |
| 1,000 | 1,000 | 500,500 | 1,000 | 91.56 |

Decoded chain peaks were 16.81, 34.42 and 103.64 MiB; scoped peaks 10.92, 11.27
and 12.08 MiB. Render peaks followed the same growth (26.97, 44.42, 113.73 MiB
versus 21.17, 21.56, 22.25 MiB). Shapes-disabled peaks stayed about 10–11 MiB.
The chain's accounted clip payload was 4,518,000, 18,036,000 and 72,072,000
bytes, with 156,875, 626,250 and 2,502,500 point occurrences.

Source objects remain linear (14N for chain, 16N for scoped). Render instruction
counts are N; full copied histories account for N(N+1)/2 path occurrences.
Subtracting the scoped control gives approximately fourfold extra decode RSS
on each doubling, consistent with that exact structural count. This is a
specific native snapshot-retention finding, not a general attribution of all
high-memory PDFs to clipping.

## Clipping follow-up — share immutable history

The user approved proceeding after reviewing the clipping results. Codex
implements; builds, experiments and runtime correctness checks remain with
the user. This follow-up is separate from the already measured scope guards.

### Representation and ownership

Each nonempty clip capture becomes one immutable node containing its subpaths
and a shared predecessor. A paint snapshot and a q/Q state save retain the
relevant history head. Appending creates a new node; earlier snapshots and
restored branches keep their existing heads. Capture geometry is owned once
per node, independent of the number of later paints.

SHAPE now owns this shared history instead of deep-copying a page-shape clip
list and a parallel group-id list. The clip bbox/intersection calculations,
capture order, per-subpath group ids and existing snapshot rule value remain.
get_clip_state() returns the shared prefix with the current rule. No clipping
intersection is cleared or deduplicated by this change.

get_paths() exposes a read-only forward range rather than a vector. Existing
repository consumers were adapted: iteration, size and empty queries do not
retain flattened geometry. Explicit to_vector() is available for intentional
materialization and the temporary native reference. Code requiring a concrete
vector must request that copy explicitly; Python-facing APIs are unchanged.

Traversal preserves oldest-capture and within-capture subpath order. Its
iterator uses inline storage for up to 16 capture pointers and transient
pointer storage for longer histories. No flattened cache lives in a snapshot.
Long uniquely owned suffixes are destroyed iteratively to avoid recursive
shared_ptr teardown exhausting the native stack.

Shifted instruction replay shares a translation cache for that replay. A
prefix is translated once for a given offset; the cache retains source owners
as well as translated nodes so source addresses cannot be reused as keys.
Changing the offset clears that cache. Independently retained translated
snapshots remain valid. Translation does not mutate original coordinates.

Renderer readers retain their existing clipping/rasterization algorithms.
Clip equality first checks shared-history identity, then compares ordered paths
when needed. The existing group-size map replaces a redundant nested scan
for same-group subpaths. No new rectangle-intersection simplification or
clip-mask representation is introduced.

Storage for a simple N-capture chain is now N nodes/N unique paths, while all
N paints still logically see N(N+1)/2 path occurrences. Rasterization may
still traverse growing histories; this patch does not claim linear render
runtime. Memory and the accepted runtime budget require measurement.

### Controlled reference and diagnostics

New optional executable: `test_memory_on_lines_copied_clips`, with only
DOCLING_PARSE_COPY_CLIP_SNAPSHOTS enabled. It materializes a complete history
for each paint and uses per-instruction translated snapshots. All source-token
delivery, scope guards and renderer readers otherwise match the candidate.
The fully buffered reference also enables this selector until final retirement.

Use a fresh paired reference/candidate run against matching current libraries.
This target isolates snapshot copying; it does not reproduce every historical
internal allocation of the old mutable clip-state representation.

The probe now distinguishes:

- instruction_clip_paths / instruction_clip_points: logical occurrences seen
  by all instructions, preserving the earlier semantic counters.
- instruction_clip_logical_bytes: cumulative path-record/coordinate payload
  corresponding to those logical occurrences.
- instruction_clip_unique_paths / instruction_clip_unique_points: geometry
  retained in distinct history nodes, counted once across snapshots.
- instruction_clip_history_nodes: distinct retained history nodes.
- instruction_clip_bytes: accounted unique node/vector/coordinate storage.
  This excludes shared_ptr control blocks and allocator overhead and is not
  process peak RSS. Earlier bundles used this field for copied vector payload;
  use clip_history_storage and the new fields to interpret the schema.

Accounting visits each distinct node once, stopping at already counted prefixes.
Per-node cumulative totals avoid walking every logical occurrence during
diagnostics. Thus the probe itself does not flatten or recreate a quadratic
history while measuring the shared representation.

decode_work_s and render_work_s isolate native page decoding and rendering
(including canvas flush) from the later diagnostics/JSON/image save work.
Their medians are added to summary.csv. The existing process-wall comparison
continues unchanged; inspect both focused work and process timing before
attributing a change to production execution.

### Added regression coverage and static checks

New CTest target `clip_history_regression`, source tests/cpp/test_clip_history.cpp:

1. Earlier snapshots and independent branches retain their own geometry/rule.
2. Forward traversal preserves capture and same-group subpath order, including
   a copied iterator on a history longer than its inline pointer buffer.
3. One thousand prefix snapshots expose 500,500 logical paths but retain only
   1,000 nodes and 1,000 unique paths.
4. Translation preserves originals, shares translated prefixes, and handles
   offset changes without reusing stale coordinates. Shifted instruction replay
   exercises the shared cache through the real dispatch path.
5. A 50,000-capture unique suffix can be released without recursive teardown.

tests/test_unit_clipping.py adds W/W* pixel cases: an early paint sees a broad
clip; a q/Q child branches left; the restored parent branches right. These
check snapshot immutability and restored-branch isolation. Existing curved,
multi-subpath, image, shading, mask and appearance suites remain necessary.

MyPy passes for 112 source files; Ruff lint and formatting pass for 117 files.
Codex has not built or run these native/semantic tests or the new experiments.

### User build and native regression gate

~~~sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON

cmake --build build --parallel 2 --target \
  test_memory_on_lines test_memory_on_lines_copied_clips \
  clip_history_regression stream_callback_regression stream_state_lifetime_regression

ctest --test-dir build --output-on-failure \
  -R '^(clip_history|stream_callback|stream_state_lifetime)_regression$'
~~~

### User paired smoke

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_copied_clips \
  --output /private/tmp/docling-memory-shared-clips-copied-smoke \
  --layouts direct,clip-chain,clip-scoped --sizes 100 --repeats 1

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-shared-clips-smoke \
  --baseline /private/tmp/docling-memory-shared-clips-copied-smoke \
  --layouts direct,clip-chain,clip-scoped --sizes 100 --repeats 1
~~~

Each side has 15 measurements. Require matching PDFs/PNGs/logical counts.
For N=100, the chain must retain 5,050 unique paths in the copied reference
and 100 in the shared candidate. Both expose 5,050 logical occurrences and
100 render instructions. The scoped layout has 100 unique paths on both sides.

### User paired clipping scaling after smoke

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe build/test_memory_on_lines_copied_clips \
  --output /private/tmp/docling-memory-shared-clips-copied-scaling \
  --layouts clip-chain,clip-scoped --sizes 250,500,1000 \
  --modes decode,skip-shapes,render --repeats 3

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-shared-clips-scaling \
  --baseline /private/tmp/docling-memory-shared-clips-copied-scaling \
  --layouts clip-chain,clip-scoped --sizes 250,500,1000 \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

Each side has 54 measurements. At N=1000, logical clip paths remain 500,500,
but unique retained paths should drop to 1,000. Seek linear unique storage and
lower measured RSS, with unchanged pixels and runtime within the accepted 5%.
Inspect decode/render work medians as well as process-wall results. Do not
raise the existing fixture cap while the copied reference remains quadratic.

### Ordinary-page overhead reference

Codex preserved the executable used for the successful state-lifetime comparison:

- `/private/tmp/docling-memory-before-shared-clips`
- SHA-256: ca7fb602142ff954be45493930a3b09b854262d3f1728060fe3f3389294eeae6

Its binary hash matches /private/tmp/docling-memory-state-lifetimes-after, and
the dependency archive hashes were unchanged at preservation. Original compiled
source provenance remains in that bundle; later current-source snapshots do
not relabel the old executable. Compare ordinary pages after rebuilding:

~~~sh
uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-shared-clips-controls \
  --baseline /private/tmp/docling-memory-state-lifetimes-after \
  --sizes 125000 --layouts direct,form,type3,pattern \
  --modes decode,skip-shapes,render --repeats 3 \
  --slowdown-limit-pct 5
~~~

This supplies 36 measurements. Retain the normal dependency safeguards. If
libraries change, obtain a reference built against matching libraries; do not
bypass hashes or treat this as the older unresolved Type3 cleanup comparison.
The preserved executable is available for focused timing rechecks if needed.

### User rebuilt-extension semantic gate

~~~sh
uv run --no-sync python local_build.py

uv run --no-sync pytest \
  tests/test_unit_clipping.py tests/test_unit_pattern_replay.py \
  tests/test_unit_patterns.py tests/test_unit_soft_mask_streaming.py \
  tests/test_unit_soft_masks.py tests/test_unit_transparency.py \
  tests/test_unit_form_streaming.py tests/test_unit_type3_streaming.py \
  tests/test_unit_type3_fonts.py tests/test_unit_damaged_content_streams.py \
  tests/test_unit_split_content_streams.py tests/test_unit_actual_text.py \
  tests/test_unit_appearance_streaming.py tests/test_unit_widget_appearances.py
~~~

The prior item-3/item-4 semantic result still has not been reported. This suite
must run against the rebuilt extension before accepting the shared-history
patch. Broader corpus/threaded validation, comparison-path retirement and the
permanent benchmark peak-RSS/timeline enhancement remain subsequent work.

## Shared clip history — measured result and renderer traversal follow-up

Date: 2026-11-12. Continuation of the authorized item-4 shared-history change.
Builds, native tests, Python semantic tests and experiments remain with the user.

### Native memory and pixel evidence

The paired smoke bundles each passed 15 measurements:

- `/private/tmp/docling-memory-shared-clips-copied-smoke`
- `/private/tmp/docling-memory-shared-clips-smoke`

All three baseline PNG pairs matched. At 100 clips, both representations expose
5,050 logical clip paths; unique retained paths drop from 5,050 to 100.

The paired scaling bundles each passed 54 measurements:

- `/private/tmp/docling-memory-shared-clips-copied-scaling`
- `/private/tmp/docling-memory-shared-clips-scaling`

All six baseline PNG pairs matched. At 1,000 clips, chain decode peak RSS fell
from 93.92 to 12.19 MiB (-87.02%), and render peak from 104.20 to 22.88 MiB
(-78.05%). The scoped control stayed near 12 MiB decode and 22 MiB render.
The chain still exposes 500,500 logical paths, but retains 1,000 unique paths;
accounted unique node/vector/coordinate storage drops from 72,144,000 to
216,000 bytes. These accounted bytes exclude allocator/control-block overhead.
This confirms the intended linear retained geometry on this fixture. It does
not establish linear rasterization work.

### Seven-repeat recheck: process gate passes, render-phase gate remains open

The new bundles each passed 28 measurements, with all four baseline PNG pairs
and both chain/scoped PNG equivalence checks matching:

- `/private/tmp/docling-memory-shared-clips-render-recheck-before`
- `/private/tmp/docling-memory-shared-clips-render-recheck-after`

| Layout / clips | Peak MiB before → after | Process ms before → after | Render work ms before → after | Render work change |
| --- | ---: | ---: | ---: | ---: |
| Chain / 500 | 42.00 → 22.00 | 83.94 → 74.55 | 56.47 → 53.03 | -6.10% |
| Scoped / 500 | 21.75 → 21.78 | 68.55 → 66.23 | 46.52 → 46.06 | -1.00% |
| Chain / 1,000 | 104.06 → 22.95 | 137.44 → 110.59 | 80.56 → 86.10 | +6.88% |
| Scoped / 1,000 | 22.42 → 22.48 | 69.41 → 69.11 | 45.01 → 43.26 | -3.90% |

At 1,000 chain clips, native decode work falls from 23.35 to 5.27 ms (-77.44%).
The render work ranges do not overlap: 77.72–82.28 ms before and 84.70–90.82 ms
after. The earlier three-repeat matrix showed the same direction (+7.06%).
Treat this as a remaining regression to address, despite the 19.54% process
improvement and 77.94% peak reduction. A passed process-wall comparison is not
a passed render-work gate; the runner currently flags process time only.

### Hypothesis and bounded source change

The immutable forward range reconstructs a temporary stack of predecessor
pointers for each traversal. Renderer helpers also rebuild clip-group counts
several times for a shape. This is a source-based performance hypothesis;
profiling has not established how much each operation contributes.

`clip_path_cursor` keeps one active traversal per renderer:

1. Ordered pointers refer to immutable path geometry. A shared head owner keeps
   the referenced history alive; path coordinates are not copied.
2. The same head reuses the traversal. A direct successor appends just its new
   capture. A skipped sequence appends its suffix in capture order.
3. A restored ancestor, sibling, independent history or translation rebuilds
   the ordered references and group counts from the requested history.
4. Group counts include repeated group IDs across captures, as shading BBox
   paths can reuse a group. Rule changes still use the instruction's rule.
5. A failed allocation clears partially updated state and rethrows. No partial
   pointer list can be reused as a valid traversal.
6. `renderer::set_size()` releases the cursor before its invalid-size early
   return. Space is proportional to one active history, not every snapshot.

The rectangular clipping, non-rectangle detection, mask building, clip bounds
and shading helpers use this traversal. The geometry operations, capture order
and fill-rule selection remain the same. Helpers do not hold cursor iterators
across nested rendering calls; each helper prepares the requested history.
The independent comparison of two clip states keeps its existing iterators.

The existing `clip_history_regression` adds checks for one/multiple-capture
extensions, address/order preservation, repeated group IDs, ancestor/sibling
restoration, translations, unrelated/empty histories, reset/reuse and source
owner release. These are written but have not been compiled or executed by Codex.

### Preserved executable and next controlled user run

Codex preserved the measured shared-history executable before rebuilding:

- `/private/tmp/docling-memory-before-clip-cursor`
- SHA-256: `60e3984699ed1e23e72efc680412f6281f6a2d8015a72094d00cbf2f31fdf596`

Its binary hash matches the recheck-after metadata. All nine dependency archive
hashes matched at preservation. The original compiled-source provenance stays
in the recheck-after bundle; a new runner snapshot does not relabel that binary.

~~~sh
cmake --build build --parallel 2 --target \
  test_memory_on_lines clip_history_regression

ctest --test-dir build --output-on-failure -R '^clip_history_regression$'

uv run python3 tests/cpp/run_memory_on_lines.py \
  --exe /private/tmp/docling-memory-before-clip-cursor \
  --output /private/tmp/docling-memory-clip-cursor-before \
  --layouts clip-chain,clip-scoped --sizes 500,1000 \
  --modes render --repeats 7

uv run python3 tests/cpp/run_memory_on_lines.py \
  --output /private/tmp/docling-memory-clip-cursor-after \
  --baseline /private/tmp/docling-memory-clip-cursor-before \
  --layouts clip-chain,clip-scoped --sizes 500,1000 \
  --modes render --repeats 7 --slowdown-limit-pct 5
~~~

Inspect both process-wall and `median_render_work_s` from summary.csv/raw JSON.
The fresh pair isolates the cursor, since both sides share clip geometry.
Also compare candidate render work against the original copied-history recheck;
keep the original +5% allowance. If dependencies change, rebuild a matching
reference instead of bypassing identity checks. Require matching PNGs/logical
counts and retained linear unique geometry, with no material new peak cost.

At this handoff, the ordinary-page controls, semantic suite and new native
clip-history CTest were awaiting results. The checkpoint below records those
results. Broad corpus/threaded acceptance, comparison-path retirement and
permanent benchmark memory reporting remain subsequent work.

## Focused acceptance checkpoint — cursor, ordinary-page controls and semantics

Date: 2026-11-12. The user returned the cursor comparison, ordinary-page
controls and the rebuilt-extension semantic results, and explicitly accepted
the control table's overlapping timing recheck as a fluke.

### Renderer cursor result

Both bundles contain 28 successful measurements:

- `/private/tmp/docling-memory-clip-cursor-before`
- `/private/tmp/docling-memory-clip-cursor-after`

All four baseline PNG pairs and both chain/scoped equivalence checks match.
Dependencies and probe settings match the original copied-history recheck too.

| Layout / clips | Peak MiB before → after | Process change | Render work ms before → after | Render work change |
| --- | ---: | ---: | ---: | ---: |
| Chain / 500 | 22.08 → 21.75 | -14.80% | 55.60 → 46.04 | -17.20% |
| Scoped / 500 | 21.72 → 21.89 | +1.66% | 45.44 → 45.55 | +0.24% |
| Chain / 1,000 | 22.86 → 22.52 | -25.98% | 86.04 → 56.92 | -33.85% |
| Scoped / 1,000 | 22.47 → 22.66 | +1.56% | 44.77 → 47.61 | +6.34% |

For the 1,000-clip chain, the render-work ranges no longer overlap in the
direction of improvement: 83.96–88.95 ms before versus 55.66–63.93 ms after.
Compared with the original copied-history recheck, peak RSS is 78.36% lower,
process time is 40.78% lower and render work is 29.35% lower.

The scoped 1,000-clip render-work change is 2.84 ms, with overlapping ranges
(40.39–47.79 ms versus 44.18–48.69 ms) and only +1.56% process time. Preserve
that observation rather than claiming every individual phase median is within
5%. No further focused timing recheck is scheduled at this checkpoint; broad
workload performance remains part of item 5.

The saved `build/Testing/Temporary/LastTest.log` records
`clip_history_regression` passing in 0.50 seconds on Nov 12 at 07:06 CET.
Codex read that log; Codex did not launch the native regression.

### Ordinary-page controls

Bundle: `/private/tmp/docling-memory-shared-clips-controls`.
Reference: `/private/tmp/docling-memory-state-lifetimes-after`.
All 36 measurements completed, with matching dependencies/settings, unchanged
logical output counters, and four byte-identical PNG pairs.

Direct, Form and pattern peaks stay close to their references. Type3 decode
peak falls from 175.22 to 134.81 MiB (-23.06%), and render peak from 180.34
to 140.11 MiB (-22.31%). Both sides have zero instruction clip paths on that
Type3 control: do not attribute this observation to sharing nonempty retained
clip geometry or use it to claim an isolated eager-font-cleanup measurement.

All process comparisons are within the configured +5% budget except pattern
with shapes disabled: approximately 19 to 20 ms, +5.86%, with overlapping
ranges. The user explicitly accepted that recheck as noise. No further
repetitions are requested for that case.

### Focused semantic acceptance

The user reported **131 passed in 5.83 seconds**, covering:

- Clipping, including both new W/W* snapshot/branch pixel cases.
- Pattern replay and the existing pattern suite.
- Soft-mask streaming, masks and transparency.
- Form streaming and procedural Type3 streaming/fonts.
- Damaged/split content streams and ActualText.
- Appearance streaming and widget appearances.

This closes the previously unreported semantic gates for completion items 3
and 4. Keep the accepted code and its focused regression coverage. This does
not mark the full parser plan complete or authorize retiring references before
broader validation.

### Item 5 handoff — existing broad validation first

No new production change is needed to run the next checks. The user keeps
execution; use the existing extension built for the 131-test suite:

~~~sh
uv run --no-sync pytest tests/test_unit_*.py -q

uv run --no-sync pytest -n 2 --dist loadfile tests/test_regression_*.py
~~~

The second command is the repository's documented CI-style regression command.
It includes sequential corpus parsing, threaded parsing/rendering, real-page
image checks, embedded fonts and locale coverage. The tests use the pinned
regression snapshot and selected corpus pages; native parser thread counts
inside those tests are independent of pytest's two workers. Existing threading
tests cover one, two and four parser threads plus backpressure and unloading.
Keep the existing ground truth and tolerances when assessing failures.

After correctness passes, collect current real-workload scaling with the
existing launcher, before changing its memory reporting:

~~~sh
REPEATS=3 THREADS=1,2,4 RUN_COMPARISON=0 DATASET=bo20 \
  ./scripts/benchmarking/refresh_performance_benchmarks.sh
~~~

This supplies current native throughput/counts and fresh-process backend replay
peak RSS. The replay already records process peaks in each report.json. Native
peak RSS and sampled timelines are not yet implemented in that launcher.
The older Nov-11 bundle is historical context, not a matched before/after pair
for all later source/dependency changes. Assess any historical comparison only
after checking settings, libraries, inputs and compiled-source provenance.

Item 1's isolated performance attribution remains unresolved because the
historical reference had changed dependencies. Item 5 still needs the broad
results above and any remaining controlled real-document evidence. Item 6
retires comparison selectors/dead APIs after that acceptance. The requested
permanent benchmark peak-RSS/timeline implementation follows parser completion.

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
