# Render a page

[Editing overview](../editing.md) · [Page geometry](pages.md)

`PdfEditor` renders a selected page using the native Blend2D rendering path.
It decodes the page's text, shapes, images and available widget appearances,
then returns a Pillow image.

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    page_image = editor.render_page(page_no=1, scale=2.0)
    page_image.save("page-1.png")
```

Both arguments are named. `page_no` starts at **1**. `scale` defaults to 1.0:
one pixel per PDF point, or 72 dpi at the usual `UserUnit=1`.
`scale=2.0` gives 144 dpi at that user unit. Scale must be finite and positive.

## Preview unsaved edits

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.rotate_page(page_no=1, angle=90)
    editor.draw_text(
        page_no=1, text="DRAFT", x=72, y=120, size=32, color="#c00000"
    )
    modified = editor.is_modified
    preview = editor.render_page(page_no=1, scale=2.0)
    assert editor.is_modified == modified
    editor.write(target="previewed.pdf")

preview.save("edited-page.png")
```

Rendering uses the current document snapshot, including edits not yet written
to a path. It leaves the document's modification state unchanged.
The returned image owns its pixels and remains usable after the editor closes.
Rendering accounts for the page crop box and display rotation.

## Image format and resource use

Images are in `RGBA` mode. Save PNG directly; convert for formats that do
not accept alpha:

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.render_page(page_no=1).convert("RGB").save("page-1.jpg")
```

Pillow is imported on demand. Larger scales increase pixel count in both
dimensions, so doubling the scale uses roughly four times the pixel memory.
Font programs and image samples are requested when rendering; simply opening
an editor does not rasterize its pages.

Missing embedded fonts can require system font substitution. Results then
depend on the available fonts; see [system-font setup](../../README.md#system-fonts).
Rendering remains subject to the renderer's supported PDF features.

This API currently exposes `page_no` and `scale`. It has no region, canvas-size,
or `RenderConfig` argument. `PdfReader` and `PdfWriter` do not expose
`render_page`; use an Editor when a preview is needed.

A zero or negative page number raises `ValueError`; an out-of-range positive
number raises `PdfEditError`. Non-integer page numbers raise `TypeError`, and
non-finite or non-positive scales raise `ValueError`.
