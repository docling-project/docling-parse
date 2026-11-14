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

from docling_parse import pdf_parsers as _native  # type: ignore[attr-defined]
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
    """The resource name the page draws the image under; empty if inline."""
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

    def extract_text(self) -> str:
        """The page's text lines in the parser's reading order, joined by newlines."""
        with _native_errors():
            return self._owner._native.extract_text(self._index, self._generation)

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
    def outline(self) -> Any:
        """The document outline (bookmarks) with resolved destinations."""
        with _native_errors():
            return self._native.outline()


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
    ) -> None:
        """Copy pages of another PDF into this document.

        `source` is a path, bytes, a binary stream, or a PdfReader, PdfWriter
        or PdfEditor. `pages` selects zero-based pages of the source (all by
        default) and may repeat or reorder them. `index` is the position the
        first copied page gets; by default pages go at the end.
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
                    native, selection, -1 if index is None else self._position(index)
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
    ) -> None:
        """Copy pages of another PDF into this document at `position`
        (None for the end). Same as `append(fileobj, pages, index=position)`."""
        self.append(fileobj, pages, index=position, password=password)

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

    def encrypt(self, user_password: str, owner_password: str | None = None) -> None:
        """Protect everything written from now on with a password.

        The output is encrypted with AES-256, the only password scheme PDF
        2.0 keeps; older, weaker algorithms are not offered. `user_password`
        is needed to open the document; pass "" together with an
        `owner_password` to let anyone open it. `owner_password` defaults to
        the user password. All permissions are granted.

        Without this call the output is unencrypted, even when the source was
        encrypted.
        """
        with _native_errors():
            self._native.set_encryption(user_password, owner_password or "")

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
