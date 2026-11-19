"""Signature adapters; all document operations are implemented by PdfEditor.

The native subset still enforces reader immutability. Each adapter invokes a
named editor operation against the same native object, without cloning it.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from typing import Any

from docling_parse._pdf_document import (
    _UNSET,
    PdfEditablePage,
    PdfPage,
    PdfPages,
    _PageView,
    _PdfDocument,
    _PdfEditing,
)
from docling_parse.pdf_editor import PdfEditor


@dataclass(frozen=True)
class _Fit:
    fit_type: str = "/Fit"
    fit_args: tuple[float | None, ...] = ()


_DEFAULT_FIT = _Fit()


class _PdfReaderCompat(_PdfDocument):
    @property
    def _editor(self) -> PdfEditor:
        editor = object.__new__(PdfEditor)
        editor._native = self._native
        editor._page_owner = self
        return editor

    def _compat_page_no(self, index: int) -> int:
        count = len(self.pages)
        resolved = index + count if index < 0 else index
        if not 0 <= resolved < count:
            raise IndexError(f"page index {index} out of range")
        return resolved + 1

    @property
    def pages(self) -> PdfPages:
        """A zero-based sequence of pages."""
        return self._pages

    def get_num_pages(self) -> int:
        return len(self.pages)

    def get_page(self, page_number: int) -> PdfPage:
        return self._get_page(page_number)

    def get_page_number(self, page: PdfPage) -> int:
        return self._get_page_number(page)

    def get_pages_showing_field(self, field: Any) -> list[PdfPage]:
        return self._editor._compat_pages_showing_field(field=field)

    def get_fields(
        self,
        tree=None,
        retval=None,
        fileobj=None,
        stack=_UNSET,
        *,
        _visited=None,
        _depth=0,
        _traversal_state=None,
    ):
        return self._editor._compat_get_fields(
            tree=tree,
            retval=retval,
            fileobj=fileobj,
            stack=stack,
            visited=_visited,
            depth=_depth,
            traversal_state=_traversal_state,
        )

    def get_form_text_fields(self, full_qualified_name: bool = False) -> dict[str, Any]:
        return self._editor._compat_text_fields(full_qualified_name=full_qualified_name)

    def get_attachment(self, name: str) -> bytes:
        return self._editor.get_attachment(name=name)


class _PdfWriterCompat(_PdfReaderCompat, _PdfEditing):
    def append(
        self,
        fileobj: Any,
        outline_item: Any = None,
        pages: Any = None,
        import_outline: bool = True,
        excluded_fields: Any = None,
    ) -> None:
        return self._editor._compat_append(
            fileobj=fileobj,
            outline_item=outline_item,
            pages=pages,
            import_outline=import_outline,
            excluded_fields=excluded_fields,
        )

    def merge(
        self,
        position: int | None,
        fileobj: Any,
        outline_item: str | None = None,
        pages: Any = None,
        import_outline: bool = True,
        excluded_fields: Any = (),
    ) -> None:
        return self._editor._compat_append(
            position=position,
            fileobj=fileobj,
            outline_item=outline_item,
            pages=pages,
            import_outline=import_outline,
            excluded_fields=excluded_fields,
        )

    def add_page(
        self, page: _PageView, excluded_keys: Iterable[str] = ()
    ) -> PdfEditablePage:
        return self._editor._compat_insert_page(
            page=page, index=None, excluded_keys=excluded_keys
        )

    def insert_page(
        self, page: _PageView, index: int = 0, excluded_keys: Iterable[str] = ()
    ) -> PdfEditablePage:
        return self._editor._compat_insert_page(
            page=page, index=index, excluded_keys=excluded_keys
        )

    def add_blank_page(
        self, width: float | None = None, height: float | None = None
    ) -> PdfEditablePage:
        return self._editor._compat_blank_page(width=width, height=height, index=None)

    def insert_blank_page(
        self, width: float | None = None, height: float | None = None, index: int = 0
    ) -> PdfEditablePage:
        return self._editor._compat_blank_page(width=width, height=height, index=index)

    def remove_page(self, page: Any, clean: bool = False) -> None:
        self._remove_page(page, clean=clean, detach_references=clean)

    def add_metadata(self, infos: dict[str, str | None]) -> None:
        return self._editor.add_metadata(infos=infos)

    def set_page_layout(self, layout: str | None) -> None:
        return self._editor.set_page_layout(layout=layout)

    def set_page_label(
        self,
        page_index_from: int,
        page_index_to: int,
        style: Any = None,
        prefix: str | None = None,
        start: int | None = 0,
    ) -> None:
        self._editor.set_page_label(
            first_page_no=self._compat_page_no(page_index_from),
            last_page_no=self._compat_page_no(page_index_to),
            style=style,
            prefix=prefix,
            start=start or 1,
        )

    def add_named_destination(
        self, title: str, page_number: int, fit: Any = None
    ) -> Any:
        return self._editor._compat_named_destination(
            title=title, page_no=self._compat_page_no(page_number), fit=fit
        )

    def add_outline_item(
        self,
        title: str,
        page_number: Any,
        parent: Any = None,
        before: Any = None,
        color: Any = None,
        bold: bool = False,
        italic: bool = False,
        fit: Any = _DEFAULT_FIT,
        is_open: bool = True,
    ) -> Any:
        return self._editor._compat_outline_item(
            title=title,
            page_number=page_number,
            parent=parent,
            before=before,
            color=color,
            bold=bold,
            italic=italic,
            fit=fit,
            is_open=is_open,
        )

    def set_need_appearances_writer(self, state: bool = True) -> None:
        return self._editor.set_need_appearances_writer(state=state)

    def add_annotation(
        self, page_number: int | PdfPage, annotation: dict[str, Any]
    ) -> dict[str, Any]:
        return self._editor._compat_add_annotation(
            page_number=page_number, annotation=annotation
        )

    def add_uri(
        self,
        page_number: int,
        uri: str,
        rect: Sequence[float],
        border: Sequence[float] | None = None,
    ) -> None:
        return self._editor.add_uri(
            page_no=self._compat_page_no(page_number),
            uri=uri,
            rect=rect,
            border=border,
        )

    def remove_annotations(self, subtypes: Any) -> None:
        self._editor.remove_annotations(subtypes=subtypes)

    def remove_links(self) -> None:
        self._editor.remove_links()

    def add_attachment(self, filename: str, data: str | bytes) -> Any:
        return self._editor.add_attachment(filename=filename, data=data)

    def add_js(self, javascript: str) -> None:
        return self._editor.add_js(javascript=javascript)

    def encrypt(
        self,
        user_password: str,
        owner_password: str | None = None,
        use_128bit: bool = True,
        permissions_flag: int = 4294967292,
        *,
        algorithm: str | None = None,
    ) -> None:
        return self._editor._compat_encrypt(
            user_password=user_password,
            owner_password=owner_password,
            use_128bit=use_128bit,
            permissions_flag=permissions_flag,
            algorithm=algorithm,
        )

    def write(self, stream: Any) -> tuple[bool, Any]:
        return self._editor._compat_write(stream=stream)
