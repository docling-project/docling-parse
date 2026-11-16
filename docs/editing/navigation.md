# Navigation: bookmarks, destinations and contents

[Editing overview](../editing.md) · [Pages](pages.md)

## Inspect navigation

```python
from docling_parse import PdfReader

with PdfReader("document.pdf") as reader:
    print(reader.outline)
    print(reader.named_destinations)
    print(reader.page_labels)
    print(reader.open_destination)
    print(reader.page_layout, reader.page_mode, reader.viewer_preferences)
```

Bookmarks are dictionaries containing `id`, `title`, `page`, `url`, `open`,
`children`, destination details and styling. Destination dictionaries contain
`page`, `kind` and `arguments`. Their `page` values are zero-based indices, or
`None` when no surviving page is resolved, on all three classes. Add 1 when
passing an inspected index into an editor `page_no` argument.

Bookmark IDs are PDF object numbers valid within the object that returned
them. Reopen a written file and retrieve its IDs again; serialization can
renumber objects. The dictionaries are inspection results, not live PDF objects.

## Create and edit bookmarks

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    chapter = editor.add_outline_item(title="Chapter 1", page_no=1)
    section = editor.add_outline_item(title="Section 1.1", page_no=2, parent=chapter)
    editor.add_outline_item(title="Figure", page_no=3, top=400, zoom=1.5)
    editor.add_outline_item(title="Notes", page_no=None)
    editor.add_outline_item(
        title="Project", page_no=None, url="https://example.org"
    )
    editor.update_outline_item(
        item_id=section, title="1.1 Introduction", page_no=3,
        color="#c00000", bold=True, is_open=False
    )
    editor.move_outline_item(item_id=section, parent=None, index=0)
    editor.remove_outline_item(item_id=chapter)
    editor.write(target="bookmarks.pdf")
```

`left`, `top` and `zoom` choose an `/XYZ` target; omitted coordinates preserve
the viewer's corresponding setting. `page_no=None` makes a heading without a
page target. `url` selects a URI action. `color=None` restores default color.
Only arguments supplied to `update_outline_item` are changed.

`move_outline_item` moves children with their parent. Its `index` is a
zero-based sibling position; `None` appends. An item cannot move beneath
itself or its descendants. `clear_outline()` removes the complete outline.

The Writer compatibility method follows pypdf's
`add_outline_item(title, page_number, parent, before, color, bold, italic, fit, is_open)`.
It supports `before` and `/Fit` or `/XYZ` fit objects. Other fit modes are
unsupported. Use the named editor methods for URL targets and direct
`left`/`top`/`zoom` arguments.

## Generate contents pages

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.add_outline_item(title="Introduction", page_no=1)
    editor.add_outline_item(title="Results", page_no=3)
    editor.add_table_of_contents(
        before_page_no=1, title="Contents", max_depth=1,
        page_size="A4", font="Times-Roman", size=12
    )
    editor.write(target="with-contents.pdf")
```

As many pages as needed are inserted. Lines are indented by bookmark depth,
show the destination's display label, and link to that page. URL bookmarks
and headings without page targets are listed without a page number or link.
`max_depth=0` includes top-level entries only.

This is a simple generated layout: widths are estimated, long titles are
shortened, and unsupported characters become `?`. For exact typography,
import a designed contents page and add links separately.

## Labels, destinations and viewer settings

This example uses a document with at least five pages.

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.set_page_label(first_page_no=1, last_page_no=2, style="/r", start=1)
    editor.set_page_label(first_page_no=3, last_page_no=5, style="/D", start=1)
    editor.add_named_destination(title="summary", page_no=3)
    editor.set_open_destination(page_no=3)
    editor.set_page_layout(layout="/TwoColumnLeft")
    editor.set_page_mode(mode="/UseOutlines")
    editor.set_viewer_preferences(preferences={"HideToolbar": True, "FitWindow": True})
    editor.write(target="navigation.pdf")
```

Label styles are `/D` (decimal), `/R`/`/r` (Roman) and `/A`/`/a`
(alphabetic); `prefix` adds text. A `None` style gives a prefix-only label.
Label ranges are inclusive. Named destinations are added or replaced by name
and currently target a fitted page.

Pass `None` to the open destination, layout, mode or preference setter to
remove that setting. Labels are stored by page position; reapply them after
inserting, deleting or reordering pages if the intended labels should follow
the original pages.

## What assembly preserves

Cloning (`PdfEditor(source=...)` or `PdfWriter(clone_from=...)`) starts from
the complete source catalog. `append()`/`merge()` assemble selected pages
into the destination and have narrower preservation behavior:

| Content | Clone starting state | Append/merge |
|---|---|---|
| Page text, graphics and resources | Retained | Copied with selected pages |
| Page annotations and form widgets | Retained | Copied, with qpdf widget repair |
| Bookmarks | Retained | Imported by default when they lead to copied pages |
| Named destination definitions | Retained | Not imported |
| Page label ranges | Retained | Not imported |
| `/Info` and XMP | Retained | Source metadata is not imported |
| Document-level embedded files | Retained | Not imported |
| Document-level JavaScript | Retained | Not imported |
| Viewer settings and open action | Retained | Source settings are not imported |
| Tagged structure | Retained initially | Structure trees are not merged |

Set `import_outline=False` to omit source bookmarks. Bookmarks to omitted
source pages are excluded. When a source page is imported more than once,
imported bookmarks target its first copy.

Moving an existing page preserves destinations that reference that page
object. Removing a page clears local links and named destinations resolved
to it, clears its open action, and leaves its bookmarks as headings without
a page target. Link annotations whose target was removed can remain as
inactive rectangles.

**Cross-document links are not comprehensively remapped during assembly.**
Copied local links and references to named destinations may not target the
intended imported page, especially for subsets or repeated imports. Check
and recreate them with `add_link` as needed. General action graphs and
tagged-structure repair are not implemented.

These preservation limits also apply when contents pages or other insertions
change page positions. See [output and signatures](output-and-encryption.md#signatures-and-unsupported-output).

