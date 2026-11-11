# PR #337 improvement plan

## Scope and integration status

Improve the performance and thread scaling of the tagged-PDF structure and marked-content APIs introduced by [PR #337](https://github.com/docling-project/docling-parse/pull/337), while preserving their semantics.

Reviewed PR head: `9e3b4eb9642576c1a39ba897037c287c85d0a033`.

Pre-feature baseline: `e90fee2` (7.24.2).

PR #337 was merged upstream as `7a920a5`. The user pulled that revision and created `fix/pr-337-improvement`. Implementation is based on that merged feature, not the old fork branch.

The merged feature was rebuilt before optimization: its five structure tests and unit suite passed (316 passed, 2 skipped). The pre-feature and merged-feature sources were also built as isolated extensions for comparative measurements.

The user authorized implementation of this plan. Supporting analysis is in [pr-337-review.md](pr-337-review.md). Implementation details, benchmark results, and validation limits are recorded in [pr-337-improvement-results.md](pr-337-improvement-results.md).

Implementation status:

- Baselines and benchmark driver: implemented; pre-feature, merged-feature, and improved builds measured at 1/2/4/8 workers, including concurrent annotation requests and an alternating-build no-output check.
- Unused tree work and JSON copies: removed.
- Separate lazy structure extraction: implemented with once publication, document ownership snapshots, QPDF synchronization, and its own timing.
- Attribute serializer: object-identity guard, propagated depth, document exclusions, and a per-value node budget implemented.
- EMC tagging scans: replaced with effective stack state and one pass over each text-show operation's newly emitted cells.
- Compact cell metadata: implemented as stream-interned immutable payloads shared by cells; native cell size is 408 bytes on the measured ABI, versus 440 for the merged feature.
- Batched Python export and caches: implemented with explicit shared-mutable-result semantics and unload/reload invalidation.
- Validation: 338 unit/focused-outline cases pass with two skips; native concurrency/JSON tests and repository Python/lock checks pass. Parser regression and sanitizer limitations are detailed in the results document.

Changes remain uncommitted for review. Cross-platform measurements, separately instrumented allocation counts, and validation on a working ThreadSanitizer host remain follow-up validation rather than completed claims.

## 1. Establish a reproducible baseline

Before optimizing, capture the integrated feature's behavior and cost using a Release build and a fixed compiler, dependency set, dataset revision, configuration, and result-queue capacity.

Measure the pre-feature baseline and the merged feature separately. Record:

- Load duration and time to first result.
- Structure extraction duration and serialized size.
- Native page extraction and decode durations.
- Python annotation and marked-content conversion durations.
- End-to-end pages per second and peak RSS.
- Native cell size on each measured platform.

Use fresh processes for memory measurements, warm-up runs where appropriate, and repeated measurements reporting medians and spread. Include both worker-only timing and full result consumption. Do not infer scaling from decode timings alone.

The minimum workload matrix should cover untagged text-heavy PDFs, large tagged trees, a one-page selection from a long document, nested marked-content sequences, and signed tagged files. Measure 1, 2, 4, and 8 workers where hardware permits. Test page-only consumption, marked-content consumption, and repeated annotation requests separately.

Deliverable: a benchmark script and a baseline results table, including exact commands and configuration. The review's 384-to-440-byte cell measurement is ABI-specific and must not be treated as a measured RSS or runtime increase.

## 2. Remove unnecessary tree work and copies

Start with low-risk changes in `src/parse/qpdf/structure.h`:

1. Check for a usable `/StructTreeRoot` before building the page-number map. Untagged documents should return without allocating that map.
2. Move completed JSON temporaries into parent objects and arrays when they are no longer needed. Cover child elements, children arrays, root elements, attributes, and the final elements array.
3. Iterate over `/K` arrays directly where practical instead of first copying all handles into temporary vectors.
4. Preserve the global visited set and depth limit. Unlike #410's current-path guard, this visited set grows with the whole tree; choose any replacement container from profiling, not by analogy with a small recursion path.

Validate that structure JSON and Python models remain equivalent, including order, references, attributes, and rotated bounding boxes. Compare untagged startup, large-tree allocation costs, and deep-tree extraction times against phase 1.

Deliverable: an independent optimization commit with before/after results and unchanged semantic tests.

## 3. Make structure extraction separately lazy

Separate structure initialization from the existing annotation bundle in `src/parse/pdf_decoders/document.h` and expose an explicit native structure accessor through `app/pybind_parse.cpp`.

Define compatibility behavior before implementation:

- Page-only loads should not build structure JSON.
- `get_structure()` should initialize structure once and return the cached result.
- Metadata and outline access should not implicitly force structure extraction.
- Preserve the documented structure field in public annotation results. If `get_annotations()` continues to mean the complete bundle, it may explicitly trigger structure initialization; document that cost.
- Distinguish not-yet-initialized from initialized-but-absent structure.

Use per-document once initialization or equivalent synchronized publication. Take the existing document mutex while reading the shared QPDF instance. Completed cache reads should avoid acquiring the page-buffer mutex. Keep published cache data immutable and define exception behavior deliberately.

Coordinate this work with #410: that PR makes annotations lazy as a bundle, which does not provide independent structure laziness. Avoid layering duplicate initialization state or unsafe plain-boolean fast paths. Add a dedicated structure timing entry that is recorded when extraction actually happens.

Test concurrent first requests, cached requests during page extraction, absent structures, failures, and unload/reload behavior. Release the GIL around native work only after checking document ownership, map access, and unload synchronization; restore it before Python conversion.

Deliverable: an explicit lazy API contract, synchronized implementation, and concurrency tests with multiple workers and multiple pages.

## 4. Bound attribute serialization work

Fix the generic serializer's depth propagation and costly cycle-guard behavior before relying on it for arbitrary structure attributes. Reuse #410's serializer improvements if that change is integrated, adapting `get_attributes()` to the new call signature.

Define which compound attribute values are supported and how skipped document objects and depth exhaustion are represented. Consider an output-node budget if depth bounds alone still allow excessive breadth.

Test shallow valid attributes, deeply nested values, cycles, shared subgraphs, and references into document structure. Confirm that the intended attribute depth limit is applied at every recursive call and that normal attributes retain their values.

Deliverable: bounded traversal behavior with regression tests and large-attribute measurements.

## 5. Replace repeated EMC range scans

Prototype effective marked-content state on the existing BMC/BDC/EMC stack. Record the effective MCID and artifact metadata when each glyph cell is created, so closing enclosing spans does not repeatedly scan their cells.

Preserve these behaviors explicitly:

- The innermost applicable MCID wins; an inner span without one inherits the appropriate enclosing state.
- Artifact metadata and MCID state are tracked independently.
- ActualText replacement and removal preserve the linkage of surviving cells.
- Form XObjects, malformed nesting, and unmatched EMC follow defined behavior.

Do not mix in named-property-list support unless needed for correctness of this refactor; that is a separate feature change.

Benchmark equal glyph counts at increasing nesting depths. The target is tagging work proportional to glyph creation plus stack operations rather than glyph count multiplied by enclosing tagged depth.

Deliverable: a tagging refactor with nested-span and ActualText tests, including multipage worker parity.

## 6. Reduce native cell metadata storage

Use phase 1 measurements to choose between compact metadata identifiers and a sidecar indexed by character cell. A practical prototype can intern artifact type/subtype strings per page and keep compact identifiers per cell, with absence represented cheaply.

Keep the exposed `PdfCell` and marked-content APIs stable where feasible. Resolve identifiers at the API boundary. Avoid a process-global intern table that would introduce synchronization or unbounded lifetime.

Account for metadata through cell copying, merging, filtering, reordering, ActualText substitution, and word/line contraction. If choosing a sidecar, define index alignment and ownership at each transformation. Derived word/line cells need a documented policy for mixed source linkage.

Measure `sizeof(PAGE_CELL)`, allocations, RSS, and throughput with character, word, and line outputs enabled and disabled. Ensure that untagged workloads benefit too; a runtime extraction switch alone cannot remove inline string-object storage.

Deliverable: a measured storage design and a separate implementation commit after the alignment and ownership rules are tested.

## 7. Batch and cache Python export

Replace Python's per-cell bound-property loop with a native batch-gathering API. Prefer compact arrays or records that avoid crossing the binding boundary for each coordinate and metadata field. Preserve the public Pydantic return types through an adapter if required.

Gather native data without the GIL only when ownership is safe. Python object construction still needs its own accounting and should not be described as GIL-free.

Cache repeated marked-content materialization where decoder/configuration lifetime permits. Cache threaded annotation models by document identity and invalidate them on unload or replacement. Decide whether callers may mutate returned models and whether repeated calls return the same object.

Provide a cheap empty result for pages known to have no linkage. Ensure that export works with the intended native character-cell retention configuration and document that Python char-cell materialization can be skipped independently.

Measure repeated getters, full-page tag export, consumer throughput, and time workers spend waiting for result-queue space.

Deliverable: batched export and explicit cache lifetime rules, with end-to-end consumer benchmarks.

## Acceptance and delivery order

Implement phases as independently reviewable commits: baseline, unused work/copies, lazy extraction, serializer bounds, tagging algorithm, compact storage, and batched export. Re-measure after each relevant change. Use the baseline results to decide whether the more invasive storage and export changes are justified.

Acceptance requires:

- Existing semantic and relevant parser regression tests pass without unexplained output changes.
- Multithreaded results match single-worker results for the exercised tagged workloads.
- No full-tree extraction occurs on page-only loads after the lazy change.
- Untagged files do not allocate the structure page map.
- Deep marked-content nesting no longer causes repeated full-span tagging scans after the tagging refactor.
- Runtime, RSS, and throughput results are reported against both the merged feature and the pre-feature baseline, with measurement spread. Treat regressions beyond the measured noise as findings to resolve or explicitly accept.
- Concurrency changes are exercised under ThreadSanitizer where the platform and dependencies support it, in addition to functional tests.

Keep correctness fixes separate from representation changes where possible. Record any necessary API or output compatibility changes and their migration path before adopting them.
