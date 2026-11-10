# PR #337 implementation and measurements

Branch: `fix/pr-337-improvement`, based on merged PR commit `7a920a5`.

## Implemented behavior

- Structure extraction is independent of load, metadata, and outline access. The first explicit structure or complete-annotation request builds it once per document. Missing and failed structures are cached as null; ordinary extraction exceptions are logged. A separate `extract_structure` timing is recorded on that first request.
- First extraction holds the existing document mutex because QPDF resolves objects lazily. Completed structure-cache reads use `call_once` publication and avoid the page-buffer mutex. Bindings capture shared document ownership while holding the GIL, then release it for native work. Direct decoding against the shared QPDF also takes the document mutex.
- Untagged structure requests check for a usable root before allocating a page-number map. JSON assembly moves completed subtrees, and child arrays are traversed without intermediate handle vectors.
- Attribute serialization uses indirect object identity, one current-path guard, propagated depth limits, document-object exclusions, and a 4,096-node budget per attribute value. Values have four processed levels. Truncated values use `[exceeding recursion]` or `[exceeding node budget]`; document references use the existing skip-marker style. The general annotation serializer has no new finite node budget.
- Marked-content state is resolved on the existing stack. Each text-show operation stamps only its newly emitted cells, before ActualText processing. Form XObjects inherit effective caller state. EMC no longer rescans enclosing cell ranges.
- Artifact type/subtype strings are immutable shared metadata, interned per stream. Cells own their metadata through shared pointers, so copying, contraction, and retaining cells after unload do not depend on a document-wide registry. Ordinary cells allocate no artifact payload.
- Python `PdfCell` properties and the three JSON fields remain available. Internal C++ artifact-string fields have been replaced by `get_artifact_type()` / `get_artifact_subtype()` accessors. Word/line contraction retains the first source cell's linkage, matching the previous behavior.
- Native marked-content export batches text, geometry, and linkage into records. Python still constructs the public Pydantic models. Repeated page/result getters share a cached mutable tag list. Repeated threaded annotation getters share a cached mutable model, cleared on successful reload and unload. Outline getters preserve the same tree instance exposed by annotations.
- Page unload also evicts a native decoder created only for marked-content access; subsequent capability upgrades rebuild it correctly.

## Native cell layout

Measured with the local Apple Clang/libc++ ABI:

| Revision | Bytes per native cell |
|---|---:|
| Before feature, `e90fee2` | 384 |
| Merged feature, `7a920a5` | 440 |
| Improved implementation | 408 |

This removes 32 bytes per cell relative to the merged PR, or 7.3% of the object size. It still adds 24 bytes relative to the pre-feature representation. These figures are object layouts, not predictions of whole-process RSS.

## Benchmark method

The driver is [scripts/benchmark_structure.py](../../scripts/benchmark_structure.py), with fixtures in [tests/structure_fixtures.py](../../tests/structure_fixtures.py). Raw samples, medians, and min-to-max spread are in [pr-337-evidence](../pr-337-evidence/).

Measurements used macOS arm64, Apple Clang 17, CPython 3.12.11, and Release C++20 builds with `-O3`. Pre-feature and merged-feature sources were extracted into isolated directories and built as separate extension modules using the same CMake compiler settings and installed native dependencies. Their only instrumentation change exposed `sizeof(PAGE_CELL)`. Each used its corresponding Python wrapper. Fixture generation is shared between revisions; SHA-256 digests are included in each sample.

Each reported median uses three fresh processes after one discarded warm-up process, with a fixed result-queue capacity of four and 1, 2, 4, or 8 workers. Timings start after parser construction and fixture creation; they include load, worker startup, result consumption, requested tag export, and requested annotation getters. Imports and initial font-resource setup are outside that timer. Peak RSS covers the process. These are local synthetic results, not a production-corpus performance claim.

Defaults are 16 pages, 50 text-show operations per page, and 128 structure elements per page. Deep trees use chains of 32 elements. Nested marked content uses 256 enclosing spans. Signed input uses four pages and 32 elements per page; its signature references the catalog. The selected-page case loads the full 16-page document but schedules one page. Annotation cases request the annotations five times.

## Merged feature versus improved implementation

Times below are median total milliseconds. RSS is median peak MiB with eight workers.

| Workload | 1 worker: merged → improved | 8 workers: merged → improved | RSS: merged → improved |
|---|---:|---:|---:|
| Untagged, pages only | 24.42 → 23.13 | 7.03 → 6.99 | 138.8 → 134.8 |
| Tagged, pages only | 41.19 → 23.43 | 23.77 → 7.15 | 150.0 → 137.7 |
| Tagged, repeated annotations | 151.99 → 50.70 | 118.19 → 34.73 | 170.7 → 158.0 |
| Deep tree, repeated annotations | 212.89 → 55.03 | 196.83 → 39.59 | 171.5 → 160.2 |
| Nested spans, tag export | 98.42 → 62.57 | 97.02 → 62.15 | 172.0 → 152.5 |
| Artifact text, tag export | 95.37 → 58.78 | 100.95 → 62.41 | 167.2 → 151.5 |
| Signed, pages only | 12.91 → 7.32 | 8.92 → 4.59 | 115.8 → 114.3 |
| One selected page | 21.16 → 3.42 | 20.23 → 3.47 | 116.1 → 103.6 |
| Untagged, char/word/line outputs disabled | 6.55 → 7.03 | 3.18 → 3.08 | 100.5 → 100.6 |

Tagged page-only load time falls from 17.27 ms to 0.38 ms with one worker. Deep-tree load time falls from 80.95 ms to 0.38 ms. The structure cost is deferred to explicit requests, not eliminated for consumers that need it.

The initial no-cell-output result has a small one-worker slowdown of about 0.48 ms. Its total-time sample spread is 0.13 ms for the merged build and 0.47 ms for the improved build. A follow-up alternating-build comparison with seven samples and 64 pages did not reproduce a slowdown: one-worker medians were 21.20 ms merged versus 21.10 ms improved, and four-worker medians were 8.23 ms versus 8.20 ms. The sample ranges overlap. Raw data is in `interleaved-no-cells.json`. This workload should remain visible in future performance checks.

An additional workload starts workers, requests annotations before consuming results, and then repeats annotation access. With eight workers, first-result latency is 29.74 ms merged versus 23.37 ms improved, and total time is 118.61 ms versus 32.25 ms. The explicit first request still adds significant latency compared with the page-only path: lazy extraction is a choice to defer work, not a guarantee that requested structure is free or never blocks page extraction.

## Scaling and the pre-feature comparison

For tagged pages without structure consumption, end-to-end speedup from one to eight workers improves from 1.73× to 3.28×. The pre-feature build achieves 3.27× on the same input. Its one-worker total is 23.56 ms, compared with 23.43 ms for the improved implementation. That comparison supports restoring page-only performance on this workload; it does not establish equivalence on every document.

For untagged page-only input, the corresponding speedups are 3.48× for the merged build, 3.31× for the improved build, and 3.61× before the feature. The small absolute differences and sample spread do not support claiming better untagged scaling.

Tag-heavy consumption remains nearly flat as worker count increases. The batch API removes repeated bound-property calls, but constructing one Pydantic tag and rectangle per character still runs under the GIL. Absolute time improves, but additional native workers cannot parallelize that consumer. A future columnar or span-based consumer API could address this remaining cost; changing the public return type was deliberately avoided here.

The pre-feature version has no structure/tag feature. Its annotation timings therefore do not represent an equivalent semantic workload and should not be used to claim feature-export speedups.

## Validation

- The merged feature baseline passed 316 unit tests with two skips before optimization.
- The implementation's final unit and focused outline run passed 338 tests with two skips. It includes 19 new structure behavior cases: lazy/absent initialization, concurrent parser and renderer readers, shared-QPDF decoding, nested tags, ActualText, Form inheritance, cache/reload/unload behavior, retained cell metadata, attribute cycles/depth limits, and a shared-DAG node budget.
- The full parser regression run passed 43 cases and found one outline object-identity failure. That was fixed with an independent outline cache; all three outline regression cases pass in the final focused run. The complete 44-case regression command was not repeated after that targeted fix.
- Native `structure_concurrency` passes against both an eight-page tagged fixture and a one-page artifact/marked-content fixture. It combines eight structure readers with direct shared-QPDF decoding and checks new and legacy cell JSON round trips.
- Repository-wide Ruff lint and formatting, MyPy, and `uv lock --check` pass. No dependency definitions or lock entries were changed.
- ThreadSanitizer could not run on this host: even an empty instrumented program exits with SIGSEGV, and running the harness outside the sandbox has the same result. Native functional concurrency tests pass, but there is no successful sanitizer result. Linked third-party libraries would also need instrumentation for a full sanitizer audit.

## Reproduction

Build the extension and native harness:

```sh
cmake -S . -B build -DDOCLING_PARSE_BUILD_STRUCTURE_TESTS=ON
cmake --build build --target pdf_parsers structure_concurrency -j 4
cmake --install build
```

Run a representative benchmark:

```sh
.venv/bin/python scripts/benchmark_structure.py --case tagged --pages 16 --elements 128 --output /tmp/tagged-pages.json
.venv/bin/python scripts/benchmark_structure.py --case tagged --pages 16 --elements 128 --tree-depth 32 --consume annotations --output /tmp/deep-annotations.json
.venv/bin/python scripts/benchmark_structure.py --case tagged --pages 16 --consume annotations --annotations-during --output /tmp/concurrent-annotations.json
```

The driver also supports external `--pdf` inputs, `--first-page`, character/word/line computation switches, nesting depth, queue capacity, repeat count, and worker counts. Use the same driver, fixture helper, dependency revisions, and options in each comparison checkout. `--revision` labels archive builds that have no Git metadata.

Generate a native harness input and run it:

```sh
.venv/bin/python -c 'from pathlib import Path; from tests.structure_fixtures import fixture; Path("/tmp/structure.pdf").write_bytes(fixture("tagged", 8, 1, 64))'
build/structure_concurrency /tmp/structure.pdf
```

## Remaining limits

Measurements cover one compiler/ABI and synthetic documents. Allocation counts were not instrumented separately from object layout and RSS. Real corpus benchmarks, other supported platforms, and a working sanitizer host remain useful follow-up validation. The existing per-document page extraction mutex and serial Python model construction still impose scaling limits.
