"""Signature adapters; all document operations are implemented by PdfEditor.

The native subset still enforces reader immutability. Each adapter invokes a
named editor operation against the same native object, without cloning it.
"""

from __future__ import annotations

import os
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from typing import Any, BinaryIO

from docling_parse._pdf_document import (
    _UNSET,
    PdfEditablePage,
    PdfPage,
    _pdf_images,
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

    def _compat_page_no(self, index: int, *, insertion: bool = False) -> int:
        count = len(self.pages)
        resolved = index + count if index < 0 else index
        maximum = count if insertion else count - 1
        if not 0 <= resolved <= maximum:
            raise IndexError(f"page index {index} out of range")
        return resolved + 1

    def get_page(self, page_number: int) -> PdfPage:
        return self._editor.get_page(
            page_no=self._compat_page_no(page_number, insertion=False)
        )

    def get_page_number(self, page: PdfPage) -> int:
        return self._editor.get_page_no(page=page) - 1

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
        self, page: PdfPage, excluded_keys: Iterable[str] = ()
    ) -> PdfEditablePage:
        return self._editor._compat_insert_page(
            page=page, index=len(self.pages), excluded_keys=excluded_keys
        )

    def insert_page(
        self, page: PdfPage, index: int = 0, excluded_keys: Iterable[str] = ()
    ) -> PdfEditablePage:
        return self._editor._compat_insert_page(
            page=page, index=index, excluded_keys=excluded_keys
        )

    def add_blank_page(
        self, width: float | None = None, height: float | None = None
    ) -> PdfEditablePage:
        return self._editor._compat_blank_page(
            width=width, height=height, index=len(self.pages)
        )

    def insert_blank_page(
        self, width: float | None = None, height: float | None = None, index: int = 0
    ) -> PdfEditablePage:
        return self._editor._compat_blank_page(width=width, height=height, index=index)

    def add_image(
        self,
        image: _pdf_images.ImageSource,
        *,
        index: int | None = None,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> list[PdfEditablePage]:
        return self._editor.add_image(
            image=image,
            before_page_no=index
            if index is None or index is _UNSET
            else self._compat_page_no(index, insertion=True),
            page_size=page_size,
            margin=margin,
            dpi=dpi,
        )

    @classmethod
    def from_images(
        cls,
        images: Iterable[_pdf_images.ImageSource],
        *,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> Any:
        constructor: Any = cls
        return constructor(
            clone_from=PdfEditor.from_images(
                images=images, page_size=page_size, margin=margin, dpi=dpi
            )
        )

    def duplicate_page(self, index: int, to: int | None = None) -> PdfEditablePage:
        return self._editor.duplicate_page(
            page_no=self._compat_page_no(index, insertion=False),
            before_page_no=to
            if to is None or to is _UNSET
            else self._compat_page_no(to, insertion=True),
        )

    def remove_page(self, page: Any, clean: bool = False) -> None:
        return self._editor.remove_page(
            page_no=self._compat_page_no(
                page.index if isinstance(page, PdfPage) else page
            ),
            clean=clean,
        )

    def move_page(self, from_index: int, to_index: int) -> None:
        return self._editor.move_page(
            page_no=self._compat_page_no(from_index, insertion=False),
            to_page_no=self._compat_page_no(to_index, insertion=False),
        )

    def add_metadata(self, infos: dict[str, str | None]) -> None:
        return self._editor.add_metadata(infos=infos)

    def set_xmp_metadata(self, packet: str | bytes | None) -> None:
        return self._editor.set_xmp_metadata(packet=packet)

    def set_page_layout(self, layout: str | None) -> None:
        return self._editor.set_page_layout(layout=layout)

    def set_page_mode(self, mode: str | None) -> None:
        return self._editor.set_page_mode(mode=mode)

    def set_viewer_preferences(self, preferences: dict[str, Any] | None) -> None:
        return self._editor.set_viewer_preferences(preferences=preferences)

    def set_open_destination(self, page_number: int | None) -> None:
        return self._editor.set_open_destination(
            page_no=None
            if page_number is None
            else self._compat_page_no(page_number, insertion=False)
        )

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

    def update_outline_item(
        self,
        item_id: int,
        *,
        title: str = _UNSET,
        page_number: int | None = _UNSET,
        url: str | None = _UNSET,
        left: float | None = _UNSET,
        top: float | None = _UNSET,
        zoom: float | None = _UNSET,
        color: Sequence[float] | str | None = _UNSET,
        bold: bool = _UNSET,
        italic: bool = _UNSET,
        is_open: bool = _UNSET,
    ) -> None:
        return self._editor.update_outline_item(
            item_id=item_id,
            title=title,
            page_no=page_number
            if page_number is None or page_number is _UNSET
            else self._compat_page_no(page_number, insertion=False),
            url=url,
            left=left,
            top=top,
            zoom=zoom,
            color=color,
            bold=bold,
            italic=italic,
            is_open=is_open,
        )

    def remove_outline_item(self, item_id: int) -> None:
        return self._editor.remove_outline_item(item_id=item_id)

    def move_outline_item(
        self, item_id: int, parent: int | None = None, index: int | None = None
    ) -> None:
        return self._editor.move_outline_item(
            item_id=item_id, parent=parent, index=index
        )

    def clear_outline(self) -> None:
        return self._editor.clear_outline()

    def add_table_of_contents(
        self,
        index: int = 0,
        *,
        title: str = "Contents",
        font: str = "Helvetica",
        size: float = 11,
        max_depth: int | None = None,
        page_size: str | Sequence[float] | None = None,
    ) -> list[PdfEditablePage]:
        return self._editor.add_table_of_contents(
            before_page_no=self._compat_page_no(index, insertion=True),
            title=title,
            font=font,
            size=size,
            max_depth=max_depth,
            page_size=page_size,
        )

    def update_form_field_values(
        self, fields: dict[str, str | bool], *, flatten: bool = False
    ) -> int:
        return self._editor.update_form_field_values(fields=fields, flatten=flatten)

    def add_text_field(
        self,
        page_number: int,
        name: str,
        rect: Sequence[float],
        value: str | None = None,
        *,
        multiline: bool = False,
        font_size: float = 12,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        return self._editor.add_text_field(
            page_no=self._compat_page_no(page_number, insertion=False),
            name=name,
            rect=rect,
            value=value,
            multiline=multiline,
            font_size=font_size,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_checkbox(
        self,
        page_number: int,
        name: str,
        rect: Sequence[float],
        checked: bool = False,
        *,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        return self._editor.add_checkbox(
            page_no=self._compat_page_no(page_number, insertion=False),
            name=name,
            rect=rect,
            checked=checked,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_choice_field(
        self,
        page_number: int,
        name: str,
        rect: Sequence[float],
        options: Sequence[str],
        value: str | None = None,
        *,
        editable: bool = False,
        font_size: float = 12,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        return self._editor.add_choice_field(
            page_no=self._compat_page_no(page_number, insertion=False),
            name=name,
            rect=rect,
            options=options,
            value=value,
            editable=editable,
            font_size=font_size,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_radio_group(
        self,
        page_number: int,
        name: str,
        buttons: dict[str, Sequence[float]],
        selected: str | None = None,
        *,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        return self._editor.add_radio_group(
            page_no=self._compat_page_no(page_number, insertion=False),
            name=name,
            buttons=buttons,
            selected=selected,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_signature_field(
        self,
        page_number: int,
        name: str,
        rect: Sequence[float],
        *,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        return self._editor.add_signature_field(
            page_no=self._compat_page_no(page_number, insertion=False),
            name=name,
            rect=rect,
            required=required,
            tooltip=tooltip,
        )

    def update_form_field(
        self,
        name: str,
        *,
        new_name: str = _UNSET,
        rect: Sequence[float] = _UNSET,
        read_only: bool = _UNSET,
        required: bool = _UNSET,
        tooltip: str | None = _UNSET,
        options: Sequence[str] = _UNSET,
    ) -> None:
        return self._editor.update_form_field(
            name=name,
            new_name=new_name,
            rect=rect,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
            options=options,
        )

    def remove_form_field(self, name: str) -> None:
        return self._editor.remove_form_field(name=name)

    def set_need_appearances_writer(self, state: bool = True) -> None:
        return self._editor.set_need_appearances_writer(state=state)

    def flatten_annotations(self) -> None:
        return self._editor.flatten_annotations()

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
            page_no=self._compat_page_no(page_number, insertion=False),
            uri=uri,
            rect=rect,
            border=border,
        )

    def add_link(
        self, page_number: int, rect: Sequence[float], target_page_number: int
    ) -> None:
        return self._editor.add_link(
            page_no=self._compat_page_no(page_number, insertion=False),
            rect=rect,
            target_page_no=self._compat_page_no(target_page_number, insertion=False),
        )

    def remove_annotations(self, subtypes: Any) -> None:
        self._editor.remove_annotations(subtypes=subtypes)

    def remove_links(self) -> None:
        self._editor.remove_links()

    def add_attachment(self, filename: str, data: str | bytes) -> Any:
        return self._editor.add_attachment(filename=filename, data=data)

    def remove_attachment(self, name: str) -> bool:
        return self._editor.remove_attachment(name=name)

    def add_js(self, javascript: str) -> None:
        return self._editor.add_js(javascript=javascript)

    def remove_javascript(self) -> int:
        return self._editor.remove_javascript()

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

    def set_output_options(
        self,
        *,
        compress_streams: bool = True,
        object_streams: str = "preserve",
        linearize: bool = False,
    ) -> None:
        return self._editor.set_output_options(
            compress_streams=compress_streams,
            object_streams=object_streams,
            linearize=linearize,
        )

    def remove_encryption(self) -> None:
        return self._editor.remove_encryption()

    def to_bytes(self) -> bytes:
        return self._editor.to_bytes()

    def write(self, stream: Any) -> tuple[bool, Any]:
        return self._editor._compat_write(stream=stream)
