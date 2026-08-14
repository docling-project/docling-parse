"""Small pypdf-compatible facade backed by docling-parse."""

from __future__ import annotations

import math
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Any, Iterable, Iterator, Sequence

from docling_core.types.doc.page import PdfPageBoundaryType, SegmentedPdfPage

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)

try:
    from docling_parse.pdf_parsers import _PdfWriter as _NativePdfWriter  # type: ignore
except ImportError:  # pragma: no cover - only relevant with a stale local extension
    _NativePdfWriter = None


class PageRange:
    """Minimal rectangle object for pypdf-style page box access."""

    def __init__(self, left: float, bottom: float, right: float, top: float):
        self.left = left
        self.bottom = bottom
        self.right = right
        self.top = top

    @property
    def width(self) -> float:
        return abs(self.right - self.left)

    @property
    def height(self) -> float:
        return abs(self.top - self.bottom)

    def __iter__(self) -> Iterator[float]:
        yield self.left
        yield self.bottom
        yield self.right
        yield self.top


@dataclass(frozen=True)
class _SourceRef:
    path: Path | None
    data: bytes | None
    password: str | None


class _VirtualPages:
    def __init__(self, reader: PdfReader):
        self._reader = reader

    def __len__(self) -> int:
        return self._reader._page_count

    def __iter__(self) -> Iterator[PdfPage]:
        for index in range(len(self)):
            yield self[index]

    def __getitem__(self, index: int | slice) -> PdfPage | list[PdfPage]:
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]

        if index < 0:
            index += len(self)
        if index < 0 or index >= len(self):
            raise IndexError("page index out of range")

        return self._reader._get_page(index)


class PdfReader:
    """Minimal pypdf-compatible reader backed by DoclingThreadedPdfParser."""

    def __init__(
        self,
        stream: str | Path | BytesIO,
        strict: bool = False,
        password: str | None = None,
        **_: Any,
    ):
        self.strict = strict
        self._password = password
        self._source = self._coerce_source(stream, password)
        self._parser = DoclingThreadedPdfParser(
            parser_config=ThreadedPdfParserConfig(
                loglevel="fatal",
                threads=4,
                boundary_type=PdfPageBoundaryType.CROP_BOX,
                page_content_config=ContentConfig(
                    char_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
                    word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
                    line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
                    shapes_content_level=ContentLevel.SKIP,
                    bitmaps_content_level=ContentLevel.SKIP,
                    include_bitmap_bytes=False,
                ),
            ),
            decode_config=DecodeConfig(keep_glyphs=True),
        )

        load_input: Path | BytesIO
        if self._source.path is not None:
            load_input = self._source.path
        elif self._source.data is not None:
            load_input = BytesIO(self._source.data)
        else:
            raise TypeError("expected a path or BytesIO stream")

        self._doc_key = self._parser.load(load_input, password=password)
        self._page_count = self._parser.page_count(self._doc_key)
        self.pages = _VirtualPages(self)
        self._decoded_pages: dict[int, PdfPage] | None = None

    @staticmethod
    def _coerce_source(stream: str | Path | BytesIO, password: str | None) -> _SourceRef:
        if isinstance(stream, (str, Path)):
            return _SourceRef(path=Path(stream), data=None, password=password)
        if isinstance(stream, BytesIO):
            pos = stream.tell()
            stream.seek(0)
            data = stream.read()
            stream.seek(pos)
            return _SourceRef(path=None, data=data, password=password)
        raise TypeError(f"expected str, Path, or BytesIO, got {type(stream)!r}")

    def __len__(self) -> int:
        return self._page_count

    def get_num_pages(self) -> int:
        return self._page_count

    def _ensure_decoded(self) -> None:
        if self._decoded_pages is not None:
            return

        pages: dict[int, PdfPage] = {}
        for result in self._parser.iterate_results():
            if not result.success:
                raise RuntimeError(result.error_message)
            index = result.page_number - 1
            pages[index] = PdfPage(
                page=result.get_page(),
                index=index,
                reader=self,
                source=self._source,
            )

        if len(pages) != self._page_count:
            raise RuntimeError(
                f"decoded {len(pages)} page(s), expected {self._page_count}"
            )
        self._decoded_pages = pages

    def _get_page(self, index: int) -> PdfPage:
        self._ensure_decoded()
        assert self._decoded_pages is not None
        return self._decoded_pages[index]


class PdfPage:
    """Minimal pypdf PageObject-like wrapper."""

    def __init__(
        self,
        *,
        page: SegmentedPdfPage,
        index: int,
        reader: PdfReader,
        source: _SourceRef,
    ):
        self._page = page
        self._index = index
        self._reader = reader
        self._source = source
        self.page_number = index + 1

    @property
    def mediabox(self) -> PageRange:
        bbox = self._page.dimension.media_bbox
        return PageRange(bbox.l, bbox.b, bbox.r, bbox.t)

    @property
    def cropbox(self) -> PageRange:
        bbox = self._page.dimension.crop_bbox
        return PageRange(bbox.l, bbox.b, bbox.r, bbox.t)

    def extract_text(self, *args: Any, **kwargs: Any) -> str:
        orientations = kwargs.pop("orientations", None)
        extraction_mode = kwargs.pop("extraction_mode", "plain")

        if args:
            first = args[0]
            if isinstance(first, (int, tuple, list)):
                orientations = first
                args = args[1:]
        if args:
            raise TypeError(f"unexpected positional arguments: {args!r}")

        allowed_orientations = _normalize_orientations(orientations)
        if extraction_mode == "plain":
            return self._extract_plain(allowed_orientations)
        if extraction_mode == "layout":
            strip_rotated = bool(kwargs.pop("layout_mode_strip_rotated", True))
            space_vertically = bool(kwargs.pop("layout_mode_space_vertically", True))
            scale_weight = float(kwargs.pop("layout_mode_scale_weight", 1.25))
            return self._extract_layout(
                allowed_orientations,
                strip_rotated=strip_rotated,
                space_vertically=space_vertically,
                scale_weight=scale_weight,
            )
        raise ValueError(f"unsupported extraction_mode: {extraction_mode!r}")

    def _extract_plain(self, orientations: tuple[int, ...]) -> str:
        cells = self._page.textline_cells or self._page.word_cells or self._page.char_cells
        lines = [
            cell.text
            for cell in cells
            if cell.text and _cell_orientation(cell) in orientations
        ]
        return "\n".join(lines)

    def _extract_layout(
        self,
        orientations: tuple[int, ...],
        *,
        strip_rotated: bool,
        space_vertically: bool,
        scale_weight: float,
    ) -> str:
        words = [
            cell
            for cell in self._page.word_cells
            if cell.text
            and _cell_orientation(cell) in orientations
            and (not strip_rotated or _cell_orientation(cell) == 0)
        ]
        if not words:
            return self._extract_plain(orientations)

        rows: list[list[Any]] = []
        for word in sorted(words, key=lambda c: (-_cell_center_y(c), _cell_left(c))):
            center_y = _cell_center_y(word)
            height = max(_cell_height(word), 1.0)
            for row in rows:
                row_center = sum(_cell_center_y(cell) for cell in row) / len(row)
                row_height = max(_cell_height(cell) for cell in row)
                if abs(center_y - row_center) <= max(height, row_height) * 0.6:
                    row.append(word)
                    break
            else:
                rows.append([word])

        char_widths = [
            max(_cell_width(word), 0.0) / max(len(word.text), 1)
            for word in words
            if word.text.strip()
        ]
        char_width = max(_median(char_widths) * scale_weight, 1.0)
        line_heights = [_cell_height(word) for word in words if _cell_height(word) > 0]
        line_height = max(_median(line_heights), 1.0)
        min_left = min(_cell_left(word) for word in words)

        output: list[str] = []
        previous_y: float | None = None
        for row in rows:
            row.sort(key=_cell_left)
            row_y = sum(_cell_center_y(cell) for cell in row) / len(row)
            if (
                space_vertically
                and previous_y is not None
                and previous_y - row_y > line_height * 1.8
            ):
                output.extend("" for _ in range(max(1, round((previous_y - row_y) / line_height) - 1)))
            previous_y = row_y

            line = ""
            for word in row:
                target_col = max(0, round((_cell_left(word) - min_left) / char_width))
                if len(line) < target_col:
                    line += " " * (target_col - len(line))
                elif line and not line.endswith(" "):
                    line += " "
                line += word.text
            output.append(line.rstrip())

        return "\n".join(output)


class _WriterPages:
    def __init__(self, writer: PdfWriter):
        self._writer = writer

    def __len__(self) -> int:
        return self._writer.page_count


class PdfWriter:
    """Minimal native QPDF-backed pypdf-compatible writer."""

    def __init__(self, *args: Any, **kwargs: Any):
        if args or kwargs:
            raise TypeError("PdfWriter() does not accept arguments")
        if _NativePdfWriter is None:
            raise RuntimeError(
                "native PdfWriter binding is unavailable; rebuild docling-parse"
            )
        self._writer = _NativePdfWriter()
        self.pages = _WriterPages(self)

    @property
    def page_count(self) -> int:
        return int(self._writer.page_count())

    def __len__(self) -> int:
        return self.page_count

    def append(
        self,
        fileobj: str | Path | BytesIO | PdfReader,
        pages: Sequence[int] | tuple[int, int] | None = None,
        password: str | None = None,
        **_: Any,
    ) -> None:
        self._add_source(fileobj, pages=pages, password=password, position=-1)

    def merge(
        self,
        position: int,
        fileobj: str | Path | BytesIO | PdfReader,
        pages: Sequence[int] | tuple[int, int] | None = None,
        password: str | None = None,
        **_: Any,
    ) -> None:
        self._add_source(fileobj, pages=pages, password=password, position=position)

    def add_page(self, page: PdfPage) -> PdfPage:
        self.insert_page(page, index=self.page_count)
        return page

    def insert_page(self, page: PdfPage, index: int = 0) -> PdfPage:
        if not isinstance(page, PdfPage):
            raise TypeError("add_page/insert_page currently accepts docling_parse PdfPage")
        self._add_source(
            page._source,
            pages=[page._index],
            password=page._source.password,
            position=index,
            pages_are_zero_based=True,
        )
        return page

    def add_blank_page(self, width: float | None = None, height: float | None = None):
        raise NotImplementedError("native blank-page creation is not implemented yet")

    def insert_blank_page(
        self,
        width: float | None = None,
        height: float | None = None,
        index: int = 0,
    ):
        raise NotImplementedError("native blank-page creation is not implemented yet")

    def add_metadata(self, infos: dict[str, str]) -> None:
        self._writer.add_metadata({str(k): str(v) for k, v in infos.items()})

    def add_outline_item(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native outline creation is not implemented yet")

    def add_annotation(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native annotation creation is not implemented yet")

    def add_form_field(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native form-field creation is not implemented yet")

    def set_form_field_value(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native form-field value editing is not implemented yet")

    def clear_form_field_value(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native form-field value clearing is not implemented yet")

    def remove_form_field(self, *args: Any, **kwargs: Any):
        raise NotImplementedError("native form-field removal is not implemented yet")

    def write(self, stream: str | Path | BytesIO) -> tuple[bool, object]:
        if isinstance(stream, (str, Path)):
            self._writer.write_file(str(stream))
            return (False, stream)
        if isinstance(stream, BytesIO):
            stream.write(self._writer.write_bytes())
            return (False, stream)
        if hasattr(stream, "write"):
            stream.write(self._writer.write_bytes())
            return (False, stream)
        raise TypeError(f"expected path or writable stream, got {type(stream)!r}")

    def _add_source(
        self,
        fileobj: str | Path | BytesIO | PdfReader | _SourceRef,
        *,
        pages: Sequence[int] | tuple[int, int] | None,
        password: str | None,
        position: int,
        pages_are_zero_based: bool = False,
    ) -> None:
        source = _source_from_writer_input(fileobj, password)
        page_indexes = _normalize_page_indexes(pages, zero_based=pages_are_zero_based)
        if source.path is not None:
            self._writer.add_pages_from_file(
                str(source.path), source.password, page_indexes, position
            )
        elif source.data is not None:
            self._writer.add_pages_from_bytes(
                source.data, source.password, "writer memory PDF", page_indexes, position
            )
        else:
            raise TypeError("invalid PDF source")


def _source_from_writer_input(
    fileobj: str | Path | BytesIO | PdfReader | _SourceRef,
    password: str | None,
) -> _SourceRef:
    if isinstance(fileobj, _SourceRef):
        return fileobj
    if isinstance(fileobj, PdfReader):
        return fileobj._source
    if isinstance(fileobj, (str, Path)):
        return _SourceRef(path=Path(fileobj), data=None, password=password)
    if isinstance(fileobj, BytesIO):
        pos = fileobj.tell()
        fileobj.seek(0)
        data = fileobj.read()
        fileobj.seek(pos)
        return _SourceRef(path=None, data=data, password=password)
    raise TypeError(f"expected path, BytesIO, or PdfReader, got {type(fileobj)!r}")


def _normalize_page_indexes(
    pages: Sequence[int] | tuple[int, int] | None,
    *,
    zero_based: bool,
) -> list[int]:
    if pages is None:
        return []
    if isinstance(pages, tuple) and len(pages) == 2:
        start, stop = pages
        values: Iterable[int] = range(start, stop)
    else:
        values = pages
    if zero_based:
        return [int(page) for page in values]
    return [int(page) for page in values]


def _normalize_orientations(value: Any) -> tuple[int, ...]:
    if value is None:
        return (0, 90, 180, 270)
    if isinstance(value, int):
        values = (value,)
    elif isinstance(value, (tuple, list)):
        values = tuple(int(v) for v in value)
    else:
        raise TypeError("orientations must be an int or a tuple/list of ints")

    normalized = tuple(angle % 360 for angle in values)
    invalid = [angle for angle in normalized if angle not in (0, 90, 180, 270)]
    if invalid:
        raise ValueError(f"unsupported text orientation(s): {invalid!r}")
    return normalized


def _cell_orientation(cell: Any) -> int:
    dx = float(cell.rect.r_x1) - float(cell.rect.r_x0)
    dy = float(cell.rect.r_y1) - float(cell.rect.r_y0)
    if dx == 0 and dy == 0:
        return 0
    angle = math.degrees(math.atan2(dy, dx)) % 360
    return int(round(angle / 90.0) * 90) % 360


def _cell_left(cell: Any) -> float:
    return min(cell.rect.r_x0, cell.rect.r_x1, cell.rect.r_x2, cell.rect.r_x3)


def _cell_right(cell: Any) -> float:
    return max(cell.rect.r_x0, cell.rect.r_x1, cell.rect.r_x2, cell.rect.r_x3)


def _cell_bottom(cell: Any) -> float:
    return min(cell.rect.r_y0, cell.rect.r_y1, cell.rect.r_y2, cell.rect.r_y3)


def _cell_top(cell: Any) -> float:
    return max(cell.rect.r_y0, cell.rect.r_y1, cell.rect.r_y2, cell.rect.r_y3)


def _cell_center_y(cell: Any) -> float:
    return (_cell_bottom(cell) + _cell_top(cell)) / 2.0


def _cell_width(cell: Any) -> float:
    return _cell_right(cell) - _cell_left(cell)


def _cell_height(cell: Any) -> float:
    return _cell_top(cell) - _cell_bottom(cell)


def _median(values: Sequence[float]) -> float:
    if not values:
        return 1.0
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0
