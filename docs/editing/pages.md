# Pages: reading, assembly and geometry

[Editing overview](../editing.md) · [Navigation and preservation](navigation.md)

Editor examples use named arguments and one-based page numbers. Reader/Writer
examples keep zero-based indices; all `pages[...]` sequences are zero-based.

## Open and inspect

A source can be a path, bytes, a binary stream, or another Reader, Writer or
Editor. Streams you provide are read and remain open. Passwords are supplied
when opening; there is no separate `decrypt()` step.

```python
from docling_parse import PdfEditor, PdfReader

with PdfEditor(source="document.pdf") as editor:
    first = editor.get_page(page_no=1)
    print(editor.get_page_no(page=first))  # 1
    print(first.index, first.page_number)  # 0, 0
    print(first.extract_text())
    print(first.extract_text(extraction_mode="layout"))
    print(first.mediabox, first.cropbox, first.rotation, first.user_unit)

with PdfReader("protected.pdf", password="secret") as reader:
    print(reader.get_page(0).extract_text())
    print(len(reader.pages), reader.pages[-1].mediabox)
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
`len(editor.pages) + 1`.

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
    editor.get_page(page_no=1).rotate(angle=90)
    editor.get_page(page_no=2).cropbox = (20, 20, 180, 280)
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

`remove_page(page_no=..., clean=True)` additionally replaces the detached
page object with PDF null so stray references cannot keep it as a page.
This does not provide general structure-tree repair.

## Transform pages

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    page = editor.get_page(page_no=1)
    page.scale_by(factor=0.5)
    page.scale_to(width=300, height=400)
    page.add_transformation(ctm=(1, 0, 0, 1, 20, 30))
    page.rotation = 180
    page.transfer_rotation_to_content()
    editor.write(target="transformed.pdf")
```

`scale(sx=..., sy=...)`, `scale_by` and `scale_to` scale content, annotations
and all five page boxes. `add_transformation` transforms content and annotation
geometry but leaves the boxes alone. A matrix is `(a, b, c, d, e, f)`.

`rotate(angle=90)` adds clockwise display rotation; setting `rotation` sets
the absolute value. Both require multiples of 90. Flattening rotation moves
it into content and annotation coordinates and adjusts page boxes.

After insertion, removal or reordering, reacquire page handles. Old handles
raise `PdfStaleHandleError`; they do not silently follow a different page.

## Stamps and watermarks

```python
from docling_parse import PdfEditor, PdfReader

with PdfReader("stamp.pdf") as stamp, PdfEditor(source="document.pdf") as editor:
    overlay = stamp.pages[0]
    editor.get_page(page_no=1).merge_page(page=overlay, over=True)
    editor.get_page(page_no=2).merge_page(page=overlay, over=False)
    editor.get_page(page_no=3).merge_page(page=overlay, fit=True)
    editor.get_page(page_no=4).merge_translated_page(page=overlay, tx=40, ty=60)
    editor.write(target="stamped.pdf")
```

Composition uses vector Form XObjects. The default keeps the other page at
its own coordinates. `fit=True` centers and fits it within the target trim
box, accounting for display rotation. Explicit placement is available through
`merge_transformed_page(page=..., ctm=...)`, `merge_scaled_page(page=..., scale=...)`
and `merge_rotated_page(page=..., rotation=...)`. The latter rotates content
counter-clockwise about the origin.

The overlaid page's annotations and form widgets are not copied. Its text
and graphics are drawn; interactive links and controls require separate edits.

