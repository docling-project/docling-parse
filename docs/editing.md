# PDF reading and editing

`PdfEditor` is the preferred docling-parse API for reading, extracting text
from, editing and writing PDFs. Use `PdfReader` and `PdfWriter` when migrating
existing pypdf code that uses the [supported workflows](#migrate-from-pypdf).

## Quick start

Open a PDF, count its pages, extract layout text, rotate the first page,
and preview and save the result.

```python
from docling_parse import PdfEditor, PdfExtractionMode

with PdfEditor(source="document.pdf") as editor:
    print(editor.page_count(), editor.metadata.get("Title"))
    print(editor.extract_text(page_no=1, extraction_mode=PdfExtractionMode.LAYOUT))
    editor.rotate_page(page_no=1, angle=90)
    editor.render_page(page_no=1, scale=2.0).save("preview.png")
    editor.write(target="changed.pdf")
```

`PdfExtractionMode.PLAIN` is the default. It returns reading-order lines.
`PdfExtractionMode.LAYOUT` retains line and column spacing in fixed-width
text. Both modes include unsaved edits.

## Guides

| Guide | Topics |
|---|---|
| [Pages](editing/pages.md) | Opening, text extraction, assembly, geometry, stamps and watermarks |
| [Navigation](editing/navigation.md) | Bookmarks, destinations, labels and contents pages; preservation limits |
| [Forms](editing/forms.md) | Inspect, fill, create, update, remove and flatten fields |
| [Annotations and attachments](editing/annotations-and-attachments.md) | Links, comments, embedded files and scripts |
| [Text and images](editing/text-and-images.md) | Draw text, export images and create PDFs from images |
| [Rendering](editing/rendering.md) | Render a selected page, including unsaved edits |
| [Output and encryption](editing/output-and-encryption.md) | Paths, streams, passwords, permissions and serialization |

## Page numbers and page views

Pass Editor arguments by keyword. Use one-based numbers for `page_no`,
`before_page_no` and the source pages in `page_nos`. The same numbering
applies to destination, bookmark, form and link results from the Editor.
Bookmark sibling positions such as `move_outline_item(index=0)` use
zero-based positions within their parent.

| Task | Editor call |
|---|---|
| Count pages | `editor.page_count()` |
| Inspect first-page geometry, annotations or images | `editor.get_page(page_no=1)` |
| Extract text from the first page | `editor.extract_text(page_no=1)` |
| Remove the first page | `editor.remove_page(page_no=1)` |
| Copy the first and third source pages | `editor.append(source=other, page_nos=[1, 3])` |

`get_page` returns a read-only view with a one-based `page_no`. Use the
Editor methods to change pages. Iterate over page numbers with
`range(1, editor.page_count() + 1)`.

## Document information

These properties are available on Reader, Writer and Editor:

| Property | Result |
|---|---|
| `metadata` | `/Info` strings as a dictionary: `{"Title": "..."}` |
| `xmp_metadata` | Raw XMP packet as text, or `None` |
| `pdf_header`, `pdf_version` | Header such as `"%PDF-1.7"`, and version such as `"1.7"` |
| `is_encrypted`, `encryption` | Encryption status and parameters |
| `permissions` | Permission booleans, or `None` for an unencrypted document |
| `outline` | Bookmark dictionaries with resolved page targets |
| `named_destinations`, `open_destination` | Resolved destinations |
| `page_labels` | Display labels, one per page |
| `page_layout`, `page_mode`, `viewer_preferences` | Viewer settings |
| `has_form`, `has_xfa` | AcroForm/XFA presence |
| `attachment_info`, `attachments`, `javascript` | Embedded-file metadata, bytes and scripts |

These are detached Python values, not pypdf's `DocumentInformation`,
`Destination` or `Field` objects. Editing a returned dictionary does not edit
the PDF. On `PdfEditor`, `get_object(object_id=12, depth=2)`, `trailer` and
`root_object` provide read-only object views; `annotations` returns the current
document's form, language, XMP, outline and structure information.

## Behavior to know about

- **Lazy work.** Opening parses document structure; text, image and render
  requests decode pages on demand. Editing and serialization run in C++.
  Measured comparisons of these editing workflows against pypdf are still
  outstanding; no speedup is claimed here.
- **Source ownership.** Input streams are read and never closed by the library.
  An opened document costs at least its input byte size in memory. An editor
  starts a separate editing copy on its first change. Imported source
  documents remain alive until the importing document closes.
- **Page handles.** Removing or moving pages, or inserting before existing
  pages, invalidates old handles with `PdfStaleHandleError`. Take a new handle
  afterward. In-place edits and appending at the end keep them valid.
- **Thread safety.** Calls on one object are serialized. Use separate objects
  for parallel throughput.
- **Signatures.** Output is a full rewrite; existing digital signatures do not
  remain valid. Signature fields can be created, but signing and signature
  validation are not implemented.
- **Permissions and scripts.** Permission flags are reported, not enforced.
  JavaScript is stored, listed and removed; it is never executed by this library.

Errors are in `docling_parse.pdf_errors`:

| Exception | Meaning |
|---|---|
| `PdfLoadError` | Input could not be opened; `category` gives the reason |
| `PdfPasswordError` | Password is missing or wrong; a `PdfLoadError` |
| `PdfEditError` | Invalid edit or failure in a native operation |
| `PdfUnsupportedError` | A requested feature is unsupported; a `PdfEditError` |
| `PdfStaleHandleError` | A handle no longer identifies its original page |
| `PdfWriteError` | Native serialization or output failed |

Python argument and stream operations can also raise `TypeError`,
`ValueError`, `IndexError` or `OSError`.

## API coverage

| Workflow | Current support and limits |
|---|---|
| Input and pages | Paths, bytes, binary streams, passwords, and read-only page views |
| Text extraction | `PdfExtractionMode.PLAIN` and `PdfExtractionMode.LAYOUT` |
| Assembly | Clone, append, insert, delete, reorder, duplicate and blank pages |
| Geometry and composition | Boxes, rotation, scale, affine transforms, vector overlays and underlays |
| Navigation | Read/add/edit/move/remove bookmarks; contents pages; read/add named destinations; page labels and viewer settings |
| Forms | Read/fill/create/update/remove/flatten; XFA presence only |
| Annotations and attachments | Read/add/remove annotations and links; read/add/replace/remove attachments; script storage/removal |
| Text drawing | Twelve Latin standard fonts and WinAnsi characters |
| Images | Export; new image pages, including multi-frame files; no image replacement or placement on existing pages |
| Rendering | `PdfEditor.render_page(page_no=..., scale=...)` |
| Encryption | AES-256 editor default; pypdf-compatible writer algorithm and permission arguments |
| Output | Stream compression, object streams and linearization; full rewrites |
| Raw PDF objects | Read-only JSON-like views on Editor; no editable object/stream model |
| Missing workflows | Incremental writing, text/image removal, deduplication, image recompression, article threads and signing |

## Migrate from pypdf

The compatibility reference is pypdf 6.21.1. For code that uses the
workflows below, start by switching the import and running the application's
tests against its PDFs.

```python
# Replace: from pypdf import PdfReader, PdfWriter
from docling_parse import PdfReader, PdfWriter

with PdfReader("document.pdf") as reader, PdfWriter() as writer:
    print(reader.pages[0].extract_text(extraction_mode="layout"))
    writer.add_page(reader.pages[0])
    writer.append("appendix.pdf", pages=[0, 2])
    writer.write("assembled.pdf")
```

| Workflow | Compatibility calls covered by tests | Contract |
|---|---|---|
| Open, count and select pages | `PdfReader(source)`, `get_num_pages()`, `get_page(0)`, `pages[-1]`, `pages[:2]` | Reader and Writer page indices and page numbers are zero-based. |
| Extract text | `page.extract_text()`, `page.extract_text(extraction_mode="layout")` | Returns docling-parse text in the selected mode. |
| Assemble and write | `add_page`, `insert_page`, `append`, `merge`, `add_blank_page`, `insert_blank_page`, `remove_page`, `write` | Call `write` explicitly. It returns `(opened_by_writer, stream)`. |
| Compose pages | `writer.pages[i].merge_page(page2, expand=False, over=True)` | Positional arguments follow pypdf. Expansion is unsupported. |
| Inspect forms | `get_fields`, `get_form_text_fields`, `get_pages_showing_field` | Fields are detached Python dictionaries. |
| Encrypt output | `writer.encrypt(..., algorithm=...)` | Accepts pypdf's algorithm and permission flags. |

Metadata, outlines, destinations and annotations return docling-parse's
inspection data. `add_attachment` and `add_named_destination` return
`None`. Use the Editor for form authoring, TOC generation, image import,
text drawing and output options.

The compatibility adapters raise `PdfUnsupportedError` for cases including
text visitors and custom extraction options, `expand=True` for page merges
or transformations, nonempty `excluded_fields` or `excluded_keys`,
non-default root-object recovery limits, subtree form traversal, and fit modes
other than `/Fit` (plus `/XYZ` for bookmarks). Writer constructor options
`incremental=True`, `full=True`, `keep_initial_header=True` and custom
incremental clone limits also raise that error.
