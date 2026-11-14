# PDF reading and editing

docling-parse reads, assembles and changes PDF files through three classes:
`PdfReader`, `PdfWriter` and `PdfEditor`. Their interface follows
[pypdf](https://pypdf.readthedocs.io/), so most pypdf code moves over by
changing an import. The work itself is done in C++, on top of
[qpdf](https://qpdf.readthedocs.io/) and the docling-parse page decoder.

- [Which class to use](#which-class-to-use)
- [Quick start](#quick-start)
- [Why use it](#why-use-it)
- [Replacing pypdf's PdfReader](#replacing-pypdfs-pdfreader)
- [Replacing pypdf's PdfWriter](#replacing-pypdfs-pdfwriter)
- [PdfEditor: reading and writing in one object](#pdfeditor-reading-and-writing-in-one-object)
- [Behavior to know about](#behavior-to-know-about)
- [Coverage of the pypdf interface](#coverage-of-the-pypdf-interface)

## Which class to use

| Class | Use it to | Can change the document |
|---|---|---|
| `PdfReader` | open a PDF and read its pages, text, images and metadata | no |
| `PdfWriter` | build a PDF from other files, or change a copy of one, and write it | yes |
| `PdfEditor` | do both with one object, plus operations pypdf has no name for | yes |

All three are faces of one native implementation. `PdfReader` and `PdfWriter`
offer the part of it that matches pypdf; `PdfEditor` offers all of it.

```python
from docling_parse import PdfEditor, PdfReader, PdfWriter
```

## Quick start

Read text and metadata:

```python
from docling_parse import PdfReader

with PdfReader("document.pdf") as reader:
    print(len(reader.pages), "pages")
    print(reader.metadata.get("Title"))
    for page in reader.pages:
        print(page.extract_text())
```

Merge files:

```python
from docling_parse import PdfWriter

writer = PdfWriter()
writer.append("file1.pdf")
writer.append("file2.pdf", pages=[0, 2])
writer.write("merged.pdf")
```

Change a file and save it:

```python
from docling_parse import PdfEditor

with PdfEditor("document.pdf") as editor:
    editor.pages[0].rotate(90)
    del editor.pages[3]
    editor.write("changed.pdf")
```

## Why use it

**Performance.** Parsing, page assembly, text extraction and serialization
run in native code, and every call releases Python's global interpreter lock
while it works. Opening a file reads its structure only: a page is decoded
when its text or images are asked for, and a file that is only split or merged
is never decoded at all. Pages copied between files keep their content streams
as they are; nothing is re-encoded.

> Measured comparisons against pypdf (load, text extraction, split, merge,
> memory) will be added here once the benchmark has been run. Until then,
> treat the paragraph above as a description of the design, not as a
> measurement.

**Ease of use.**

- One import and the pypdf method names you already know: `pages`, `append`,
  `add_page`, `merge_page`, `rotate`, `encrypt`, `write`.
- A source can be a path, `bytes`, a binary stream, or another reader, writer
  or editor. A stream you pass in is read and never closed.
- Readers, writers and editors are context managers.
- Writing to a path is atomic: the file appears complete or not at all.
- A page handle that no longer points at the page you took it from raises an
  error instead of quietly acting on a different page.
- One object can be shared between threads.
- Failures raise typed exceptions that say what went wrong: a wrong password
  is not reported the same way as a damaged file.

## Replacing pypdf's PdfReader

```python
# pypdf
from pypdf import PdfReader

# docling-parse
from docling_parse import PdfReader
```

### Opening a document

```python
reader = PdfReader("document.pdf")
reader = PdfReader(open("document.pdf", "rb"))
reader = PdfReader(pdf_bytes)
```

An encrypted document takes its password when it is opened. There is no
separate `decrypt()` step:

```python
# pypdf
reader = PdfReader("protected.pdf")
if reader.is_encrypted:
    reader.decrypt("secret")

# docling-parse
reader = PdfReader("protected.pdf", password="secret")
```

Opening without the password, or with a wrong one, raises `PdfPasswordError`.

### Pages and text

The same in both libraries:

```python
number_of_pages = len(reader.pages)

first = reader.pages[0]
last = reader.pages[-1]
some = reader.pages[2:5]

for page in reader.pages:
    text = page.extract_text()
```

`extract_text()` returns the page's text lines joined by newlines, in the
reading order of the docling-parse page decoder. The text is not guaranteed to
be character for character what pypdf returns: the two libraries group
characters into lines differently.

### Page geometry

```python
page = reader.pages[0]

page.mediabox            # PdfBox(left, bottom, right, top)
page.mediabox.width
page.mediabox.height
page.cropbox, page.trimbox, page.bleedbox, page.artbox
page.rotation            # 0, 90, 180 or 270
page.user_unit
page.page_number         # zero-based position in the document
```

Boxes are in points (1/72 inch). A box the page does not define takes its
default: the crop box defaults to the media box, the other three to the crop
box.

### Document information

```python
reader.metadata          # {"Title": "...", "Author": "...", ...}
reader.xmp_metadata      # the XMP packet as a string, or None
reader.pdf_header        # "%PDF-1.7"
reader.is_encrypted
reader.encryption        # None, or the encryption parameters
reader.outline           # bookmarks, with resolved destinations
```

Differences from pypdf:

- `metadata` is a plain dictionary keyed by the PDF names without their slash
  (`metadata["Title"]`, not `metadata.title`).
- `xmp_metadata` is the raw packet, not a parsed object.
- `outline` is a nested list of dictionaries, not pypdf `Destination` objects.

### Images

```python
for image in reader.pages[0].images:
    print(image.name, image.width, image.height, image.format)
    image.save(f"image{image.extension}")
```

Each image carries `name`, `bbox` (where it sits on the page), `width`,
`height`, `format` (`"jpeg"`, `"jp2"`, `"jbig2"` or `"raw"`), `mode` and
`data`. `image.to_pil()` returns a Pillow image; Pillow is imported only when
that method is called.

### Not available on the reader yet

Form fields, attachments, named destinations, page labels, viewer preferences
and access to raw PDF objects. See
[Coverage of the pypdf interface](#coverage-of-the-pypdf-interface).

## Replacing pypdf's PdfWriter

```python
# pypdf
from pypdf import PdfWriter

# docling-parse
from docling_parse import PdfWriter
```

### Merging and splitting

The same in both libraries:

```python
writer = PdfWriter()
writer.append("file1.pdf")
writer.append("file2.pdf", pages=[0, 2])      # selected pages
writer.merge(1, "cover.pdf")                  # at a position
writer.write("merged.pdf")
```

```python
reader = PdfReader("document.pdf")

for index, page in enumerate(reader.pages):
    writer = PdfWriter()
    writer.add_page(page)
    writer.write(f"page-{index}.pdf")
```

`pages` accepts a list of zero-based indices (which may repeat or reorder
pages) or a slice. `append` also takes `index=` to choose where the pages go.

### Changing an existing document

```python
writer = PdfWriter(clone_from="document.pdf")

writer.pages[0].rotate(90)          # add 90 degrees clockwise
writer.pages[1].rotation = 0        # set the rotation outright
writer.pages[2].cropbox = (36, 36, 559, 806)

del writer.pages[3]                 # or writer.remove_page(3)
writer.move_page(0, 2)
writer.add_blank_page(595, 842)
writer.insert_blank_page(0, 595, 842)

writer.write("changed.pdf")
```

`clone_from` starts from a complete copy of the document: its forms, outline,
structure tree and attachments are kept. The source is never modified.

Note the argument order of `insert_blank_page(index, width, height)`: pypdf
takes `(width, height, index)`.

### Scaling and transforming pages

```python
page = writer.pages[0]

page.scale(2, 0.5)                          # horizontally, vertically
page.scale_by(0.5)
page.scale_to(595, 842)                     # to a size in points
page.add_transformation((1, 0, 0, 1, 20, 0))  # a b c d e f
page.transfer_rotation_to_content()
```

`scale` changes the content, the annotations and the page boxes together.
`add_transformation` changes the content and the annotations and leaves the
boxes alone.

### Stamps and watermarks

```python
stamp = PdfReader("stamp.pdf").pages[0]

for page in writer.pages:
    page.merge_page(stamp)              # on top of the page: a stamp
    page.merge_page(stamp, over=False)  # underneath: a watermark
```

The other page is drawn as vector content, at its own coordinates. Variants
place it differently:

```python
page.merge_scaled_page(stamp, 0.5)
page.merge_translated_page(stamp, 100, 200)
page.merge_rotated_page(stamp, 45)                    # counter-clockwise
page.merge_transformed_page(stamp, (1, 0, 0, 1, 50, 50))
page.merge_page(stamp, fit=True)                      # centered, scaled to fill
```

`fit=True` has no pypdf counterpart. The annotations and form fields of the
merged page are not copied.

### Encryption

```python
writer.encrypt("user-password")
writer.encrypt("user-password", owner_password="owner-password")
writer.write("protected.pdf")
```

Output is encrypted with AES-256; older, weaker algorithms are not offered,
and there is no `algorithm` argument. All permissions are granted.

Output is unencrypted unless `encrypt()` was called, also when the source was
encrypted.

### Writing

```python
writer.write("out.pdf")         # a path
writer.write(stream)            # a binary stream; not closed
data = writer.to_bytes()        # the file as bytes
```

### Not available on the writer yet

Writing metadata, creating or editing outlines, filling forms, adding
annotations or attachments, and the compression and content-removal options.

## PdfEditor: reading and writing in one object

`PdfEditor` opens a document for reading and becomes a writer when you first
change something. Until that first change it costs the same as a `PdfReader`.

```python
from docling_parse import PdfEditor

with PdfEditor("document.pdf") as editor:
    print(editor.metadata, editor.pages[0].extract_text())

    editor.pages[0].rotate(90)
    editor.append("appendix.pdf")
    print(editor.pages[-1].extract_text())   # reads the edited document

    editor.write("out.pdf")
```

`PdfEditor()` without a source starts an empty document. An opened source is
not modified unless you write to its path.

It has everything `PdfReader` and `PdfWriter` have, and:

| | |
|---|---|
| `editor.is_modified` | whether anything has been changed yet |
| `editor.annotations` | form, language, outline and structure tree of the document as it currently is |
| `editor.duplicate_page(index, to=None)` | add a copy of a page that can be edited separately |
| `editor.move_page(from_index, to_index)` | reorder pages |
| `page.merge_page(other, fit=True)` | overlay centered and scaled to the page |
| `editor.remove_encryption()` | undo `encrypt()` |

`duplicate_page`, `move_page`, `merge_page(fit=True)` and `remove_encryption`
are available on `PdfWriter` as well.

## Behavior to know about

**Page handles go stale.** A page object refers to a position in the page
list. After pages are removed, moved or inserted in front of it, using that
object raises `PdfStaleHandleError`; take a new one from `pages`. Editing a
page in place, or adding pages at the end, keeps existing handles valid.

```python
page = writer.pages[2]
del writer.pages[0]
page.rotate(90)             # raises PdfStaleHandleError
writer.pages[1].rotate(90)  # the same page, by its new position
```

**Thread safety.** One reader, writer or editor may be used from several
threads. Calls on the same object run one after the other; for parallel
throughput use one object per thread.

**Memory.** A document is read into memory when it is opened, so an open
document costs at least its file size.

**Signatures.** Writing always produces a full rewrite of the file. Digital
signatures in the source do not stay valid.

**Permissions.** The permission flags of an encrypted document are reported
(`reader.encryption["permissions"]`) and not enforced.

**Errors.** All in `docling_parse.pdf_errors`:

| Exception | Raised when |
|---|---|
| `PdfLoadError` | a document cannot be opened; `category` says why |
| `PdfPasswordError` | the password is missing or wrong (a `PdfLoadError`) |
| `PdfEditError` | an operation fails; `category` says why |
| `PdfStaleHandleError` | a page handle is used after the page list changed (a `PdfEditError`) |
| `PdfWriteError` | the output cannot be produced or written (a `PdfEditError`) |

## Coverage of the pypdf interface

What works today, by task:

| Task | Status |
|---|---|
| Open from path, bytes or stream; encrypted input | supported |
| Page count, indexing, slicing, iteration | supported |
| Text extraction | plain text; no layout mode or visitor functions |
| Page boxes, rotation, user unit | supported |
| Metadata, XMP, outline | read only |
| Images | export; no replacement |
| Merge, split, append, insert, delete, reorder, blank pages | supported |
| Rotate, crop, scale, transform | supported |
| Stamps and watermarks | supported; annotations of the merged page are not copied |
| Encrypt output | AES-256; no permission settings |
| Write metadata, outlines, page labels | not yet |
| Forms: read, fill, flatten | not yet |
| Annotations and attachments | not yet |
| Compression and content removal | not yet |
| Incremental writing | not yet |
