# Output and encryption

[Editing overview](../editing.md) · [Assembly preservation](navigation.md#what-assembly-preserves)

## Write to a path, stream or bytes

```python
from io import BytesIO

from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.add_metadata(infos={"/Title": "Annual report", "/Author": "Finance"})
    editor.write(target="report.pdf")
    output = BytesIO()
    editor.write(target=output)
    pdf_bytes = editor.to_bytes()
```

Path output uses a temporary sibling and atomic replacement, so a failed
serialization does not leave a partially written target. Writing to the
source path is supported because input is already in memory.

Stream output serializes to memory and performs a Python write; the stream
is not closed. Output-sized memory is needed, and a stream failure can leave
partial output in that stream. `PdfEditor.write` returns `None`.

The Writer compatibility return value follows pypdf:

```python
from io import BytesIO

from docling_parse import PdfWriter

with PdfWriter(clone_from="document.pdf") as writer:
    output = BytesIO()
    opened_by_writer, stream = writer.write(output)
    assert opened_by_writer is False and stream is output
```

For path output, the returned flag is `True` and the returned file object is
closed. For a caller-supplied stream, it is `False` and the stream remains open.

## Metadata and output options

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.add_metadata(infos={"Title": "Report", "Subject": None})
    editor.set_xmp_metadata(packet=None)
    editor.set_output_options(
        compress_streams=True, object_streams="generate", linearize=False
    )
    editor.write(target="compact.pdf")
```

Metadata keys accept an optional leading slash. A `None` value removes an
entry. XMP is supplied as a raw string or byte packet; `None` removes it.

Streams are compressed by default. `object_streams` is `"preserve"` (default),
`"generate"` or `"disable"`. `linearize=True` enables fast-web-view ordering.
Each `set_output_options` call supplies a complete option set; omitted
parameters take their defaults. These options do not merge identical objects
or recompress images, and they do not promise a smaller file for every input.

## Passwords and permissions

```python
from docling_parse import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    editor.encrypt(
        user_password="secret", owner_password="owner-secret",
        allow_print="low", allow_extract=False, allow_modify=False
    )
    editor.write(target="protected-output.pdf")
```

Editor defaults to `algorithm="AES-256"`. `owner_password` defaults to the user
password. A nonempty owner password with an empty user password permits opening
without a password while still setting owner permissions.

Editor permission arguments are `allow_print`, `allow_modify`, `allow_extract`,
`allow_annotate`, `allow_fill_forms`, `allow_accessibility` and `allow_assemble`.
Printing accepts `True`, `False` or `"low"`. Defaults allow all permissions.
Conforming viewers interpret the flags; docling-parse reports them and does
not enforce them as access control. Older encryption revisions cannot represent
all newer permission distinctions.

The Writer adapter accepts pypdf's `use_128bit`, `permissions_flag` and
`algorithm` arguments. Its default algorithm follows pypdf: **RC4-128**, or
RC4-40 when `use_128bit=False` and no algorithm is specified. Request AES-256
explicitly for this workflow:

```python
from docling_parse import PdfWriter

with PdfWriter(clone_from="document.pdf") as writer:
    writer.encrypt("secret", owner_password="owner-secret", algorithm="AES-256")
    writer.write("protected-compat.pdf")
```

Accepted algorithm names are `"RC4-40"`, `"RC4-128"`, `"AES-128"`,
`"AES-256-R5"` and `"AES-256"`. The older schemes exist for compatibility;
AES-256 is the current editor default.

`remove_encryption()` clears requested output encryption. Output is
unencrypted unless `encrypt` was called, including when the input was encrypted.
While editing, `is_encrypted` and `encryption` describe the planned output.

To clone a protected document through Writer, open a Reader with its password
and pass that Reader as `clone_from`; the Writer constructor has no password
argument:

```python
from docling_parse import PdfReader, PdfWriter

with PdfReader("protected.pdf", password="secret") as reader:
    with PdfWriter(clone_from=reader) as writer:
        writer.write("unprotected-copy.pdf")
```

`PdfReader(..., strict=True)` rejects input that qpdf repaired or that produced
structural warnings. This concerns input loading; it is not a strict
signature-preservation option. Custom `root_object_recovery_limit` values are
unsupported.

## Signatures and unsupported output

Every write is a full rewrite. Existing digital signatures do not remain
valid. The current API does not implement signing, signature validation, or
a warn/refuse policy specifically for signed input.

Incremental writing is unavailable:
`PdfWriter(..., incremental=True)` raises `PdfUnsupportedError`.
The other pypdf-shaped constructor options do not add an incremental serializer
or editable PDF object model. Use the documented workflows rather than
assuming that accepting a constructor signature implies every corresponding
pypdf mode is implemented.

