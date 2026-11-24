# Forms

[Editing overview](../editing.md) · [Rendering](rendering.md)

## Read fields

The inspection example assumes the form has a field named `name`.

```python
from docling_parse import PdfEditor

with PdfEditor(source="form.pdf") as editor:
    fields = editor.get_fields()
    for name, field in fields.items():
        print(
            name, field["kind"], field["value"], field["states"],
            field["pages"], field["bboxes"]
        )
    print(editor.get_form_text_fields())
    print(editor.get_pages_showing_field(name="name"))
    print(editor.has_form(), editor.has_form_on_page(page_no=1), editor.has_xfa())
```

`get_fields()` returns a dictionary keyed by fully qualified field name.
Each field reports `kind`, `value`, `default_value`, `choices`, `states`,
`flags`, `read_only`, its names, and information about where it appears.

A field holds one logical value. A widget is a page annotation that gives
the field a position and an appearance. A radio group with three choices on
page 1 has one selected value, `pages=[1]`, and three `bboxes`. Each choice
needs its own clickable area. A text field shown on pages 1 and 3 can have
two widgets that display the same value and update together. The distinction
lets the PDF store shared field data alongside each widget's placement and
appearance.

`pages` lists distinct one-based page numbers. `bboxes` contains one `PdfBox`
per widget, with `left`, `bottom`, `right`, and `top` coordinates in default
PDF user space. Each box comes from the widget's PDF `/Rect` entry. The
lists cannot be paired by position because several widgets can occupy the
same page. Reader and Writer report zero-based `pages` and raw `rects`.

Editor `get_pages_showing_field(name="name")` returns read-only page views.
Reader and Writer accept a field dictionary or a name in
`get_pages_showing_field(field)`.

`has_form()` checks for the document's AcroForm dictionary.
`has_form_on_page(page_no=...)` checks for a field widget on a specific page.
`has_xfa()` checks for an XFA definition at the document level. The editor
reports XFA presence only. Reader and Writer expose `has_form` and `has_xfa`
as properties.

Removing a widget through `PdfEditor.remove_annotations(subtypes="/Widget")`
prunes its field only when the field has no remaining widgets. Moving a
single widget with `update_form_field(rect=...)` preserves its appearance.

Kinds include text, checkbox, radio, choice, pushbutton and signature.
The compatibility `get_fields` signature accepts pypdf's traversal arguments,
but non-default subtree traversal is unsupported.

## Create fields

This example creates a new one-page form:

```python
from docling_parse import PdfEditor

with PdfEditor() as editor:
    editor.add_blank_page(width=595, height=842)
    editor.add_text_field(
        page_no=1, name="name", rect=(72, 700, 300, 720),
        value="Ada", tooltip="Your full name"
    )
    editor.add_text_field(
        page_no=1, name="notes", rect=(72, 600, 300, 690), multiline=True
    )
    editor.add_checkbox(
        page_no=1, name="agree", rect=(72, 570, 86, 584), checked=True
    )
    editor.add_choice_field(
        page_no=1, name="country", rect=(72, 540, 300, 560),
        options=["Belgium", "Italy", "Switzerland"], value="Italy"
    )
    editor.add_radio_group(
        page_no=1, name="plan",
        buttons={"basic": (72, 510, 86, 524), "premium": (120, 510, 134, 524)},
        selected="premium"
    )
    editor.add_signature_field(
        page_no=1, name="signature", rect=(72, 420, 300, 480),
        required=True, tooltip="Signature"
    )
    editor.write(target="created-form.pdf")
```

Names must be unique in the form and contain no period. Rectangles are
`(left, bottom, right, top)` in PDF user space.

| Field | Additional options |
|---|---|
| Text | `value`, `multiline`, `font_size`, `read_only`, `required`, `tooltip` |
| Checkbox | `checked`, `read_only`, `required`, `tooltip` |
| Choice | `options`, `value`, `editable`, `font_size`, `read_only`, `required`, `tooltip` |
| Radio group | `buttons`, `selected`, `read_only`, `required`, `tooltip` |
| Signature placeholder | `required`, `tooltip`; no constructor `read_only` argument |

Choice fields are drop-downs; `editable=True` allows a value outside the
option list. Radio button names must be distinct and cannot be `Off`.
Signature fields are empty placeholders for a signing application; the library
does not sign.

Creation builds appearance streams: text and choices use Helvetica, while
checkboxes, radio buttons and signature frames are drawn by the editor.
List boxes, comb fields, custom field fonts and setting default values are
not exposed.

## Fill and update

```python
from docling_parse import PdfEditor

with PdfEditor(source="created-form.pdf") as editor:
    editor.update_form_field_values(
        fields={"name": "Grace", "agree": False, "country": "Belgium", "plan": "/basic"}
    )
    editor.update_form_field(
        name="name", new_name="full_name",
        rect=(72, 690, 400, 720), read_only=True
    )
    editor.update_form_field(name="country", options=["Belgium", "France"])
    editor.update_form_field(name="full_name", tooltip=None)
    editor.remove_form_field(name="notes")
    editor.write(target="filled.pdf")
```

Fields are addressed by fully qualified name, such as `address.street`.
An unambiguous partial name also works. Unknown names and button states raise
`PdfEditError`. Checkboxes accept booleans or state names and update every
widget of the field. A radio value selects a named state, with or without
its leading slash. `/Off` clears the selection.

Text and choice values are strings. Use an option's export value when a
PDF defines separate export and display values. An editable combo box also
accepts unlisted text. Replacing options clears a removed selection and
updates any selection indices. Multi-select values are returned as lists.
Changing a multi-select field's selection or options raises `PdfUnsupportedError`.

The editor generates an appearance when the field's font can represent the
text. Otherwise it preserves the Unicode value and sets `NeedAppearances`
so a viewer can redraw it.

`update_form_field` changes the supplied properties. `rect` requires a
single widget. Removing a field takes all of its widgets off their pages.

Reader/Writer's `get_form_text_fields()` defaults to partial names, adding
suffixes to distinguish duplicate partial names.
`full_qualified_name=True` returns fully qualified names. Editor's no-argument
`get_form_text_fields()` returns fully qualified names.

## Flatten

Flattening paints the widgets into page content and removes the interactive
form fields.

```python
from docling_parse import PdfEditor

with PdfEditor(source="filled.pdf") as editor:
    editor.update_form_field_values(fields={"full_name": "Grace Hopper"}, flatten=True)
    editor.write(target="flattened.pdf")
```

Use `fields={}` to flatten the current values. The editor prepares missing
appearances for visible widgets and paints them into the page. Other
annotations stay interactive. XFA forms and widgets that cannot be painted
raise `PdfUnsupportedError` before values or pages change.

`editor.flatten_annotations()` also flattens non-form annotations that
have appearances.
