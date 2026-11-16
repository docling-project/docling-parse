# Drawing text and working with images

[Editing overview](../editing.md) · [Rendering](rendering.md)

## Draw text

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    page = editor.get_page(page_no=1)
    page.draw_text(
        text="CONFIDENTIAL", x=72, y=780, size=24,
        font="Helvetica-Bold", color="#c00000"
    )
    page.draw_text(text="Approved by:\nDate:", x=72, y=120)
    editor.write(target="labelled.pdf")
```

`x` and `y` locate the first baseline, measured from the lower-left origin
in default PDF user space. At the usual `UserUnit=1`, units and font sizes are
points. Each newline moves down by 1.2 times `size`.
`color` is `"#rrggbb"` or an RGB triple with components from 0 to 1.
The output is PDF text that can be selected, searched and extracted.

`font` is one of the twelve Latin standard fonts, also exported as
`STANDARD_FONTS`:

| Family | Names |
|---|---|
| Helvetica | `Helvetica`, `Helvetica-Bold`, `Helvetica-Oblique`, `Helvetica-BoldOblique` |
| Times | `Times-Roman`, `Times-Bold`, `Times-Italic`, `Times-BoldItalic` |
| Courier | `Courier`, `Courier-Bold`, `Courier-Oblique`, `Courier-BoldOblique` |

No font program is embedded. WinAnsiEncoding supports Western European text,
including the euro sign and smart quotes. Characters outside that encoding,
such as Greek, Cyrillic and CJK text, raise `PdfUnsupportedError`. General
Unicode text drawing and exact font-metric layout are not implemented.
The [contents-page generator](navigation.md#generate-contents-pages) has its own
replacement and truncation policy for unsupported or long titles.

## Export existing page images

```python
from docling_parse import PdfReader

with PdfReader("document.pdf") as reader:
    for index, image in enumerate(reader.pages[0].images):
        print(image.name, image.bbox, image.width, image.height, image.format)
        if image.format == "raw":
            image.to_pil().save(f"extracted-{index}.png")
        else:
            image.save(f"extracted-{index}{image.extension}")
```

Each `PdfImage` carries `name`, `bbox`, `width`, `height`, `format`, `mode` and
`data`. Enumeration decodes the page. `save` writes the stored bytes as-is;
it does not convert their format. Formats include:

| Format | Stored data |
|---|---|
| `jpeg`, `jp2` | Encoded image file |
| `jbig2` | Embedded JBIG2 stream |
| `raw` | Uncompressed samples described by `mode`, `width` and `height` |

Raw samples saved directly with `save` are not a PNG or JPEG. Use `to_pil()`
and Pillow's save methods to encode them. `to_pil()` supports JPEG, JP2 and raw
samples; unsupported formats raise `PdfUnsupportedError`.
Failed decoding can produce an empty `data` value.

## Create PDFs from images

```python
from docling_parse import PdfEditor

with PdfEditor.from_images(
    images=["scan-1.jpg", "scan-2.png"], page_size="A4", margin=36
) as editor:
    editor.add_image(image="photo.jpg", before_page_no=1, dpi=300)
    editor.write(target="image-pages.pdf")
```

An image source can be a path, bytes, a binary stream or a PIL image, in a
format Pillow can read. Pillow is imported when image operations need it.

- Suitable 8-bit JPEG files are embedded without re-encoding. Mirrored EXIF
  orientations and unsupported JPEG sample formats take the decoded path.
- Other supported formats, including PNG, TIFF, GIF, BMP and WebP, are decoded
  and stored losslessly at the supported sample depth. Transparency becomes
  a soft mask. Sixteen-bit grayscale is reduced to eight bits.
- Multi-page TIFF and animated GIF inputs add one page per frame.
  `add_image` returns the added page handles.
- EXIF orientation is applied. CMYK JPEG handling accounts for Adobe sample
  inversion.

Without `page_size`, image dimensions use the requested `dpi`, the file's
declared resolution, or 72 dpi. `margin` adds a border.
With `page_size`, the image is centered and scaled to **fit within** the area
inside the margins, preserving its proportions. It may leave unused space;
it is not cropped to fill the page.

Page sizes are `"A3"`, `"A4"`, `"A5"`, `"Letter"`, `"Legal"` or
`(width, height)` in points. Image input failures can raise Pillow's
`UnidentifiedImageError`, an `OSError`.

The Writer convenience API also offers
`PdfWriter.from_images(images, page_size=...)` and
`writer.add_image(image, index=...)`, with zero-based insertion positions.
Creating an image page does not place an image onto an existing page:
replacement, recompression and drawing new images on existing pages are not
currently exposed.

