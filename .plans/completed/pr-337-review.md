# PR #337 review: performance and thread scaling

Reviewed PR: https://github.com/docling-project/docling-parse/pull/337

Reviewed head: `9e3b4eb9642576c1a39ba897037c287c85d0a033`

Compared base: `6b5b6fd0c70fbcde43bc35d0dad004047fdee7a2`

Review date: 2026-11-01

## Assessment

Yes, this PR can affect both performance and end-to-end thread scaling. Some costs apply even when the caller never requests structure data, and some apply to untagged PDFs.

It adds no new mutex or global shared state to the page workers. The main concerns are additional serial work during load and Python conversion, increased native cell memory and copying, and repeated cell scans in nested marked-content sequences.

The runtime impact has not been benchmarked. The memory-layout increase was checked with the local compiler: native text cells grow from 384 to 440 bytes, an increase of 56 bytes or 14.6% per cell instance on this machine.

| Finding | Where the cost applies | Scaling implication |
|---|---|---|
| Structure tree extracted eagerly | Every document load; full traversal for tagged documents | Serial startup grows before page workers start |
| Page-number map built before checking for a tree | Untagged documents too | Extra allocation and O(P log P) startup work |
| Larger native cell type | All cells, including untagged cells and copied cells | More memory, copying, and potential bandwidth pressure |
| Range scan at each tagged EMC | Marked-content spans with MCID or artifact tagging | Work grows with both glyph count and nesting |
| Python tag construction | When marked-content getters are called | Additional consumer work under the GIL |
| Whole annotation conversion | When annotations are requested | Large trees are copied and validated; threaded wrapper does not cache |
| Generic attribute serialization | Structure attributes containing compound values | Existing serializer can follow unexpectedly large object graphs |

## 1. Structure extraction becomes mandatory serial load work

The PR adds:

```cpp
json_annots["structure"] = pdf_structure(qpdf_document, qpdf_pages).get();
```

to `ensure_annots_loaded()`. At the reviewed head, `process_document_from_bytesio()` still calls that function during load. The new `get_structure()` Python method is therefore an accessor to an already built structure, not the trigger for lazy extraction.

Sources: [structure extraction](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/document.h#L153), [eager call during load](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/document.h#L308).

Consequences:

- Tagged documents pay for the full tree even when only page text is wanted.
- Selecting one page does not restrict tree extraction. The reader receives the full document page vector and traverses the structure root's children.
- The threaded parser loads the document before normalizing the scheduled page selection. Workers start later, through `has_tasks()`. Increasing the worker count cannot accelerate this startup traversal.
- Load bindings retain the GIL. The new work extends an existing period during which other Python threads cannot run normally.

Sources: [load and page selection](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/pybind/docling_threaded_base.h#L225), [worker startup](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/pybind/docling_threaded_base.h#L565), [load binding](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/app/pybind_parse.cpp#L1097).

A useful simplified model is:

```text
T(n) ≈ serial load and structure work
     + existing serialized page-extraction work
     + parallel page-decode work / n
     + Python result-consumption work
```

This is a reasoning model, not a measured timing equation: stages can overlap and workers can be imbalanced. It illustrates why extra startup work can reduce end-to-end speedup even if worker-only scaling stays similar.

**Recommendation:** make structure extraction separately lazy or explicitly configurable. Asking for metadata or a table of contents should not require building a potentially large structure tree. Add separate structure-extraction timing.

## 2. Untagged documents still build an unnecessary page map

The `pdf_structure` constructor inserts every page into a `std::map<QPDFObjGen, int>`. Only afterward does `get()` check whether `/StructTreeRoot` exists.

Source: [constructor and absence check](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/qpdf/structure.h#L83).

For P pages, this introduces O(P log P) map-building work and O(P) temporary map storage even on an untagged PDF. It also duplicates the page-indexing pattern already used by the outline reader.

**Recommendation:** check for a usable structure root before building the page map. Consider sharing a document-level page-number index between readers. An unordered map could reduce indexing cost, but eliminating unused work is the first priority.

## 3. Every native cell becomes larger

The PR adds an integer and two `std::string` members directly to `page_item<PAGE_CELL>`.

Source: [cell fields](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/page_items/page_cell.h#L123).

I compiled the exact base and head class declarations with the local C++20 compiler and standard-library headers, using syntax-only size probes:

| Native cell layout | Size |
|---|---:|
| Base | 384 bytes |
| PR head | 440 bytes |
| Increase | 56 bytes, or 14.6% |

This is an ABI-specific class-layout measurement, not a whole-process RSS measurement.

One million simultaneously allocated cell instances would add approximately 53.4 MiB of object storage, before vector spare capacity, dynamically allocated string contents, or other overhead.

Empty strings still occupy inline object storage, so untagged documents pay this cost too. The fields also increase cell construction, destruction, and copy costs. Existing code copies the base cells into the public character-cell output and into word/line processing containers.

Source: [character-cell copy](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/page.h#L967), [word and line processing](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/page.h#L2057).

This can affect scaling without lock contention: multiple workers and queued results retain more cell storage, and copying larger cells can increase allocator and memory-bandwidth pressure. The size measurement does not establish a 14.6% runtime or RSS regression.

**Recommendation:** consider compact identifiers or a separate per-character linkage array instead of two strings in every cell. If the inline fields remain, measure both tagged and untagged workloads with different content configurations and queue sizes.

## 4. Nested marked content causes repeated cell scans

At each EMC, `apply_marked_content_tags()` scans from the sequence's starting cell to the current end. It runs independently of whether structure data is requested and independently of `apply_actual_text`.

Source: [EMC and tagging loop](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/stream.h#L1853).

The early return avoids scanning ordinary sequences with neither MCID nor artifact status. However, for nested qualifying spans, the same cell is visited at each enclosing EMC, even when it already has its innermost tags.

The added work is proportional to:

```text
sum(number of surviving cells covered by each qualifying closing span)
```

With G glyph cells covered by D nested qualifying spans, this can be O(G × D). For example, 100,000 cells inside 100 such spans imply about 10 million loop visits. This is an operation-count illustration, not a benchmark.

The structure reader's depth limit of 64 does not bound the independent marked-content stack in page streams.

The work is local to each page, so it does not directly serialize workers. It can increase CPU cost and create long-running pages that leave other workers idle near the end of a batch.

**Recommendation:** consider recording effective MCID/artifact state when a glyph is emitted, using the existing marked-content stack. Preserve innermost-value and ActualText behavior. If retaining the EMC approach, benchmark nesting depth explicitly.

## 5. Python conversion can become the consumer bottleneck

`_marked_content_from_decoder()` iterates over native character cells in Python. For each tagged cell it fetches several bound attributes and constructs both a `BoundingRectangle` and a `PdfMarkedContentTag`. Calls are not cached.

Source: [Python tag conversion](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/docling_parse/pdf_parser.py#L799).

The native cell container is exposed by reference, so this is not a full native-vector copy merely to obtain the iterator. Nevertheless, traversal and per-cell Python object construction are additional work under the GIL. An untagged page is still scanned when this getter is called, even though it produces an empty list.

When a consumer invokes this for every threaded result, more native workers do not parallelize the Python loop. A slow consumer can fill the bounded result queue and cause workers to wait for queue space.

Sources: [reference binding](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/app/pybind_parse.cpp#L602), [result queue backpressure](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/pybind/docling_threaded_parser.h#L110).

**Recommendation:** provide a batched native export, consider compact arrays or span records, and cache materialized tags if repeated access is expected. Native gathering could release the GIL where lifetime guarantees allow; Python object creation still has its own cost.

## 6. Tree assembly and annotation getters copy large structures

The reader uses lvalues in operations such as:

```cpp
kids.push_back(item);
result["kids"] = kids;
elements.push_back(e);
structure["elements"] = elements;
```

These copy JSON subtrees. A descendant's data can be copied again at successive ancestor levels. With serialized subtree size J and depth D, repeated assembly can cost O(J × D), with D capped at 64 in this reader.

Source: [tree assembly](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/qpdf/structure.h#L280). JSON copy construction is linear in the copied value's size, while moving transfers ownership in constant time. [nlohmann JSON constructor documentation](https://json.nlohmann.me/api/basic_json/basic_json/)

Returning cached `json_annots` by value then copies the document annotations. The Python binding converts that JSON to Python containers, and `_to_annotations()` validates the structure into Pydantic models.

The single-document Python wrapper caches annotations. The threaded wrapper does not: repeated `get_annotations(doc_key)` calls repeat native copying, conversion, and model construction.

Sources: [native getter](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/pdf_decoders/document.h#L186), [model conversion](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/docling_parse/pdf_parser.py#L862), [threaded getter](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/docling_parse/pdf_parser.py#L1817).

**Recommendation:** move completed temporary JSON values into their parents and cache threaded annotation models per document, with invalidation on unload/replacement. A dedicated structure getter could avoid repeatedly transferring unrelated annotations.

## 7. Attribute serialization retains an expensive graph-walk path

The main structure traversal has useful protections: a global visited set prevents repeated expansion of indirect elements, the recursion depth is capped, and page geometry is cached per referenced page.

However, attribute values are serialized using:

```cpp
pdflib::to_json(dict.getKey(key), {}, 0, 4)
```

Source: [attribute serialization](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/qpdf/structure.h#L411).

At this PR's head, the generic serializer still uses unparsed object text for cycle detection, copies the guard into recursive calls, and fails to forward `max_level`. Thus the intended limit of four does not apply throughout nested values. Generic serialization also does not use the structure reader's global visited set.

Source: [generic serializer](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/src/parse/qpdf/to_json.h#L66).

For ordinary shallow attributes, this may be minor. For compound or unexpected attributes referring into larger object graphs, it creates a route to substantial serial CPU and memory use. This is a conditional risk, not evidence that normal structure attributes reproduce the signed-form issue.

**Recommendation:** use a properly depth-bounded attribute serializer and add tests for compound attributes, cycles, shared references, and document references. PR #410 addresses relevant generic serializer defects, but its new signature requires adapting this four-argument call when combining the PRs.

## Thread safety and interaction with PR #410

PR #337 itself introduces no new worker mutex. Its tree reader uses per-invocation containers, and the new tagging state belongs to the page's stream decoder. At the reviewed head, eager tree extraction finishes during load, before page workers start.

If combined with PR #410's lazy annotations, the tree traversal would instead execute on the first annotation request under the shared document mutex. That removes eager work for page-only use, but a large structure tree can extend the critical section and delay page extraction.

It would be unsafe to simply run lazy structure extraction concurrently against the shared QPDF document without synchronization. QPDF requires exclusive use of an individual object instance at a time. [QPDF threading documentation](https://qpdf.readthedocs.io/en/stable/library.html)

Also, #410 makes the annotation bundle lazy as a whole. Combining the PRs does not by itself make structure separately optional when a caller only requests metadata or the outline.

## Validation needed before merging

The new tests check structure semantics, geometry, tags, and untagged behavior. The threaded test uses one configured worker and asks for annotations before starting page-result processing. It does not establish multicore scaling or concurrent-read behavior.

Source: [new tests](https://github.com/docling-project/docling-parse/blob/9e3b4eb9642576c1a39ba897037c287c85d0a033/tests/test_unit_structure_tree.py).

Compare the same base and head with:

1. Untagged text-heavy documents, including a long document and a one-page selection from it.
2. Tagged documents with increasing element counts, broad/deep trees, and Layout attributes.
3. Pages with increasing marked-content nesting depth.
4. Thread counts 1, 2, 4, and 8 where supported, at a fixed result-queue limit.
5. Page-only consumption, marked-content export on every result, and repeated annotation requests.
6. Native character cells retained/suppressed and word/line computation enabled/disabled.

Measure load time, first-result latency, worker decode time, total consumption time, pages/second, peak RSS, structure JSON size, and Python conversion time separately. Report native cell sizes on the supported build platforms too.

The most useful changes before accepting a performance-neutral claim are separately optional structure extraction, the early no-tree check, removal of unnecessary JSON copies, and evidence on nested-span and per-cell overhead.

## Validation performed and limitations

- Reviewed the full eight-file PR diff and relevant unchanged code at the exact head.
- Compared native cell declarations against the PR base with the local compiler.
- Verified 384-byte base and 440-byte head layouts using compiler diagnostics from syntax-only probes. No executable or probe source file was written.
- Consulted primary QPDF and nlohmann JSON documentation for thread-safety and copy semantics.
- Did not build the PR, run its test suite, or benchmark runtime, RSS, or multicore throughput.
- Performance and scaling conclusions beyond the class-layout measurement are source-based analysis. No implementation files were changed.

