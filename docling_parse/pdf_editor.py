"""PdfEditor: the complete docling-parse PDF surface in one object.

PdfReader and PdfWriter offer the subset of these operations that matches
pypdf. PdfEditor offers all of them, and is where functionality without a
pypdf counterpart appears first.

```python
from docling_parse.pdf_editor import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    print(editor.metadata, editor.pages[0].extract_text())
    editor.pages[0].rotate(angle=90)
    editor.append(source="appendix.pdf")
    editor.write(target="out.pdf")
```
"""

from __future__ import annotations

import math
import operator
import os
from collections.abc import Iterable, Sequence
from typing import Any, BinaryIO

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
from docling_parse._pdf_document import (
    _UNSET,
    STANDARD_FONTS,
    PdfBox,
    PdfEditablePage,
    PdfImage,
    PdfPage,
    PdfPages,
    PdfSource,
    _native_errors,
    _pdf_images,
    _PdfDocument,
    _PdfEditing,
)
from docling_parse.pdf_errors import (
    PdfEditError,
    PdfEditErrorCategory,
    PdfLoadError,
    PdfLoadErrorCategory,
    PdfUnsupportedError,
)

__all__ = [
    "STANDARD_FONTS",
    "PdfBox",
    "PdfEditablePage",
    "PdfEditor",
    "PdfImage",
    "PdfPages",
]


class PdfEditor(_PdfEditing):
    """A PDF document that can be inspected, changed and written.

    `PdfEditor(source=source)` opens a document; `PdfEditor()` starts empty. An
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
        *,
        source: PdfSource | _PdfDocument | None = None,
        password: str | None = None,
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

    def render_page(self, *, page_no: int, scale: float = 1.0) -> Any:
        """Render a page of the current document as a PIL RGBA image.

        ``page_no`` starts at 1. ``scale=1`` gives one pixel per PDF point
        (72 dpi); ``scale=2`` gives 144 dpi. Rendering includes unsaved edits
        and leaves the document unchanged. Pillow is imported on demand.
        """
        if isinstance(page_no, bool):
            raise TypeError("page_no must be an integer, starting at 1")
        page_no = operator.index(page_no)
        if page_no < 1:
            raise ValueError("page_no must be at least 1")
        if not math.isfinite(scale) or scale <= 0:
            raise ValueError("scale must be finite and positive")
        with _native_errors():
            data, shape = self._native.render_page(page_no=page_no, scale=scale)

        from PIL import Image

        height, width, _ = shape
        return Image.frombytes("RGBA", (width, height), data)

    def get_object(self, *, object_id: int, depth: int = 3) -> Any:
        """A read-only view of the PDF object with this object number, as
        nested dicts and lists down to `depth` levels. Names keep their
        slash; a stream is shown as its dictionary."""
        if object_id < 1:
            raise ValueError("object numbers start at 1")
        with _native_errors():
            return self._native.object_json(object_id, depth)

    @property
    def trailer(self) -> Any:
        """A read-only view of the file trailer."""
        with _native_errors():
            return self._native.object_json(0, 2)

    @property
    def root_object(self) -> Any:
        """A read-only view of the document catalog."""
        with _native_errors():
            return self._native.object_json(-1, 3)

    @property
    def annotations(self) -> Any:
        """Form, language, XMP, outline and structure tree of the document
        as it currently is, including edits not yet written."""
        with _native_errors():
            return self._native.annotations()

    def open(
        self, *, source: PdfSource | _PdfDocument, password: str | None = None
    ) -> None:
        """Open a document, replacing the current state."""
        self._open(source, password)

    def _page_no_index(self, *, page_no: int, insertion: bool = False) -> int:
        if isinstance(page_no, bool):
            raise TypeError("page_no must be an integer, starting at 1")
        value = operator.index(page_no)
        maximum = len(self.pages) + int(insertion)
        if not 1 <= value <= maximum:
            raise IndexError(f"page_no {value} out of range; expected 1..{maximum}")
        return value - 1

    def _selection(self, *, page_nos: Iterable[int] | None) -> list[int] | None:
        if page_nos is None:
            return None
        result = []
        for value in page_nos:
            if isinstance(value, bool):
                raise TypeError("page_nos must contain integers, starting at 1")
            value = operator.index(value)
            if value < 1:
                raise ValueError("page_nos must start at 1")
            result.append(value - 1)
        return result

    def get_page(self, *, page_no: int) -> PdfPage:
        "Get page. Page numbers start at 1."
        return _PdfDocument._get_page(
            self, page_number=self._page_no_index(page_no=page_no, insertion=False)
        )

    def get_page_no(self, *, page: PdfPage) -> int:
        """The one-based number of a page belonging to this document."""
        return _PdfDocument._get_page_number(self, page=page) + 1

    def get_pages_showing_field(self, *, name: str) -> list[PdfPage]:
        "The pages that show the form field with this fully qualified name."
        return _PdfDocument._get_pages_showing_field(self, name=name)

    def get_fields(self) -> dict[str, dict[str, Any]]:
        'The form fields by fully qualified name. Each field has "kind"\n(text, checkbox, radio, choice, pushbutton, signature), "value",\n"default_value", "choices", "states", "read_only", "pages", and the\nraw "type" and "flags".'
        return _PdfDocument._get_fields(
            self,
        )

    def get_form_text_fields(self) -> dict[str, str]:
        "The value of every text field, by fully qualified name."
        return _PdfDocument._get_form_text_fields(
            self,
        )

    def get_attachment(self, *, name: str) -> bytes:
        "The content of the embedded file registered under `name`."
        return _PdfDocument._get_attachment(self, name=name)

    def append(
        self,
        *,
        source: Any,
        page_nos: Iterable[int] | None = None,
        before_page_no: int | None = None,
        password: str | None = None,
        import_outline: bool = True,
    ) -> None:
        "Append. Page numbers start at 1."
        return _PdfEditing._append(
            self,
            source=source,
            pages=self._selection(page_nos=page_nos),
            index=(
                before_page_no
                if before_page_no is None or before_page_no is _UNSET
                else self._page_no_index(page_no=before_page_no, insertion=True)
            ),
            password=password,
            import_outline=import_outline,
        )

    def merge(
        self,
        *,
        before_page_no: int | None,
        fileobj: Any,
        page_nos: Iterable[int] | None = None,
        password: str | None = None,
        import_outline: bool = True,
    ) -> None:
        "Merge. Page numbers start at 1."
        return _PdfEditing._merge(
            self,
            position=(
                None
                if before_page_no is None
                else self._page_no_index(page_no=before_page_no, insertion=True)
            ),
            fileobj=fileobj,
            pages=self._selection(page_nos=page_nos),
            password=password,
            import_outline=import_outline,
        )

    def add_page(self, *, page: PdfPage) -> PdfEditablePage:
        "Append a copy of a page of any reader, writer or editor."
        return _PdfEditing._add_page(self, page=page)

    def insert_page(self, *, page: PdfPage, before_page_no: int = 1) -> PdfEditablePage:
        "Insert page. Page numbers start at 1."
        return _PdfEditing._insert_page(
            self,
            page=page,
            index=self._page_no_index(page_no=before_page_no, insertion=True),
        )

    def add_blank_page(self, *, width: float, height: float) -> PdfEditablePage:
        "Add an empty page at the end. Sizes are in points (1/72 inch)."
        return _PdfEditing._add_blank_page(self, width=width, height=height)

    def insert_blank_page(
        self, *, before_page_no: int, width: float, height: float
    ) -> PdfEditablePage:
        "Insert blank page. Page numbers start at 1."
        return _PdfEditing._insert_blank_page(
            self,
            index=self._page_no_index(page_no=before_page_no, insertion=True),
            width=width,
            height=height,
        )

    def add_image(
        self,
        *,
        image: _pdf_images.ImageSource,
        before_page_no: int | None = None,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> list[PdfEditablePage]:
        "Add image. Page numbers start at 1."
        return _PdfEditing._add_image(
            self,
            image=image,
            index=(
                before_page_no
                if before_page_no is None or before_page_no is _UNSET
                else self._page_no_index(page_no=before_page_no, insertion=True)
            ),
            page_size=page_size,
            margin=margin,
            dpi=dpi,
        )

    @classmethod
    def from_images(
        cls,
        *,
        images: Iterable[_pdf_images.ImageSource],
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> Any:
        document = cls()
        for image in images:
            document.add_image(image=image, page_size=page_size, margin=margin, dpi=dpi)
        return document

    def duplicate_page(
        self, *, page_no: int, before_page_no: int | None = None
    ) -> PdfEditablePage:
        "Duplicate page. Page numbers start at 1."
        return _PdfEditing._duplicate_page(
            self,
            index=self._page_no_index(page_no=page_no, insertion=False),
            to=(
                before_page_no
                if before_page_no is None or before_page_no is _UNSET
                else self._page_no_index(page_no=before_page_no, insertion=True)
            ),
        )

    def remove_page(self, *, page_no: int, clean: bool = False) -> None:
        "Remove page. Page numbers start at 1."
        return _PdfEditing._remove_page(
            self,
            index=self._page_no_index(page_no=page_no, insertion=False),
            clean=clean,
        )

    def move_page(self, *, page_no: int, to_page_no: int) -> None:
        "Move page. Page numbers start at 1."
        return _PdfEditing._move_page(
            self,
            from_index=self._page_no_index(page_no=page_no, insertion=False),
            to_index=self._page_no_index(page_no=to_page_no, insertion=False),
        )

    def add_metadata(self, *, infos: dict[str, str | None]) -> None:
        'Set entries of the document information dictionary. Keys may be\nwritten "Title" or "/Title"; a value of None removes the entry.'
        return _PdfEditing._add_metadata(self, infos=infos)

    def set_xmp_metadata(self, *, packet: str | bytes | None) -> None:
        "Replace the XMP metadata packet, or remove it with None."
        return _PdfEditing._set_xmp_metadata(self, packet=packet)

    def set_page_layout(self, *, layout: str | None) -> None:
        'Set how pages are laid out when the document opens: "/SinglePage",\n"/OneColumn", "/TwoColumnLeft", "/TwoColumnRight", "/TwoPageLeft" or\n"/TwoPageRight". None removes the setting.'
        return _PdfEditing._set_page_layout(self, layout=layout)

    def set_page_mode(self, *, mode: str | None) -> None:
        'Set what a viewer shows beside the pages: "/UseNone",\n"/UseOutlines", "/UseThumbs", "/FullScreen", "/UseOC" or\n"/UseAttachments". None removes the setting.'
        return _PdfEditing._set_page_mode(self, mode=mode)

    def set_viewer_preferences(self, *, preferences: dict[str, Any] | None) -> None:
        'Replace the viewer preferences, for example\n`{"/HideToolbar": True, "/Direction": "/R2L"}`. None removes them.'
        return _PdfEditing._set_viewer_preferences(self, preferences=preferences)

    def set_open_destination(self, *, page_no: int | None) -> None:
        "Set open destination. Page numbers start at 1."
        return _PdfEditing._set_open_destination(
            self,
            page_number=(
                None
                if page_no is None
                else self._page_no_index(page_no=page_no, insertion=False)
            ),
        )

    def set_page_label(
        self,
        *,
        first_page_no: int,
        last_page_no: int,
        style: str | None = None,
        prefix: str | None = None,
        start: int | None = None,
    ) -> None:
        "Set page label. Page numbers start at 1."
        return _PdfEditing._set_page_label(
            self,
            page_index_from=self._page_no_index(page_no=first_page_no, insertion=False),
            page_index_to=self._page_no_index(page_no=last_page_no, insertion=False),
            style=style,
            prefix=prefix,
            start=start,
        )

    def add_named_destination(self, *, title: str, page_no: int) -> None:
        "Add named destination. Page numbers start at 1."
        return _PdfEditing._add_named_destination(
            self,
            title=title,
            page_number=(
                None
                if page_no is None
                else self._page_no_index(page_no=page_no, insertion=False)
            ),
        )

    def add_outline_item(
        self,
        *,
        title: str,
        page_no: int | None,
        parent: int | None = None,
        url: str | None = None,
        left: float | None = None,
        top: float | None = None,
        zoom: float | None = None,
        color: Sequence[float] | str | None = None,
        bold: bool = False,
        italic: bool = False,
        is_open: bool = True,
    ) -> int:
        "Add outline item. Page numbers start at 1."
        return _PdfEditing._add_outline_item(
            self,
            title=title,
            page_number=(
                None
                if page_no is None
                else self._page_no_index(page_no=page_no, insertion=False)
            ),
            parent=parent,
            url=url,
            left=left,
            top=top,
            zoom=zoom,
            color=color,
            bold=bold,
            italic=italic,
            is_open=is_open,
        )

    def update_outline_item(
        self,
        *,
        item_id: int,
        title: str = _UNSET,
        page_no: int | None = _UNSET,
        url: str | None = _UNSET,
        left: float | None = _UNSET,
        top: float | None = _UNSET,
        zoom: float | None = _UNSET,
        color: Sequence[float] | str | None = _UNSET,
        bold: bool = _UNSET,
        italic: bool = _UNSET,
        is_open: bool = _UNSET,
    ) -> None:
        "Update outline item. Page numbers start at 1."
        return _PdfEditing._update_outline_item(
            self,
            item_id=item_id,
            title=title,
            page_number=(
                page_no
                if page_no is None or page_no is _UNSET
                else self._page_no_index(page_no=page_no, insertion=False)
            ),
            url=url,
            left=left,
            top=top,
            zoom=zoom,
            color=color,
            bold=bold,
            italic=italic,
            is_open=is_open,
        )

    def remove_outline_item(self, *, item_id: int) -> None:
        "Remove a bookmark and every bookmark below it."
        return _PdfEditing._remove_outline_item(self, item_id=item_id)

    def move_outline_item(
        self, *, item_id: int, parent: int | None = None, index: int | None = None
    ) -> None:
        "Move a bookmark below `parent` (None: the top level), at position\n`index` among its new siblings (None: last). Its children move with\nit."
        return _PdfEditing._move_outline_item(
            self, item_id=item_id, parent=parent, index=index
        )

    def clear_outline(self) -> None:
        "Remove every bookmark."
        return _PdfEditing._clear_outline(
            self,
        )

    def add_table_of_contents(
        self,
        *,
        before_page_no: int = 1,
        title: str = "Contents",
        font: str = "Helvetica",
        size: float = 11,
        max_depth: int | None = None,
        page_size: str | Sequence[float] | None = None,
    ) -> list[PdfEditablePage]:
        "Add table of contents. Page numbers start at 1."
        return _PdfEditing._add_table_of_contents(
            self,
            index=self._page_no_index(page_no=before_page_no, insertion=True),
            title=title,
            font=font,
            size=size,
            max_depth=max_depth,
            page_size=page_size,
        )

    def update_form_field_values(
        self, *, fields: dict[str, str | bool], flatten: bool = False
    ) -> int:
        'Fill in form fields, by fully qualified name; a partial name works\nwhen only one field has it.\n\nText and choice fields take a string. A checkbox takes True, False,\nor one of its states; a radio group takes the state to select\n("/Choice1", or "Choice1"), or "/Off". The fields\' appearances are\nregenerated so the values show in every viewer.\n\nWith `flatten`, the form is then turned into page content. Returns\nthe number of fields set; an unknown name raises `PdfEditError`.'
        return _PdfEditing._update_form_field_values(
            self, fields=fields, flatten=flatten
        )

    def add_text_field(
        self,
        *,
        page_no: int,
        name: str,
        rect: Sequence[float],
        value: str | None = None,
        multiline: bool = False,
        font_size: float = 12,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        "Add text field. Page numbers start at 1."
        return _PdfEditing._add_text_field(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
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
        *,
        page_no: int,
        name: str,
        rect: Sequence[float],
        checked: bool = False,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        "Add checkbox. Page numbers start at 1."
        return _PdfEditing._add_checkbox(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            name=name,
            rect=rect,
            checked=checked,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_choice_field(
        self,
        *,
        page_no: int,
        name: str,
        rect: Sequence[float],
        options: Sequence[str],
        value: str | None = None,
        editable: bool = False,
        font_size: float = 12,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        "Add choice field. Page numbers start at 1."
        return _PdfEditing._add_choice_field(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
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
        *,
        page_no: int,
        name: str,
        buttons: dict[str, Sequence[float]],
        selected: str | None = None,
        read_only: bool = False,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        "Add radio group. Page numbers start at 1."
        return _PdfEditing._add_radio_group(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            name=name,
            buttons=buttons,
            selected=selected,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
        )

    def add_signature_field(
        self,
        *,
        page_no: int,
        name: str,
        rect: Sequence[float],
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        "Add signature field. Page numbers start at 1."
        return _PdfEditing._add_signature_field(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            name=name,
            rect=rect,
            required=required,
            tooltip=tooltip,
        )

    def update_form_field(
        self,
        *,
        name: str,
        new_name: str = _UNSET,
        rect: Sequence[float] = _UNSET,
        read_only: bool = _UNSET,
        required: bool = _UNSET,
        tooltip: str | None = _UNSET,
        options: Sequence[str] = _UNSET,
    ) -> None:
        "Change a form field. Only the arguments given change.\n\n`new_name` renames the field (its last name part). `rect` moves or\nresizes a field that has one widget. `tooltip` None removes it.\n`options` replaces the entries of a choice field."
        return _PdfEditing._update_form_field(
            self,
            name=name,
            new_name=new_name,
            rect=rect,
            read_only=read_only,
            required=required,
            tooltip=tooltip,
            options=options,
        )

    def remove_form_field(self, *, name: str) -> None:
        "Remove a form field, and its widgets from the pages."
        return _PdfEditing._remove_form_field(self, name=name)

    def set_need_appearances_writer(self, *, state: bool = True) -> None:
        "Set the form's NeedAppearances flag, which asks a viewer to build\nthe field appearances itself."
        return _PdfEditing._set_need_appearances_writer(self, state=state)

    def flatten_annotations(self) -> None:
        "Draw every visible annotation, form fields included, into its\npage and remove the annotations. Values stay visible; the document\nis no longer interactive."
        return _PdfEditing._flatten_annotations(
            self,
        )

    def add_annotation(self, *, page_no: int, annotation: dict[str, Any]) -> None:
        "Add annotation. Page numbers start at 1."
        return _PdfEditing._add_annotation(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            annotation=annotation,
        )

    def add_uri(
        self,
        *,
        page_no: int,
        uri: str,
        rect: Sequence[float],
        border: Sequence[float] | None = None,
    ) -> None:
        "Add uri. Page numbers start at 1."
        return _PdfEditing._add_uri(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            uri=uri,
            rect=rect,
            border=border,
        )

    def add_link(
        self, *, page_no: int, rect: Sequence[float], target_page_no: int
    ) -> None:
        "Add link. Page numbers start at 1."
        return _PdfEditing._add_link(
            self,
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            rect=rect,
            target_page_number=self._page_no_index(
                page_no=target_page_no, insertion=False
            ),
        )

    def remove_annotations(self, *, subtypes: Iterable[str] | str | None = None) -> int:
        'Remove annotations from every page: those of the given subtypes\n("/Link", "/Highlight", ...), or with None all of them except form\nfields. Naming "/Widget" removes form fields too. Returns the number\nremoved.'
        return _PdfEditing._remove_annotations(self, subtypes=subtypes)

    def remove_links(self) -> int:
        "Remove every link annotation. Returns the number removed."
        return _PdfEditing._remove_links(
            self,
        )

    def add_attachment(
        self,
        *,
        filename: str,
        data: str | bytes,
        description: str | None = None,
        mime_type: str | None = None,
    ) -> None:
        "Embed a file in the document. An attachment of the same name is\nreplaced."
        return _PdfEditing._add_attachment(
            self,
            filename=filename,
            data=data,
            description=description,
            mime_type=mime_type,
        )

    def remove_attachment(self, *, name: str) -> bool:
        "Remove an embedded file. Returns False if there was none."
        return _PdfEditing._remove_attachment(self, name=name)

    def add_js(self, *, javascript: str, name: str | None = None) -> None:
        "Add a document-level script, which a viewer runs when it opens\nthe document. docling-parse stores the script and never runs it."
        return _PdfEditing._add_js(self, javascript=javascript, name=name)

    def remove_javascript(self) -> int:
        "Remove the document-level scripts and scripted document actions.\nReturns the number of entries removed."
        return _PdfEditing._remove_javascript(
            self,
        )

    def encrypt(
        self,
        *,
        user_password: str,
        owner_password: str | None = None,
        algorithm: str = "AES-256",
        allow_print: bool | str = True,
        allow_modify: bool = True,
        allow_extract: bool = True,
        allow_annotate: bool = True,
        allow_fill_forms: bool = True,
        allow_accessibility: bool = True,
        allow_assemble: bool = True,
    ) -> None:
        'Protect everything written from now on with a password.\n\nThe output is encrypted with AES-256, the only password scheme PDF\n2.0 keeps; older, weaker algorithms are not offered. `user_password`\nis needed to open the document; pass "" together with an\n`owner_password` to let anyone open it. `owner_password` defaults to\nthe user password.\n\nThe `allow_*` arguments say what someone who opens the document with\nthe user password may do. `allow_print` is True, False, or "low" for\nlow-resolution printing only. Viewers honor these flags; they are\nnot access control.\n\nWithout this call the output is unencrypted, even when the source was\nencrypted.'
        return _PdfEditing._encrypt(
            self,
            user_password=user_password,
            owner_password=owner_password,
            algorithm=algorithm,
            allow_print=allow_print,
            allow_modify=allow_modify,
            allow_extract=allow_extract,
            allow_annotate=allow_annotate,
            allow_fill_forms=allow_fill_forms,
            allow_accessibility=allow_accessibility,
            allow_assemble=allow_assemble,
        )

    def set_output_options(
        self,
        *,
        compress_streams: bool = True,
        object_streams: str = "preserve",
        linearize: bool = False,
    ) -> None:
        'Choose how the file is written.\n\n`compress_streams` compresses streams that are stored uncompressed.\n`object_streams` is "preserve", "generate" (pack objects into\ncompressed object streams: smaller files) or "disable" (larger, and\nreadable by PDF 1.4 software). `linearize` orders the file so a\nviewer can show the first page before the rest has arrived.'
        return _PdfEditing._set_output_options(
            self,
            compress_streams=compress_streams,
            object_streams=object_streams,
            linearize=linearize,
        )

    def remove_encryption(self) -> None:
        "Undo `encrypt`: write unencrypted output again."
        return _PdfEditing._remove_encryption(
            self,
        )

    def to_bytes(self) -> bytes:
        "The document serialized as a complete PDF file."
        return _PdfEditing._to_bytes(
            self,
        )

    def write(self, *, target: str | os.PathLike[str] | BinaryIO) -> None:
        "Write the document to a path or a binary stream.\n\nA path is written through a temporary sibling file that replaces the\ntarget only once it is complete. A stream receives the whole file in\none `write` call and is not closed."
        return _PdfEditing._write(self, target=target)

    # Compatibility policy belongs here. Reader and writer adapters only bind
    # pypdf-shaped arguments and forward them to these named operations.
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
        self.open(
            source=stream,
            password=password.decode("latin-1")
            if isinstance(password, bytes)
            else password,
        )
        if strict:
            try:
                with _native_errors():
                    self._native.validate_strict()
            except PdfEditError as exc:
                self.close()
                raise PdfLoadError(
                    "<source>", PdfLoadErrorCategory.DAMAGED, str(exc)
                ) from exc

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
    ) -> None:
        if incremental:
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "incremental output requires the incremental serializer",
            )
        source = clone_from
        if source is None and fileobj is not None and fileobj != "":
            if isinstance(fileobj, (str, os.PathLike)):
                if os.path.isfile(fileobj) and os.path.getsize(fileobj):
                    source = fileobj
            elif hasattr(fileobj, "read"):
                position = fileobj.tell()
                if fileobj.seek(0, 2):
                    source = fileobj
                fileobj.seek(position)
            else:
                source = fileobj
        if source is None:
            self._native.create_empty()
        else:
            self._compat_open(stream=source, strict=strict)

    def _compat_get_fields(
        self,
        *,
        tree: Any,
        retval: Any,
        fileobj: Any,
        stack: Any,
        visited: Any,
        depth: int,
        traversal_state: Any,
    ) -> dict[str, Any]:
        if (
            tree is not None
            or visited is not None
            or depth
            or traversal_state is not None
        ):
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "subtree traversal requires the PDF object editing API",
            )
        fields = self.get_fields()
        if retval is not None:
            retval.update(fields)
            fields = retval
        if fileobj is not None:
            for name, field in fields.items():
                fileobj.write(f"{name}: {field['value']}\n")
        return fields

    def _compat_text_fields(self, *, full_qualified_name: bool) -> dict[str, Any]:
        fields = self.get_form_text_fields()
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

    def _compat_pages_showing_field(self, *, field: Any) -> list[PdfPage]:
        if isinstance(field, str):
            name = field
        else:
            name = field.get("name", field.get("/T", ""))
        return self.get_pages_showing_field(name=str(name))

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
        native, temporary = self._import_source(fileobj, None)
        try:
            count = native.page_count()
            if pages is None:
                selected = list(range(count))
            elif isinstance(pages, slice):
                selected = list(range(*pages.indices(count)))
            elif isinstance(pages, tuple):
                selected = list(range(*pages))
            elif hasattr(pages, "indices"):
                selected = list(range(*pages.indices(count)))
            else:
                selected = [
                    p.index if isinstance(p, PdfPage) else operator.index(p)
                    for p in pages
                ]
            selected = [p + count if p < 0 else p for p in selected]
            if any(p < 0 or p >= count for p in selected):
                raise IndexError("source page index out of range")
            insertion = (
                len(self.pages) if position is None else self._position(position)
            )
            # Reuse an already-open source and keep it alive until copying ends.
            source = temporary if temporary is not None else fileobj
            self.append(
                source=source,
                page_nos=[p + 1 for p in selected],
                before_page_no=insertion + 1,
                import_outline=import_outline,
            )
            if outline_item is not None and selected:
                self.add_outline_item(title=outline_item, page_no=insertion + 1)
        finally:
            if temporary is not None:
                temporary.close()

    def _compat_insert_page(
        self, *, page: PdfPage, index: int, excluded_keys: Iterable[str]
    ) -> PdfEditablePage:
        if tuple(excluded_keys):
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "excluding page keys requires the PDF object editing API",
            )
        return self.insert_page(page=page, before_page_no=self._position(index) + 1)

    def _compat_blank_page(
        self, *, width: float | None, height: float | None, index: int
    ) -> PdfEditablePage:
        if width is None or height is None:
            if not self.pages:
                raise ValueError("width and height are required for the first page")
            box = self.pages[min(index, len(self.pages) - 1)].mediabox
            width = box.width if width is None else width
            height = box.height if height is None else height
        return self.insert_blank_page(
            before_page_no=self._position(index) + 1, width=width, height=height
        )

    def _compat_add_annotation(
        self, *, page_number: int | PdfPage, annotation: dict[str, Any]
    ) -> dict[str, Any]:
        index = (
            page_number.index
            if isinstance(page_number, PdfPage)
            else self._page_index(page_number)
        )
        self.add_annotation(page_no=index + 1, annotation=annotation)
        return self.pages[index].annotations[-1]

    def _compat_named_destination(self, *, title: str, page_no: int, fit: Any) -> Any:
        if fit is not None and getattr(fit, "fit_type", "/Fit") != "/Fit":
            raise PdfUnsupportedError(
                PdfEditErrorCategory.UNSUPPORTED,
                "this destination fit mode is not supported",
            )
        return self.add_named_destination(title=title, page_no=page_no)

    def _compat_outline_item(
        self,
        *,
        title: str,
        page_number: Any,
        parent: Any,
        before: Any,
        color: Any,
        bold: bool,
        italic: bool,
        fit: Any,
        is_open: bool,
    ) -> int:
        page_no = (
            None
            if page_number is None
            else (
                page_number.index + 1
                if isinstance(page_number, PdfPage)
                else self._page_index(page_number) + 1
            )
        )
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
        item = self.add_outline_item(
            title=title,
            page_no=page_no,
            parent=parent,
            color=color,
            bold=bold,
            italic=italic,
            is_open=is_open,
            **options,
        )
        if before is not None:
            siblings = self.outline
            if parent is not None:
                pending = list(siblings)
                while pending:
                    entry = pending.pop()
                    if entry["id"] == parent:
                        siblings = entry["children"]
                        break
                    pending.extend(entry["children"])
            indices = [i for i, entry in enumerate(siblings) if entry["id"] == before]
            if not indices:
                self.remove_outline_item(item_id=item)
                raise ValueError("before must be a bookmark with the same parent")
            self.move_outline_item(item_id=item, parent=parent, index=indices[0])
        return item

    def _compat_encrypt(
        self,
        *,
        user_password: str,
        owner_password: str | None,
        use_128bit: bool,
        permissions_flag: int,
        algorithm: str | None,
    ) -> None:
        flags = int(permissions_flag)
        self.encrypt(
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

    def _compat_write(self, *, stream: Any) -> tuple[bool, Any]:
        if stream == "":
            raise ValueError("output path is empty")
        self.write(target=stream)
        if isinstance(stream, (str, os.PathLike)):
            with open(stream, "rb+") as result:
                result.seek(0, 2)
            return True, result
        if hasattr(stream, "flush"):
            stream.flush()
        return False, stream
