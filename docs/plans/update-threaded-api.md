# `docling-parse` Upstream Plan for `docling_release` Threaded Backend

## Summary

Prepare `docling-parse` so `docling_release` can later build a `ThreadedDoclingParse...Backend` on top of the existing threaded public API.

This upstream pass should address three concrete gaps:

- public cleanup for loaded threaded documents
- selected-page scheduling at load time
- backend-style page image rendering from `PageParseResult`, with `scale` and Python-side `cropbox` support

## Key Changes

### 1. Add selected-page scheduling to `DoclingThreadedPdfParser.load(...)`
- Extend `load(...)` with optional `page_numbers: Sequence[int] | None = None`.
- Treat `page_numbers` as 1-indexed physical page numbers.
- Normalize at load time and reject out-of-range values.
- Store the selected subset per loaded document and build the threaded task queue from that subset instead of all pages.
- Keep `page_count(doc_key)` as the physical document page count.
- Add `scheduled_page_count(doc_key) -> int` for the number of pages that will actually be emitted.

### 2. Add public lifecycle cleanup to the threaded parser
- Add `unload(doc_key: str) -> bool`.
- Add `unload_all() -> None`.
- Clear document storage plus Python-side bookkeeping for page counts and selected-page subsets.
- Make unload idempotent after processing is complete.
- Do not add mid-stream cancellation in this pass; unloading during active iteration should raise a clear error.

### 3. Extend `PageParseResult.get_image(...)`
- Change `PageParseResult.get_image()` to accept:
  - `scale: float = 1.0`
  - `cropbox: ... | None = None`
- Keep the no-argument behavior compatible with today’s render-config mode.
- Keep current gating: `get_image(...)` only works when the threaded parser was configured with `parser_config.render_config`; parse-only results still fail clearly.
- Implement true rerendering from the retained `PdfPageDecoder` for scaled requests.
- Do not implement scaled output by resizing the existing pre-rendered image.

### 4. Keep cropping in Python, not C++
- Do not add crop-aware rendering to the C++ layer in this pass.
- `get_image(scale=..., cropbox=...)` should:
  - render the full page at the requested scale
  - crop the rendered PIL image in Python
- The cropbox contract should match the current `docling_release` expectations: page-coordinate crop input, converted in Python against the rendered page size.
- This keeps semantics aligned with current page-image caching in `docling_release` while avoiding immediate C++ rendering changes.

### 5. Cache policy for threaded result images
- Keep `get_image(...)` lazy.
- Preserve the existing pre-rendered full-page image as a fast path for the default full-page request when available.
- For non-default `scale`, rerender from the decoder.
- For `cropbox`, crop from the full-page image at the requested scale in Python.
- Do not require aggressive per-crop caching in `docling-parse`; `docling_release` already caches full-page images by scale.

## Test Plan

- Full-document threaded loads still emit all pages with correct 1-indexed `page_number`.
- `load(..., page_numbers=[...])` emits only the selected physical pages.
- `page_count(doc_key)` returns the full document count; `scheduled_page_count(doc_key)` returns the subset count.
- Invalid, duplicate, and unsorted page-number inputs are handled deterministically.
- Multi-document threaded parsing works with different subsets per document.
- `unload(doc_key)` succeeds after consumption, is idempotent, and removes the document from lookup state.
- `unload()` during active iteration raises the documented error.
- `get_image()` with no arguments still works in render-config mode.
- `get_image(scale=...)` produces a true rerender at the requested scale.
- `get_image(scale=..., cropbox=...)` returns the correct crop from the full-page rendered image at that scale.
- Repeated default full-page requests can reuse the pre-rendered fast path; scaled requests rerender from the decoder.

## Assumptions and Defaults

- Sequential `DoclingPdfParser` / `PdfDocument` APIs stay unchanged.
- No mid-stream cancellation is added in this pass.
- `docling_release` will continue to manage page-level image caching by scale on its side.
- Other `docling_release` pipelines may still fail against the draft threaded backend; this upstream work is specifically to unblock the later threaded PDF backend integration.
