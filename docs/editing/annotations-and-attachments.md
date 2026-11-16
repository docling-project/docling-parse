# Annotations, attachments and scripts

[Editing overview](../editing.md) · [Navigation preservation](navigation.md#what-assembly-preserves)

## Inspect annotations

```python
from docling_parse import PdfReader

with PdfReader("document.pdf") as reader:
    for annotation in reader.pages[0].annotations:
        print(annotation["/Subtype"], annotation["/Rect"], annotation.get("/Contents"))
```

Annotations are detached dictionaries keyed by PDF names, such as `/Subtype`
and `/Contents`. A local link also reports a resolved zero-based `page` index.
Appearance streams and back-references such as `/P` are omitted from this
inspection view. Changing the dictionary does not change the document.

## Add comments and links

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.add_uri(
        page_no=1, uri="https://example.org", rect=(72, 700, 200, 720)
    )
    editor.add_link(page_no=1, rect=(72, 660, 200, 680), target_page_no=3)
    editor.add_annotation(
        page_no=1,
        annotation={
            "/Subtype": "/Text",
            "/Rect": [300, 700, 320, 720],
            "/Contents": "Please check this figure",
        }
    )
    editor.write(target="annotated.pdf")
```

Rectangles are `(left, bottom, right, top)`. For dictionary input, strings
starting with `/` represent PDF names; other strings represent text.
`/Subtype` and `/Rect` are required for generic annotations.
A URI link has no border unless `border` is supplied to `add_uri`.

## Remove or flatten

```python
from docling_parse import PdfEditor

with PdfEditor(source="annotated.pdf") as editor:
    editor.remove_links()
    editor.remove_annotations(subtypes="/Text")
    editor.remove_annotations()
    editor.write(target="without-annotations.pdf")
```

An omitted Editor `subtypes` removes non-widget annotations and keeps form
fields. Name `/Widget` explicitly to remove widgets and their fields.
`flatten_annotations()` instead draws visible appearances into the page and
removes interactive annotations.

The Writer compatibility method requires the `subtypes` argument:
`writer.remove_annotations(None)` removes non-widget annotations.
`writer.remove_annotations` and `writer.remove_links` return `None`;
the named Editor operations return removal counts.

## Embedded files

```python
from docling_parse import PdfEditor, PdfReader

with PdfEditor(source="document.pdf") as editor:
    editor.add_attachment(
        filename="data.csv", data=b"year,value\n2026,42\n",
        description="Source data", mime_type="text/csv"
    )
    editor.add_attachment(filename="notes.txt", data="Reviewer notes")
    editor.write(target="with-attachments.pdf")

with PdfReader("with-attachments.pdf") as reader:
    print(reader.attachment_info)
    print(reader.attachments["data.csv"])
    print(reader.get_attachment("data.csv"))

with PdfEditor(source="with-attachments.pdf") as editor:
    editor.remove_attachment(name="notes.txt")
    editor.write(target="remaining-attachments.pdf")
```

`attachments` maps names to lists of byte strings. `attachment_info` includes
filename, description, size, MIME type and dates. Adding the same name replaces
its existing entry. The Writer adapter takes `add_attachment(filename, data)`;
description and MIME type options belong to the named Editor API.
Neither currently returns pypdf's live `EmbeddedFile` object.

## JavaScript as data

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.add_js(javascript="this.print({bUI: true});", name="print")
    print(editor.javascript)
    editor.remove_javascript()
    editor.write(target="without-scripts.pdf")
```

Scripts are never executed by docling-parse. `remove_javascript()` removes
document-level scripts, a scripted open action and document additional actions.
It does not promise to sanitize every page, annotation or nested action graph.

Cloning retains document-level scripts and embedded files initially.
`append()` and `merge()` do **not** import these document-level collections.
Page-local actions or file-attachment annotations may still arrive with copied
pages. See [assembly preservation](navigation.md#what-assembly-preserves).

