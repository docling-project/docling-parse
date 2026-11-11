The actionable findings are:

  1. Workers can deadlock during destruction.
     The base destructor (src/pybind/docling_threaded_base.h:187) simply joins workers. Meanwhile, renderer workers (src/pybind/docling_threaded_renderer.h:194) can wait indefinitely for space in the bounded results queue. If consumption stops early,
     nobody frees that space.

     Proposed fix: add an explicit cancellation/shutdown operation. It should stop scheduling, wake both condition variables, allow blocked producers to exit, and then join workers. Expose this through Python close() and a context manager.

  2. Renderer members can be destroyed while workers still use them.
     The renderer owns shared font caches and configuration (src/pybind/docling_threaded_renderer.h:28), but joins workers only in its base destructor. C++ destroys derived members before executing the base destructor. Destroying a renderer with active
     workers therefore risks accessing destroyed members.

     Proposed fix: call the cancellation-and-join operation from the renderer’s own destructor, before its members are destroyed. Make shutdown idempotent.

     The benchmark normally drains results before unloading (scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py:1255), so these two defects are real lifecycle problems, not confirmed explanations for this particular exit.

  3. Exceptions can escape the worker thread and terminate the process.
     Worker cache construction (src/pybind/docling_threaded_renderer.h:78) occurs outside the per-page try. Result construction and queue insertion also have unprotected operations. An escaping C++ exception invokes std::terminate, bypassing normal
     Python cleanup—consistent with a leftover-semaphore warning afterward.

     Proposed fix: protect the entire worker entry point, record fatal worker exceptions, and propagate them through get_task(). Use scope-based cleanup for worker counters and notifications so consumers cannot wait forever after a worker fails. This
     would not catch segmentation faults or an OS memory kill.

  4. A geometry query can make this benchmark appear hung.
     Connected-shape grouping (src/parse/pdf_decoders/page.h:713) repeatedly scans all bounding boxes; even disjoint shapes require quadratic work. The Python benchmark calls it for every page (scripts/benchmarking/
     run_performance_benchmarking_with_docling_settings.py:1168).

     Proposed fix: use a spatial index to find overlapping candidates. This addresses stalls on complex vector pages, rather than explaining an abrupt exit.