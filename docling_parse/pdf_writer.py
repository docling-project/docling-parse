"""PdfWriter: PDF assembly and output for pypdf migration.

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
from docling_parse._pdf_compat import _PdfWriterCompat
from docling_parse._pdf_document import (
    STANDARD_FONTS,
    PdfBox,
    PdfEditablePage,
    PdfImage,
    PdfPages,
    PdfSource,
    _PdfDocument,
)

__all__ = [
    "STANDARD_FONTS",
    "PdfBox",
    "PdfEditablePage",
    "PdfImage",
    "PdfPages",
    "PdfWriter",
]


class PdfWriter(_PdfWriterCompat):
    """A writable PDF document with pypdf-style assembly and output methods.

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
        fileobj: PdfSource | _PdfDocument | None = "",
        clone_from: PdfSource | _PdfDocument | None = None,
        incremental: bool = False,
        full: bool = False,
        strict: bool = False,
        *,
        keep_initial_header: bool = False,
        incremental_clone_object_count_limit: int | None = 500000,
        incremental_clone_object_id_limit: int | None = 1000000,
    ):
        super().__init__()
        cloned = self._editor._compat_init_writer(
            fileobj=fileobj,
            clone_from=clone_from,
            incremental=incremental,
            full=full,
            strict=strict,
            keep_initial_header=keep_initial_header,
            incremental_clone_object_count_limit=incremental_clone_object_count_limit,
            incremental_clone_object_id_limit=incremental_clone_object_id_limit,
        )
        self._context_target = None if cloned else fileobj

    def __exit__(self, *exc_info: object) -> None:
        try:
            if self._context_target is not None and self._context_target != "":
                self.write(self._context_target)
        finally:
            super().__exit__(*exc_info)
