# PDF reading and editing

Use `PdfReader` to inspect a PDF, `PdfWriter` for the implemented pypdf-style
workflows, and `PdfEditor` for the full editing API and page rendering. The
three classes share one native implementation; Reader and Writer adapt their
calls to the editor.

The compatibility reference is **pypdf 6.21.1**, pinned in the development
dependencies. Signatures of the implemented Reader/Writer methods are checked
against that version. This is a supported subset, with different result types
and unsupported options; it is not a drop-in replacement for all pypdf code.
See [API coverage](#api-coverage) before migrating a workflow.

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

## Which class to use

| Class | Purpose | Can edit |
|---|---|---|
| `PdfReader` | Inspect pages, text, images and document information | No |
| `PdfWriter` | Assemble or clone documents through pypdf-style methods | Yes |
| `PdfEditor` | Inspect, edit, render and write through named methods | Yes |

`PdfReader` exposes read-only page handles. `PdfWriter` and `PdfEditor` expose
editable ones. Import all three from the package:

```python
from docling_parse import PdfEditor, PdfReader, PdfWriter
```

## Arguments and page numbering

**All public PdfEditor method parameters, including its constructor, are
keyword-only.** Arguments such as `page_no` and `before_page_no` start at 1.
`page_nos` selects one-based source pages. Zero and negative page numbers are
rejected.

Reader/Writer compatibility methods keep the pypdf argument names, order and
zero-based page indices. Every `pages` sequence is a Python sequence, including
`editor.pages`: indexing starts at 0, negative indices count from the end, and
slices are half-open.

| Expression | Meaning |
|---|---|
| `editor.get_page(page_no=1)` | First page |
| `editor.render_page(page_no=1, scale=2.0)` | Render the first page |
| `editor.remove_page(page_no=1)` | Remove the first page |
| `editor.append(source=other, page_nos=[1, 3])` | Copy the first and third source pages |
| `reader.get_page(0)` or `reader.pages[0]` | First page |
| `writer.remove_page(0)` | Remove the first page |
| `writer.append(other, pages=[0, 2])` | Copy the first and third source pages |

Page handles keep zero-based `index` and `page_number` properties.
`editor.get_page_no(page=page)` returns a one-based number;
`reader.get_page_number(page)` returns a zero-based index. Inspection
dictionaries also retain zero-based `page` and `pages` values on all three
classes. Convert such a value with `index + 1` when passing it to an editor
page-number argument. Bookmark sibling positions, such as
`move_outline_item(index=0)`, are ordinary zero-based positions, not page numbers.

## Quick start

Read text and metadata:

```python
from docling_parse import PdfReader

with PdfReader("document.pdf") as reader:
    print(len(reader.pages), reader.metadata.get("Title"))
    for page in reader.pages:
        print(page.extract_text())
```

Merge selected pages through the compatibility API:

```python
from docling_parse import PdfWriter

with PdfWriter() as writer:
    writer.append("document.pdf")
    writer.append("appendix.pdf", pages=[0, 2])
    writer.write("merged.pdf")
```

Edit and render before saving (this example assumes at least four pages):

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.get_page(page_no=1).rotate(angle=90)
    editor.remove_page(page_no=4)
    editor.render_page(page_no=1, scale=2.0).save("preview.png")
    editor.write(target="changed.pdf")
```

Make a PDF from image files:

```python
from docling_parse import PdfEditor

with PdfEditor.from_images(
    images=["scan-1.jpg", "scan-2.png"], page_size="A4"
) as editor:
    editor.write(target="scans.pdf")
```

## Document information

These properties are available on Reader, Writer and Editor:

| Property | Result |
|---|---|
| `metadata` | `/Info` strings as a dictionary: `{"Title": "..."}` |
| `xmp_metadata` | Raw XMP packet as text, or `None` |
| `pdf_header`, `pdf_version` | Header such as `"%PDF-1.7"`, and version such as `"1.7"` |
| `is_encrypted`, `encryption` | Encryption status and parameters |
| `permissions` | Permission booleans, or `None` for an unencrypted document |
| `outline` | Bookmark dictionaries with resolved page indices |
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
| Input and pages | Paths, bytes, binary streams, passwords; lazy page sequence |
| Text extraction | Plain and layout modes; no visitors or other pypdf extraction options |
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

Matching a method signature does not imply that every pypdf option is
implemented. In particular, nonempty `excluded_fields`/`excluded_keys`,
non-default root-object recovery limits, subtree form traversal, and fit modes
other than `/Fit` (plus `/XYZ` for bookmarks) raise `PdfUnsupportedError`.
`PdfWriter(incremental=True)` also raises that error.

`PdfWriter.write(stream)` returns `(opened_by_writer, stream)`, following pypdf;
`PdfEditor.write(target=...)` returns `None`. Writer methods that resemble
pypdf object factories do not provide its live object model; for example,
`add_attachment` and `add_named_destination` currently return `None`.

The topic guides document these implemented workflows and their limits.
The unsupported capabilities listed above remain unfinished work; signature
compatibility alone is not evidence of full feature parity.

