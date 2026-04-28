# Threaded Parser Public API Design

**Status:** Draft — in iteration  
**Date:** 2026-04-24  
**Scope:** `docling-parse` only — docling integration is a separate concern

---

## Constraints

- **Sequential `PdfDocument`-based API is frozen.** No breaking changes to `DoclingPdfParser`, `PdfDocument`, `PdfDocument.get_page()`, `PdfDocument.iterate_pages()`, or any of their signatures. Existing code that uses the sequential path continues to work unchanged.
- **Threaded API may break.** `DoclingThreadedPdfParser`, `PageDecodeResult`, `PdfPageRenderResult`, and `ThreadedPdfParserConfig` can all change. There are no known external users relying on the current threaded API shape.

---

## Problems with the current threaded API

### 1. C++ internals leak into user code

`DoclingThreadedPdfParser.get_task()` returns a raw `PageDecodeResult` whose `.get()` returns `(PdfPageDecoder, timings_dict)`. `PdfPageDecoder` is a C++ binding object with no documented Python interface. Callers must know to call `PdfDocument._to_segmented_page_from_decoder()` on it — a private method not intended for external use.

The benchmark works around this with:
```python
dummy_doc = PdfDocument.__new__(PdfDocument)
dummy_doc._boundary_type = PdfPageBoundaryType.CROP_BOX
seg_page = dummy_doc._to_segmented_page_from_decoder(page_decoder, config)
```
This is a hack that will break silently if `PdfDocument` internals change.

`DoclingThreadedPdfRenderer.get_task()` has the same problem: `PdfPageRenderResult.get()` also returns `(PdfPageDecoder, timings_dict)`.

The current split also exposes an implementation detail as a public API split. The renderer result is essentially a decoded page result plus an optional rendered image. Users should not have to pick a different threaded class and result type just because they want the image bytes produced during decode.

### 2. Conversion logic is private and on the wrong class

`PdfDocument._to_segmented_page_from_decoder()` converts `PdfPageDecoder → SegmentedPdfPage`. Logically this is a pure function: it does not depend on document state, only on `_boundary_type`. It belongs at module level, not as an instance method on `PdfDocument`.

### 3. Page numbering inconsistency

`PageDecodeResult.page_number` is **0-indexed**.  
`PdfPageRenderResult.page_number` is also **0-indexed**.  
`PdfDocument.get_page()` and `iterate_pages()` are **1-indexed**.  
Callers of the threaded paths must remember to add 1. This is an unnecessary and error-prone divergence.

### 4. No Pythonic iteration

The `has_tasks()` / `get_task()` loop is functional but requires callers to write the same boilerplate every time. The sequential API provides `iterate_pages()`. Neither threaded class has an equivalent.

### 5. `timings` returned as a raw dict

The sequential path exposes the typed `Timings` model (with `.total()`, `.get()`, `.keys()`, etc.). The threaded `get()` returns a plain `dict`. These should be consistent.

### 6. `boundary_type` has no home in the threaded path

The sequential `DoclingPdfParser.load()` accepts `boundary_type`. There is no way to set it for the threaded parser — the conversion hack requires setting it manually on the dummy `PdfDocument` instance.

### 7. No way to query page count before iteration

After `parser.load()` / `renderer.load()`, callers have no way to ask how many pages a document has without starting iteration. This is needed by consumers that must pre-allocate structures or define termination conditions before any page arrives.

### 8. Parser and renderer public APIs are redundant

The public distinction between `DoclingThreadedPdfParser` and `DoclingThreadedPdfRenderer` is not strong enough to justify two APIs. Rendering does not produce a fundamentally different page outcome; it produces the same decoded page outcome with an additional optional image artifact. This should be represented as one threaded parser interface whose configuration decides whether page images are produced.

This keeps user code stable when a workflow later starts needing page images: users change config and start calling `get_image()`, not swap classes, result types, and import paths.

On the C++ side this is already mostly true structurally. `docling_threaded_parser` and `docling_threaded_renderer` both inherit from the same `docling_threaded_base<Derived, ResultType>`, and their worker loops perform the same document lookup, page decoder construction, `decode_page(config)`, optional word-cell creation, optional line-cell creation, result queueing, and error handling. The renderer adds only this extra step after decoding:

```cpp
pdflib::renderer<pdflib::BLEND2D> rnd(render_cfg);
page_decoder->get_instructions().iterate_over_instructions(rnd);

result.image_data = rnd.get_canvas();
result.image_shape = rnd.get_shape();
```

So the current second backend is not a fundamentally different threading model. It is the same threaded decode pipeline with an optional render stage and a wider result payload.

---

## Proposed changes

### A. Public module-level conversion function

Extract `PdfDocument._to_segmented_page_from_decoder` into a public, standalone function:

```python
# docling_parse/pdf_parser.py
def segmented_page_from_decoder(
    page_decoder: PdfPageDecoder,
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
) -> SegmentedPdfPage:
    """Convert a C++ PdfPageDecoder to a SegmentedPdfPage.

    This is the single canonical conversion point for both the sequential and
    threaded parse paths. PdfDocument._to_segmented_page_from_decoder() becomes
    a thin wrapper calling this function.

    Note: DecodePageConfig is applied by the C++ decoder before this function
    is called; there is nothing left to configure at the Python conversion stage.
    """
    ...
```

`PdfDocument._to_segmented_page_from_decoder()` delegates to this function, so the sequential path is untouched.

---

### B. Configuration controls parse-only vs parse-and-render

Keep one public threaded parser interface. Configuration, not the class name, decides whether page images are rendered.

```python
class ThreadedPdfParserConfig(BaseModel):
    loglevel: str = "fatal"
    threads: int = 4
    max_concurrent_results: int = 32
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX  # new
    render_config: RenderConfig | None = None
```

When `render_config is None`, `DoclingThreadedPdfParser` uses the parse-only backend. When `render_config` is provided, it uses the threaded render backend internally and surfaces the same `PageParseResult` type with image access enabled.

`DecodePageConfig` remains the decode configuration. Rendering should be activated by supplying `RenderConfig`, because render options such as canvas width and drawing flags already belong there. The key API point is that parse-only versus parse-and-render is a configuration choice on one threaded parser interface, not a separate public parser class.

Keep `DecodePageConfig` and `RenderConfig` as distinct types. They describe different pipeline stages:

- `DecodePageConfig` controls what is extracted from the PDF and how decoded page content is normalized: page boundary, sanitization, keeping chars/shapes/bitmaps, word and line cell creation, threading safety, glyph/debug retention, and related merge tolerances.
- `RenderConfig` controls how an already decoded page is rasterized: whether to draw text, whether to draw text bounding boxes, font resolution behavior, font matching cutoff, and target canvas dimensions.

Merging render fields into `DecodePageConfig` would make parse-only callers carry rasterization settings that do not affect decoding, and it would blur the contract of `DecodePageConfig` in the frozen sequential parser API. The better shape is a composed threaded execution config: decoding remains configured by `DecodePageConfig`; rendering remains configured by `RenderConfig`; the threaded parser config decides whether a render stage is enabled.

---

### C. Typed result object: `PageParseResult`

Replace both raw `PageDecodeResult` and `PdfPageRenderResult` with a clean Python class. `PdfPageDecoder` never appears in user-facing code — the conversion happens inside `get_page()`.

```python
class PageParseResult:
    """Outcome of one page processed by DoclingThreadedPdfParser."""

    doc_key: str      # document identifier returned by .load()
    page_number: int  # 1-indexed — consistent with the sequential API
    page_width: float # page width in points (from boundary box; cheap, no full conversion needed)
    page_height: float
    success: bool

    def get_page(self) -> SegmentedPdfPage:
        """Return the parsed page. Lazy: converts on first call, caches the result.

        Calls segmented_page_from_decoder() internally using the boundary_type
        from the parser that produced this result.
        Raises RuntimeError if success is False.
        """
        ...

    def get_timings(self) -> Timings:
        """Return structured timing data for this page parse."""
        ...

    def get_image(self) -> PILImage.Image:
        """Return the rendered page image.

        Raises RuntimeError if this result was produced with rendering disabled
        or if success is False.
        """
        ...

    @property
    def has_image(self) -> bool:
        """Whether get_image() can return a rendered image for this result."""
        ...

    @property
    def error_message(self) -> str:
        """Error description; empty string when successful."""
        ...
```

`page_width` and `page_height` are extracted from `page_decoder.get_page_dimension()` without triggering the full `SegmentedPdfPage` conversion. Dimension decoding is a distinct internal step (see `TIMING_KEY_DECODE_DIMENSIONS`) and the data is available on the decoder object as soon as `get_task()` returns.

`get_page()` is **lazy**: it converts on first call and caches the result. This keeps conversion cost on the worker/consumer thread rather than on the task-delivery path, and avoids wasted work on error paths where `get_page()` is never called.

`get_image()` is available on the same result type but only succeeds when the parser was configured with `render_config`. A parse-only result has `has_image == False` and raises a clear `RuntimeError` from `get_image()`. This makes misuse fail loudly while keeping the page result type uniform.

---

### D. Iterator API on `DoclingThreadedPdfParser`

```python
class DoclingThreadedPdfParser:

    def page_count(self, doc_key: str) -> int:
        """Return the total page count for a loaded document.

        Available immediately after load(), before iteration begins.
        """
        ...

    def iterate_results(self) -> Iterator[PageParseResult]:
        """Yield page results as they complete.

        Pages are returned in COMPLETION ORDER, not page-number order.
        Worker threads start on the first call (same as has_tasks()).

        Use result.page_number and result.doc_key to route results.
        To process in page order, collect into a dict keyed by page_number
        and sort after iteration is complete.
        """
        while self.has_tasks():
            yield self.get_task()

    def get_task(self) -> PageParseResult:   # return type changes
        """Block until the next result is available and return it."""
        ...

    # has_tasks() is unchanged — stays for callers needing manual control
```

---

### E. Remove the separate threaded renderer API

Do not introduce a second primary public interface for rendering. `DoclingThreadedPdfParser` should select the existing C++ threaded parser or renderer implementation internally based on `ThreadedPdfParserConfig.render_config`.

Longer term, the C++ implementation can also be collapsed into one threaded worker implementation with an optional render stage. That would remove the duplicated worker-loop logic and keep the only behavioral branch close to the actual difference: whether `RenderConfig` is present.

Remove `DoclingThreadedPdfRenderer`, `PdfPageRenderResult`, and `ThreadedPdfRendererConfig` as part of the threaded API break. There is no stable public interface for the threaded component yet, so keeping deprecated aliases would add compatibility surface without protecting a real external contract.

Documentation and examples should point users to `DoclingThreadedPdfParser` only.

---

## Resulting user-facing API

**Parse only (no images):**

```python
from docling_parse.pdf_parser import DoclingThreadedPdfParser, ThreadedPdfParserConfig
from docling_parse.pdf_parsers import DecodePageConfig

decode_config = DecodePageConfig()
decode_config.create_line_cells = True

parser_config = ThreadedPdfParserConfig(threads=4, max_concurrent_results=32)
parser = DoclingThreadedPdfParser(parser_config=parser_config, decode_config=decode_config)

doc_key = parser.load(path)
total = parser.page_count(doc_key)

for result in parser.iterate_results():
    if result.success:
        seg_page = result.get_page()      # SegmentedPdfPage, lazy
        size = (result.page_width, result.page_height)  # available without get_page()
    else:
        print(f"p{result.page_number} ERROR: {result.error_message}")
```

**Parse and render (with images):**

```python
from docling_parse.pdf_parser import DoclingThreadedPdfParser, ThreadedPdfParserConfig
from docling_parse.pdf_parsers import DecodePageConfig, RenderConfig

render_config = RenderConfig()
render_config.canvas_width = 1024

parser = DoclingThreadedPdfParser(
    parser_config=ThreadedPdfParserConfig(threads=4, render_config=render_config),
    decode_config=DecodePageConfig(),
)

doc_key = parser.load(path)
total = parser.page_count(doc_key)

for result in parser.iterate_results():
    if result.success:
        seg_page = result.get_page()   # SegmentedPdfPage
        image = result.get_image()     # PIL RGBA Image
    else:
        print(f"p{result.page_number} ERROR: {result.error_message}")
```

**In-order collection (when page order matters):**

```python
pages: dict[int, SegmentedPdfPage] = {}
for result in parser.iterate_results():
    if result.success:
        pages[result.page_number] = result.get_page()

for page_no in sorted(pages):
    process(pages[page_no])
```

---

## Sequential path — unchanged

The following remain exactly as-is. No signature changes, no behaviour changes:

- `DoclingPdfParser`
- `PdfDocument`
- `PdfDocument.get_page(page_no, *, config)`
- `PdfDocument.iterate_pages(*, config)`
- `PdfDocument.get_page_with_timings(page_no, *, config)`
- All `Timings`, `PdfAnnotations`, `PdfTocEntry` models

`PdfDocument._to_segmented_page_from_decoder()` stays as a private method (it will delegate to the new public `segmented_page_from_decoder()` function internally). External callers should migrate to using `PageParseResult.get_page()` instead.

---

## Resolved questions

- **`iterate_results()` timeout?** Decided no — the caller's concern. The `has_tasks()` / `get_task()` escape hatch exists for manual control.
- **`render_config` on `ThreadedPdfParserConfig` or as a constructor argument?** Decided on `ThreadedPdfParserConfig`: rendering is a threaded execution mode, while `DecodePageConfig` remains focused on decoded page content. Implemented.
