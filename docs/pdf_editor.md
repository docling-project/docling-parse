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

Turn scans or photos into a PDF:

```python
from docling_parse import PdfWriter

writer = PdfWriter.from_images(["scan-1.jpg", "scan-2.png"], page_size="A4")
writer.write("scans.pdf")
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

Layout mode keeps the page's columns, indentation and vertical spacing by
placing the words on a character grid. Use it for tables and forms that you
want to read as fixed-width text:

```python
text = page.extract_text(extraction_mode="layout")
```

Visitor functions and the other `extract_text` options of pypdf are not
available.

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
reader.permissions       # None, or {"print": True, "extract": False, ...}
```

Differences from pypdf:

- `metadata` is a plain dictionary keyed by the PDF names without their slash
  (`metadata["Title"]`, not `metadata.title`).
- `xmp_metadata` is the raw packet, not a parsed object.
- `permissions` is a dictionary of booleans; pypdf's
  `user_access_permissions` is a flag enumeration.

### Navigation

```python
reader.outline               # bookmarks, as a tree
reader.page_labels           # ["i", "ii", "1", "2", ...]
reader.named_destinations    # {"chapter-2": {"page": 4, "kind": "/Fit", ...}}
reader.open_destination      # where the document opens, or None
reader.page_layout           # "/TwoColumnLeft", ... or None
reader.page_mode             # "/UseOutlines", ... or None
reader.viewer_preferences    # {"/HideToolbar": True, ...} or None
```

Every place in the document is given as a zero-based page index, already
resolved: there are no destination objects to look up.

```python
def show(items, depth=0):
    for item in items:
        print("  " * depth, item["title"], "-> page", item["page"])
        show(item["children"], depth + 1)

show(reader.outline)
```

A bookmark is a dictionary with `title`, `page` (or None), `url` (for a
bookmark that opens a web address, else None), `open` (whether its children
are shown) and `children`. Its `id` identifies it to the writer's outline
methods; it is valid for the object that returned it and changes when the
file is written and opened again.

This replaces pypdf's nested lists of `Destination` objects and its
`get_destination_page_number()`.

### Forms

```python
reader.get_fields()              # every field, by fully qualified name
reader.get_form_text_fields()    # {"name": "Ada Lovelace", ...}
reader.has_form, reader.has_xfa

for name, field in reader.get_fields().items():
    print(name, field["kind"], field["value"])
```

A field is a dictionary with `kind` (`text`, `checkbox`, `radio`, `choice`,
`pushbutton`, `signature`), `value`, `default_value`, `choices`, `states`
(what a checkbox or radio group can be set to), `read_only` and `pages`.
`reader.get_pages_showing_field(name)` returns the pages a field appears on.

### Annotations

```python
for annotation in reader.pages[0].annotations:
    print(annotation["/Subtype"], annotation["/Rect"], annotation.get("/Contents"))
```

Each annotation is a dictionary keyed by PDF name. A link that points into
the document also has `"page"`, the index of its target.

### Attachments

```python
reader.attachments                    # {"notes.txt": [b"..."]}, as in pypdf
reader.get_attachment("notes.txt")    # b"..."
reader.attachment_info                # name, size, MIME type, dates
```

`reader.javascript` lists the document's scripts by name. Scripts are data:
docling-parse never runs them.

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

### Not available on the reader

Visitor functions for text extraction, article threads, the XFA form data,
and pypdf's object model (`get_object`, `IndirectObject`, `DictionaryObject`).
`PdfEditor` offers a read-only view of raw objects; see
[below](#pdfeditor-reading-and-writing-in-one-object).

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

Bookmarks come along: the bookmarks of the source that lead to a copied page
are added to the outline, pointing at the copies. Pass `import_outline=False`
to leave them behind. Bookmarks to pages that were not copied are dropped,
not left dangling.

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

### Metadata

```python
writer.add_metadata({"/Title": "Annual report", "/Author": "Finance"})
writer.add_metadata({"/Subject": None})      # removes the entry
writer.set_xmp_metadata(xmp_packet)          # or None to remove it
```

Keys may be written with or without their slash. Text outside ASCII is
stored correctly; no encoding step is needed.

### Bookmarks

```python
chapter = writer.add_outline_item("Chapter 1", 0)
writer.add_outline_item("Section 1.1", 2, parent=chapter)
writer.add_outline_item("Appendix", None)                  # a heading without a target
writer.add_outline_item("Project site", None, url="https://example.org")

# open at a position and magnification instead of the whole page
writer.add_outline_item("Figure 3", 7, top=400, zoom=1.5)

# styled, and folded until clicked
writer.add_outline_item("Notes", 9, color="#c00000", bold=True, is_open=False)
```

`add_outline_item` returns the bookmark's id, which `parent` and the editing
methods take. `left`, `top` and `zoom` give a position in points and a
magnification; any of them left at None keeps what the viewer has.

Existing bookmarks, including those of the opened file, are changed through
their ids from `writer.outline`:

```python
item = writer.outline[0]["id"]

writer.update_outline_item(item, title="1 Introduction", page_number=2)
writer.update_outline_item(item, bold=False, color=None)   # default style
writer.update_outline_item(item, page_number=None)         # no target
writer.move_outline_item(item, parent=None, index=0)       # first, top level
writer.remove_outline_item(item)                           # with its children
writer.clear_outline()                                     # all of them
```

An item cannot be moved below itself or its own children.

When a page is removed, bookmarks that led to it stay as headings without a
target, and bookmarks to the pages behind it keep pointing at those pages.

### A contents page

```python
writer.add_table_of_contents()                      # in front of page 1
writer.add_table_of_contents(index=1, title="Inhalt", max_depth=1,
                             page_size="A4", font="Times-Roman", size=12)
```

This inserts as many pages as the outline needs. Every bookmark becomes a
line, indented by its depth, with the label of the page it opens on the
right, and the whole line is a link to that page. Bookmarks that open a web
address or nothing are listed without a page and without a link.

The pages are drawn with one of the standard fonts (see
[Drawing text](#drawing-text)). A character those fonts cannot show is
replaced by "?", and a title that does not fit is shortened, by an estimate
of the font's character width. For a designed contents page, make it
elsewhere, add it with `append`, and lay links over it with `add_link`.

### Page labels and destinations

```python
writer.set_page_label(0, 3, "/r")                # i, ii, iii, iv
writer.set_page_label(4, 19, "/D")               # 1, 2, 3, ...
writer.set_page_label(20, 24, "/D", prefix="A-") # A-1, A-2, ...

writer.add_named_destination("summary", 5)
writer.set_open_destination(1)                   # open at the second page
```

### How the document opens

```python
writer.set_page_layout("/TwoColumnLeft")
writer.set_page_mode("/UseOutlines")
writer.set_viewer_preferences({"/HideToolbar": True, "/FitWindow": True})
```

Each takes `None` to remove the setting. In pypdf these are the `page_layout`
and `page_mode` properties and `create_viewer_preferences()`.

### Filling in forms

```python
writer = PdfWriter(clone_from="form.pdf")

writer.update_form_field_values({
    "name": "Ada Lovelace",       # a text field
    "country": "United Kingdom",  # a choice field
    "newsletter": True,           # a checkbox
    "plan": "/Premium",           # a radio group: the state to select
})
writer.write("filled.pdf")
```

- Fields are addressed by fully qualified name (`"address.street"`); a
  partial name works when only one field has it.
- The appearance of each field is rebuilt, so the values show in every
  viewer, not only in those that redraw forms themselves.
- A name the form does not have, or a state a button does not have, raises
  `PdfEditError`. Nothing is skipped silently.
- `writer.get_fields()` shows the fields, with `states` listing what each
  checkbox and radio group accepts.

Unlike pypdf's `update_page_form_field_values(page, fields)`, there is no
page argument: fields are set wherever they appear.

To turn a filled form into plain page content:

```python
writer.update_form_field_values(values, flatten=True)
# or, at any time
writer.flatten_annotations()
```

The values stay visible and the document is no longer interactive.
`flatten_annotations()` flattens the other visible annotations as well.

### Creating form fields

```python
writer.add_text_field(0, "name", (72, 700, 300, 720), "Ada Lovelace",
                      tooltip="Your full name")
writer.add_text_field(0, "notes", (72, 600, 300, 690), multiline=True)
writer.add_checkbox(0, "newsletter", (72, 570, 86, 584), checked=True)
writer.add_choice_field(0, "country", (72, 540, 300, 560),
                        ["Belgium", "Italy", "Switzerland"], "Italy")
writer.add_radio_group(0, "plan", {
    "basic": (72, 510, 86, 524),
    "premium": (120, 510, 134, 524),
}, selected="premium")
writer.add_signature_field(0, "signature", (72, 420, 300, 480))
```

- The first two arguments are the page and the field name; the name must be
  new in the form and contain no period. Rectangles are
  `(left, bottom, right, top)` in points.
- Every field kind takes `read_only`, `required` and `tooltip`; text and
  choice fields also take `font_size`.
- A choice field is a drop-down list. Its value must be one of the options,
  unless `editable=True` lets the user type their own.
- A radio group's buttons are named by their state; filling the form selects
  one by that name: `update_form_field_values({"plan": "/basic"})`.
- A signature field is an empty frame for a signing application to fill.
  docling-parse does not sign.
- Each field is drawn: text in Helvetica inside a thin frame, a check mark,
  a filled circle. The form shows in every viewer and can be flattened.

Existing fields, of the opened file or newly added, are changed and removed by
name:

```python
writer.update_form_field("name", new_name="full_name",
                         rect=(72, 690, 400, 720), read_only=True)
writer.update_form_field("country", options=["Belgium", "France"])
writer.update_form_field("full_name", tooltip=None)
writer.remove_form_field("newsletter")
```

`update_form_field` changes only what it is given. `rect` works for a field
with one widget; `options` for a choice field. `remove_form_field` takes the
field's widgets off their pages too.

### Annotations and links

```python
writer.add_uri(0, "https://example.org", (72, 700, 200, 720))
writer.add_link(0, (72, 660, 200, 680), 4)       # jump to page 5

writer.add_annotation(0, {
    "/Subtype": "/Text",
    "/Rect": [300, 700, 320, 720],
    "/Contents": "Please check this figure",
})

writer.remove_links()
writer.remove_annotations("/Highlight")
writer.remove_annotations()                      # all, except form fields
```

An annotation is given as a plain dictionary: a string starting with `/` is a
PDF name, any other string is text. Rectangles are
`(left, bottom, right, top)` in points. `remove_annotations("/Widget")`
removes form fields.

### Attachments

```python
writer.add_attachment("data.csv", csv_bytes)
writer.add_attachment("notes.txt", "plain text",
                      description="Reviewer notes", mime_type="text/plain")
writer.remove_attachment("data.csv")
```

### Scripts

```python
writer.add_js("this.print({bUI: true});")
writer.remove_javascript()
```

`remove_javascript()` strips the document's scripts, a scripted open action
and its additional actions: use it on files from sources you do not control.
Scripts, links and embedded files of a source are carried along by
`clone_from` and by `append`.

### Drawing text

```python
page = writer.pages[0]
page.draw_text("CONFIDENTIAL", 72, 800, size=24, font="Helvetica-Bold",
               color="#c00000")
page.draw_text("Approved by:\nDate:", 72, 120)    # two lines
```

`(x, y)` is where the first line's baseline starts, in points from the
bottom-left corner. A newline starts a new line 1.2 x `size` lower. `color`
is `(r, g, b)` with values from 0 to 1, or `"#rrggbb"`.

`font` is one of the twelve Latin standard fonts every PDF viewer has
(`STANDARD_FONTS`: Helvetica, Times and Courier, each regular, bold, italic
and bold italic), so nothing is embedded. They show the Western European
character set (WinAnsiEncoding: Latin-1 plus a few such as € and smart
quotes); other characters, such as Greek, Cyrillic or CJK, raise
`PdfUnsupportedError`. The text is real text: it can be selected, searched
and extracted.

This has no pypdf counterpart; pypdf users draw text with another library
and merge the page.

### Pages from images

```python
writer = PdfWriter.from_images(["page-1.jpg", "page-2.png", "page-3.tif"])

writer.add_image("photo.jpg")                         # at the end
writer.add_image(png_bytes, index=0)                  # in front
writer.add_image("scan.png", page_size="A4", margin=36)
writer.add_image(pil_image, dpi=300)
```

- An image is a path, bytes, a binary stream or a PIL image, in any format
  Pillow reads: JPEG, PNG, TIFF, GIF, BMP, WebP, ...
- JPEG files are stored as they are: no loss and no re-encoding. Other
  formats are stored losslessly, and transparency is kept.
- A multi-page TIFF or an animated GIF gives one page per frame. `add_image`
  returns the pages it added.
- Without `page_size`, a page is the size of its image at the image's
  resolution: `dpi`, or the resolution the file declares, or 72 (one pixel
  per point). `margin` adds a border.
- With `page_size` (`"A4"`, `"Letter"`, `"A3"`, `"A5"`, `"Legal"` or
  `(width, height)` in points) the image is centered and scaled to fill the
  page inside the margin, keeping its proportions.
- A photo stored sideways with an EXIF orientation shows upright.
- CMYK JPEGs keep their colors.

A file Pillow cannot read raises `PIL.UnidentifiedImageError` (an `OSError`).

`from_images` exists on `PdfEditor` as well. pypdf has no counterpart.

### Encryption

```python
writer.encrypt("user-password")
writer.encrypt("user-password", owner_password="owner-password")
writer.write("protected.pdf")
```

Output is encrypted with AES-256; older, weaker algorithms are not offered,
and there is no `algorithm` argument.

What the user password allows is set by keyword; everything is allowed unless
turned off:

```python
writer.encrypt(
    "user-password",
    "owner-password",
    allow_print="low",        # True, False or "low" (low resolution only)
    allow_extract=False,
    allow_modify=False,
    # allow_annotate, allow_fill_forms, allow_assemble, allow_accessibility
)
```

Viewers honor these flags; they are not access control.
`writer.remove_encryption()` undoes `encrypt()`.

Output is unencrypted unless `encrypt()` was called, also when the source was
encrypted.

### Writing

```python
writer.write("out.pdf")         # a path
writer.write(stream)            # a binary stream; not closed
data = writer.to_bytes()        # the file as bytes
```

File size and structure are chosen with `set_output_options`:

```python
writer.set_output_options(object_streams="generate")   # smallest files
writer.set_output_options(linearize=True)              # fast web view
writer.set_output_options(compress_streams=False,
                          object_streams="disable")    # readable in a text editor
```

Streams are compressed by default. `object_streams` is `"preserve"` (the
default), `"generate"` or `"disable"`.

### Not available on the writer

- Removing images or text from page content (`remove_images`, `remove_text`),
  replacing images, and merging identical objects
  (`compress_identical_objects`).
- Incremental writing: every write is a full rewrite, so signatures of the
  source do not stay valid.
- Article threads.
- Text in fonts other than the standard fonts, so no text outside the
  Western European character set; and no images placed on existing pages.
- pypdf's object model. Annotations and viewer preferences are given as
  plain dictionaries instead.

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
| `editor.get_object(n)` | a read-only view of PDF object number `n`, as nested dicts and lists |
| `editor.trailer`, `editor.root_object` | the same view of the file trailer and the document catalog |
| `editor.duplicate_page(index, to=None)` | add a copy of a page that can be edited separately |
| `editor.move_page(from_index, to_index)` | reorder pages |
| `page.merge_page(other, fit=True)` | overlay centered and scaled to the page |
| `editor.remove_encryption()` | undo `encrypt()` |

`duplicate_page`, `move_page`, `merge_page(fit=True)` and `remove_encryption`
are available on `PdfWriter` as well.

The object views are for looking inside a file, not for changing it:

```python
editor.root_object["/Pages"]["/Count"]
editor.get_object(12, depth=2)
```

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
(`reader.permissions`) and not enforced.

**Scripts and actions.** JavaScript in a document is stored, listed and
removed. It is never run.

**XFA forms.** `has_xfa` tells you a form carries an XFA definition. Values
set with `update_form_field_values` go to the AcroForm fields, not into the
XFA data.

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
| Text extraction | plain and layout mode; no visitor functions |
| Page boxes, rotation, user unit | supported |
| Metadata and XMP | read and write; XMP as a raw packet |
| Outline | read, add, edit, move, remove; positions, zoom, URLs, color and style; carried over by `append` |
| Contents pages | generated from the outline, with links |
| Page labels, named destinations, open destination | read and write |
| Page layout, page mode, viewer preferences | read and write |
| Images | export; no replacement |
| Merge, split, append, insert, delete, reorder, blank pages | supported |
| Rotate, crop, scale, transform | supported |
| Stamps and watermarks | supported; annotations of the merged page are not copied |
| Forms | read, fill, flatten; create text, checkbox, radio, choice and signature fields; rename, move, change flags and options, remove; XFA is reported only |
| Annotations and links | read, add, remove, flatten |
| Attachments | read, add, replace, remove |
| JavaScript | read, add, remove; never run |
| Encrypt output | AES-256 with permission flags |
| Output options | stream compression, object streams, linearization |
| Drawing text | standard fonts, Western European characters |
| PDF from images | JPEG without re-encoding; PNG, TIFF, GIF and others losslessly; transparency, EXIF orientation, multi-frame files |
| Raw objects | read-only view on `PdfEditor` |
| Removing images or text, replacing images, merging identical objects | not available |
| Incremental writing | not available |
| Article threads | not available |
