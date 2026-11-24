"""PdfEditor: the preferred docling-parse API for working with PDFs.

PdfReader and PdfWriter support migration from pypdf.

```python
from docling_parse.pdf_editor import PdfEditor

with PdfEditor(source="document.pdf") as editor:
    print(editor.metadata, editor.extract_text(page_no=1))
    editor.rotate_page(page_no=1, angle=90)
    editor.append(source="appendix.pdf")
    editor.write(target="out.pdf")
```
"""

from __future__ import annotations

import math
import operator
import os
from collections.abc import Iterable, Sequence
from enum import Enum
from typing import Any, BinaryIO, Literal

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
from docling_parse._pdf_document import (
    _ANY_GENERATION,
    _UNSET,
    STANDARD_FONTS,
    PdfBox,
    PdfEditablePage,
    PdfImage,
    PdfPage,
    PdfSource,
    _native_errors,
    _PageView,
    _pdf_images,
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

__all__ = [
    "STANDARD_FONTS",
    "PdfBox",
    "PdfEditor",
    "PdfEditorPage",
    "PdfExtractionMode",
    "PdfImage",
]


class PdfExtractionMode(Enum):
    """Select reading-order text or fixed-width layout text."""

    PLAIN = "plain"
    LAYOUT = "layout"


def _one_based_target(target: dict[str, Any]) -> dict[str, Any]:
    page = target.get("page")
    return {**target, "page": page + 1} if page is not None else target


def _one_based_outline(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {**_one_based_target(item), "children": _one_based_outline(item["children"])}
        for item in items
    ]


class PdfEditorPage(_PageView):
    """A read-only view of an Editor page, addressed by its one-based number."""

    __slots__ = ()

    def __repr__(self) -> str:
        return f"{type(self).__name__}(page_no={self._index + 1})"

    @property
    def page_no(self) -> int:
        """The one-based page number."""
        return self._checked_index() + 1

    @property
    def annotations(self) -> list[dict[str, Any]]:
        """Page annotations with one-based local link targets."""
        return [_one_based_target(item) for item in super().annotations]


class PdfEditor(_PdfEditing):
    """A PDF document that can be inspected, changed and written.

    `PdfEditor(source=source)` opens a document; `PdfEditor()` starts empty. An
    opened document is only read until the first change, at which point the
    editor continues on its own complete copy. The source is never modified
    unless `write` is pointed at its path.

    Writing creates a complete PDF and invalidates existing signatures.
    Call `encrypt()` before writing to encrypt the output.

    One editor may be shared between threads: calls are serialized.
    """

    _native_class = _native.pdf_editor

    def __init__(
        self,
        *,
        source: PdfSource | _PdfDocument | None = None,
        password: str | bytes | None = None,
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

    def page_count(self) -> int:
        """Return the number of pages in the current document."""
        with _native_errors():
            return self._native.page_count()

    def extract_text(
        self,
        *,
        page_no: int,
        extraction_mode: PdfExtractionMode = PdfExtractionMode.PLAIN,
    ) -> str:
        """Extract text from a one-based page, including unsaved edits.

        PLAIN returns reading-order lines. LAYOUT preserves column and line
        spacing in a fixed-width representation.
        """
        if not isinstance(extraction_mode, PdfExtractionMode):
            raise TypeError("extraction_mode must be a PdfExtractionMode")
        page = self._get_page(self._page_no_index(page_no=page_no))
        return page._extract_text(layout=extraction_mode is PdfExtractionMode.LAYOUT)

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
        self, *, source: PdfSource | _PdfDocument, password: str | bytes | None = None
    ) -> None:
        """Open a document, replacing the current state."""
        self._open(source, password)

    def _page_no_index(self, *, page_no: int, insertion: bool = False) -> int:
        if isinstance(page_no, bool):
            raise TypeError("page_no must be an integer, starting at 1")
        value = operator.index(page_no)
        maximum = self.page_count() + int(insertion)
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

    @staticmethod
    def _page_view(page: _PageView) -> PdfEditorPage:
        return PdfEditorPage(page._owner, page._index, page._generation)

    def get_page(self, *, page_no: int) -> PdfEditorPage:
        """Inspect the page with the given one-based number."""
        return self._page_view(self._get_page(self._page_no_index(page_no=page_no)))

    def get_page_no(self, *, page: _PageView) -> int:
        """The one-based number of a page belonging to this document."""
        return self._get_page_number(page) + 1

    def get_pages_showing_field(self, *, name: str) -> list[PdfEditorPage]:
        """Inspect pages showing the field with the given fully qualified name."""
        return [self._page_view(page) for page in self._get_pages_showing_field(name)]

    @property
    def outline(self) -> list[dict[str, Any]]:
        """Bookmarks with one-based destination page numbers."""
        return _one_based_outline(self._outline())

    @property
    def named_destinations(self) -> dict[str, dict[str, Any]]:
        """Named destinations with one-based page numbers."""
        return {
            name: _one_based_target(target)
            for name, target in super().named_destinations.items()
        }

    @property
    def open_destination(self) -> dict[str, Any] | None:
        """The document's open destination with a one-based page number."""
        target = super().open_destination
        return None if target is None else _one_based_target(target)

    def has_form(self) -> bool:
        """Whether the document contains an AcroForm dictionary."""
        return self._form_flags()[0]

    def has_form_on_page(self, *, page_no: int) -> bool:
        """Whether an AcroForm field has a widget on the given one-based page."""
        index = self._page_no_index(page_no=page_no)
        with _native_errors():
            return self._native.has_form_on_page(index)

    def has_xfa(self) -> bool:
        """Whether the document's AcroForm contains an XFA definition."""
        return self._form_flags()[1]

    def get_fields(self) -> dict[str, dict[str, Any]]:
        """Form fields by fully qualified name, with distinct one-based pages
        and a bounding box for each widget.
        """
        return {
            name: {
                **field,
                "pages": [index + 1 for index in field["pages"]],
                "bboxes": [PdfBox(*rect) for rect in field["rects"]],
            }
            for name, field in self._get_fields().items()
        }

    def _editable_page(self, page_no: int) -> PdfEditablePage:
        page = self._get_page(self._page_no_index(page_no=page_no))
        assert isinstance(page, PdfEditablePage)
        return page

    def rotate_page(self, *, page_no: int, angle: int) -> None:
        """Add clockwise display rotation in multiples of 90 degrees."""
        self._editable_page(page_no).rotate(angle)

    def set_page_rotation(self, *, page_no: int, angle: int) -> None:
        """Set clockwise display rotation in multiples of 90 degrees."""
        self._editable_page(page_no).rotation = angle

    def set_page_box(
        self,
        *,
        page_no: int,
        box: Literal["media", "crop", "bleed", "trim", "art"],
        bounds: Sequence[float],
    ) -> None:
        """Set a page boundary in left, bottom, right, top order."""
        self._editable_page(page_no)._set_box(box, bounds)

    def scale_page(self, *, page_no: int, sx: float, sy: float) -> None:
        """Scale page content, annotations and boxes by the given factors."""
        self._editable_page(page_no).scale(sx, sy)

    def scale_page_to(self, *, page_no: int, width: float, height: float) -> None:
        """Scale a page to the given media box size in points."""
        self._editable_page(page_no).scale_to(width, height)

    def transform_page(self, *, page_no: int, ctm: Sequence[float]) -> None:
        """Apply an affine transform to the page content and annotations."""
        self._editable_page(page_no).add_transformation(ctm)

    def transfer_page_rotation_to_content(self, *, page_no: int) -> None:
        """Apply display rotation to the content, geometry and boxes."""
        self._editable_page(page_no).transfer_rotation_to_content()

    def overlay_page(
        self,
        *,
        page_no: int,
        source_page: _PageView,
        over: bool = True,
        fit: bool = False,
        ctm: Sequence[float] | None = None,
    ) -> None:
        """Draw another page as vector content, above or below this page.

        Use `fit` to center and scale the source or `ctm` to place it.
        """
        if fit and ctm is not None:
            raise ValueError("choose either fit or ctm for overlay placement")
        matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0) if ctm is None else ctm
        self._editable_page(page_no)._overlay(source_page, over, fit, matrix)

    def draw_text(
        self,
        *,
        page_no: int,
        text: str,
        x: float,
        y: float,
        size: float = 12,
        font: str = "Helvetica",
        color: Sequence[float] | str = (0, 0, 0),
    ) -> None:
        """Draw text starting at the given baseline in PDF coordinates."""
        self._editable_page(page_no)._draw_text(
            text, x, y, size=size, font=font, color=color
        )

    def get_form_text_fields(self) -> dict[str, str]:
        "The value of every text field, by fully qualified name."
        return self._get_form_text_fields()

    def get_attachment(self, *, name: str) -> bytes:
        "The content of the embedded file registered under `name`."
        return self._get_attachment(name=name)

    def append(
        self,
        *,
        source: Any,
        page_nos: Iterable[int] | None = None,
        before_page_no: int | None = None,
        password: str | bytes | None = None,
        import_outline: bool = True,
    ) -> None:
        """Copy selected pages into this document.

        `page_nos` uses one-based numbers in the source. Omit it to copy
        every page. An empty selection leaves the document alone.
        `before_page_no` selects an insertion point in this document.
        """
        return self._append(
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
        password: str | bytes | None = None,
        import_outline: bool = True,
    ) -> None:
        """Insert pages before the specified one-based page number."""
        self.append(
            source=fileobj,
            before_page_no=before_page_no,
            page_nos=page_nos,
            password=password,
            import_outline=import_outline,
        )

    def add_page(self, *, page: _PageView) -> PdfEditorPage:
        "Append a copy of a page of any reader, writer or editor."
        return self._page_view(self._add_page(page))

    def insert_page(self, *, page: _PageView, before_page_no: int = 1) -> PdfEditorPage:
        """Copy a page before the given one-based position.

        Returns a view of the inserted page. A stale source view raises
        `PdfStaleHandleError`.
        """
        return self._page_view(
            self._insert_page(
                page, self._page_no_index(page_no=before_page_no, insertion=True)
            )
        )

    def add_blank_page(self, *, width: float, height: float) -> PdfEditorPage:
        "Add an empty page at the end. Sizes are in points (1/72 inch)."
        return self._page_view(self._add_blank_page(width, height))

    def insert_blank_page(
        self, *, before_page_no: int, width: float, height: float
    ) -> PdfEditorPage:
        """Insert a blank page before the given position and return its view.

        Supply finite, positive dimensions in PDF points.
        """
        return self._page_view(
            self._insert_blank_page(
                self._page_no_index(page_no=before_page_no, insertion=True),
                width,
                height,
            )
        )

    def add_image(
        self,
        *,
        image: _pdf_images.ImageSource,
        before_page_no: int | None = None,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> list[PdfEditorPage]:
        """Create PDF pages from an image and return their page views.

        Multi-frame images produce one page per frame. Omit
        `before_page_no` to append the pages.
        """
        pages = self._add_image(
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
        return [self._page_view(page) for page in pages]

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
    ) -> PdfEditorPage:
        """Copy a page within this document and return the new page view."""
        page = self._duplicate_page(
            index=self._page_no_index(page_no=page_no, insertion=False),
            to=(
                before_page_no
                if before_page_no is None or before_page_no is _UNSET
                else self._page_no_index(page_no=before_page_no, insertion=True)
            ),
        )
        return self._page_view(page)

    def remove_page(self, *, page_no: int, clean: bool = False) -> None:
        """Delete the specified page and detach its document references.

        Existing page views become stale.
        """
        return self._remove_page(
            index=self._page_no_index(page_no=page_no, insertion=False),
            clean=clean,
        )

    def move_page(self, *, page_no: int, to_page_no: int) -> None:
        """Move a page to its final one-based position in the new order."""
        return self._move_page(
            from_index=self._page_no_index(page_no=page_no, insertion=False),
            to_index=self._page_no_index(page_no=to_page_no, insertion=False),
        )

    def add_metadata(self, *, infos: dict[str, str | None]) -> None:
        """Update the document information dictionary.

        Keys accept an optional leading slash. A value of `None` removes
        the entry.
        """
        return self._add_metadata(infos=infos)

    def set_xmp_metadata(self, *, packet: str | bytes | None) -> None:
        "Replace the XMP metadata packet, or remove it with None."
        return self._set_xmp_metadata(packet=packet)

    def set_page_layout(self, *, layout: str | None) -> None:
        """Set how a viewer arranges pages when the document opens.

        Supported values include `/SinglePage`, `/OneColumn`, `/TwoColumnLeft`,
        `/TwoColumnRight`, `/TwoPageLeft`, and `/TwoPageRight`. Use `None`
        to clear the setting.
        """
        return self._set_page_layout(layout=layout)

    def set_page_mode(self, *, mode: str | None) -> None:
        """Set which panel or mode a viewer opens with the document.

        Values include `/UseNone`, `/UseOutlines`, `/UseThumbs`, `/FullScreen`,
        `/UseOC`, and `/UseAttachments`. Use `None` to clear the setting.
        """
        return self._set_page_mode(mode=mode)

    def set_viewer_preferences(self, *, preferences: dict[str, Any] | None) -> None:
        """Replace viewer preferences such as toolbar visibility or direction.

        Use `None` to remove the preferences dictionary.
        """
        return self._set_viewer_preferences(preferences=preferences)

    def set_open_destination(self, *, page_no: int | None) -> None:
        """Choose the page displayed on open, or clear it with `None`."""
        return self._set_open_destination(
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
        """Set the label style, prefix, and starting number for a page range."""
        return self._set_page_label(
            page_index_from=self._page_no_index(page_no=first_page_no, insertion=False),
            page_index_to=self._page_no_index(page_no=last_page_no, insertion=False),
            style=style,
            prefix=prefix,
            start=start,
        )

    def add_named_destination(self, *, title: str, page_no: int) -> None:
        """Register a named destination for a page in this document."""
        return self._add_named_destination(
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
        """Add a bookmark and return its ID for later edits.

        Use `parent` to add it under an existing bookmark. For a URL
        destination, pass `page_no=None` and supply `url`.
        """
        return self._add_outline_item(
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
        """Change the supplied attributes on an existing bookmark.

        A removed or unknown `item_id` raises `PdfEditError`.
        """
        return self._update_outline_item(
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
        return self._remove_outline_item(item_id=item_id)

    def move_outline_item(
        self, *, item_id: int, parent: int | None = None, index: int | None = None
    ) -> None:
        """Move a bookmark and its children under a new parent.

        `parent=None` moves it to the top level. `index` is the zero-based
        position among its new siblings and defaults to the end.
        """
        return self._move_outline_item(item_id=item_id, parent=parent, index=index)

    def clear_outline(self) -> None:
        "Remove every bookmark."
        return self._clear_outline()

    def add_table_of_contents(
        self,
        *,
        before_page_no: int = 1,
        title: str = "Contents",
        font: str = "Helvetica",
        size: float = 11,
        max_depth: int | None = None,
        page_size: str | Sequence[float] | None = None,
    ) -> list[PdfEditorPage]:
        """Insert linked contents pages built from the outline.

        Returns the inserted page views. Text must be encodable by the
        selected standard font.
        """
        pages = self._add_table_of_contents(
            index=self._page_no_index(page_no=before_page_no, insertion=True),
            title=title,
            font=font,
            size=size,
            max_depth=max_depth,
            page_size=page_size,
        )
        return [self._page_view(page) for page in pages]

    def update_form_field_values(
        self, *, fields: dict[str, str | bool], flatten: bool = False
    ) -> int:
        """Fill fields by fully qualified or unambiguous partial name.

        Text and choice fields take a string. Checkboxes take a boolean or
        state name. Radio buttons take a state name or `/Off`.

        With `flatten=True`, paint the form widgets into page content and
        keep other annotations. Use an empty mapping to flatten current
        values. Return the number of supplied fields updated.
        """
        return self._update_form_field_values(fields=fields, flatten=flatten)

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
        """Create a text field with one widget at `rect` on the given page."""
        return self._add_text_field(
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
        """Create a checkbox and set its initial checked state."""
        return self._add_checkbox(
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
        """Create a single-selection choice field from `options`.

        With `editable=False`, the selected value must be one of the
        listed choices.
        """
        return self._add_choice_field(
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
        """Create a radio field with a rectangle for each named state."""
        return self._add_radio_group(
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
        """Create an empty signature field for signing in a PDF viewer."""
        return self._add_signature_field(
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
        """Change the supplied attributes on a form field.

        `new_name` changes the last component of its name. Set `rect` to move
        or resize a single-widget field, and `tooltip=None` to clear the
        tooltip. `options` replaces the choices on a choice field.
        """
        return self._update_form_field(
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
        return self._remove_form_field(name=name)

    def set_need_appearances_writer(self, *, state: bool = True) -> None:
        """Set whether the form asks a viewer to build its field appearances."""
        return self._set_need_appearances_writer(state=state)

    def flatten_annotations(self) -> None:
        """Paint visible annotations, including form widgets, into page content."""
        return self._flatten_annotations()

    def add_annotation(self, *, page_no: int, annotation: dict[str, Any]) -> None:
        """Add an annotation from a dictionary of PDF entries."""
        self._add_annotation(
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
        """Add a link to an external URI at the rectangle on the page."""
        return self._add_uri(
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            uri=uri,
            rect=rect,
            border=border,
        )

    def add_link(
        self, *, page_no: int, rect: Sequence[float], target_page_no: int
    ) -> None:
        """Add a link at `rect` that jumps to another page in this document."""
        return self._add_link(
            page_number=self._page_no_index(page_no=page_no, insertion=False),
            rect=rect,
            target_page_number=self._page_no_index(
                page_no=target_page_no, insertion=False
            ),
        )

    def remove_annotations(self, *, subtypes: Iterable[str] | str | None = None) -> int:
        """Remove annotations of the selected subtypes and return the count.

        With `subtypes=None`, form widgets are preserved. Include `/Widget`
        to remove widgets and prune fields that lose their last widget.
        """
        return self._remove_annotations(subtypes=subtypes)

    def remove_links(self) -> int:
        "Remove every link annotation. Returns the number removed."
        return self._remove_links()

    def add_attachment(
        self,
        *,
        filename: str,
        data: str | bytes,
        description: str | None = None,
        mime_type: str | None = None,
    ) -> None:
        """Embed a file, replacing any attachment registered under its name."""
        return self._add_attachment(
            filename=filename,
            data=data,
            description=description,
            mime_type=mime_type,
        )

    def remove_attachment(self, *, name: str) -> bool:
        "Remove an embedded file. Returns False if there was none."
        return self._remove_attachment(name=name)

    def add_js(self, *, javascript: str, name: str | None = None) -> None:
        """Store a script that a PDF viewer can run when opening the document."""
        return self._add_js(javascript=javascript, name=name)

    def remove_javascript(self) -> int:
        """Remove document scripts and scripted actions and return the count."""
        return self._remove_javascript()

    def encrypt(
        self,
        *,
        user_password: str | bytes,
        owner_password: str | bytes | None = None,
        algorithm: str = "AES-256",
        allow_print: bool | str = True,
        allow_modify: bool = True,
        allow_extract: bool = True,
        allow_annotate: bool = True,
        allow_fill_forms: bool = True,
        allow_accessibility: bool = True,
        allow_assemble: bool = True,
    ) -> None:
        """Encrypt output with the selected algorithm, defaulting to AES-256.

        Text passwords use the revision's encoding. Bytes are passed through.
        An omitted owner password defaults to the user password.
        Pass an empty user password and a nonempty owner password to allow
        opening the file without credentials.
        """
        return self._encrypt(
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
        """Set stream compression and fast-web-view ordering for output.

        `object_streams` accepts `"preserve"`, `"generate"`, or `"disable"`.
        Use `linearize=True` to let a viewer load the first page early.
        Each call supplies the complete set of output options.
        """
        return self._set_output_options(
            compress_streams=compress_streams,
            object_streams=object_streams,
            linearize=linearize,
        )

    def remove_encryption(self) -> None:
        "Undo `encrypt`: write unencrypted output again."
        return self._remove_encryption()

    def to_bytes(self) -> bytes:
        "The document serialized as a complete PDF file."
        return self._to_bytes()

    def write(self, *, target: str | os.PathLike[str] | BinaryIO) -> None:
        """Write a complete PDF to an atomically replaced path or a binary stream.

        A caller-owned stream stays open. A stalled write raises
        `PdfWriteError` and can leave partial data in the stream.
        """
        self._write(target=target)
