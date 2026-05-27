# Threaded Parser Abort And Cancellation Plan

**Status:** Proposed  
**Last updated:** 2026-05-27  
**Scope:** `docling-parse` threaded parser and threaded renderer lifecycle

This document describes the current unload and iteration behavior of the threaded parser stack, the operational gap it leaves for long-running workloads, and a concrete plan for adding explicit cancellation of one document or all documents.

---

## Context

The threaded parser and threaded renderer are designed for high-throughput page processing:

- pages are scheduled up front
- worker threads decode or render pages in parallel
- a bounded result queue limits memory growth
- the Python caller consumes results in completion order

That design is appropriate for batch completion workflows, but it becomes operationally awkward for large PDFs and long-running jobs:

- a user may only want the first few pages
- an orchestrator may need to enforce timeouts
- an interactive CLI user may press `Ctrl+C`
- a service may need to stop processing one document while continuing others

Today the threaded stack does not support those cases as first-class behavior.

---

## Why Abort Support Is Needed

Abort support is needed because the current API assumes that once iteration starts, all scheduled work should run to completion.

For small documents this is usually acceptable. For long documents or expensive rendering configurations it is not:

- abort latency can become unbounded from the Python caller's perspective
- queued work that is no longer wanted still runs
- workers can continue producing results even after the caller intends to stop
- unload cannot be used as an escape hatch during active iteration
- blocking waits inside native code make `KeyboardInterrupt` handling poor

In practice this means the threaded stack is good at draining work, but not at stopping work.

---

## Current Behavior

The current behavior is internally consistent and appears to be designed intentionally as a drain-only model.

### Thread startup and scheduling

- documents and selected pages are loaded before iteration starts
- the first `has_tasks()` call builds the task queue and starts workers
- tasks are represented as `(doc_key, page_number)` pairs in a shared queue

### Worker execution

- workers run until the task queue is empty
- there is no stop flag or cancelled-document state
- each worker decodes or renders a page, then pushes a result into the bounded `results_queue`
- if the result queue is full, workers block until the consumer drains it

### Consumer execution

- `iterate_results()` loops on `has_tasks()` and `get_task()`
- `get_task()` blocks until a result is available or all workers have exited
- Python bindings release the GIL while waiting in `get_task()`

### Unload semantics

- `unload_document()` and `unload_all_documents()` explicitly reject active iteration
- active iteration is currently defined by `tasks_remaining > 0`
- once all scheduled results have been consumed, unload clears document state, joins worker threads, resets queues, and returns the parser to a pre-start state

### Page-level cleanup

- in the threaded backend used by `docling`, `ThreadedDoclingParsePageBackend` holds a `PageParseResult`
- `page_backend.unload()` is a no-op
- per-page native ownership is therefore not part of an explicit unload model

---

## What This Design Gets Right

The current design has useful properties for batch processing:

- it is simple to reason about for full-drain workloads
- unload only happens after a clean completion boundary
- result queue backpressure prevents unbounded memory growth
- the worker lifecycle is deterministic once all tasks are consumed

None of that is inherently wrong. The issue is that those semantics are the only semantics currently available.

---

## Gap In The Current Model

The missing capability is explicit cooperative cancellation.

More specifically, the current implementation cannot express:

- stop scheduling or processing a specific document
- stop all queued and future work globally
- wake a blocked consumer because work has been cancelled
- wake workers blocked on a full result queue because the consumer is exiting
- distinguish queued work from in-flight work from consumable results
- report that iteration ended because of cancellation rather than normal drain

This gap matters most for:

- very long PDFs
- large multi-document batches
- service environments with timeout or cancellation propagation
- CLI or notebook workflows that need responsive interruption

---

## Design Goal

Add explicit, cooperative cancellation to the threaded parser and threaded renderer without changing the core throughput-oriented architecture.

The goal is not hard preemption of native page decoding. The goal is:

- no new tasks start after cancellation is requested
- queued tasks can be dropped
- in-flight tasks are allowed to finish
- finished results for cancelled work can be discarded
- blocked waiters are woken promptly
- Python can observe cancellation and exit iteration cleanly

Worst-case abort latency should therefore be bounded by the runtime of the currently executing pages, not the runtime of the full scheduled workload.

---

## Proposed Public Behavior

### New native operations

Add explicit cancellation operations to both threaded implementations:

- `abort_document(doc_key: str) -> bool`
- `abort_all() -> None`

These should be exposed both in the native pybind layer and in the public Python `DoclingThreadedPdfParser` wrapper.

### Cancellation semantics

- cancelling a document prevents any queued pages for that document from starting
- cancelling all documents prevents any queued pages from starting
- in-flight pages may still complete
- results produced after cancellation may either be surfaced as cancelled terminal results or discarded internally, but the behavior must be explicit and documented
- unload becomes valid after cancellation has quiesced the relevant work, not only after normal completion

### Iteration semantics

Blocking result retrieval must become cancellation-aware. Two viable shapes are:

- `get_task(timeout_ms=...)` with polling from Python
- an internal timed wait loop that periodically checks Python signals and cancellation state before waiting again

The key requirement is that a blocked consumer can observe abort and exit without waiting for full workload drain.

---

## Required Internal Changes

### 1. Add explicit cancellation state

The threaded base needs native state for:

- global abort requested
- per-document abort requested

This state should be visible to both workers and the consumer path.

### 2. Separate lifecycle accounting

`tasks_remaining` currently behaves like "results still expected by the consumer". That is enough for drain-only behavior, but not enough for cancellation.

The implementation needs to distinguish at least:

- queued tasks not yet started
- tasks currently in flight
- results ready for consumption
- results still expected under the current cancellation state

Without that separation, unload, abort completion, and iterator termination cannot be defined precisely.

This point needs to be made explicit before implementation starts: today `tasks_remaining` is initialized from the full scheduled queue size and decremented in `get_task()` on the consumer side. That works only if every scheduled task eventually produces a consumable result.

Once cancellation can drop queued tasks, that assumption no longer holds. If a cancelled task is skipped and no result is ever pushed, `tasks_remaining` must still be decremented or otherwise accounted for, or the consumer side will stall:

- `has_tasks()` will continue to return `True`
- `get_task()` can block waiting for results that will never arrive
- unload and iterator termination conditions become unreachable

The implementation therefore needs an explicit rule for dropped tasks. The recommended rule is:

- when a worker observes that a task should be skipped because its document or the full parser has been cancelled, that worker must retire the task explicitly
- retiring a task must decrement the same logical "results still expected" accounting that normal `get_task()` consumption decrements
- the accounting update must happen even though no result object is ever enqueued

This can be implemented either by keeping `tasks_remaining` as the canonical "deliverable-or-retired tasks still outstanding" counter, or by replacing it with clearer split counters. The important contract is that every scheduled task must reach exactly one terminal accounting path:

- consumed as a real result
- retired as skipped due to cancellation
- retired as otherwise undeliverable terminal work

If that invariant is not established, cancellation can leave the parser in a permanently active state from the consumer's perspective.

### 3. Make worker loops cancellation-aware

Workers should check cancellation:

- before taking a task from the queue
- after taking a task but before expensive decode/render work
- while waiting for result-queue capacity

This avoids the current behavior where a worker can remain blocked on `cv_results_consumed` even though the consumer has already decided to exit.

### 4. Wake all blocked waiters on abort

Abort must notify both condition variables:

- `cv_results_available`
- `cv_results_consumed`

Otherwise:

- the consumer may stay blocked waiting for results that will never matter
- workers may stay blocked waiting for queue capacity that will never be used

### 5. Define terminal iteration behavior

The system needs a clear terminal condition for cancelled iteration.

Examples:

- no more deliverable work remains for the requested document set
- global abort requested and all in-flight workers have quiesced

That condition must be distinct from normal queue drain.

### 6. Add explicit page-result release

`PageParseResult` currently owns page-decoder and optional rendered-image state, while the `docling` backend's page unload is a no-op.

Adding an explicit `release()` operation to page results would make ownership clearer and would allow callers to drop native resources promptly, regardless of whether iteration ends normally or via abort.

This is not a substitute for cancellation, but it complements the lifecycle model.

### 7. Keep in-flight cancellation cooperative

The implementation should not attempt unsafe hard interruption of native decode/render operations.

Instead, document and preserve the cooperative model:

- queued work can be skipped
- in-flight work may finish
- abort latency is bounded by the slowest in-flight page

That tradeoff is much easier to make correct than hard preemption.

---

## Python Integration Plan

The public Python wrapper should mirror the native behavior explicitly.

### Wrapper additions

Add to `DoclingThreadedPdfParser`:

- `abort(doc_key: str) -> bool`
- `abort_all() -> None`

### Iteration changes

`iterate_results()` and `get_task()` should become capable of stopping promptly when:

- the caller requested abort
- Python signal handling wants to interrupt the wait
- no more deliverable results remain after cancellation

### Downstream integration in `docling`

Once `docling-parse` exposes explicit cancellation, the threaded backend in `docling` can:

- catch `KeyboardInterrupt`
- request parser abort
- stop iterating without waiting for full drain
- unload once cancellation has quiesced native work

That is the path required to make `Ctrl+C` behave like an intentional stop rather than a full background drain.

---

## Open Questions

The implementation should settle these points explicitly:

- Should cancelled in-flight pages still be surfaced as results, or dropped?
- Should cancellation be document-scoped only, or also consumer-scoped?
- Should `get_task()` expose timeouts publicly, or keep them internal?
- What exact terminal signal should Python iteration observe on cancellation?
- Where does accounting for a dropped task happen: in the worker that skips it, or in a separate drain path?
- Should unload after abort require all in-flight work to finish, or should abort itself own that quiescing step?

None of these block the design direction, but they should be resolved before coding to avoid lifecycle ambiguity.

Two of these questions should be resolved before implementation starts because they define the core lifecycle contract.

### Recommended resolution: dropped-task accounting

Dropped-task accounting should happen in the worker path that decides to skip the task.

That is the point where the implementation has definitive knowledge that:

- the task was scheduled
- the task will never produce a result
- the consumer-visible outstanding-work accounting must be updated

A separate drain path is possible, but it makes ownership of state transitions less clear and increases the risk of double retirement or missed retirement. The worker-side retirement path is the simpler and safer model.

### Recommended resolution: unload after abort

Abort should own quiescing of the affected work, and unload should remain a non-blocking cleanup operation that is valid only after quiescence has been reached.

In other words:

- `abort_document()` / `abort_all()` should drive the parser toward a state where no more relevant worker activity or deliverable results remain
- once that quiesced state is reached, unload should succeed without introducing a second blocking lifecycle phase

This keeps the contract easier to explain in Python:

- abort is the stop operation
- unload is the post-stop cleanup operation

If unload is also allowed to block waiting for in-flight work after abort, the lifecycle becomes harder to reason about and harder to expose cleanly in downstream integrations such as `docling`.

---

## Recommended Implementation Order

1. Introduce native abort state and wake-up mechanics in the threaded base.
2. Make worker loops and queue accounting cancellation-aware.
3. Expose `abort_document()` and `abort_all()` in pybind.
4. Add Python wrapper methods and define iterator termination semantics.
5. Add page-result explicit release to improve ownership clarity.
6. Integrate abort-aware shutdown in the `docling` threaded backend.
7. Add tests for:
   - abort one document before work starts
   - abort one document during active iteration
   - abort all documents during active iteration
   - consumer blocked in `get_task()`
   - workers blocked on full result queue
   - unload after normal completion vs unload after abort

---

## Summary

The current threaded parser lifecycle is coherent for full-drain batch processing, and the current unload behavior reflects that design.

The missing piece is not a bug in unload itself. The missing piece is a first-class cancellation model.

Adding explicit cooperative abort of one document or all documents will make the threaded stack suitable for long documents, interactive interruption, and service-side timeout propagation without discarding the current throughput-first architecture.
