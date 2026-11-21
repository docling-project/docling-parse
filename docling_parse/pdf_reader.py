"""PdfReader: read-only PDF inspection for pypdf migration.

```python
from docling_parse.pdf_reader import PdfReader

with PdfReader("document.pdf") as reader:
    print(len(reader.pages), reader.metadata.get("Title"))
    for page in reader.pages:
        print(page.extract_text())
```
"""

from __future__ import annotations

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
from docling_parse._pdf_compat import _PdfReaderCompat
from docling_parse._pdf_document import (
    PdfBox,
    PdfImage,
    PdfPage,
    PdfPages,
    PdfSource,
    _PdfDocument,
)

__all__ = ["PdfBox", "PdfImage", "PdfPage", "PdfPages", "PdfReader"]


class PdfReader(_PdfReaderCompat):
    """A read-only PDF document with a zero-based pypdf-style page sequence.

    `stream` is a path, bytes, a binary stream (read, never closed), or
    another PdfReader, PdfWriter or PdfEditor. Opening parses the document
    structure only; a page is decoded when its text is asked for.

    One reader may be shared between threads: calls are serialized.
    """

    _native_class = _native.pdf_reader

    def __init__(
        self,
        stream: PdfSource | _PdfDocument,
        strict: bool = False,
        password: str | bytes | None = None,
        *,
        root_object_recovery_limit: int | None = 10000,
    ):
        super().__init__()
        self._compat_open(
            stream=stream,
            strict=strict,
            password=password,
            root_object_recovery_limit=root_object_recovery_limit,
        )
