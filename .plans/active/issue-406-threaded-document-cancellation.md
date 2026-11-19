# Cancel threaded PDF work by document key

Status — First iteration implemented. PR #474 review follow-up planned.
Source — [Issue #406](https://github.com/docling-project/docling-parse/issues/406).

## Scope and API

- Add `DoclingThreadedPdfParser.cancel(doc_key: str) -> int` for the key returned by
  `load()`. Use the same API for parse-only and parse-and-render.
- Before the first `has_tasks()`, cancel the key's entire normalized page
  selection so that its pages are never scheduled. After iteration starts,
  remove every pending task and every unread buffered result for that key.
- Treat a task as started when a worker dequeues it. Claimed pages may finish
  their native work. Discard their results, including results already waiting
  for queue capacity. Results already dequeued by a consumer remain usable.
- Return the number of pages removed from the pending schedule or task queue
  plus the number of buffered results evicted during the call. Repeated calls,
  unknown keys, and calls after all results for the key were consumed return
  zero. Cancellation returns after the queue update without waiting for workers.
- Keep the document loaded, with its original page counts and metadata.
  `unload()` and `unload_all()` use the existing batch-wide completion guard.
  After cancelled in-flight pages finish and other results are consumed, unload
  can proceed without draining the cancelled document's results.
- Defer `document_timeout` integration, native decode checkpoints, worker
  abandonment or replacement, and bounded unload or shutdown to later work.

## Why the current code needs changes

- [Threaded base](../../src/pybind/docling_threaded_base.h) builds the task queue
  lazily from selected pages. `tasks_remaining` is reduced on consumption, and
  `get_task()` waits for a result or for every worker to exit.
- [Parser worker](../../src/pybind/docling_threaded_parser.h) and
  [renderer worker](../../src/pybind/docling_threaded_renderer.h) each wait for
  result capacity before publishing. Removing queued tasks alone can leave a
  completed page waiting for space or let it publish after cancellation.
- [Python iteration](../../docling_parse/pdf_parser.py) currently wraps the
  native end-of-stream sentinel as a failed result on page zero.

## Implementation

1. In the threaded base, track cancelled keys for the lifetime of each loaded
   submission. Skip those keys when building the queue. Clear the marker on
   unload or reset and on a successful pre-start reload of the same key.
2. Use one mutex for queue transitions and wait predicates. Under that mutex,
   mark the key cancelled, remove matching tasks and results in place, preserve
   FIFO order for other keys, and subtract only the removed queue entries from
   `tasks_remaining`. Release evicted payloads outside the lock. Wake producers
   and consumers after updating the queues.
3. Share task claiming and result publication in the base. A result producer
   wakes when there is capacity or its key is cancelled. Check cancellation
   again under the queue lock before publishing. A discarded in-flight result
   decrements `tasks_remaining` exactly once, when the worker finishes with the
   document. Hold the queue lock for worker-exit and completion-predicate
   updates and release it before any worker join.
4. Let `get_task()` wake on a result or on completion, including when
   `tasks_remaining` reaches zero through cancellation. Return the existing
   native sentinel when the queue is empty at completion. Keep direct Python
   `get_task()` compatible and have `iterate_results()` end on the sentinel.
5. Bind `cancel_document(key)` for both native classes in
   [the pybind module](../../app/pybind_parse.cpp) and forward from Python.
   Keep cancellation serialized with the existing GIL-held lifecycle calls.
   `get_task()` already releases the GIL while waiting. Document the return
   count and in-flight behavior in the [threaded Python example](../../README.md).

## Verification

- Parameterize Python coverage across parse-only and render modes. Cover
  cancellation before startup using a page selection, cancellation partway
  through a batch, repeat and unknown keys, cancellation after full consumption,
  unload after cancellation, and reloading the same key.
- Use deterministic worker synchronization for races. Cover a full result
  queue, a cancelled producer waiting behind another key's buffered result,
  cancellation concurrent with publication, and a blocked consumer when the
  batch reaches zero outstanding work. Check that the iterator ends without a
  page-zero result and that unrelated keys deliver all their selected pages.
- Rebuild the Python extension and run the new cancellation tests alongside
  `tests/test_unit_threaded_page_range.py`,
  `tests/test_regression_threaded_parse.py`, and
  `tests/test_regression_threaded_render.py`.
- For the native queue tests, configure with
  `-DDOCLING_PARSE_BUILD_THREADED_TESTS=ON`, build the
  `threaded_cancellation` target, and run
  `ctest --test-dir build -R '^threaded_cancellation$' --output-on-failure`.

## PR #474 follow-up for cau-git’s review

The steps below update the first iteration’s API and worker accounting.
[GitHub review](https://github.com/docling-project/docling-parse/pull/474#pullrequestreview-5955961087)

1. Make lifecycle transitions explicit under `queue_mutex`. Recheck that
   loading is allowed after the `BytesIO` callbacks and before updating the
   document maps. Run Python callbacks outside the mutex. Keep document maps
   stable while workers can read them. Restore `const` on
   `validate_unload_state()`.
2. Fix shutdown and unload before changing the cancellation count. Keep the
   batch-wide guard for unread results. Once no results remain, join workers
   before erasing document entries. Release the GIL during the blocking
   joins when held, with `queue_mutex` unlocked. Guard against concurrent
   startup and reset while a join is in progress.
3. Track claimed pages per document under `queue_mutex` until publication or
   discard, including while they wait for result capacity. On cancellation,
   subtract queued, buffered, and claimed pages from `tasks_remaining` exactly
   once. Wake blocked readers as soon as no result can still arrive. Use
   `active_workers` separately to track when workers have exited.
4. Make result cleanup consistent. Remove matching buffered results while
   preserving FIFO order for other documents. Move discarded payloads out
   under the lock and release them outside it in cancellation, shutdown, and
   reset.
5. Update both native bindings, the Python wrapper, and the README. Make
   `cancel()` return `None` and remove the two test-only status getters.
   Keep `all_scheduled_work_completed()`. Document that direct `get_task()`
   can return a page-zero sentinel after concurrent cancellation. Check the
   native negative page number before wrapping results in `iterate_results()`.
6. Update the gated native harness to prove that a blocked reader wakes before
   a cancelled page finishes and that unload waits safely while another
   Python thread can run. Keep the full-queue and multi-document cases. Add
   the reentrant `BytesIO` regression in both parse and render modes. Rebuild
   the extension, rerun the verification above, and wire `threaded_cancellation`
   into one CI job with `ctest`.
7. Check the contract against Docling’s downstream cleanup. For a backend
   that owns one parser and one document, verify that background unload can
   replace draining and polling once cancellation stops result delivery and
   unload can join safely.
   [Docling PR #5456](https://github.com/docling-project/docling/pull/5456)

### Native rendering performance check

Add an opt-in `--native-render-only` flag to the refresh launcher. Run only
the native C++ `render-only` profile and save its logs and timings. Skip
native render-with-text, both Python replays, and the full-suite report.

```sh
BUILD_MODE=clean THREADS=12 sh ./scripts/benchmarking/refresh_performance_benchmarks.sh --native-render-only
```

Use the default pinned `bo767` corpus on the same machine. Compare the
benchmark's `wall_time (s)` with the expected ~105–110 seconds and record the
page count and errors. The local 12-thread reference was 107.611 seconds on
Apple M3 Max.
