# Pages: reading, assembly and geometry

[Editing overview](../editing.md) · [Navigation and preservation](navigation.md)

Editor methods take named arguments and one-based page numbers.
Reader and Writer keep zero-based indices for pypdf migration.

## Open and inspect

A source can be a path, bytes, a binary stream, or another Reader, Writer or
Editor. Streams you provide are read and remain open. Passwords are supplied
when opening; there is no separate `decrypt()` step.

```python
from docling_parse import PdfEditor, PdfExtractionMode

with PdfEditor(source="document.pdf") as editor:
    first = editor.get_page(page_no=1)
    print(editor.page_count(), first.page_no)
    print(editor.extract_text(page_no=1))
    print(editor.extract_text(page_no=1, extraction_mode=PdfExtractionMode.LAYOUT))
    print(first.mediabox, first.cropbox, first.rotation, first.user_unit)
```

Plain extraction joins the decoder's line cells with newlines. Layout mode
places words on a character grid to retain columns, indentation and vertical
spacing. Neither promises character-for-character agreement with pypdf.
Visitor functions and other pypdf text extraction options are unavailable.

`mediabox`, `cropbox`, `trimbox`, `bleedbox` and `artbox` are
`PdfBox(left, bottom, right, top)` values with `width` and `height`.
Coordinates are in default PDF user space: normally one unit is 1/72 inch;
`user_unit` reports any additional physical scale. The crop box defaults to
the media box, and the other three boxes default to the crop box.

## Assemble or split

```python
from docling_parse import PdfEditor

with PdfEditor() as editor:
    editor.append(source="document.pdf", page_nos=[1, 3])
    editor.append(source="appendix.pdf", before_page_no=2)
    editor.insert_blank_page(before_page_no=1, width=595, height=842)
    editor.write(target="assembled.pdf")
```

Omit `page_nos` to copy all source pages. A list can reorder or repeat pages.
Omit `before_page_no` to append at the end; insertion accepts 1 through
`editor.page_count() + 1`.

The compatibility methods use pypdf's argument order:

```python
from docling_parse import PdfWriter

with PdfWriter() as writer:
    writer.append("document.pdf", pages=[0, 2])
    writer.merge(1, "appendix.pdf", pages=[0])
    writer.insert_blank_page(width=595, height=842, index=0)
    writer.write("assembled-compat.pdf")
```

Writer page selection also accepts half-open range tuples such as `(0, 3)`
and `(0, 6, 2)`, slices, and objects with pypdf PageRange-style `indices()`.
Use a list such as `[0, 3]` to select individual pages rather than a range.
A second positional argument to `append` can be an outline heading; use the
`pages` keyword for clarity.

Page-local content, annotations and form widgets are copied. Document-level
preservation differs from cloning: see [the preservation table](navigation.md#what-assembly-preserves).
Nonempty pypdf `excluded_fields` and `excluded_keys` arguments are unsupported.

## Clone and change an existing document

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.rotate_page(page_no=1, angle=90)
    editor.set_page_box(page_no=2, box="crop", bounds=(20, 20, 180, 280))
    editor.duplicate_page(page_no=1)
    editor.move_page(page_no=3, to_page_no=1)
    editor.remove_page(page_no=4)
    editor.write(target="reordered.pdf")
```

Cloning starts with the whole document graph, including its metadata, forms,
bookmarks, structure, attachments and scripts. The source is only overwritten
if you explicitly write to its path. `PdfWriter(clone_from=source)` offers the
same starting point through the compatibility API.

`duplicate_page` appends by default, or inserts before `before_page_no`.
For a page handle from any Reader, Writer or Editor, use
`editor.add_page(page=page)` or `editor.insert_page(page=page, before_page_no=1)`.

`PdfEditor.remove_page` detaches the removed page's widgets and tagged
structure references. Fields shared with surviving widgets keep their values.
`clean=True` also replaces the detached page object with PDF null, so custom
references cannot retain that page object. Shared resources and content copies
can remain elsewhere in the document.

`PdfWriter.remove_page(page, clean=False)` and `del writer.pages[index]`
retain form and structure references for pypdf compatibility.
`PdfWriter.remove_page(page, clean=True)` also detaches those references
and nulls the removed page object.

## Transform pages

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.scale_page(page_no=1, sx=0.5, sy=0.5)
    editor.scale_page_to(page_no=1, width=300, height=400)
    editor.transform_page(page_no=1, ctm=(1, 0, 0, 1, 20, 30))
    editor.set_page_rotation(page_no=1, angle=180)
    editor.transfer_page_rotation_to_content(page_no=1)
    editor.write(target="transformed.pdf")
```

`scale_page` and `scale_page_to` scale content, annotations and all five page
boxes. `transform_page` leaves page boxes in place. A matrix is
`(a, b, c, d, e, f)`. Annotation geometry includes rectangles, quad points,
vertices, line and callout points, and ink paths. Widgets keep their field
identity, including when a field is shared across pages.

`rotate_page` adds clockwise display rotation. `set_page_rotation` sets the
absolute value. Both require multiples of 90. Transferring rotation to the
content adjusts text and graphics, annotation coordinates and page boxes.

After insertion, removal or reordering, reacquire page handles. Old handles
raise `PdfStaleHandleError`; they do not silently follow a different page.

## Stamps and watermarks

```python
from docling_parse import PdfEditor

with PdfEditor(source="stamp.pdf") as stamp, PdfEditor(source="document.pdf") as editor:
    overlay = stamp.get_page(page_no=1)
    editor.overlay_page(page_no=1, source_page=overlay)
    editor.overlay_page(page_no=2, source_page=overlay, over=False)
    editor.overlay_page(page_no=3, source_page=overlay, fit=True)
    editor.overlay_page(page_no=4, source_page=overlay, ctm=(1, 0, 0, 1, 40, 60))
    editor.write(target="stamped.pdf")
```

Composition uses vector Form XObjects. The default keeps the other page at
its own coordinates. `fit=True` centers and fits it within the target trim
box, accounting for display rotation. Pass a transformation matrix with
`ctm` for explicit placement.

The overlaid page's annotations and form widgets are not copied. Its text
and graphics are drawn; interactive links and controls require separate edits.

