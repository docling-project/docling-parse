# Threaded Parser Public API Design

**Status:** Implemented  
**Last updated:** 2026-06-22  
**Scope:** `docling-parse` only

This document is the consolidated design and behavior reference for the public threaded parser API in `docling-parse`.

It supersedes the narrower `update-threaded-api.md` plan. The decisions from that follow-up plan are folded in here, and the examples below reflect the current implementation rather than an earlier proposal draft.

---

## Goals

- Keep the sequential `PdfDocument`-based API close to the existing shape.
- Provide one public threaded parser entry point for both parse-only and parse-and-render workflows.
- Hide C++ decoder objects from normal Python callers.
- Keep page results typed, lazy, and consistent with the sequential API where possible.
- Support selected-page scheduling and explicit cleanup for multi-document threaded workloads.

---

## Stable constraints

- The sequential API still centers on:
  - `DoclingPdfParser`
  - `PdfDocument`
  - `PdfDocument.get_page()`
  - `PdfDocument.iterate_pages()`
  - `PdfDocument.get_page_with_timings()`
- v7 did change the public config model on both sequential and threaded paths:
  - `DecodeConfig` is open-time compute tuning
  - `ContentConfig` is page-content selection/materialization
- Rendering remains optional and is enabled by configuration, not by switching to a separate public threaded class.

---

## Final public shape

### One threaded parser interface

The public threaded entry point is:

```python
DoclingThreadedPdfParser(
    parser_config: ThreadedPdfParserConfig | None = None,
    decode_config: DecodeConfig | None = None,
)
```

There is no separate public `DoclingThreadedPdfRenderer` API anymore. Parse-only and parse-and-render share the same Python interface.

### Threaded parser configuration

```python
class ThreadedPdfParserConfig(BaseModel):
    loglevel: str = "fatal"
    threads: int = 4
    max_concurrent_results: int = 32
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX
    render_config: RenderConfig | None = None
    page_content_config: ContentConfig | None = None
```

Key points:

- `boundary_type` now has an explicit home in the threaded path.
- `render_config=None` selects parse-only operation.
- `render_config` present selects parse-and-render operation.
- `page_content_config` fixes the batch decode/materialization mask for the threaded run.
- `DecodeConfig` and `RenderConfig` stay separate because they configure different pipeline stages.

### Public result type

`get_task()` and `iterate_results()` return `PageParseResult`.

`PageParseResult` exposes:

- `doc_key: str`
- `page_number: int`
- `page_width: float`
- `page_height: float`
- `success: bool`
- `error_message: str`
- `has_image: bool`
- `timings: PageDecodeTimings | PageRenderTimings`
- `get_page(content_config: ContentConfig | None = None) -> SegmentedPdfPage`
- `get_timings() -> Timings`
- `get_image(...) -> PIL.Image.Image`

Notable behavior:

- `page_number` is 1-indexed, matching the sequential API.
- `get_page()` is lazy and caches the converted `SegmentedPdfPage`.
- `get_page(content_config=...)` may raise if it asks for an entity the threaded batch skipped at decode time.
- `get_timings()` returns the typed `Timings` model, not a raw dict.
- `timings` exposes the top-level threaded task timings directly.
- Failed results keep `page_width` and `page_height` at `0.0`, and `get_page()` / `get_image()` raise clearly.

---

## Why this design replaced the earlier threaded API

The old threaded surface had several problems:

- It leaked `PdfPageDecoder` into user code.
- It required private `PdfDocument` conversion helpers to turn results into `SegmentedPdfPage`.
- It used 0-indexed page numbers, unlike the sequential API.
- It split parsing and rendering into redundant public threaded classes.
- It returned raw timing dicts instead of `Timings`.
- It had no first-class selected-page scheduling or unload lifecycle on the Python API.

The implemented design resolves those issues without changing the sequential parser contract.

---

## Conversion model

The canonical conversion helper is now the public module-level function:

```python
segmented_page_from_decoder(
    page_decoder: PdfPageDecoder,
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
    content_config: ContentConfig | None = None,
) -> SegmentedPdfPage
```

This is used by both the sequential and threaded paths.

`PdfDocument._to_segmented_page_from_decoder()` still exists as a thin wrapper for internal sequential use, but threaded callers no longer need any private `PdfDocument` methods.

---

## Document loading and scheduling

### Loading

```python
doc_key = parser.load(
    path_or_stream,
    password: str | None = None,
    page_numbers: Sequence[int] | None = None,
)
```

Behavior:

- `page_numbers` is optional.
- When provided, it is interpreted as 1-indexed physical page numbers.
- The C++ layer normalizes the scheduled subset by sorting and de-duplicating it.
- Out-of-range page numbers raise a `RuntimeError`.
- The returned `doc_key` is the routing key for later results and metadata queries.

### Page counts

Two count queries are available immediately after `load()`:

```python
page_count(doc_key) -> int
scheduled_page_count(doc_key) -> int
```

Semantics:

- `page_count(doc_key)` is the physical page count of the loaded document.
- `scheduled_page_count(doc_key)` is the number of pages that will actually be emitted by the threaded parser for that document.

This distinction matters when `page_numbers` is used.

---

## Config model after v7

The v7 bump moved the public API away from exposing the pybind
`DecodePageConfig` directly.

### `DecodeConfig`

`DecodeConfig` controls how a page is computed:

- sanitization
- cell-merging tolerances
- bitmap/line caps
- thread-safety/native-memory knobs
- glyph and qpdf-warning retention

It is fixed when the document or threaded batch is opened.

### `ContentConfig`

`ContentConfig` controls what page entities are:

- skipped
- computed in C++ only
- computed and materialized into `SegmentedPdfPage`

Those levels are expressed with `ContentLevel`:

- `SKIP`
- `COMPUTE`
- `COMPUTE_AND_MATERIALIZE`

Sequential behavior:

- `DoclingPdfParser.load(..., content_config=...)` sets the document default
- `PdfDocument.get_page(..., content_config=...)` may escalate a page and trigger a re-decode when the cached decoder lacks the requested entities

Threaded behavior:

- `ThreadedPdfParserConfig.page_content_config` sets one batch-wide decode mask
- `PageParseResult.get_page(content_config=...)` may materialize more from that already-computed batch result
- it cannot recover entities the batch skipped entirely

---

## Result delivery model

### Completion order

`iterate_results()` yields results in completion order, not page-number order.

If callers need in-order processing, they should collect by `page_number` and sort after consumption.

### Manual vs iterator control

The threaded parser intentionally exposes both:

- `has_tasks()`
- `get_task()`
- `iterate_results()`

`has_tasks()` is not deprecated. It remains the manual-control escape hatch.

Important runtime detail:

- The first call to `has_tasks()` starts the threaded work by building the task queue and launching workers.
- `iterate_results()` simply loops on `has_tasks()` and `get_task()`.

---

## Cleanup and unload behavior

The threaded parser now has explicit lifecycle cleanup:

```python
unload(doc_key: str) -> bool
unload_all() -> None
```

Semantics:

- `unload(doc_key)` removes one loaded document after threaded processing has completed.
- `unload_all()` clears all loaded documents after threaded processing has completed.
- Python-side count bookkeeping is cleared together with the underlying parser state.
- `unload(doc_key)` is idempotent after successful consumption:
  - first unload returns `True`
  - unloading the same key again returns `False`
- Unloading during active threaded iteration raises a clear runtime error.

The current implementation defines "safe to unload" by checking whether results remain to be consumed, not whether worker threads have fully wound down. That matches the intended public contract: unloading should succeed once result consumption is complete.

---

## Image rendering model

Rendering is available only when the parser was created with `parser_config.render_config`.

For parse-only results:

- `has_image` is `False`
- `get_image(...)` raises `RuntimeError`

For parse-and-render results:

- the default render is produced during threaded parsing
- the image is exposed lazily through `get_image(...)`

### `get_image(...)` signature

```python
get_image(
    scale: float | None = None,
    canvas_size: tuple[int, int] | None = None,
    cropbox: BoundingBox | None = None,
) -> PIL.Image.Image
```

### Supported behavior

- `scale` and `canvas_size` are mutually exclusive.
- Calling `get_image()` with no arguments returns the default pre-rendered image.
- Calling `get_image(scale=...)` performs a true rerender from the retained `PdfPageDecoder` when needed.
- Calling `get_image(canvas_size=...)` rerenders to the requested canvas size when needed.
- Calling `get_image(..., cropbox=...)` crops in Python after full-page rendering.

### Important decisions reflected in the implementation

- `get_image(scale=...)` is allowed whenever `render_config` is present.
- It is not restricted to cases where the original `render_config` used `scale`.
- A caller may configure the threaded parser with `canvas_width` / `canvas_height` and later request `get_image(scale=2.0)`.
- Non-default scale requests rerender from the decoder; they do not resize the existing default bitmap.

### Crop semantics

- `cropbox` is specified in page coordinates.
- Cropping is done in Python against the rendered full-page image.
- Page-coordinate conversion uses the page height and rendered image dimensions.
- Degenerate page dimensions are handled defensively by returning the uncropped image rather than dividing by zero.

### Cache behavior

- The default full-page image is cached lazily per `PageParseResult`.
- Requests matching the default render can reuse that cached image.
- Rerendered `scale` and `canvas_size` requests are generated on demand from the decoder.
- There is no aggressive per-scale or per-crop cache inside `docling-parse`.

### Thread efficiency

The expensive C++ rerender path used by `PageParseResult.get_image(scale=...)` / `get_image(canvas_size=...)` releases the Python GIL during instruction replay, matching the threaded API's performance goals.

---

## Parse-only example

```python
from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)

parser = DoclingThreadedPdfParser(
    parser_config=ThreadedPdfParserConfig(
        threads=4,
        page_content_config=ContentConfig(
            word_cells_content_level=ContentLevel.COMPUTE,
            line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        ),
    ),
    decode_config=DecodeConfig(),
)

doc_key = parser.load(path, page_numbers=[1, 3, 5])
total_pages = parser.page_count(doc_key)
scheduled_pages = parser.scheduled_page_count(doc_key)

for result in parser.iterate_results():
    if not result.success:
        print(f"{result.doc_key} p{result.page_number}: {result.error_message}")
        continue

    page = result.get_page(
        ContentConfig(
            word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
            line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        )
    )
    size = (result.page_width, result.page_height)
```

---

## Parse-and-render example

```python
from docling_core.types.doc.base import BoundingBox, CoordOrigin
from docling_parse.pdf_parser import (
    DecodeConfig,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

render_config = RenderConfig()
render_config.canvas_width = 1024

parser = DoclingThreadedPdfParser(
    parser_config=ThreadedPdfParserConfig(
        threads=4,
        render_config=render_config,
    ),
    decode_config=DecodeConfig(),
)

doc_key = parser.load(path)

for result in parser.iterate_results():
    if not result.success:
        continue

    page = result.get_page()
    default_image = result.get_image()
    scaled_image = result.get_image(scale=2.0)
    cropped = result.get_image(
        scale=2.0,
        cropbox=BoundingBox(
            l=10,
            t=20,
            r=60,
            b=90,
            coord_origin=CoordOrigin.TOPLEFT,
        ),
    )
```

---

## Sequential path after v7

The sequential parser kept the same main entry points, but not the same config
surface.

What stayed:

- `DoclingPdfParser`
- `PdfDocument`
- `PdfDocument.get_page()`
- `PdfDocument.iterate_pages()`
- `PdfDocument.get_page_with_timings()`

What changed:

- `DecodePageConfig` is no longer the public Python config type
- `DoclingPdfParser.load()` now accepts `decode_config` and `content_config`
- `PdfDocument.get_page()` and `iterate_pages()` accept `content_config`, not per-call decode config
- escalating `content_config` may evict and re-decode a page when the cached decoder lacks the requested entities

---

## Summary of implemented decisions

- One public threaded parser interface, not separate parser and renderer APIs.
- Public `DecodeConfig` + `ContentConfig` split instead of exposing pybind `DecodePageConfig` directly.
- Typed `PageParseResult` objects instead of raw decoder-centric result objects.
- Public `segmented_page_from_decoder(...)` as the canonical conversion entry point.
- 1-indexed threaded `page_number`.
- `boundary_type` configured on `ThreadedPdfParserConfig`.
- `page_content_config` for batch-wide threaded content selection.
- `page_count()` plus `scheduled_page_count()` for subset-aware scheduling.
- `unload()` and `unload_all()` as explicit threaded lifecycle cleanup.
- `PageParseResult.get_page(content_config=...)` for per-result materialization upgrades within the batch mask.
- `get_image(scale=...)`, `get_image(canvas_size=...)`, and Python-side `cropbox` support on `PageParseResult`.
- True rerendering from the retained decoder for non-default render requests.
