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

Path output stages the PDF in a private temporary file beside the resolved
target. It syncs the file before replacement and syncs the parent directory
where supported. An existing target keeps its permissions, ownership, and
access ACL on supported filesystems. A new file has owner-only access.
Symbolic links are followed, including links to a new file in an existing
directory. A destination with multiple hard links raises `PdfWriteError`.
Writing to the source path is supported because input is already in memory.

Stream output serializes to memory and retries short writes until the PDF is
complete. A stream that reports no progress raises `PdfWriteError`. A failed
stream write can leave partial output in the stream. The caller owns the
stream, and `PdfEditor.write` returns `None`.

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

`with PdfWriter(fileobj=target) as writer:` writes the PDF at context exit
when starting from an empty or new target, including when the body raises.
A Writer initialized by cloning uses an explicit `write()` call. A
caller-supplied output stream remains open in either case.

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
entry. XMP takes a text or byte packet. Text is written using its XML
encoding declaration or UTF-8 by default. Reading recognizes a byte-order
mark and the XML declaration. Pass `None` to remove the packet.

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

| Algorithm | PDF security revision | Text password encoding |
|---|---|---|
| `RC4-40` | 2 | Latin-1, with UTF-8 fallback |
| `RC4-128` | 3 | Latin-1, with UTF-8 fallback |
| `AES-128` | 4 | Latin-1, with UTF-8 fallback |
| `AES-256-R5` | 5 | SASLprep and UTF-8 |
| `AES-256` | 6 | SASLprep and UTF-8 |

These text encodings follow pypdf. A text password that fails SASLprep
uses plain UTF-8. Supply `bytes` to use the original encoded password
directly, including when opening legacy files that use PDFDocEncoding.
The native password API rejects embedded NUL bytes. PDF applies its own
length limit of 32 bytes for revisions 2–4 and 127 bytes for revisions 5–6.

`remove_encryption()` clears requested output encryption. Output is
unencrypted unless `encrypt` was called, including when the input was encrypted.
On an untouched Editor, `is_encrypted` and `encryption` describe the input.
Once editing starts, they describe the planned output. `to_bytes()` and
`write()` preserve that session state and keep existing page handles valid.

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

