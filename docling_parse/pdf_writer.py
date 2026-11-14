"""PdfWriter: assemble, change and write PDF documents.

```python
from docling_parse.pdf_writer import PdfWriter

writer = PdfWriter()
writer.append("file1.pdf")
writer.append("file2.pdf", pages=[0, 2])
writer.write("merged.pdf")

writer = PdfWriter(clone_from="document.pdf")
writer.pages[0].rotate(90)
del writer.pages[3]
writer.write("changed.pdf")
```
"""

from __future__ import annotations

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
from docling_parse._pdf_document import (
    PdfBox,
    PdfEditablePage,
    PdfImage,
    PdfPages,
    PdfSource,
    _PdfDocument,
    _PdfEditing,
)

__all__ = ["PdfBox", "PdfEditablePage", "PdfImage", "PdfPages", "PdfWriter"]


class PdfWriter(_PdfEditing):
    """A PDF document being written.

    `PdfWriter()` starts empty; pages are added with `append` and
    `add_blank_page`. `PdfWriter(clone_from=...)` starts from a complete copy
    of an existing document, keeping everything it contains (forms, outline,
    structure, attachments), and never modifies that source.

    Output is a full, unencrypted rewrite: signatures of the source do not
    stay valid, and an encrypted source is written in the clear.

    One writer may be shared between threads: calls are serialized.
    """

    _native_class = _native.pdf_writer

    def __init__(
        self,
        clone_from: PdfSource | _PdfDocument | None = None,
        password: str | None = None,
        *,
        loglevel: str = "fatal",
    ):
        super().__init__(loglevel)
        if clone_from is None:
            self._native.create_empty()
        else:
            self._open(clone_from, password)
