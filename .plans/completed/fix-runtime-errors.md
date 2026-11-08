# Runtime failures: findings and investigation

Date: 2026-11-03

## Reported failures

1. Python benchmark:

   ```sh
   uv run python ./scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py --threads 4,8 --backend threaded
   ```

   The displayed progress stopped at 3/54584 pages, followed by a Python
   `multiprocessing.resource_tracker` warning about one leaked semaphore.

2. Native C++ benchmark:

   ```sh
   ./run_scaling.exe /Users/taa/Documents/projects/_data/bo767/pdf --mode render
   ```

   The first configuration, with one render worker, terminated with
   `zsh: trace trap` at displayed progress 33698/54730 (61.6%), after 21:11.
   The displayed completion count is not necessarily the failing page: workers
   render ahead and results are buffered.

The native failure establishes that Python is not required for at least one
failure. A trace trap suggests SIGTRAP; the crash report/debugger must establish
the actual signal, stack and cause. The earlier Python warning does not prove
that semaphore exhaustion caused that failure, or that both runs have the same
cause.

## Initial source-review findings

These are source-review findings, not yet confirmed explanations of either
reported termination.

### 1. The semaphore warning is probably a shutdown symptom

The installed `tqdm/std.py` creates a `multiprocessing.RLock` in
`TqdmDefaultWriteLock.create_mp_lock`, even for threaded progress bars. This is a
plausible source of the single named semaphore. CPython 3.12's resource tracker
reports registered resources left over at shutdown. Abrupt native termination
can bypass the finalizer that normally unregisters this lock.

The parser's worker pool uses C++ mutexes and condition variables, rather than
Python multiprocessing semaphores. Replacing tqdm's lock with a threading RLock
could avoid this resource in the threaded benchmark, but would not repair a
native crash and must not be mistaken for a root-cause fix.

### 2. Destruction can deadlock with a full results queue

Sources:

- `src/pybind/docling_threaded_base.h`: `~docling_threaded_base()`.
- `src/pybind/docling_threaded_renderer.h`: worker results-queue wait.
- `src/pybind/docling_threaded_parser.h`: equivalent producer wait.

The base destructor joins workers without cancellation. A worker can be blocked
waiting for space in the bounded results queue after the consumer has stopped.
Joining that worker then waits indefinitely.

Proposed fix: introduce idempotent cancellation/shutdown, stop taking new tasks,
wake both condition variables, allow blocked producers to abandon publication,
and join workers. Synchronize stop-state changes with the wait predicates.
Expose explicit Python `close()` and context-manager support. The benchmark
normally drains results in `measure()`'s finally block, so this defect alone
does not explain the reported abrupt exit.

### 3. Renderer members can be destroyed before workers are joined

Sources:

- `src/pybind/docling_threaded_renderer.h`: renderer-owned configuration,
  font resolver and embedded font cache.
- `src/pybind/docling_threaded_base.h`: worker join in the base destructor.

Derived members are destroyed before the base destructor runs. Destroying a
renderer while workers remain active can therefore destroy state still used by
those workers.

Proposed fix: call cancellation-and-join from the derived renderer destructor
before its members are destroyed; keep the base shutdown idempotent. This is an
early-destruction defect, not an established cause of an active one-worker C++
benchmark crash. The standalone benchmark uses a separate pool implementation.

### 4. Exceptions can escape the worker entry point

Source: `src/pybind/docling_threaded_renderer.h`, `worker_loop()`.

Worker cache construction, some result construction, queue insertion and other
operations sit outside the per-page try/catch. An escaping exception from a
`std::thread` entry point invokes `std::terminate` and bypasses normal Python
cleanup. The standalone pool in `app/run_scaling.cpp` also has operations outside
its per-page try/catch.

Proposed fix: guard the entire worker entry point, record a fatal worker
exception, propagate it to the consumer, and use scope-based cleanup for worker
counters and notifications. Ensure the consumer does not repeatedly return an
empty sentinel while unfinished-task counts remain positive. Exception handling
does not recover from SIGSEGV, SIGTRAP or an OS memory kill.

### 5. Connected-shape grouping can stall complex pages

Sources:

- `src/parse/pdf_decoders/page.h`: `get_connected_shape_bounding_boxes()`.
- `scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py`:
  `consume()` requests connected-shape bounds for each page.

Grouping repeatedly scans all shape bounding boxes. Disjoint shapes already
require quadratic work; merged components can require further scans. The binding
also holds the GIL during this query.

Proposed fix: use a spatial index to find candidate overlaps while preserving
the existing grouping semantics. This addresses apparent hangs, not the native
trace trap: the standalone C++ benchmark does not run this query.

## Differences between the benchmark paths

`app/run_scaling.cpp` owns a separate native worker pool. Its defaults include
render scale 2, result queue capacity 64 and no word/line creation. The Python
Docling-settings replay uses scale 1, queue capacity 32, creates words/lines,
materializes models and queries separator geometry. A shared native decoder or
renderer can fail in either path, but one successful path would not validate all
operations or lifecycle behavior in the other.

## Investigation plan

1. Read the macOS DiagnosticReports for both reported runs. Compare exception
   types, crashing threads, native frames and binary identities.
2. Use the existing executable and dataset to isolate the failing PDF/page,
   starting from crash evidence rather than repeating the entire 21-minute run.
3. Capture a debugger stack at the native trap. Distinguish explicit traps or
   assertions from memory corruption, allocation failures and worker exceptions.
4. Minimize the reproducer and compare parse-only, bitmap decoding and rendering
   when those comparisons help isolate the fault.
5. Propose a focused source patch for review before applying implementation
   changes. Keep diagnostic harnesses/build outputs separate where practical.
6. Validate the approved fix against the reproducer and relevant existing
   regression checks; record evidence and remaining uncertainties here.

## Investigation results

The user has authorized running investigative commands and creating this
document. Implementation changes remain subject to review and approval.

**Confirmed fault:** the pinned Blend2D/AsmJit ARM64 JIT pipeline can write past
a tightly packed A8 image while filling a fractional rectangle. A standalone
Blend2D-only canary reproducer detects 183 overwritten bytes beyond a 21 x 63
image; disabling JIT produces zero overwritten bytes. This reproducer needs no
PDF parser, Python, worker pool or multiprocessing semaphore.

Both suspect PDFs also fail under Guard Malloc in the same generated
masked-shape rasterization path. The original lifecycle findings remain useful
separate work, but fixing them will not repair this overwrite.

### Crash reports establish native heap corruption

Read these existing macOS reports under
`/Users/taa/Library/Logs/DiagnosticReports/`:

- `run_scaling.exe-2026-11-03-184635.ips`: SIGTRAP / EXC_BREAKPOINT;
  `BUG IN CLIENT OF LIBMALLOC: memory corruption of free block`.
  The allocator detects damage while allocating a vector of clipping paths in
  `shape_instruction` copying during page decoding. This is a detection site,
  not proof that the copying caused the corruption.
- `python3.12-2026-11-03-173651.ips`: the same SIGTRAP and allocator diagnosis,
  inside the native extension on a worker thread.
- `python3.12-2026-11-03-173130.ips`: SIGSEGV with an invalid pointer containing
  repeated `0x4b` bytes, consistent with overwritten memory.

The current native executable and extension UUIDs match those reports. Python
cleanup and exception-handling changes cannot repair this native memory damage.

### Short native reproducer: 1110594.pdf

The saved Python JSONL files contain substantially more pages than the pasted
progress display suggested: 6803 and 6887 rows in the two four-worker runs. Both
end inside the Hugging Face blob corresponding to `1110594.pdf` (307 pages).
The final serialized row is not necessarily the offending page: results are
unordered, buffered, and the output file itself may have buffered writes.

From `build/`:

```sh
./run_scaling.exe /Users/taa/Documents/projects/_data/bo767/pdf/1110594.pdf \
  --mode render --threads 1,4 --max-concurrent-results 32 --scale 1 \
  --enable-timing --timing-csv /private/tmp/docling-runtime-1110594.csv
```

Observed: the one-worker pass completed all 307 pages in about four seconds;
the four-worker pass terminated with SIGSEGV around displayed page 163, in less
than one second of that pass. Completion of one pass does not establish memory
safety. Repeating four workers alone under LLDB also failed around that region,
with a different eventual crash site, as expected for heap corruption.

The new report `run_scaling.exe-2026-11-03-185407.ips` ends in
`renderer<BLEND2D>::multiply_a8_by_a8()` called from the masked-shape paint path.

### Guard Malloc catches an invalid access inside a generated pipeline

Running the isolated PDF under LLDB with
`DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib` stops inside generated Blend2D
code called by `fill_clipped_edges` / `fill_unclipped_geometry`, while
`render_shape()` draws into an A8 coverage window.

In one capture, the faulting instruction was `ldr s2, [x4]` (four-byte load),
with `x4 = 0x1430ad73ffd` and the guard page starting at `0x1430ad74000`.
The registers and short stack were captured in the tool output; benchmark logs
are `/private/tmp/docling-runtime-guard.stdout` and
`/private/tmp/docling-runtime-guard.stderr`.

This localizes an invalid access to mask rasterization. It does not yet
establish whether the generated code, input geometry, or another lifetime error
is the first cause of the original overwrite.

### Diagnostic builds and limits

- `/private/tmp/docling-runtime-scaling-asan`: unchanged application source,
  Apple compiler sanitizers, existing uninstrumented dependencies. Its sanitizer
  runtime deadlocks before main on this host; sampled and stopped that process.
  The sample is `/private/tmp/docling-runtime-asan-startup.sample`.
- `/private/tmp/docling-runtime-scaling-asan-llvm`: unchanged application source
  built with local Homebrew LLVM 23.1.1 and ASan/UBSan, still using the existing
  uninstrumented dependencies. It completed the isolated four-worker run. This
  does not clear the native dependency: generated JIT memory accesses are not
  instrumented, and sanitizer allocation changes can hide ordinary heap damage.
- `/private/tmp/docling-runtime-blend2d-asan/`: a separate ASan/UBSan build of the
  pinned Blend2D and AsmJit sources, using LLVM 23.1.1 and the installed macOS
  15.5 SDK. No existing build outputs or dependency sources were changed.
- `/private/tmp/docling-runtime-a8-probe.cpp`: an independent canary-buffer
  probe for A8 fill-path, stroke-path and fill-all, widths 1..512 and heights
  1..5. Both the JIT and reference pipeline passed these simple geometries.
- `/private/tmp/docling-runtime-page-probe.cpp`: temporary sequential page/shape
  tracing harness used to isolate the failing page and operation.

### Page and operation isolated

The sequential tracing harness, run under Guard Malloc, fails on physical page
183 of `1110594.pdf`, at zero-based shape index 26. Running just that one page
also fails. Disabling `render_non_rect_clip_masks` lets the page complete, but
changes rendering and is only a diagnostic control.

The shape is a filled rectangle with page-space corners `(133.02, 47.7)` and
`(153.06, 109.80000000000001)`. The page box is 612 x 792 with /Rotate 90.
The renderer's coverage window is 21 x 63, with a tightly packed A8 stride of
21 bytes. `render_shape()` creates an A8 `BLContext` and fills the path into
that window. Unlike `rasterize_path_to_a8()`, which draws through PRGB32 and
extracts alpha, this path directly exercises the A8 drawing pipeline.

Temporary single-page harness commands:

```sh
DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
  /private/tmp/docling-runtime-page-probe \
  /Users/taa/Documents/projects/_data/bo767/pdf/1110594.pdf 183 183

# Diagnostic control: adds an argument that disables non-rectangular masks.
DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
  /private/tmp/docling-runtime-page-probe \
  /Users/taa/Documents/projects/_data/bo767/pdf/1110594.pdf 183 183 disable-mask
```

Observed exit codes from Python subprocess: original `-10` (SIGBUS under Guard
Malloc), mask-disabled `0`. Logs:
`/private/tmp/docling-runtime-page183-masked.log` and
`/private/tmp/docling-runtime-page183-mask-disabled.log`.

### Standalone Blend2D-only heap-overwrite reproducer

Source: `/private/tmp/docling-runtime-a8-repro.cpp` (no docling headers).
It wraps a zero-initialized 21 x 63 A8 buffer with stride 21 and trailing
canaries, translates by `(-133, -682)`, and fills the rectangle above in
canvas coordinates. A 4096-byte trailing canary region contains the overwrite
so the experiment can inspect it without damaging neighboring allocations.

Build from the repository root with the same pinned dependency archive:

```sh
/opt/homebrew/opt/llvm/bin/clang++ \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk \
  -std=c++20 -O1 -g -DBL_STATIC -I build/_deps/blend2d-src \
  /private/tmp/docling-runtime-a8-repro.cpp \
  build/_deps/blend2d-build/libblend2d.a \
  -o /private/tmp/docling-runtime-a8-repro

/private/tmp/docling-runtime-a8-repro
# pipeline=JIT damaged_bytes=183; exit 1

/private/tmp/docling-runtime-a8-repro reference
# pipeline=reference damaged_bytes=0; exit 0
```

The initial generic canary probe used four-byte-aligned rows and passed. Once
it matched the library's actual tightly packed stride, the overwrite became
visible. This distinction is material: allocating row padding can hide the bug
and must not be used as evidence of a repair. The precise faulty JIT-generation
instruction sequence still needs reduction for an upstream dependency fix.

### Original C++ corpus crash region independently reproduced

Counting the current local corpus in the executable's lexicographic order gives
54730 pages, matching the reported run. Global pages 33612..33768 belong to
`1662984.pdf` (157 pages), so displayed progress 33698 is approximately its
page 87; producer look-ahead and delayed heap detection prevent assigning an
exact failing page from that count alone.

An ordinary isolated run of this PDF at scale 2 and one worker completed. Under
Guard Malloc it stops around displayed page 89 in generated code called by:

```text
fill_clipped_edges
  -> fill_unclipped_geometry
  -> fill_geometry_rgba32_impl
  -> render_shape() masked coverage drawing
  -> threaded_benchmark::worker_loop()
```

The faulting generated instruction in this capture is `ldr d0, [x4]`, an
eight-byte load crossing into a guard page. This is the same operation family
isolated with the other PDF. Logs:
`/private/tmp/docling-runtime-1662984-original.log`,
`/private/tmp/docling-runtime-1662984-guard.stdout`, and
`/private/tmp/docling-runtime-1662984-guard.stderr`.

### Controls and candidate validation

| Experiment | Observed result |
| --- | --- |
| `1110594.pdf`, parse without bitmaps, 1 and 4 workers | 307 pages each, exit 0 |
| `1110594.pdf`, parse with bitmap decoding, 1 and 4 workers | 307 pages each, exit 0 |
| `1110594.pdf`, original rendering, 4 workers | SIGSEGV |
| Page 183, original sequential renderer under Guard Malloc | SIGBUS at shape 26 |
| Page 183, clipping masks disabled, Guard Malloc | Exit 0; output semantics changed |
| Blend2D-only A8 canary probe, JIT | 183 trailing bytes changed |
| Same probe, reference pipeline | Zero trailing bytes changed |
| Page 183, candidate reference-pipeline A8 context, Guard Malloc | Exit 0; masks remain enabled |
| All 307 pages, candidate sequential renderer, Guard Malloc | Exit 0; all pages completed |
| `1662984.pdf`, original native pool, 1 worker, scale 2, Guard Malloc | Invalid access in generated masked-shape pipeline |

The application and Blend2D ASan/UBSan build completed `1110594.pdf` but also
reported null-pointer arithmetic in `edgebuilder_p.h:190` and an incompatible
function-pointer call in `path.cpp:1845`. Those are separate dependency UB
findings; neither is established as the cause of this heap overwrite. JIT
loads/stores are not instrumented by ASan, so its completion does not contradict
the canary and Guard Malloc evidence.

### Proposed immediate patch, pending approval

Keep masks enabled and use Blend2D's reference pipeline only for the A8 coverage
drawing context in `src/render/blend2d_renderer.h`, inside `paint_masked`:

```diff
       {
-        BLContext cctx(coverage);
+        // The pinned ARM64 JIT can overrun tightly packed A8 coverage images.
+        BLContextCreateInfo coverage_options{};
+        coverage_options.flags = BL_CONTEXT_CREATE_FLAG_DISABLE_JIT;
+        BLContext cctx(coverage, coverage_options);
         cctx.clear_all();
         cctx.translate(-mask_area.x, -mask_area.y);
         draw(cctx);
         cctx.end();
       }
```

The candidate was tested using only a temporary header copy at
`/private/tmp/docling-runtime-reference/include/render/blend2d_renderer.h` and
`/private/tmp/docling-runtime-page-probe-reference`. Repository implementation
files and existing build outputs are unchanged.

This is a targeted mitigation for the dependency defect. It may affect the
performance of masked-shape coverage generation. Pixel parity and performance
have not yet been measured; the full corpus has not been rerun with it. An
alternative is to rasterize coverage in PRGB32 and extract alpha, following the
existing helper. The long-term repair is a validated Blend2D/AsmJit JIT fix and
dependency pin update, with the independent narrow-A8 canary case retained as
a regression reproducer.

After approval: apply the focused patch, add meaningful regression coverage for
narrow clipped shapes, run the existing clipping/rendering checks, and exercise
both isolated PDFs with the real benchmark pool at the original thread counts
and scales. Keep lifecycle/cancellation fixes separate from the confirmed
memory-corruption fix.

## Root cause and dependency fix (2026-11-03)

The overwrite is in `inline_fill_span_loop()` (`blend2d/pipeline/jit/fetchutilsinlineloops.cpp`),
4-byte-granularity path for 16-byte vectors (AArch64; also x86 without AVX2). The analytic
filler sends opaque solid `SrcCopy` spans there with granularity 4. The alignment step rounded `dst`
down to 16 bytes, which also cleared the two LSB bits. When an A8 row does not start 4-byte aligned
(stride 21), the remaining count stops being a multiple of 4. The final 4-byte store then overruns
the span by 1-3 bytes, and `dst` drifts by that much on every fully covered row: 62 rows x 3 = 183
bytes for the reproducer. The `vec_size >= 32` path already preserved those bits.

Fix in `.references/blend2d` (`docling/0.21.2-patches`, uncommitted): the alignment mask clears only
the bits above the granularity. That leaves 32-bit pixels unchanged. Stores after alignment are
unaligned unless `item_size == 4`. `inline_copy_span_loop()` gets the same change. New unit test:
`context_a8_unaligned_rows`. Upstream 0.21.3 still contains the defect.

Verified in the scratchpad (no repository build outputs touched):
- Standalone reproducer: 183 damaged bytes before the fix, 0 after.
- JIT-vs-reference sweep over 115,200 cases (A8/PRGB32, widths 1..96, stride padding, row offsets,
  path/box/blit fills): 3,269 overruns and 10,629 mismatches before, 0 overruns and 0 analytic-fill
  mismatches after. 900 mismatches remain for clipped aligned blits at a fractional clip
  (1.3, 0.9), for both formats. They are identical before and after the fix and none write out of
  bounds; this is a separate pre-existing difference.
- `bl_test_runner`: the new test fails without the fix, and all 61 units pass with it.

With the dependency pin updated, the `BL_CONTEXT_CREATE_FLAG_DISABLE_JIT` mitigation in
`paint_masked` is no longer needed.
