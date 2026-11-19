# Forms

[Editing overview](../editing.md) · [Rendering](rendering.md)

## Read fields

The inspection example assumes the form has a field named `name`.

```python
from docling_parse import PdfEditor

with PdfEditor(source="form.pdf") as editor:
    fields = editor.get_fields()
    for name, field in fields.items():
        print(name, field["kind"], field["value"], field["states"], field["pages"])
    print(editor.get_form_text_fields())
    print(editor.get_pages_showing_field(name="name"))
    print(editor.has_form, editor.has_xfa)
```

`get_fields()` returns a dictionary keyed by fully qualified field name.
Each field reports `kind`, `value`, `default_value`, `choices`, `states`,
`flags`, `read_only`, `rects`, `pages` and its names. Editor `pages` results
contain one-based page numbers. Reader and Writer results contain zero-based
indices.
Radio groups report their combined selectable states and all widget rectangles.

Editor `get_pages_showing_field(name="name")` returns read-only page views.
Reader and Writer accept a field dictionary or a name in
`get_pages_showing_field(field)`.

Removing a widget through `PdfEditor.remove_annotations(subtypes="/Widget")`
prunes its field only when the field has no remaining widgets. Moving a
single widget with `update_form_field(rect=...)` preserves its appearance.

Kinds include text, checkbox, radio, choice, pushbutton and signature.
XFA presence is reported; XFA data is not parsed or updated.
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
`PdfEditError`. Text and choice values are strings; checkbox values can be
booleans or state names. A radio value selects a named state, with or without
its leading slash; `/Off` clears the selection.

Values update the appearance streams. `update_form_field` changes only the
arguments supplied. `rect` requires a single widget; it cannot resize an
entire multi-widget radio group. `options` applies to choice fields.
Removal takes all of the field's widgets off their pages.

Reader/Writer's `get_form_text_fields()` defaults to partial names, adding
suffixes to distinguish duplicate partial names.
`full_qualified_name=True` returns fully qualified names. Editor's no-argument
`get_form_text_fields()` returns fully qualified names.

## Flatten

```python
from docling_parse import PdfEditor

with PdfEditor(source="filled.pdf") as editor:
    editor.update_form_field_values(fields={"full_name": "Grace Hopper"}, flatten=True)
    editor.write(target="flattened.pdf")
```

Alternatively call `editor.flatten_annotations()`. Flattening draws visible
annotations, including form appearances, into page content and removes the
interactive annotations. It affects other visible annotations as well as forms.

