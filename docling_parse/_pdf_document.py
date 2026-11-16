"""Shared Python half of PdfReader, PdfWriter and PdfEditor.

All PDF work happens in the native `docling_editor` class; the three public
classes bind three native types that publish different subsets of it. This
module holds the Python conveniences they share: opening from a path, bytes or
a stream, the `pages` sequence, page handles and error translation.

Nothing here imports docling-core, pydantic or PIL, and nothing imports
`docling_parse.pdf_parser`.
"""

from __future__ import annotations

import math
import os
from collections.abc import Iterable, Iterator, Sequence
from contextlib import contextmanager
from typing import Any, BinaryIO, NamedTuple, Union, overload

from docling_parse import (
    _pdf_images,
    pdf_parsers as _native,  # type: ignore[attr-defined]
)
from docling_parse.pdf_errors import (
    PdfEditError,
    PdfEditErrorCategory,
    PdfLoadError,
    PdfLoadErrorCategory,
    PdfPasswordError,
    PdfStaleHandleError,
    PdfUnsupportedError,
    PdfWriteError,
)

PdfSource = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview, BinaryIO]

# A sentinel for "not given", where None is a value (it removes something).
_UNSET: Any = object()

# The fonts text can be drawn in: the standard 14 fonts that hold Latin text,
# which every PDF viewer provides without embedding.
STANDARD_FONTS = (
    "Helvetica",
    "Helvetica-Bold",
    "Helvetica-Oblique",
    "Helvetica-BoldOblique",
    "Times-Roman",
    "Times-Bold",
    "Times-Italic",
    "Times-BoldItalic",
    "Courier",
    "Courier-Bold",
    "Courier-Oblique",
    "Courier-BoldOblique",
)


def _color(value: Sequence[float] | str | None) -> list[float] | None:
    """An RGB color as three numbers in 0..1, from a tuple or "#rrggbb"."""
    if value is None:
        return None
    if isinstance(value, str):
        text = value.lstrip("#")
        if len(text) != 6:
            raise ValueError(f"a color is (r, g, b) or '#rrggbb', not {value!r}")
        return [int(text[i : i + 2], 16) / 255 for i in (0, 2, 4)]
    components = [float(component) for component in value]
    if len(components) != 3 or not all(0 <= c <= 1 for c in components):
        raise ValueError("a color is three numbers between 0 and 1")
    return components


def _rect(value: Sequence[float]) -> list[float]:
    numbers = [float(v) for v in value]
    if len(numbers) != 4:
        raise ValueError("a rectangle is (left, bottom, right, top)")
    return numbers


def _win_ansi(text: str) -> str:
    """`text` with every character the standard fonts cannot show replaced
    by "?". cp1252 is WinAnsiEncoding up to five unassigned codes."""
    return "".join(
        character if character.encode("cp1252", errors="ignore") else "?"
        for character in text
    )


_ERROR_TYPES: dict[PdfEditErrorCategory, type[PdfEditError]] = {
    PdfEditErrorCategory.STALE_HANDLE: PdfStaleHandleError,
    PdfEditErrorCategory.WRITE: PdfWriteError,
    PdfEditErrorCategory.UNSUPPORTED: PdfUnsupportedError,
}


@contextmanager
def _native_errors() -> Iterator[None]:
    """Turn the native (category, message) error into its typed exception."""
    try:
        yield
    except _native.pdf_edit_error as exc:
        raw_category, detail = exc.args
        try:
            category = PdfEditErrorCategory(raw_category)
        except ValueError:
            category = PdfEditErrorCategory.UNKNOWN
        raise _ERROR_TYPES.get(category, PdfEditError)(category, detail) from None


def _pdf_syntax(value: Any) -> str:
    """Write a Python value as a PDF object, for the native calls that take a
    dictionary: annotations and viewer preferences.

    A `str` starting with "/" is a PDF name, any other `str` a text string.
    `bool`, `int`, `float`, `None`, lists/tuples and dicts map to their PDF
    counterparts; dict keys are names, with or without their slash.
    """
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("a PDF number must be finite")
        return f"{value:.6f}".rstrip("0").rstrip(".") or "0"
    if isinstance(value, str):
        if value.startswith("/"):
            return _pdf_name(value)
        # Hexadecimal strings need no escaping. Text outside ASCII is written
        # as UTF-16BE with its byte order mark (ISO 32000-1, 7.9.2.2).
        try:
            raw = value.encode("ascii")
        except UnicodeEncodeError:
            raw = b"\xfe\xff" + value.encode("utf-16-be")
        return f"<{raw.hex()}>"
    if isinstance(value, (list, tuple)):
        return "[" + " ".join(_pdf_syntax(item) for item in value) + "]"
    if isinstance(value, dict):
        items = " ".join(
            f"{_pdf_name(str(key))} {_pdf_syntax(item)}" for key, item in value.items()
        )
        return f"<< {items} >>"
    raise TypeError(f"cannot write {type(value).__name__} as a PDF object")


def _pdf_name(name: str) -> str:
    """A PDF name with its slash; bytes outside the regular characters are
    written as #xx (ISO 32000-1, 7.3.5)."""
    body = name[1:] if name.startswith("/") else name
    if not body:
        raise ValueError("a PDF name must not be empty")
    encoded = "".join(
        chr(byte)
        if 0x21 <= byte <= 0x7E and chr(byte) not in "#/()<>[]{}%"
        else f"#{byte:02x}"
        for byte in body.encode("utf-8")
    )
    return "/" + encoded


# /P of an encryption dictionary: bit positions are 1-based (ISO 32000-1,
# table 22).
_PERMISSION_BITS = {
    "print": 3,
    "modify": 4,
    "extract": 5,
    "annotate": 6,
    "fill_forms": 9,
    "accessibility": 10,
    "assemble": 11,
    "print_high_quality": 12,
}


class PdfBox(NamedTuple):
    """A page boundary in default user space units, normalized so that
    `left < right` and `bottom < top`."""

    left: float
    bottom: float
    right: float
    top: float

    @property
    def width(self) -> float:
        return self.right - self.left

    @property
    def height(self) -> float:
        return self.top - self.bottom


_IMAGE_EXTENSIONS = {"jpeg": ".jpg", "jp2": ".jp2", "jbig2": ".jb2"}


class PdfImage(NamedTuple):
    """An image drawn by a page, with the bytes to save it.

    `format` says what `data` is: "jpeg" and "jp2" are complete image files,
    "jbig2" is the embedded JBIG2 stream, and "raw" is uncompressed samples in
    `mode` ("1", "L", "RGB" or "CMYK"), `width` x `height` pixels. `data` is
    empty when the image could not be decoded.
    """

    name: str
    """The resource name the page draws the image under."""
    bbox: PdfBox
    """Where the image sits on the page."""
    width: int
    height: int
    format: str
    mode: str
    data: bytes

    @property
    def extension(self) -> str:
        """A file extension for `data`, with its dot: ".jpg", ".jp2", ..."""
        return _IMAGE_EXTENSIONS.get(self.format, ".bin")

    def save(self, path: str | os.PathLike[str]) -> None:
        """Write `data` to `path` as it is."""
        with open(path, "wb") as stream:
            stream.write(self.data)

    def to_pil(self) -> Any:
        """The image as a PIL image. Needs Pillow, which is imported here and
        nowhere else in this module."""
        from io import BytesIO

        from PIL import Image

        if self.format in ("jpeg", "jp2"):
            return Image.open(BytesIO(self.data))
        if self.format == "raw":
            return Image.frombytes(self.mode, (self.width, self.height), self.data)
        raise PdfUnsupportedError(
            PdfEditErrorCategory.UNSUPPORTED,
            f"cannot build a PIL image from {self.format} data",
        )


class PdfPage:
    """A page of a document, by position. Nothing is decoded until asked for.

    The handle belongs to the page list it was taken from: once pages are
    inserted, removed or moved, using it raises `PdfStaleHandleError` instead
    of silently addressing whichever page now has that index.
    """

    __slots__ = ("_generation", "_index", "_owner")

    def __init__(self, owner: _PdfDocument, index: int, generation: int):
        self._owner = owner
        self._index = index
        self._generation = generation

    def __repr__(self) -> str:
        return f"{type(self).__name__}(index={self._index})"

    @property
    def index(self) -> int:
        """Zero-based position of the page in its document."""
        return self._index

    @property
    def page_number(self) -> int:
        """Same as `index`: the zero-based position, under pypdf's name."""
        return self._index

    def _info(self) -> dict[str, Any]:
        with _native_errors():
            return self._owner._native.page_info(self._index, self._generation)

    @property
    def mediabox(self) -> PdfBox:
        return PdfBox(*self._info()["media_box"])

    @property
    def cropbox(self) -> PdfBox:
        return PdfBox(*self._info()["crop_box"])

    @property
    def bleedbox(self) -> PdfBox:
        return PdfBox(*self._info()["bleed_box"])

    @property
    def trimbox(self) -> PdfBox:
        return PdfBox(*self._info()["trim_box"])

    @property
    def artbox(self) -> PdfBox:
        return PdfBox(*self._info()["art_box"])

    @property
    def rotation(self) -> int:
        """Clockwise display rotation in degrees: 0, 90, 180 or 270."""
        return self._info()["rotation"]

    @property
    def user_unit(self) -> float:
        """Size of one user space unit in 1/72 inch; 1.0 unless the page says otherwise."""
        return self._info()["user_unit"]

    def extract_text(self, extraction_mode: str = "plain") -> str:
        """The text of the page.

        "plain" returns the text lines in the parser's reading order, joined
        by newlines. "layout" places the text on a character grid, so
        columns, indentation and vertical spacing of the page survive in the
        string; it suits tables and forms read as fixed-width text.
        """
        if extraction_mode not in ("plain", "layout"):
            raise ValueError('extraction_mode is "plain" or "layout"')
        with _native_errors():
            return self._owner._native.extract_text(
                self._index, self._generation, layout=extraction_mode == "layout"
            )

    @property
    def annotations(self) -> list[dict[str, Any]]:
        """The page's annotations, one dictionary each, keyed by PDF name
        ("/Subtype", "/Rect", "/Contents", ...). A link into the document
        also has "page", the zero-based index of its target."""
        with _native_errors():
            return self._owner._native.page_annotations(self._index, self._generation)

    @property
    def images(self) -> list[PdfImage]:
        """The images the page draws, in drawing order. Decodes the page."""
        with _native_errors():
            items = self._owner._native.page_images(self._index, self._generation)
        return [
            PdfImage(
                name=item["name"],
                bbox=PdfBox(*item["bbox"]),
                width=item["width"],
                height=item["height"],
                format=item["format"],
                mode=item["mode"],
                data=item["data"],
            )
            for item in items
        ]


class PdfEditablePage(PdfPage):
    """A page of a writer or editor: `PdfPage` plus in-place changes."""

    __slots__ = ()

    def rotate(self, angle: int) -> PdfEditablePage:
        """Rotate clockwise by `angle` degrees (a multiple of 90), on top of
        the page's current rotation."""
        with _native_errors():
            self._owner._native.rotate_page(
                self._index, angle, relative=True, generation=self._generation
            )
        return self

    def merge_page(
        self, page: PdfPage, over: bool = True, *, fit: bool = False
    ) -> PdfEditablePage:
        """Draw another page on this one as vector content: a stamp or
        overlay with `over=True`, a watermark or underlay with `over=False`.

        By default the two pages are superimposed at their own coordinates,
        unscaled. With `fit=True` the other page is centered on this one and
        scaled to fill it, upright with respect to this page's rotation.

        `page` may belong to any reader, writer or editor, including this
        one. Its text and graphics are drawn; its annotations and form
        fields are not copied.
        """
        return self._overlay(page, over, fit, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0))

    def _overlay(
        self, page: PdfPage, over: bool, fit: bool, ctm: Sequence[float]
    ) -> PdfEditablePage:
        matrix = [float(value) for value in ctm]
        if len(matrix) != 6:
            raise ValueError("a transformation matrix has six entries")
        with _native_errors():
            self._owner._native.overlay_page(
                self._index,
                page._owner._native,
                page._index,
                under=not over,
                fit=fit,
                matrix=matrix,
                generation=self._generation,
                other_generation=page._generation,
            )
        return self

    def merge_transformed_page(
        self, page: PdfPage, ctm: Sequence[float], over: bool = True
    ) -> PdfEditablePage:
        """`merge_page`, with the other page transformed by the matrix
        `(a, b, c, d, e, f)` before it is drawn."""
        return self._overlay(page, over, False, ctm)

    def merge_scaled_page(
        self, page: PdfPage, scale: float, over: bool = True
    ) -> PdfEditablePage:
        """`merge_page`, with the other page scaled about the origin."""
        return self._overlay(page, over, False, (scale, 0.0, 0.0, scale, 0.0, 0.0))

    def merge_translated_page(
        self, page: PdfPage, tx: float, ty: float, over: bool = True
    ) -> PdfEditablePage:
        """`merge_page`, with the other page moved by `(tx, ty)`."""
        return self._overlay(page, over, False, (1.0, 0.0, 0.0, 1.0, tx, ty))

    def merge_rotated_page(
        self, page: PdfPage, rotation: float, over: bool = True
    ) -> PdfEditablePage:
        """`merge_page`, with the other page turned counter-clockwise by
        `rotation` degrees about the origin."""
        angle = math.radians(rotation)
        cos, sin = math.cos(angle), math.sin(angle)
        return self._overlay(page, over, False, (cos, sin, -sin, cos, 0.0, 0.0))

    def draw_text(
        self,
        text: str,
        x: float,
        y: float,
        *,
        size: float = 12,
        font: str = "Helvetica",
        color: Sequence[float] | str = (0, 0, 0),
    ) -> PdfEditablePage:
        """Draw `text` on the page, its first baseline starting at (x, y).

        `font` is one of `STANDARD_FONTS`, which every viewer has, so nothing
        is embedded. Those fonts hold Latin text (WinAnsiEncoding); other
        characters raise `PdfUnsupportedError`. A newline starts a new line
        1.2 x `size` lower. `color` is (r, g, b) in 0..1, or "#rrggbb".
        """
        with _native_errors():
            self._owner._native.draw_text(
                self._index,
                text,
                float(x),
                float(y),
                size=float(size),
                font=font,
                color=_color(color),
                generation=self._generation,
            )
        return self

    def transfer_rotation_to_content(self) -> PdfEditablePage:
        """Turn the page's rotation into a rotation of its content, boxes and
        annotations. The page looks the same and `rotation` becomes 0."""
        with _native_errors():
            self._owner._native.flatten_page_rotation(self._index, self._generation)
        return self

    def add_transformation(self, ctm: Sequence[float]) -> PdfEditablePage:
        """Apply the matrix `(a, b, c, d, e, f)` to the page's content and
        annotations. The page boundaries are not changed."""
        matrix = [float(value) for value in ctm]
        if len(matrix) != 6:
            raise ValueError("a transformation matrix has six entries")
        with _native_errors():
            self._owner._native.transform_page(self._index, matrix, self._generation)
        return self

    def scale(self, sx: float, sy: float) -> PdfEditablePage:
        """Scale the page by `sx` horizontally and `sy` vertically: its
        content, its annotations and all of its boundaries."""
        with _native_errors():
            self._owner._native.scale_page(
                self._index, float(sx), float(sy), self._generation
            )
        return self

    def scale_by(self, factor: float) -> PdfEditablePage:
        """Scale the page by the same factor in both directions."""
        return self.scale(factor, factor)

    def scale_to(self, width: float, height: float) -> PdfEditablePage:
        """Scale the page so that its media box gets the given size."""
        box = self.mediabox
        if not (box.width > 0 and box.height > 0):
            raise PdfEditError(
                PdfEditErrorCategory.INVALID_EDIT, "the page has an empty media box"
            )
        return self.scale(width / box.width, height / box.height)

    def _set_box(self, name: str, box: Sequence[float]) -> None:
        left, bottom, right, top = (float(value) for value in box)
        with _native_errors():
            self._owner._native.set_page_box(
                self._index, name, [left, bottom, right, top], self._generation
            )

    # The setters repeat the getters because a property is one object: a
    # subclass cannot add only the setter to an inherited read-only property.

    @property
    def rotation(self) -> int:
        return self._info()["rotation"]

    @rotation.setter
    def rotation(self, angle: int) -> None:
        with _native_errors():
            self._owner._native.rotate_page(
                self._index, angle, relative=False, generation=self._generation
            )

    @property
    def mediabox(self) -> PdfBox:
        return PdfBox(*self._info()["media_box"])

    @mediabox.setter
    def mediabox(self, box: Sequence[float]) -> None:
        self._set_box("media", box)

    @property
    def cropbox(self) -> PdfBox:
        return PdfBox(*self._info()["crop_box"])

    @cropbox.setter
    def cropbox(self, box: Sequence[float]) -> None:
        self._set_box("crop", box)

    @property
    def bleedbox(self) -> PdfBox:
        return PdfBox(*self._info()["bleed_box"])

    @bleedbox.setter
    def bleedbox(self, box: Sequence[float]) -> None:
        self._set_box("bleed", box)

    @property
    def trimbox(self) -> PdfBox:
        return PdfBox(*self._info()["trim_box"])

    @trimbox.setter
    def trimbox(self, box: Sequence[float]) -> None:
        self._set_box("trim", box)

    @property
    def artbox(self) -> PdfBox:
        return PdfBox(*self._info()["art_box"])

    @artbox.setter
    def artbox(self, box: Sequence[float]) -> None:
        self._set_box("art", box)


class PdfPages(Sequence[PdfPage]):
    """The pages of a document: zero-based, with negative indices and slices.

    On a writer or editor, `del pages[i]` removes a page.
    """

    def __init__(self, owner: _PdfDocument):
        self._owner = owner

    def __len__(self) -> int:
        with _native_errors():
            return self._owner._native.page_count()

    def _resolve(self, index: int, count: int) -> int:
        resolved = index + count if index < 0 else index
        if not 0 <= resolved < count:
            raise IndexError(f"page index {index} out of range for {count} pages")
        return resolved

    @overload
    def __getitem__(self, index: int) -> PdfPage: ...

    @overload
    def __getitem__(self, index: slice) -> list[PdfPage]: ...

    def __getitem__(self, index: int | slice) -> PdfPage | list[PdfPage]:
        # Generation first: if the page list changes between the two reads,
        # the handles made here are stale rather than wrong.
        generation = self._owner._native.generation()
        count = len(self)
        page_class = self._owner._page_class

        if isinstance(index, slice):
            return [
                page_class(self._owner, i, generation)
                for i in range(*index.indices(count))
            ]
        return page_class(self._owner, self._resolve(index, count), generation)

    def __delitem__(self, index: int) -> None:
        if not isinstance(self._owner, _PdfEditing):
            raise TypeError(f"{type(self._owner).__name__} is read-only")
        self._owner.remove_page(index)


class _PdfDocument:
    """Opening, closing and inspection: common to reader, writer and editor."""

    _native_class: type
    _page_class: type[PdfPage] = PdfPage

    def __init__(self, loglevel: str = "fatal"):
        self._native = self._native_class(loglevel)

    def _open(self, source: Any, password: str | None) -> None:
        native = self._native

        if isinstance(source, _PdfDocument):
            description = repr(source)
            with _native_errors():
                opened = native.open_source(source._native)
        elif isinstance(source, (str, os.PathLike)):
            description = os.fspath(source)
            opened = native.open_file(description, password)
        elif isinstance(source, (bytes, bytearray, memoryview)):
            description = "<bytes>"
            opened = native.open_bytes(bytes(source), password)
        elif hasattr(source, "read"):
            # The stream stays the caller's: it is read, never closed.
            description = getattr(source, "name", "<stream>")
            if hasattr(source, "seek") and source.seekable():
                source.seek(0)
            opened = native.open_bytes(bytes(source.read()), password)
        else:
            raise TypeError(
                f"cannot open a PDF from {type(source).__name__}: expected a path, "
                "bytes, a binary stream, or a PdfReader/PdfWriter/PdfEditor"
            )

        if not opened:
            raise self._load_error(str(description), password)

    def _load_error(self, key: str, password: str | None) -> PdfLoadError:
        info = self._native.take_open_error()
        try:
            category = PdfLoadErrorCategory(info.get("category", "unknown"))
        except ValueError:
            category = PdfLoadErrorCategory.UNKNOWN
        detail = info.get("message", "")

        if category is PdfLoadErrorCategory.PASSWORD:
            return PdfPasswordError(key, detail, password_supplied=password is not None)
        return PdfLoadError(key, category, detail)

    def close(self) -> None:
        """Release the document. The object can not be used afterwards."""
        self._native.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def _document_info(self) -> dict[str, Any]:
        with _native_errors():
            return self._native.document_info()

    @property
    def pages(self) -> PdfPages:
        return PdfPages(self)

    def get_num_pages(self) -> int:
        """The number of pages; same as `len(self.pages)`."""
        return len(self.pages)

    def get_page(self, page_number: int) -> PdfPage:
        """The page at a zero-based position; same as `self.pages[page_number]`."""
        return self.pages[page_number]

    def get_page_number(self, page: PdfPage) -> int:
        """The zero-based position of a page of this document."""
        if page._owner is not self:
            raise ValueError("the page belongs to another document")
        return page.index

    def get_pages_showing_field(self, name: str) -> list[PdfPage]:
        """The pages that show the form field with this fully qualified name."""
        fields = self.get_fields()
        if name not in fields:
            raise KeyError(name)
        return [self.pages[index] for index in fields[name]["pages"]]

    @property
    def pdf_header(self) -> str:
        """The first line of the file, for example "%PDF-1.7"."""
        return f"%PDF-{self.pdf_version}"

    @property
    def pdf_version(self) -> str:
        """The PDF version the file declares, for example "1.7"."""
        return self._document_info()["pdf_version"]

    @property
    def is_encrypted(self) -> bool:
        return self._document_info()["encryption"] is not None

    @property
    def encryption(self) -> dict[str, Any] | None:
        """The encryption parameters, or None for an unencrypted document.

        `permissions` is the raw /P flag word. The flags are reported, not
        enforced.
        """
        return self._document_info()["encryption"]

    @property
    def metadata(self) -> dict[str, str]:
        """The document information dictionary (/Info): Title, Author, ..."""
        return self._document_info()["info"]

    @property
    def xmp_metadata(self) -> str | None:
        """The XMP metadata packet, or None when the document has none."""
        xmp = self._document_info()["xmp"]
        return None if xmp is None else xmp.decode("utf-8", errors="replace")

    @property
    def permissions(self) -> dict[str, bool] | None:
        """What the user password allows, by name, or None for an unencrypted
        document. The flags are reported, not enforced."""
        encryption = self.encryption
        if encryption is None:
            return None
        flags = encryption["permissions"]
        return {
            name: bool(flags & (1 << (bit - 1)))
            for name, bit in _PERMISSION_BITS.items()
        }

    @property
    def outline(self) -> list[dict[str, Any]]:
        """The document outline (bookmarks) as a tree: each item has "title",
        "page" (zero-based index, or None), "children" and "id"."""
        with _native_errors():
            return self._native.outline_tree()

    def _settings(self) -> dict[str, Any]:
        with _native_errors():
            return self._native.document_settings()

    @property
    def page_layout(self) -> str | None:
        """How pages are laid out when the document opens ("/OneColumn",
        "/TwoPageLeft", ...), or None when the document does not say."""
        return self._settings()["page_layout"]

    @property
    def page_mode(self) -> str | None:
        """What a viewer shows beside the pages ("/UseOutlines",
        "/FullScreen", ...), or None when the document does not say."""
        return self._settings()["page_mode"]

    @property
    def viewer_preferences(self) -> dict[str, Any] | None:
        """The viewer preferences dictionary, keyed by PDF name."""
        return self._settings()["viewer_preferences"]

    @property
    def open_destination(self) -> dict[str, Any] | None:
        """Where the document opens: "page", "kind" and "arguments", or None
        when it has no open destination."""
        return self._settings()["open_destination"]

    @property
    def page_labels(self) -> list[str]:
        """The label of every page as a viewer shows it ("i", "ii", "1",
        "A-1", ...); "1", "2", ... when the document defines none."""
        with _native_errors():
            return self._native.page_labels()

    @property
    def named_destinations(self) -> dict[str, dict[str, Any]]:
        """Named destinations: name to "page", "kind" and "arguments"."""
        with _native_errors():
            return self._native.named_destinations()

    def _form(self) -> dict[str, Any]:
        with _native_errors():
            return self._native.form()

    def get_fields(self) -> dict[str, dict[str, Any]]:
        """The form fields by fully qualified name. Each field has "kind"
        (text, checkbox, radio, choice, pushbutton, signature), "value",
        "default_value", "choices", "states", "read_only", "pages", and the
        raw "type" and "flags"."""
        return {field["name"]: field for field in self._form()["fields"]}

    def get_form_text_fields(self) -> dict[str, str]:
        """The value of every text field, by fully qualified name."""
        return {
            field["name"]: field["value"]
            for field in self._form()["fields"]
            if field["kind"] == "text"
        }

    @property
    def has_form(self) -> bool:
        return self._form()["has_form"]

    @property
    def has_xfa(self) -> bool:
        """Whether the form carries an XFA definition. XFA is reported, not
        interpreted: values set here do not reach the XFA data."""
        return self._form()["has_xfa"]

    @property
    def attachment_info(self) -> list[dict[str, Any]]:
        """The embedded files: "name", "filename", "description", "size",
        "mime_type", "creation_date" and "modification_date" of each."""
        with _native_errors():
            return self._native.attachments()

    def get_attachment(self, name: str) -> bytes:
        """The content of the embedded file registered under `name`."""
        with _native_errors():
            return self._native.attachment_data(name)

    @property
    def attachments(self) -> dict[str, list[bytes]]:
        """The embedded files by name; each value is a list with the file's
        content, the shape pypdf uses."""
        return {
            item["name"]: [self.get_attachment(item["name"])]
            for item in self.attachment_info
        }

    @property
    def javascript(self) -> dict[str, str]:
        """The document-level scripts by name. They are data: docling-parse
        never runs them."""
        with _native_errors():
            return self._native.javascript()


class _PdfEditing(_PdfDocument):
    """Page assembly and output: common to writer and editor."""

    _page_class = PdfEditablePage

    def _import_source(
        self, source: Any, password: str | None
    ) -> tuple[Any, _PdfDocument | None]:
        """The native object to import from, and a reader to close if one was
        opened just for this call."""
        if isinstance(source, _PdfDocument):
            return source._native, None

        temporary = _ImportReader()
        temporary._open(source, password)
        return temporary._native, temporary

    def append(
        self,
        source: Any,
        pages: Iterable[int] | slice | None = None,
        *,
        index: int | None = None,
        password: str | None = None,
        import_outline: bool = True,
    ) -> None:
        """Copy pages of another PDF into this document.

        `source` is a path, bytes, a binary stream, or a PdfReader, PdfWriter
        or PdfEditor. `pages` selects zero-based pages of the source (all by
        default) and may repeat or reorder them. `index` is the position the
        first copied page gets; by default pages go at the end.

        With `import_outline`, the source's bookmarks that lead to a copied
        page are added to this document's outline, pointing at the copies.
        """
        native, temporary = self._import_source(source, password)
        try:
            with _native_errors():
                selection: list[int] | None = None
                if pages is not None:
                    count = native.page_count()
                    if isinstance(pages, slice):
                        selection = list(range(*pages.indices(count)))
                    else:
                        selection = [p + count if p < 0 else p for p in pages]

                self._native.append_pages(
                    native,
                    selection,
                    -1 if index is None else self._position(index),
                    import_outline=import_outline,
                )
        finally:
            if temporary is not None:
                temporary.close()

    def merge(
        self,
        position: int | None,
        fileobj: Any,
        pages: Iterable[int] | slice | None = None,
        *,
        password: str | None = None,
        import_outline: bool = True,
    ) -> None:
        """Copy pages of another PDF into this document at `position`
        (None for the end). Same as `append(fileobj, pages, index=position)`."""
        self.append(
            fileobj,
            pages,
            index=position,
            password=password,
            import_outline=import_outline,
        )

    def add_page(self, page: PdfPage) -> PdfEditablePage:
        """Append a copy of a page of any reader, writer or editor."""
        return self.insert_page(page, len(self.pages))

    def insert_page(self, page: PdfPage, index: int = 0) -> PdfEditablePage:
        """Insert a copy of a page of any reader, writer or editor so that it
        becomes page `index`."""
        with _native_errors():
            position = self._native.append_pages(
                page._owner._native,
                [page._index],
                self._position(index),
                other_generation=page._generation,
            )
        return self.pages[position]  # type: ignore[return-value]

    def _position(self, index: int) -> int:
        """An insertion position: 0..len, negative counted from the end."""
        count = len(self.pages)
        position = index + count if index < 0 else index
        if not 0 <= position <= count:
            raise IndexError(f"position {index} out of range for {count} pages")
        return position

    def add_blank_page(self, width: float, height: float) -> PdfEditablePage:
        """Add an empty page at the end. Sizes are in points (1/72 inch)."""
        return self.insert_blank_page(len(self.pages), width, height)

    def insert_blank_page(
        self, index: int, width: float, height: float
    ) -> PdfEditablePage:
        """Insert an empty page so that it becomes page `index`."""
        with _native_errors():
            position = self._native.insert_blank_page(
                float(width), float(height), self._position(index)
            )
        return self.pages[position]  # type: ignore[return-value]

    def add_image(
        self,
        image: _pdf_images.ImageSource,
        *,
        index: int | None = None,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> list[PdfEditablePage]:
        """Add pages showing an image: one page, or one per frame of a
        multi-page TIFF or animated GIF. Returns the pages added.

        `image` is a path, bytes, a binary stream, or a PIL image, in any
        format Pillow reads: PNG, JPEG, TIFF, GIF, BMP, WebP, ... JPEG files
        are stored as they are, without re-encoding; other formats are
        stored losslessly, transparency included.

        Without `page_size` each page is the size of its image at the
        image's resolution (`dpi`, or the resolution the file declares, or
        72: one pixel per point), plus `margin` on every side. With
        `page_size` ("A4", "Letter", "A3", "A5", "Legal", or (width, height)
        in points) the image is centered and scaled to fill the page inside
        the margin. Pages turned by the image's EXIF orientation show it
        upright.
        """
        size = _pdf_images.page_size_of(page_size)
        position = len(self.pages) if index is None else self._position(index)

        added: list[int] = []
        for picture in _pdf_images.pictures(image):
            geometry = _pdf_images.layout(picture, size, dpi, float(margin))
            with _native_errors():
                added.append(
                    self._native.insert_image_page(
                        picture.width,
                        picture.height,
                        picture.colorspace,
                        picture.data,
                        picture.is_jpeg,
                        geometry.page_width,
                        geometry.page_height,
                        list(geometry.placement),
                        invert=picture.invert,
                        alpha=picture.alpha,
                        rotate=picture.rotate,
                        at=position,
                    )
                )
            position += 1

        return [self.pages[i] for i in added]  # type: ignore[misc]

    @classmethod
    def from_images(
        cls,
        images: Iterable[_pdf_images.ImageSource],
        *,
        page_size: str | Sequence[float] | None = None,
        margin: float = 0,
        dpi: float | None = None,
    ) -> Any:
        """A new document with one page per image, in order. The arguments
        are those of `add_image`."""
        document = cls()
        for image in images:
            document.add_image(image, page_size=page_size, margin=margin, dpi=dpi)
        return document

    def duplicate_page(self, index: int, to: int | None = None) -> PdfEditablePage:
        """Add a copy of the page at `index`, at position `to` or, by
        default, at the end. The copy can be edited independently."""
        count = len(self.pages)
        resolved = index + count if index < 0 else index
        with _native_errors():
            position = self._native.duplicate_page(
                resolved, -1 if to is None else self._position(to)
            )
        return self.pages[position]  # type: ignore[return-value]

    def remove_page(self, index: int) -> None:
        """Remove the page at `index`. Existing page handles become stale."""
        count = len(self.pages)
        resolved = index + count if index < 0 else index
        with _native_errors():
            self._native.remove_page(resolved)

    def move_page(self, from_index: int, to_index: int) -> None:
        """Move a page so that it ends up at `to_index`."""
        count = len(self.pages)
        source = from_index + count if from_index < 0 else from_index
        target = to_index + count if to_index < 0 else to_index
        with _native_errors():
            self._native.move_page(source, target)

    # ---- document information ----

    def add_metadata(self, infos: dict[str, str | None]) -> None:
        """Set entries of the document information dictionary. Keys may be
        written "Title" or "/Title"; a value of None removes the entry."""
        with _native_errors():
            self._native.set_info(dict(infos))

    def set_xmp_metadata(self, packet: str | bytes | None) -> None:
        """Replace the XMP metadata packet, or remove it with None."""
        data = packet.encode("utf-8") if isinstance(packet, str) else packet
        with _native_errors():
            self._native.set_xmp(data)

    def set_page_layout(self, layout: str | None) -> None:
        """Set how pages are laid out when the document opens: "/SinglePage",
        "/OneColumn", "/TwoColumnLeft", "/TwoColumnRight", "/TwoPageLeft" or
        "/TwoPageRight". None removes the setting."""
        with _native_errors():
            self._native.set_page_layout(layout or "")

    def set_page_mode(self, mode: str | None) -> None:
        """Set what a viewer shows beside the pages: "/UseNone",
        "/UseOutlines", "/UseThumbs", "/FullScreen", "/UseOC" or
        "/UseAttachments". None removes the setting."""
        with _native_errors():
            self._native.set_page_mode(mode or "")

    def set_viewer_preferences(self, preferences: dict[str, Any] | None) -> None:
        """Replace the viewer preferences, for example
        `{"/HideToolbar": True, "/Direction": "/R2L"}`. None removes them."""
        text = "" if preferences is None else _pdf_syntax(dict(preferences))
        with _native_errors():
            self._native.set_viewer_preferences(text)

    def set_open_destination(self, page_number: int | None) -> None:
        """Open the document at this page, fitted to the window. None removes
        the open action."""
        with _native_errors():
            self._native.set_open_destination(
                -1 if page_number is None else self._page_index(page_number)
            )

    def _page_index(self, index: int) -> int:
        """An existing page: 0..len-1, negative counted from the end."""
        count = len(self.pages)
        resolved = index + count if index < 0 else index
        if not 0 <= resolved < count:
            raise IndexError(f"page index {index} out of range for {count} pages")
        return resolved

    # ---- navigation ----

    def set_page_label(
        self,
        page_index_from: int,
        page_index_to: int,
        style: str | None = None,
        prefix: str | None = None,
        start: int | None = None,
    ) -> None:
        """Label the pages `page_index_from` .. `page_index_to` (inclusive).

        `style` is "/D" (1, 2, 3), "/R" (I, II, III), "/r" (i, ii, iii),
        "/A" (A, B, C) or "/a" (a, b, c); None gives labels made of `prefix`
        alone. `start` is the number of the first page of the range, 1 by
        default. Pages after the range keep their labels.
        """
        with _native_errors():
            self._native.set_page_label(
                self._page_index(page_index_from),
                self._page_index(page_index_to),
                style or "",
                prefix or "",
                start or 1,
            )

    def add_named_destination(self, title: str, page_number: int) -> None:
        """Add, or replace, a named destination that opens a page."""
        with _native_errors():
            self._native.add_named_destination(title, self._page_index(page_number))

    def add_outline_item(
        self,
        title: str,
        page_number: int | None,
        parent: int | None = None,
        *,
        url: str | None = None,
        left: float | None = None,
        top: float | None = None,
        zoom: float | None = None,
        color: Sequence[float] | str | None = None,
        bold: bool = False,
        italic: bool = False,
        is_open: bool = True,
    ) -> int:
        """Add a bookmark. Returns its id, which `parent` and the other
        outline methods take.

        The bookmark opens `page_number`, fitted to the window; with `left`,
        `top` or `zoom` it opens at that position and magnification instead
        (None keeps what the viewer has). With `url` it opens a web address;
        with neither it is a heading without a target. It becomes the last
        child of `parent`, or the last top-level item.

        `color` is (r, g, b) in 0..1 or "#rrggbb"; `is_open=False` shows the
        bookmark with its children folded away.
        """
        spec: dict[str, Any] = {"title": title, "parent": parent or 0}
        spec.update(self._outline_target(page_number, url, left, top, zoom))
        if color is not None:
            spec["color"] = _color(color)
        if bold:
            spec["bold"] = True
        if italic:
            spec["italic"] = True
        if not is_open:
            spec["open"] = False

        with _native_errors():
            return self._native.add_outline_entry(spec)

    def _outline_target(
        self,
        page_number: Any,
        url: Any,
        left: Any,
        top: Any,
        zoom: Any,
    ) -> dict[str, Any]:
        """The target entries of an outline spec. A page or a URL becomes the
        target; when either was given as None, the target is removed."""
        positioned = any(value not in (None, _UNSET) for value in (left, top, zoom))

        if url not in (None, _UNSET) and page_number not in (None, _UNSET):
            raise ValueError("a bookmark opens a page or a URL, not both")
        if positioned and page_number in (None, _UNSET):
            raise ValueError("left, top and zoom need a page_number")

        target: dict[str, Any] = {}
        if url not in (None, _UNSET):
            target["url"] = url
        elif page_number not in (None, _UNSET):
            target["page"] = self._page_index(page_number)
            if positioned:
                for key, value in (("left", left), ("top", top), ("zoom", zoom)):
                    target[key] = None if value is _UNSET else value
        elif page_number is not _UNSET or url is not _UNSET:
            target["page"] = None
        return target

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
        """Change a bookmark. Only the arguments given change; `page_number`
        or `url` replaces the target, and None for both removes it. `color`
        None goes back to the viewer's default."""
        changes: dict[str, Any] = {}
        if title is not _UNSET:
            changes["title"] = title
        if page_number is not _UNSET or url is not _UNSET:
            changes.update(self._outline_target(page_number, url, left, top, zoom))
        elif any(value is not _UNSET for value in (left, top, zoom)):
            raise ValueError("left, top and zoom need a page_number")
        if color is not _UNSET:
            changes["color"] = _color(color)
        if bold is not _UNSET:
            changes["bold"] = bool(bold)
        if italic is not _UNSET:
            changes["italic"] = bool(italic)
        if is_open is not _UNSET:
            changes["open"] = bool(is_open)

        with _native_errors():
            self._native.update_outline_item(item_id, changes)

    def remove_outline_item(self, item_id: int) -> None:
        """Remove a bookmark and every bookmark below it."""
        with _native_errors():
            self._native.remove_outline_item(item_id)

    def move_outline_item(
        self, item_id: int, parent: int | None = None, index: int | None = None
    ) -> None:
        """Move a bookmark below `parent` (None: the top level), at position
        `index` among its new siblings (None: last). Its children move with
        it."""
        with _native_errors():
            self._native.move_outline_item(
                item_id, parent or 0, -1 if index is None else index
            )

    def clear_outline(self) -> None:
        """Remove every bookmark."""
        with _native_errors():
            self._native.clear_outline()

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
        """Insert contents pages, made from the outline, at `index`.

        Every bookmark becomes a line, indented by its depth, with the label
        of the page it opens on the right; the whole line is a link to that
        page. Bookmarks deeper than `max_depth` (0: top level only) are left
        out. The pages take the size of the page they are inserted in front
        of, or `page_size` ("A4", "Letter", or (width, height) in points).
        Returns the pages added; none when the document has no outline.

        Text is drawn in a standard font; characters it cannot show become
        "?". Long titles are shortened to fit, by an estimate of the font's
        average character width.
        """
        entries = [
            entry
            for entry in self._outline_entries(self.outline, 0)
            if max_depth is None or entry[0] <= max_depth
        ]
        if not entries:
            return []

        count = len(self.pages)
        position = self._position(index)
        size_given = _pdf_images.page_size_of(page_size)
        if size_given is not None:
            width, height = size_given
        elif count > 0:
            box = self.pages[min(position, count - 1)].mediabox
            width, height = box.width, box.height
        else:
            width, height = _pdf_images.PAGE_SIZES["a4"]

        margin = 72.0
        line_height = 1.6 * size
        title_height = 3.0 * size
        per_page = max(1, int((height - 2 * margin - title_height) // line_height))
        number_of_pages = -(-len(entries) // per_page)

        added = [
            self.insert_blank_page(position + offset, width, height)
            for offset in range(number_of_pages)
        ]

        # Read again: the targets moved behind the new pages.
        entries = [
            entry
            for entry in self._outline_entries(self.outline, 0)
            if max_depth is None or entry[0] <= max_depth
        ]
        labels = self.page_labels

        number_column = width - margin - 4 * size
        for page_offset in range(number_of_pages):
            page: PdfEditablePage = self.pages[position + page_offset]  # type: ignore[assignment]
            page_number = position + page_offset
            y = height - margin - size
            if page_offset == 0:
                page.draw_text(_win_ansi(title), margin, y, size=1.6 * size, font=font)
            y -= title_height

            for depth, text, target in entries[
                page_offset * per_page : (page_offset + 1) * per_page
            ]:
                x = margin + 1.5 * size * depth
                # Helvetica and Times average about half an em per
                # character; Courier is exactly 0.6 em.
                ratio = 0.6 if font.startswith("Courier") else 0.5
                room = int((number_column - x - size) / (ratio * size))
                line = _win_ansi(text)
                if len(line) > room:
                    line = line[: max(room - 1, 1)] + "\u2026"
                page.draw_text(line, x, y, size=size, font=font)

                if target is not None:
                    page.draw_text(
                        labels[target], number_column, y, size=size, font=font
                    )
                    self.add_link(
                        page_number,
                        (x, y - 0.3 * size, width - margin, y + size),
                        target,
                    )
                y -= line_height

        return [self.pages[page.index] for page in added]  # type: ignore[misc]

    @staticmethod
    def _outline_entries(
        items: list[dict[str, Any]], depth: int
    ) -> list[tuple[int, str, int | None]]:
        result: list[tuple[int, str, int | None]] = []
        for item in items:
            result.append((depth, item["title"], item["page"]))
            result.extend(_PdfEditing._outline_entries(item["children"], depth + 1))
        return result

    # ---- forms ----

    def update_form_field_values(
        self, fields: dict[str, str | bool], *, flatten: bool = False
    ) -> int:
        """Fill in form fields, by fully qualified name; a partial name works
        when only one field has it.

        Text and choice fields take a string. A checkbox takes True, False,
        or one of its states; a radio group takes the state to select
        ("/Choice1", or "Choice1"), or "/Off". The fields' appearances are
        regenerated so the values show in every viewer.

        With `flatten`, the form is then turned into page content. Returns
        the number of fields set; an unknown name raises `PdfEditError`.
        """
        values = {
            name: ("true" if value else "false")
            if isinstance(value, bool)
            else str(value)
            for name, value in fields.items()
        }
        with _native_errors():
            updated = self._native.set_form_values(values)
        if flatten:
            self.flatten_annotations()
        return updated

    def _add_field(
        self,
        page_number: int,
        spec: dict[str, Any],
        read_only: bool,
        required: bool,
        tooltip: str | None,
    ) -> None:
        spec["read_only"] = bool(read_only)
        spec["required"] = bool(required)
        if tooltip is not None:
            spec["tooltip"] = tooltip
        with _native_errors():
            self._native.add_form_field(self._page_index(page_number), spec)

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
        """Add a text field over `rect` (left, bottom, right, top) on a page.

        `name` must be new in the form and contain no period. The field is
        drawn with a thin frame and its value in Helvetica, so it shows in
        every viewer.
        """
        spec: dict[str, Any] = {
            "kind": "text",
            "name": name,
            "rect": _rect(rect),
            "multiline": bool(multiline),
            "font_size": float(font_size),
        }
        if value is not None:
            spec["value"] = value
        self._add_field(page_number, spec, read_only, required, tooltip)

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
        """Add a checkbox over `rect`. Its on state is "/Yes"."""
        spec = {
            "kind": "checkbox",
            "name": name,
            "rect": _rect(rect),
            "checked": bool(checked),
        }
        self._add_field(page_number, spec, read_only, required, tooltip)

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
        """Add a drop-down list over `rect`. `value` must be one of `options`
        unless `editable` lets the user type a value of their own."""
        spec: dict[str, Any] = {
            "kind": "choice",
            "name": name,
            "rect": _rect(rect),
            "options": list(options),
            "editable": bool(editable),
            "font_size": float(font_size),
        }
        if value is not None:
            spec["value"] = value
        self._add_field(page_number, spec, read_only, required, tooltip)

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
        """Add a group of radio buttons: `buttons` maps each button's state
        name to its rectangle. At most one button is on; `selected` names it.
        """
        spec: dict[str, Any] = {
            "kind": "radio",
            "name": name,
            "options": [
                {"state": state, "rect": _rect(rect)} for state, rect in buttons.items()
            ],
            "selected": selected,
        }
        self._add_field(page_number, spec, read_only, required, tooltip)

    def add_signature_field(
        self,
        page_number: int,
        name: str,
        rect: Sequence[float],
        *,
        required: bool = False,
        tooltip: str | None = None,
    ) -> None:
        """Add an empty signature field over `rect`, for a signing
        application to fill. docling-parse does not sign."""
        spec = {"kind": "signature", "name": name, "rect": _rect(rect)}
        self._add_field(page_number, spec, False, required, tooltip)

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
        """Change a form field. Only the arguments given change.

        `new_name` renames the field (its last name part). `rect` moves or
        resizes a field that has one widget. `tooltip` None removes it.
        `options` replaces the entries of a choice field.
        """
        changes: dict[str, Any] = {}
        if new_name is not _UNSET:
            changes["name"] = new_name
        if rect is not _UNSET:
            changes["rect"] = _rect(rect)
        if read_only is not _UNSET:
            changes["read_only"] = bool(read_only)
        if required is not _UNSET:
            changes["required"] = bool(required)
        if tooltip is not _UNSET:
            changes["tooltip"] = tooltip
        if options is not _UNSET:
            changes["options"] = list(options)
        with _native_errors():
            self._native.update_form_field(name, changes)

    def remove_form_field(self, name: str) -> None:
        """Remove a form field, and its widgets from the pages."""
        with _native_errors():
            self._native.remove_form_field(name)

    def set_need_appearances_writer(self, state: bool = True) -> None:
        """Set the form's NeedAppearances flag, which asks a viewer to build
        the field appearances itself."""
        with _native_errors():
            self._native.set_need_appearances(bool(state))

    def flatten_annotations(self) -> None:
        """Draw every visible annotation, form fields included, into its
        page and remove the annotations. Values stay visible; the document
        is no longer interactive."""
        with _native_errors():
            self._native.flatten_annotations()

    # ---- annotations ----

    def add_annotation(self, page_number: int, annotation: dict[str, Any]) -> None:
        """Add an annotation to a page, given as a dictionary keyed by PDF
        name, for example
        `{"/Subtype": "/Text", "/Rect": [50, 700, 70, 720], "/Contents": "Note"}`.
        "/Subtype" and "/Rect" are required."""
        with _native_errors():
            self._native.add_annotation(
                self._page_index(page_number), _pdf_syntax(dict(annotation))
            )

    def add_uri(
        self,
        page_number: int,
        uri: str,
        rect: Sequence[float],
        border: Sequence[float] | None = None,
    ) -> None:
        """Add a link over `rect` (left, bottom, right, top) that opens a
        URL. Without `border` the link has no visible frame."""
        self.add_annotation(
            page_number,
            {
                "/Subtype": "/Link",
                "/Rect": [float(value) for value in rect],
                "/Border": list(border) if border is not None else [0, 0, 0],
                "/A": {"/Type": "/Action", "/S": "/URI", "/URI": uri},
            },
        )

    def add_link(
        self, page_number: int, rect: Sequence[float], target_page_number: int
    ) -> None:
        """Add a link over `rect` that jumps to another page of this
        document."""
        with _native_errors():
            self._native.add_page_link(
                self._page_index(page_number),
                [float(value) for value in rect],
                self._page_index(target_page_number),
            )

    def remove_annotations(self, subtypes: Iterable[str] | str | None = None) -> int:
        """Remove annotations from every page: those of the given subtypes
        ("/Link", "/Highlight", ...), or with None all of them except form
        fields. Naming "/Widget" removes form fields too. Returns the number
        removed."""
        if isinstance(subtypes, str):
            subtypes = [subtypes]
        selected = {s if s.startswith("/") else "/" + s for s in subtypes or ()}

        removed = 0
        with _native_errors():
            for index in range(len(self.pages)):
                removed += self._native.remove_annotations(index, selected)
        return removed

    def remove_links(self) -> int:
        """Remove every link annotation. Returns the number removed."""
        return self.remove_annotations("/Link")

    # ---- attachments and scripts ----

    def add_attachment(
        self,
        filename: str,
        data: str | bytes,
        *,
        description: str | None = None,
        mime_type: str | None = None,
    ) -> None:
        """Embed a file in the document. An attachment of the same name is
        replaced."""
        content = data.encode("utf-8") if isinstance(data, str) else bytes(data)
        with _native_errors():
            self._native.add_attachment(
                filename, content, description or "", mime_type or ""
            )

    def remove_attachment(self, name: str) -> bool:
        """Remove an embedded file. Returns False if there was none."""
        with _native_errors():
            return self._native.remove_attachment(name)

    def add_js(self, javascript: str, name: str | None = None) -> None:
        """Add a document-level script, which a viewer runs when it opens
        the document. docling-parse stores the script and never runs it."""
        if name is None:
            name = f"script-{len(self.javascript) + 1}"
        with _native_errors():
            self._native.add_javascript(name, javascript)

    def remove_javascript(self) -> int:
        """Remove the document-level scripts and scripted document actions.
        Returns the number of entries removed."""
        with _native_errors():
            return self._native.remove_javascript()

    # ---- output ----

    def encrypt(
        self,
        user_password: str,
        owner_password: str | None = None,
        *,
        allow_print: bool | str = True,
        allow_modify: bool = True,
        allow_extract: bool = True,
        allow_annotate: bool = True,
        allow_fill_forms: bool = True,
        allow_accessibility: bool = True,
        allow_assemble: bool = True,
    ) -> None:
        """Protect everything written from now on with a password.

        The output is encrypted with AES-256, the only password scheme PDF
        2.0 keeps; older, weaker algorithms are not offered. `user_password`
        is needed to open the document; pass "" together with an
        `owner_password` to let anyone open it. `owner_password` defaults to
        the user password.

        The `allow_*` arguments say what someone who opens the document with
        the user password may do. `allow_print` is True, False, or "low" for
        low-resolution printing only. Viewers honor these flags; they are
        not access control.

        Without this call the output is unencrypted, even when the source was
        encrypted.
        """
        if allow_print == "low":
            print_level = 1
        elif isinstance(allow_print, bool):
            print_level = 0 if allow_print else 2
        else:
            raise ValueError('allow_print is True, False or "low"')

        with _native_errors():
            self._native.set_encryption(
                user_password,
                owner_password or "",
                print=print_level,
                modify=allow_modify,
                extract=allow_extract,
                annotate=allow_annotate,
                fill_forms=allow_fill_forms,
                accessibility=allow_accessibility,
                assemble=allow_assemble,
            )

    def set_output_options(
        self,
        *,
        compress_streams: bool = True,
        object_streams: str = "preserve",
        linearize: bool = False,
    ) -> None:
        """Choose how the file is written.

        `compress_streams` compresses streams that are stored uncompressed.
        `object_streams` is "preserve", "generate" (pack objects into
        compressed object streams: smaller files) or "disable" (larger, and
        readable by PDF 1.4 software). `linearize` orders the file so a
        viewer can show the first page before the rest has arrived.
        """
        with _native_errors():
            self._native.set_output_options(compress_streams, object_streams, linearize)

    def remove_encryption(self) -> None:
        """Undo `encrypt`: write unencrypted output again."""
        with _native_errors():
            self._native.clear_encryption()

    def to_bytes(self) -> bytes:
        """The document serialized as a complete PDF file."""
        with _native_errors():
            return self._native.write_to_bytes()

    def write(self, target: str | os.PathLike[str] | BinaryIO) -> None:
        """Write the document to a path or a binary stream.

        A path is written through a temporary sibling file that replaces the
        target only once it is complete. A stream receives the whole file in
        one `write` call and is not closed.
        """
        if isinstance(target, (str, os.PathLike)):
            with _native_errors():
                self._native.write_to_file(os.fspath(target))
        elif hasattr(target, "write"):
            target.write(self.to_bytes())
        else:
            raise TypeError(
                f"cannot write a PDF to {type(target).__name__}: "
                "expected a path or a binary stream"
            )


class _ImportReader(_PdfDocument):
    """A reader opened only to copy pages out of a path, bytes or stream."""

    _native_class = _native.pdf_reader
