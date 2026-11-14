"""PdfEditor: the complete docling-parse PDF surface in one object.

PdfReader and PdfWriter offer the subset of these operations that matches
pypdf. PdfEditor offers all of them, and is where functionality without a
pypdf counterpart appears first.

```python
from docling_parse.pdf_editor import PdfEditor

with PdfEditor("document.pdf") as editor:
    print(editor.metadata, editor.pages[0].extract_text())
    editor.pages[0].rotate(90)
    editor.append("appendix.pdf")
    editor.write("out.pdf")
```
"""

from __future__ import annotations

from typing import Any

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
from docling_parse._pdf_document import (
    PdfBox,
    PdfEditablePage,
    PdfImage,
    PdfPages,
    PdfSource,
    _native_errors,
    _PdfDocument,
    _PdfEditing,
)

__all__ = ["PdfBox", "PdfEditablePage", "PdfEditor", "PdfImage", "PdfPages"]


class PdfEditor(_PdfEditing):
    """A PDF document that can be inspected, changed and written.

    `PdfEditor(source)` opens a document; `PdfEditor()` starts empty. An
    opened document is only read until the first change, at which point the
    editor continues on its own complete copy. The source is never modified
    unless `write` is pointed at its path.

    Output is a full, unencrypted rewrite: signatures of the source do not
    stay valid, and an encrypted source is written in the clear.

    One editor may be shared between threads: calls are serialized.
    """

    _native_class = _native.pdf_editor

    def __init__(
        self,
        source: PdfSource | _PdfDocument | None = None,
        password: str | None = None,
        *,
        loglevel: str = "fatal",
    ):
        super().__init__(loglevel)
        if source is None:
            self._native.create_empty()
        else:
            self._open(source, password)

    @property
    def is_modified(self) -> bool:
        """True once the editor works on its own copy of the document."""
        return self._native.is_editing()

    @property
    def annotations(self) -> Any:
        """Form, language, XMP, outline and structure tree of the document
        as it currently is, including edits not yet written."""
        with _native_errors():
            return self._native.annotations()
