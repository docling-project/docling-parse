"""PdfReader: open a PDF and inspect it without changing it.

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
from docling_parse._pdf_document import (
    PdfBox,
    PdfImage,
    PdfPage,
    PdfPages,
    PdfSource,
    _PdfDocument,
)

__all__ = ["PdfBox", "PdfImage", "PdfPage", "PdfPages", "PdfReader"]


class PdfReader(_PdfDocument):
    """A read-only PDF document.

    `source` is a path, bytes, a binary stream (read, never closed), or
    another PdfReader, PdfWriter or PdfEditor. Opening parses the document
    structure only; a page is decoded when its text is asked for.

    One reader may be shared between threads: calls are serialized.
    """

    _native_class = _native.pdf_reader

    def __init__(
        self,
        source: PdfSource | _PdfDocument,
        password: str | None = None,
        *,
        loglevel: str = "fatal",
    ):
        super().__init__(loglevel)
        self._open(source, password)
