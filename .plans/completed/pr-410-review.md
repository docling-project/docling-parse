# PR #410 review: annotation locking and JSON cycle detection

Reviewed PR: https://github.com/docling-project/docling-parse/pull/410

Reviewed commit: `618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5`

Review date: 2026-11-01

The review covers the recursive serializer, document decoder, worker loop, Python bindings, and new tests.

1. The new lock is necessary for lazy extraction, but its placement introduces avoidable contention.
2. An `unordered_set` is correct here with a custom hash, although a substantial performance gain is unlikely.

## The `lock_guard` and scaling

The new lock in [`ensure_annots_loaded()`](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/src/parse/pdf_decoders/document.h#L182) protects the same QPDF document used by page extraction. That protection is necessary once annotations become lazy: annotation extraction can now overlap with worker activity. QPDF explicitly disallows concurrent use of the same object instance across threads. [QPDF threading documentation](https://qpdf.readthedocs.io/en/stable/library.html)

Its effect depends on the workload:

| Situation | Effect |
|---|---|
| Pages only; annotations never requested | No acquisition of the new lock; eager annotation work is avoided. |
| First annotation request during page processing | Annotation extraction and page extraction block each other for that document. |
| Subsequent annotation requests | Still acquire the page-extraction mutex, despite the annotations already being cached. |
| Different documents | Have separate mutexes; this lock does not directly serialize them. |
| Workers already decoding extracted pages | Continue decoding outside this mutex. |

The existing page-buffer code already holds this mutex across [`QPDFWriter::write()`](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/src/parse/pdf_decoders/document.h#L399). Consequently, the new lock does not turn the entire page-decoding pipeline into serial execution. It adds annotation extraction to an existing serial stage.

Two review concerns follow:

- **Cached getters can unnecessarily wait behind page serialization.** The mutex is acquired before checking `annots_loaded`. A request that only needs cached JSON may therefore wait for an expensive page write. The regular Python document wrapper caches annotations, mitigating repeated calls there; the threaded wrapper calls into C++ each time. [Python wrappers](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/docling_parse/pdf_parser.py#L1724)
- **That wait happens while holding the GIL.** The annotation bindings do not release it. A getter waiting for this document's mutex can therefore delay unrelated Python threads, including result consumption. This is a latency and application-level scaling concern, even though native workers can continue. [Annotation binding](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/app/pybind_parse.cpp#L1206)

The first getter builds the form, metadata, language, and outline together. Its critical section includes all that work, not just the corrected `/AcroForm` traversal.

### Recommendation

Keep synchronization around the initial QPDF access, but use a per-document `std::once_flag`/`std::call_once` for initialization, acquiring `thread_safe_buffer_mutex` inside the initialization callable. Completed calls would then avoid the page-buffer mutex. Keep the published JSON immutable and use non-mutating access in getters.

Moving the current plain `bool` check outside the lock would introduce a data race; it needs proper publication through `call_once` or equivalent synchronization.

Releasing the GIL during native extraction/waiting is another improvement, but requires checking document lifetime and concurrent unload behavior before making that change.

## The `set` versus `unordered_set`

The replacement preserves semantics. In [`to_json_impl()`](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/src/parse/qpdf/to_json.h#L122), `path` is used only for:

- Insertion and checking whether an object is already present.
- Erasing that object when recursion returns.

It is never iterated, so ordering contributes nothing to JSON output or cycle detection.

However, `std::unordered_set<QPDFObjGen>` alone does not compile with the installed QPDF 12.2 headers: `QPDFObjGen` provides equality and ordering, but no default `std::hash` specialization. A custom hasher can cover both object number and generation:

```cpp
struct ObjGenHash
{
  std::size_t operator()(const QPDFObjGen& id) const noexcept
  {
    auto a = std::hash<int>{}(id.getObj());
    auto b = std::hash<int>{}(id.getGen());
    return a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2));
  }
};

using ObjectPath = std::unordered_set<QPDFObjGen, ObjGenHash>;
```

Both the reference parameter in `to_json_impl` and the local container in `to_json` must use that type. It needs `<functional>` and `<cstddef>` in addition to `<unordered_set>`.

The performance argument is less compelling:

- `set` operations are `O(log D)`; hash operations are average `O(1)`.
- Here, `D` is current recursion depth, not the total number of PDF objects.
- With the default limit, the path contains at most roughly 33 entries, including the depth-limit call.
- Hashing, buckets, and allocations can outweigh the saved comparisons at that size.

The major improvement already comes from removing `unparse()`-based keys and recursive container copies. Either container is acceptable; retaining `set` is reasonable unless profiling shows a benefit from hashing.

## Review gaps

The new threaded test uses one worker and one page, then retrieves that page's result before requesting annotations. It therefore does not exercise annotation extraction competing with page serialization. A multipage test with several workers and annotation requests during processing would cover this gap. [New tests](https://github.com/docling-project/docling-parse/blob/618e2b1f25b2e8d8f87eec52cfbd1bc7fef696d5/tests/test_unit_signed_form_json.py)

The cycle guard also remains path-local: shared subtrees can still be expanded repeatedly through different branches. The document-type exclusions address the reported explosion, but neither `set` nor `unordered_set` guarantees bounded total output.

## Validation and limitations

The default-hash compilation failure and custom-hash compilation success were verified with syntax-only compiler checks against the installed QPDF headers. The PR's test suite was not run, and contention was not measured. No implementation files were changed as part of this review.
