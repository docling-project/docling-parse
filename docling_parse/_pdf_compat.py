"""pypdf-compatible argument and result adapters over shared PDF operations."""

from __future__ import annotations

import operator
import os
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from typing import Any

from docling_parse._pdf_document import (
    _UNSET,
    PdfEditablePage,
    PdfPage,
    PdfPages,
    _native_errors,
    _PageView,
    _PdfDocument,
    _PdfEditing,
)
from docling_parse.pdf_errors import (
    PdfEditError,
    PdfEditErrorCategory,
    PdfLoadError,
    PdfLoadErrorCategory,
    PdfStaleHandleError,
    PdfUnsupportedError,
)


@dataclass(frozen=True)
class _Fit:
    fit_type: str = "/Fit"
    fit_args: tuple[float | None, ...] = ()


_DEFAULT_FIT = _Fit()


class _PdfReaderCompat(_PdfDocument):
    def _compat_open(
        self,
        *,
        stream: Any,
        strict: bool = False,
        password: str | bytes | None = None,
        root_object_recovery_limit: int | None = 10000,
    ) -> None:
        if root_object_recovery_limit != 10000:
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "qpdf does not expose pypdf's root-object recovery limit",
            )
        self._open(stream, password)
        if strict:
            try:
                with _native_errors():
                    self._native.validate_strict()
            except PdfEditError as exc:
                self.close()
                raise PdfLoadError(
                    "<source>", PdfLoadErrorCategory.DAMAGED, str(exc)
                ) from exc

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
        if isinstance(field, str):
            name = field
        else:
            name = field.get("name", field.get("/T", ""))
        return self._get_pages_showing_field(str(name))

    @property
    def has_form(self) -> bool:
        return self._form_flags()[0]

    @property
    def has_xfa(self) -> bool:
        return self._form_flags()[1]

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
        if (
            tree is not None
            or stack is not _UNSET
            or _visited is not None
            or _depth
            or _traversal_state is not None
        ):
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "subtree traversal requires the PDF object editing API",
            )
        fields = self._get_fields()
        if retval is not None:
            retval.update(fields)
            fields = retval
        if fileobj is not None:
            for name, field in fields.items():
                fileobj.write(f"{name}: {field['value']}\n")
        return fields

    def get_form_text_fields(self, full_qualified_name: bool = False) -> dict[str, Any]:
        fields = self._get_form_text_fields()
        if full_qualified_name:
            return fields
        result: dict[str, Any] = {}
        for name, value in fields.items():
            partial = name.rsplit(".", 1)[-1]
            key, suffix = partial, 2
            while key in result:
                key = f"{partial}.{suffix}"
                suffix += 1
            result[key] = value
        return result

    def get_attachment(self, name: str) -> bytes:
        return self._get_attachment(name)


class _PdfWriterCompat(_PdfReaderCompat, _PdfEditing):
    def _compat_init_writer(
        self,
        *,
        fileobj: Any,
        clone_from: Any,
        incremental: bool,
        full: bool,
        strict: bool,
        keep_initial_header: bool,
        incremental_clone_object_count_limit: int | None,
        incremental_clone_object_id_limit: int | None,
    ) -> bool:
        if incremental or full:
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "incremental output requires the incremental serializer",
            )
        if (
            keep_initial_header
            or incremental_clone_object_count_limit != 500_000
            or incremental_clone_object_id_limit != 1_000_000
        ):
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "header preservation and custom incremental clone limits are unsupported",
            )
        source = clone_from
        if source is None and fileobj is not None and fileobj != "":
            if isinstance(fileobj, (str, os.PathLike)):
                if os.path.isfile(fileobj) and os.path.getsize(fileobj):
                    source = fileobj
            elif (
                hasattr(fileobj, "read")
                and getattr(fileobj, "seekable", lambda: True)()
            ):
                position = fileobj.tell()
                if fileobj.seek(0, 2):
                    source = fileobj
                fileobj.seek(position)
            elif not hasattr(fileobj, "write"):
                source = fileobj
        if source is None:
            self._native.create_empty()
        else:
            self._compat_open(stream=source, strict=strict)
        return source is not None

    def _compat_append(
        self,
        *,
        fileobj: Any,
        outline_item: Any = None,
        pages: Any = None,
        import_outline: bool = True,
        excluded_fields: Any = None,
        position: int | None = None,
    ) -> None:
        if outline_item is not None and not isinstance(outline_item, str):
            if pages is not None:
                raise TypeError("page selection supplied twice")
            pages, outline_item = outline_item, None
        if excluded_fields:
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "excluding imported page keys requires the PDF object editing API",
            )
        at = -1 if position is None else self._position(position)
        if isinstance(pages, (list, range)) and not pages:
            return
        native, temporary = self._import_source(fileobj, None)
        try:
            source = temporary if temporary is not None else fileobj
            with _native_errors():
                count, generation = native.page_list_state()
            if pages is None:
                selected = list(range(count))
            elif isinstance(pages, slice):
                selected = list(range(*pages.indices(count)))
            elif isinstance(pages, tuple):
                selected = list(range(*pages))
            elif hasattr(pages, "indices"):
                selected = list(range(*pages.indices(count)))
            else:
                selected = []
                for page in pages:
                    if isinstance(page, _PageView):
                        index, expected = source._page_handle(page)
                        if expected != generation:
                            raise PdfStaleHandleError(
                                PdfEditErrorCategory.STALE_HANDLE,
                                "the source page list changed since this handle was obtained",
                            )
                    else:
                        index = operator.index(page)
                    selected.append(index)
            selected = [p + count if p < 0 else p for p in selected]
            if any(p < 0 or p >= count for p in selected):
                raise IndexError("source page index out of range")
            with _native_errors():
                self._native.append_pages(
                    native,
                    selected,
                    at,
                    other_generation=generation,
                    import_outline=import_outline,
                    outline_item=outline_item,
                    transient=temporary is not None,
                )
        finally:
            if temporary is not None:
                temporary.close()

    def _compat_insert_page(
        self, *, page: _PageView, index: int | None, excluded_keys: Iterable[str]
    ) -> PdfEditablePage:
        if tuple(excluded_keys):
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "excluding page keys requires the PDF object editing API",
            )
        return self._insert_page(page, index)

    def _compat_blank_page(
        self, *, width: float | None, height: float | None, index: int | None
    ) -> PdfEditablePage:
        if width is None or height is None:
            count = self.get_num_pages()
            if not count:
                raise ValueError("width and height are required for the first page")
            position = count - 1 if index is None else min(index, count - 1)
            box = self._get_page(position).mediabox
            width = box.width if width is None else width
            height = box.height if height is None else height
        return self._insert_blank_page(index, width, height)

    def append(
        self,
        fileobj: Any,
        outline_item: Any = None,
        pages: Any = None,
        import_outline: bool = True,
        excluded_fields: Any = None,
    ) -> None:
        return self._compat_append(
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
        return self._compat_append(
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
        return self._compat_insert_page(
            page=page, index=None, excluded_keys=excluded_keys
        )

    def insert_page(
        self, page: _PageView, index: int = 0, excluded_keys: Iterable[str] = ()
    ) -> PdfEditablePage:
        return self._compat_insert_page(
            page=page, index=index, excluded_keys=excluded_keys
        )

    def add_blank_page(
        self, width: float | None = None, height: float | None = None
    ) -> PdfEditablePage:
        return self._compat_blank_page(width=width, height=height, index=None)

    def insert_blank_page(
        self, width: float | None = None, height: float | None = None, index: int = 0
    ) -> PdfEditablePage:
        return self._compat_blank_page(width=width, height=height, index=index)

    def remove_page(self, page: Any, clean: bool = False) -> None:
        self._remove_page(page, clean=clean, detach_references=clean)

    def add_metadata(self, infos: dict[str, str | None]) -> None:
        return self._add_metadata(infos)

    def set_page_layout(self, layout: str | None) -> None:
        return self._set_page_layout(layout)

    def set_page_label(
        self,
        page_index_from: int,
        page_index_to: int,
        style: Any = None,
        prefix: str | None = None,
        start: int | None = 0,
    ) -> None:
        self._set_page_label(
            page_index_from,
            page_index_to,
            style=style,
            prefix=prefix,
            start=start or 1,
        )

    def add_named_destination(
        self, title: str, page_number: int, fit: Any = None
    ) -> Any:
        if fit is not None and getattr(fit, "fit_type", "/Fit") != "/Fit":
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "this destination fit mode is not supported",
            )
        return self._add_named_destination(title, page_number)

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
        options: dict[str, Any] = {}
        if fit is not None:
            kind = getattr(fit, "fit_type", "/Fit")
            if kind == "/XYZ":
                options.update(zip(("left", "top", "zoom"), fit.fit_args))
            elif kind != "/Fit":
                raise PdfUnsupportedError(
                    PdfEditErrorCategory.UNSUPPORTED,
                    "this outline fit mode is not supported",
                )
        return self._add_outline_item(
            title=title,
            page_number=page_number,
            parent=parent,
            before=before,
            color=color,
            bold=bold,
            italic=italic,
            is_open=is_open,
            **options,
        )

    def set_need_appearances_writer(self, state: bool = True) -> None:
        return self._set_need_appearances_writer(state)

    def add_annotation(
        self, page_number: int | PdfPage, annotation: dict[str, Any]
    ) -> dict[str, Any]:
        result = self._add_annotation(page_number, annotation, report_annotation=True)
        assert result is not None
        return result

    def add_uri(
        self,
        page_number: int,
        uri: str,
        rect: Sequence[float],
        border: Sequence[float] | None = None,
    ) -> None:
        return self._add_uri(
            page_number=page_number,
            uri=uri,
            rect=rect,
            border=border,
        )

    def remove_annotations(self, subtypes: Any) -> None:
        self._remove_annotations(subtypes)

    def remove_links(self) -> None:
        self._remove_links()

    def add_attachment(self, filename: str, data: str | bytes) -> Any:
        return self._add_attachment(filename, data)

    def add_js(self, javascript: str) -> None:
        return self._add_js(javascript)

    def encrypt(
        self,
        user_password: str | bytes,
        owner_password: str | bytes | None = None,
        use_128bit: bool = True,
        permissions_flag: int = 4294967292,
        *,
        algorithm: str | None = None,
    ) -> None:
        flags = int(permissions_flag)
        self._encrypt(
            user_password=user_password,
            owner_password=owner_password,
            algorithm=algorithm or ("RC4-128" if use_128bit else "RC4-40"),
            allow_print=(True if flags & 2048 else "low") if flags & 4 else False,
            allow_modify=bool(flags & 8),
            allow_extract=bool(flags & 16),
            allow_annotate=bool(flags & 32),
            allow_fill_forms=bool(flags & 256),
            allow_accessibility=bool(flags & 512),
            allow_assemble=bool(flags & 1024),
        )

    def write(self, stream: Any) -> tuple[bool, Any]:
        if stream == "":
            raise ValueError("output path is empty")
        result = self._write(target=stream)
        if result is not None:
            return True, result
        if hasattr(stream, "flush"):
            stream.flush()
        return False, stream
