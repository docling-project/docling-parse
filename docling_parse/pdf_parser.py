"""Parser for PDF files"""

import hashlib
import logging
import math
from io import BytesIO
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Sequence, Tuple, Union

from docling_core.types.doc.base import BoundingBox, CoordOrigin, ImageRefMode
from docling_core.types.doc.document import ImageRef
from docling_core.types.doc.page import (
    BitmapResource,
    BoundingRectangle,
    ColorRGBA,
    Coord2D,
    PdfHyperlink,
    PdfMetaData,
    PdfPageBoundaryType,
    PdfPageGeometry,
    PdfShape,
    PdfTableOfContents,
    PdfTextCell,
    PdfWidget,
    SegmentedPdfPage,
    TextCell,
    TextDirection,
)
from PIL import Image as PILImage
from pydantic import BaseModel, ConfigDict

from docling_parse.pdf_parsers import (  # type: ignore[import]
    TIMING_KEY_CREATE_LINE_CELLS,
    TIMING_KEY_CREATE_WORD_CELLS,
    TIMING_KEY_DECODE_ANNOTS,
    TIMING_KEY_DECODE_CONTENTS,
    TIMING_KEY_DECODE_DIMENSIONS,
    TIMING_KEY_DECODE_DOCUMENT,
    TIMING_KEY_DECODE_FONTS,
    TIMING_KEY_DECODE_FONTS_TOTAL,
    TIMING_KEY_DECODE_GRPHS,
    TIMING_KEY_DECODE_GRPHS_TOTAL,
    TIMING_KEY_DECODE_PAGE,
    TIMING_KEY_DECODE_RESOURCES,
    TIMING_KEY_DECODE_XOBJECTS,
    TIMING_KEY_DECODE_XOBJECTS_TOTAL,
    TIMING_KEY_EXTRACT_ANNOTS_JSON,
    TIMING_KEY_EXTRACT_DOC_ANNOTATIONS,
    TIMING_KEY_PROCESS_DOCUMENT_FROM_BYTESIO,
    TIMING_KEY_PROCESS_DOCUMENT_FROM_FILE,
    TIMING_KEY_QPDF_PROCESS,
    TIMING_KEY_ROTATE_CONTENTS,
    TIMING_KEY_SANITISE_CONTENTS,
    TIMING_KEY_SANITIZE_CELLS,
    TIMING_KEY_SANITIZE_ORIENTATION,
    TIMING_KEY_TO_JSON_PAGE,
    TIMING_PREFIX_DECODE_FONT,
    TIMING_PREFIX_DECODE_GRPH,
    TIMING_PREFIX_DECODE_PAGE,
    TIMING_PREFIX_DECODE_XOBJECT,
    TIMING_PREFIX_DECODING_PAGE,
    DecodePageConfig,  # type: ignore[import]
    PdfPageDecoder,  # type: ignore[import]
    RenderConfig,  # type: ignore[import]
    _threaded_pdf_parser,  # type: ignore[import]
    _threaded_pdf_renderer,  # type: ignore[import]
    get_decode_page_timing_keys,
    get_static_timing_keys,
    is_static_timing_key,
    pdf_parser,  # type: ignore[import]
)

# Configure logging
_log = logging.getLogger(__name__)


class PdfTocEntry(BaseModel):
    """PDF table of contents entry (recursive structure).

    Attributes:
        title: The text of the TOC entry
        level: Nesting level in the hierarchy (0 for top level)
        page: Page number this entry points to (optional)
        children: Nested TOC entries (optional)
    """

    model_config = ConfigDict(extra="allow")

    title: str
    level: int | None = None
    page: int | None = None
    children: List["PdfTocEntry"] | None = None


class PdfAnnotations(BaseModel):
    """PDF document annotations including form fields, language, metadata, and table of contents.

    Attributes:
        form: AcroForm data containing interactive form fields (raw dict structure). None if no forms present.
        language: Document language code (e.g., 'en-US', 'fr-FR'). None if not specified.
        meta_xml: XMP metadata as XML string. None if no metadata present.
        table_of_contents: Document outline/bookmark structure as list of entries. None if no TOC.
    """

    model_config = ConfigDict(validate_assignment=True, extra="allow")

    form: Dict[str, Any] | None = None
    language: str | None = None
    meta_xml: str | None = None
    table_of_contents: List[PdfTocEntry] | None = None


class Timings(BaseModel):
    """Timing information from PDF page parsing.

    Provides detailed timing breakdown of the parsing process, useful for
    performance analysis and optimization.

    Attributes:
        data: Dictionary mapping operation names to elapsed time in seconds (summed).
            Common keys include:
            - 'decode_page': Total page decoding time
            - 'decode_dimensions': Time to parse page dimensions
            - 'decode_resources': Time to decode page resources (fonts, etc.)
            - 'decode_contents': Time to decode page content streams
            - 'decode_annots': Time to decode annotations
            - 'create_word_cells': Time to create word cells (if requested)
            - 'create_line_cells': Time to create line cells (if requested)
        raw_data: Dictionary mapping operation names to list of elapsed times.
            This is useful when an operation is repeated multiple times
            (e.g., decoding multiple fonts) and you want to see individual timings.
    """

    model_config = ConfigDict(validate_assignment=True)

    data: Dict[str, float] = {}
    raw_data: Dict[str, List[float]] = {}

    def total(self) -> float:
        """Get total time across all operations."""
        return sum(self.data.values())

    def get(self, key: str, default: float = 0.0) -> float:
        """Get timing for a specific operation (summed if repeated)."""
        return self.data.get(key, default)

    def get_all(self, key: str) -> List[float]:
        """Get all timing values for a specific operation."""
        return self.raw_data.get(key, [])

    def get_count(self, key: str) -> int:
        """Get the number of times an operation was timed."""
        return len(self.raw_data.get(key, []))

    def __getitem__(self, key: str) -> float:
        return self.data[key]

    def keys(self):
        """Get all timing operation names."""
        return self.data.keys()

    def items(self):
        """Get all timing items as (name, seconds) pairs."""
        return self.data.items()

    def get_static_timings(self) -> Dict[str, float]:
        """Get only static (constant) timing keys."""
        return {k: v for k, v in self.data.items() if is_static_timing_key(k)}

    def get_dynamic_timings(self) -> Dict[str, float]:
        """Get only dynamic timing keys."""
        return {k: v for k, v in self.data.items() if not is_static_timing_key(k)}

    @staticmethod
    def static_keys() -> set:
        """Get all static timing key names."""
        return get_static_timing_keys()

    @staticmethod
    def decode_page_keys() -> List[str]:
        """Get timing keys used in decode_page method (in order, excluding global timer)."""
        return get_decode_page_timing_keys()


def _to_bounding_rectangle(
    bbox: tuple[float, float, float, float],
) -> BoundingRectangle:
    return BoundingRectangle(
        r_x0=bbox[0],
        r_y0=bbox[1],
        r_x1=bbox[2],
        r_y1=bbox[1],
        r_x2=bbox[2],
        r_y2=bbox[3],
        r_x3=bbox[0],
        r_y3=bbox[3],
        coord_origin=CoordOrigin.BOTTOMLEFT,
    )


def _to_bounding_box(bbox: tuple[float, float, float, float]) -> BoundingBox:
    return BoundingBox(
        l=bbox[0],
        b=bbox[1],
        r=bbox[2],
        t=bbox[3],
        coord_origin=CoordOrigin.BOTTOMLEFT,
    )


def _get_boundary_bbox(
    page_dim,
    boundary_type: PdfPageBoundaryType,
) -> tuple[float, float, float, float]:
    media_bbox = tuple(page_dim.get_media_bbox())
    crop_bbox = tuple(page_dim.get_crop_bbox())

    if boundary_type == PdfPageBoundaryType.MEDIA_BOX:
        return media_bbox

    return crop_bbox


def _to_page_geometry_from_decoder(
    page_dim,
    boundary_type: PdfPageBoundaryType,
) -> PdfPageGeometry:
    crop_bbox = tuple(page_dim.get_crop_bbox())
    media_bbox = tuple(page_dim.get_media_bbox())
    boundary_bbox = _get_boundary_bbox(page_dim, boundary_type)

    return PdfPageGeometry(
        angle=page_dim.get_angle(),
        boundary_type=boundary_type,
        rect=_to_bounding_rectangle(boundary_bbox),
        art_bbox=_to_bounding_box(crop_bbox),
        media_bbox=_to_bounding_box(media_bbox),
        trim_bbox=_to_bounding_box(crop_bbox),
        crop_bbox=_to_bounding_box(crop_bbox),
        bleed_bbox=_to_bounding_box(crop_bbox),
    )


def _to_cells_from_decoder(cells_container) -> List[Union[PdfTextCell, TextCell]]:
    result: List[Union[PdfTextCell, TextCell]] = []

    for ind, cell in enumerate(cells_container):
        result.append(
            PdfTextCell(
                rect=BoundingRectangle(
                    r_x0=cell.r_x0,
                    r_y0=cell.r_y0,
                    r_x1=cell.r_x1,
                    r_y1=cell.r_y1,
                    r_x2=cell.r_x2,
                    r_y2=cell.r_y2,
                    r_x3=cell.r_x3,
                    r_y3=cell.r_y3,
                ),
                text=cell.text,
                orig=cell.text,
                font_key=cell.font_key,
                font_name=cell.font_name,
                widget=cell.widget,
                text_direction=(
                    TextDirection.LEFT_TO_RIGHT
                    if cell.left_to_right
                    else TextDirection.RIGHT_TO_LEFT
                ),
                index=ind,
                rendering_mode=cell.rendering_mode,
            )
        )

    return result


def _to_shapes_from_decoder(shapes_container) -> List[PdfShape]:
    result: List[PdfShape] = []

    for ind, shape in enumerate(shapes_container):
        x_coords = shape.get_x()
        y_coords = shape.get_y()
        indices = shape.get_i()

        for pair_idx in range(0, len(indices), 2):
            i0: int = indices[pair_idx + 0]
            i1: int = indices[pair_idx + 1]

            points: List[Coord2D] = []
            for k in range(i0, i1):
                points.append(Coord2D(x_coords[k], y_coords[k]))

            rgb_s = shape.get_rgb_stroking_ops()
            rgb_f = shape.get_rgb_filling_ops()

            result.append(
                PdfShape(
                    index=ind,
                    parent_id=pair_idx,
                    points=points,
                    has_graphics_state=shape.get_has_graphics_state(),
                    line_width=shape.get_line_width(),
                    miter_limit=shape.get_miter_limit(),
                    line_cap=shape.get_line_cap(),
                    line_join=shape.get_line_join(),
                    dash_phase=shape.get_dash_phase(),
                    dash_array=list(shape.get_dash_array()),
                    flatness=shape.get_flatness(),
                    rgb_stroking=ColorRGBA(r=rgb_s[0], g=rgb_s[1], b=rgb_s[2]),
                    rgb_filling=ColorRGBA(r=rgb_f[0], g=rgb_f[1], b=rgb_f[2]),
                )
            )

    return result


def _to_widgets_from_decoder(widgets_container) -> List[PdfWidget]:
    result: List[PdfWidget] = []

    for ind, widget in enumerate(widgets_container):
        result.append(
            PdfWidget(
                index=ind,
                rect=BoundingRectangle(
                    r_x0=widget.x0,
                    r_y0=widget.y0,
                    r_x1=widget.x1,
                    r_y1=widget.y0,
                    r_x2=widget.x1,
                    r_y2=widget.y1,
                    r_x3=widget.x0,
                    r_y3=widget.y1,
                ),
                widget_text=widget.text or None,
                widget_description=widget.description or None,
                widget_field_name=widget.field_name or None,
                widget_field_type=widget.field_type or None,
            )
        )

    return result


def _to_hyperlinks_from_decoder(hyperlinks_container) -> List[PdfHyperlink]:
    result: List[PdfHyperlink] = []

    for ind, hyperlink in enumerate(hyperlinks_container):
        result.append(
            PdfHyperlink(
                index=ind,
                rect=BoundingRectangle(
                    r_x0=hyperlink.x0,
                    r_y0=hyperlink.y0,
                    r_x1=hyperlink.x1,
                    r_y1=hyperlink.y0,
                    r_x2=hyperlink.x1,
                    r_y2=hyperlink.y1,
                    r_x3=hyperlink.x0,
                    r_y3=hyperlink.y1,
                ),
                uri=hyperlink.uri or None,
            )
        )

    return result


def _to_bitmap_resources_from_decoder(images_container) -> List[BitmapResource]:
    result: List[BitmapResource] = []

    for ind, image in enumerate(images_container):
        image_ref = None
        mode = ImageRefMode.PLACEHOLDER

        try:
            image_bytes = image.get_image_as_bytes()

            if image_bytes and len(image_bytes) > 0:
                fmt = image.get_image_format()
                pil_image: PILImage.Image | None = None

                if fmt in ("jpeg", "jp2"):
                    pil_image = PILImage.open(BytesIO(image_bytes))
                elif fmt in ("raw", "jbig2"):
                    pil_mode = image.get_pil_mode()
                    w = image.image_width
                    h = image.image_height
                    if w > 0 and h > 0:
                        pil_image = PILImage.frombytes(pil_mode, (w, h), image_bytes)

                if pil_image is not None:
                    if pil_image.mode != "RGBA":
                        pil_image = pil_image.convert("RGBA")

                    bbox_width = abs(image.x1 - image.x0)
                    if bbox_width > 0 and image.image_width > 0:
                        dpi = round(image.image_width * 72.0 / bbox_width)
                    else:
                        dpi = 72

                    image_ref = ImageRef.from_pil(pil_image, dpi=dpi)
                    mode = ImageRefMode.EMBEDDED

        except Exception:
            _log.debug(
                "Failed to extract image data for bitmap, falling back to placeholder"
            )

        result.append(
            BitmapResource(
                index=ind,
                rect=BoundingRectangle(
                    r_x0=image.x0,
                    r_y0=image.y0,
                    r_x1=image.x1,
                    r_y1=image.y0,
                    r_x2=image.x1,
                    r_y2=image.y1,
                    r_x3=image.x0,
                    r_y3=image.y1,
                ),
                uri=None,
                image=image_ref,
                mode=mode,
            )
        )

    return result


def segmented_page_from_decoder(
    page_decoder: PdfPageDecoder,
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
) -> SegmentedPdfPage:
    """Convert a C++ PdfPageDecoder to a SegmentedPdfPage."""
    char_cells = _to_cells_from_decoder(page_decoder.get_char_cells())

    segmented_page = SegmentedPdfPage(
        dimension=_to_page_geometry_from_decoder(
            page_decoder.get_page_dimension(), boundary_type
        ),
        char_cells=char_cells,
        word_cells=[],
        textline_cells=[],
        has_chars=len(char_cells) > 0,
        bitmap_resources=_to_bitmap_resources_from_decoder(
            page_decoder.get_page_images()
        ),
        shapes=_to_shapes_from_decoder(page_decoder.get_page_shapes()),
        widgets=_to_widgets_from_decoder(page_decoder.get_page_widgets()),
        hyperlinks=_to_hyperlinks_from_decoder(page_decoder.get_page_hyperlinks()),
    )

    if page_decoder.has_word_cells():
        segmented_page.word_cells = _to_cells_from_decoder(
            page_decoder.get_word_cells()
        )
        segmented_page.has_words = len(segmented_page.word_cells) > 0

    if page_decoder.has_line_cells():
        segmented_page.textline_cells = _to_cells_from_decoder(
            page_decoder.get_line_cells()
        )
        segmented_page.has_lines = len(segmented_page.textline_cells) > 0

    return segmented_page


def _timings_from_decoder(page_decoder: PdfPageDecoder) -> Timings:
    return Timings(
        data=dict(page_decoder.get_timings()),
        raw_data=dict(page_decoder.get_timings_raw()),
    )


def _page_size_from_decoder(
    page_decoder: PdfPageDecoder,
    boundary_type: PdfPageBoundaryType,
) -> tuple[float, float]:
    bbox = _get_boundary_bbox(page_decoder.get_page_dimension(), boundary_type)
    return abs(bbox[2] - bbox[0]), abs(bbox[3] - bbox[1])


class PdfDocument:
    def __init__(
        self,
        parser: "pdf_parser",
        key: str,
        boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
    ):
        self._parser: pdf_parser = parser
        self._key = key
        self._boundary_type = boundary_type
        self._pages: Dict[int, SegmentedPdfPage] = {}
        self._toc: PdfTableOfContents | None = None
        self._meta: PdfMetaData | None = None
        self._annotations: PdfAnnotations | None = None

    def _default_config(self) -> DecodePageConfig:
        config = DecodePageConfig()
        config.page_boundary = self._boundary_type.value
        config.do_sanitization = False
        return config

    def is_loaded(self) -> bool:
        return self._parser.is_loaded(key=self._key)

    def unload(self) -> bool:
        self._pages.clear()

        if self.is_loaded():
            return self._parser.unload_document(self._key)
        else:
            return False

    def unload_pages(self, page_range: tuple[int, int]):
        """unload page in range [page_range[0], page_range[1]["""
        for page_no in range(page_range[0], page_range[1]):
            if page_no < 1:
                _log.error("page_no should always be >=1!")

            if page_no in self._pages:
                # we are using 0 indexing in the C++ docling-parse!
                page_num = page_no - 1
                self._parser.unload_document_page(key=self._key, page=page_num)
                del self._pages[page_no]

    def number_of_pages(self) -> int:
        if self.is_loaded():
            return self._parser.number_of_pages(key=self._key)
        else:
            raise RuntimeError("This document is not loaded.")

    def get_meta(self) -> PdfMetaData | None:

        if self._meta is not None:
            return self._meta

        if self.is_loaded():
            xml = self._parser.get_meta_xml(key=self._key)

            if xml is None:
                return self._meta

            if isinstance(xml, str):
                self._meta = PdfMetaData(xml=xml)
                self._meta.initialise()

            return self._meta

        else:
            raise RuntimeError("This document is not loaded.")

    def get_table_of_contents(self) -> PdfTableOfContents | None:
        if self.is_loaded():
            toc = self._parser.get_table_of_contents(key=self._key)

            if toc is None:
                return self._toc

            if self._toc is not None:
                return self._toc

            self._toc = PdfTableOfContents(text="<root>")
            self._toc.children = self._to_table_of_contents(toc=toc)

            return self._toc
        else:
            raise RuntimeError("This document is not loaded.")

    def iterate_pages(
        self,
        *,
        config: DecodePageConfig | None = None,
    ) -> Iterator[Tuple[int, SegmentedPdfPage]]:
        if config is None:
            config = self._default_config()
        for page_no in range(self.number_of_pages()):
            yield (
                page_no + 1,
                self.get_page(
                    page_no + 1,
                    config=config,
                ),
            )

    def _to_table_of_contents(self, toc: dict) -> List[PdfTableOfContents]:

        result = []
        for item in toc:
            subtoc = PdfTableOfContents(text=item["title"])
            if "children" in item:
                subtoc.children = self._to_table_of_contents(toc=item["children"])
            result.append(subtoc)

        return result

    def _to_pdf_toc_entry(self, toc_list: List[Dict]) -> List[PdfTocEntry]:
        """Convert raw TOC dict list to PdfTocEntry objects."""
        result = []
        for item in toc_list:
            entry = PdfTocEntry(
                title=item.get("title", ""),
                level=item.get("level"),
                page=item.get("page"),
            )
            if item.get("children"):
                entry.children = self._to_pdf_toc_entry(item["children"])
            result.append(entry)
        return result

    def get_annotations(self) -> PdfAnnotations | None:
        """Get document annotations including form fields, language, metadata, and TOC.

        Returns:
            Optional[PdfAnnotations]: Annotations object with form, language, meta_xml,
                and table_of_contents fields. None if document is not loaded or no annotations.
        """
        if self._annotations is not None:
            return self._annotations

        if self.is_loaded():
            annots_dict = self._parser.get_annotations(key=self._key)

            if annots_dict is None:
                return self._annotations

            # Convert table_of_contents list of dicts to PdfTocEntry objects if present
            toc_entries = None
            if annots_dict.get("table_of_contents"):
                toc_entries = self._to_pdf_toc_entry(annots_dict["table_of_contents"])

            self._annotations = PdfAnnotations(
                form=annots_dict.get("form"),
                language=annots_dict.get("language"),
                meta_xml=annots_dict.get("meta_xml"),
                table_of_contents=toc_entries,
            )

            return self._annotations
        else:
            raise RuntimeError("This document is not loaded.")

    def get_page(
        self,
        page_no: int,
        *,
        config: DecodePageConfig | None = None,
    ) -> SegmentedPdfPage:
        """Get page using typed API (zero-copy from C++)."""
        if config is None:
            config = self._default_config()
        return self._get_page_typed(page_no, config=config)

    def get_page_with_timings(
        self,
        page_no: int,
        *,
        config: DecodePageConfig | None = None,
    ) -> Tuple[SegmentedPdfPage, Timings]:
        """Get page along with timing information.

        Similar to get_page() but also returns timing data from the parsing process.
        Useful for performance analysis and benchmarking.

        Note: This method does NOT use the page cache to ensure fresh timing data.

        Args:
            page_no: Page number (1-indexed).
            config: Page decoding configuration. If None, uses default config.

        Returns:
            Tuple of (SegmentedPdfPage, Timings) with the parsed page data and timing info.
        """
        if config is None:
            config = self._default_config()

        if not (1 <= page_no <= self.number_of_pages()):
            raise ValueError(
                f"incorrect page_no: {page_no} for key={self._key} "
                f"(min:1, max:{self.number_of_pages()})"
            )

        return self._get_page_with_timings_typed(page_no, config=config)

    def _get_page_with_timings_typed(
        self,
        page_no: int,
        *,
        config: DecodePageConfig,
    ) -> Tuple[SegmentedPdfPage, Timings]:
        """Get page with timings using typed API."""
        page_decoder = self._parser.get_page_decoder(
            key=self._key,
            page=page_no - 1,
            config=config,
        )

        if page_decoder is None:
            raise ValueError(f"Failed to decode page {page_no}")

        segmented_page = self._to_segmented_page_from_decoder(
            page_decoder=page_decoder,
        )

        # Get timings from the page decoder
        timings_dict = page_decoder.get_timings()
        raw_timings_dict = page_decoder.get_timings_raw()
        timings = Timings(data=dict(timings_dict), raw_data=dict(raw_timings_dict))

        return segmented_page, timings

    def load_all_pages(self, config: DecodePageConfig | None = None):
        if config is None:
            config = self._default_config()
        for page_no in range(1, self.number_of_pages() + 1):
            self.get_page(page_no, config=config)

    def _to_page_geometry_from_decoder(self, page_dim) -> PdfPageGeometry:
        """Convert typed PdfPageDimension to PdfPageGeometry."""
        return _to_page_geometry_from_decoder(page_dim, self._boundary_type)

    def _to_cells_from_decoder(
        self, cells_container
    ) -> List[Union[PdfTextCell, TextCell]]:
        """Convert typed PdfCells container to list of PdfTextCell objects."""
        return _to_cells_from_decoder(cells_container)

    def _to_shapes_from_decoder(self, shapes_container) -> List[PdfShape]:
        """Convert typed PdfShapes container to list of PdfShape objects."""
        return _to_shapes_from_decoder(shapes_container)

    def _to_widgets_from_decoder(self, widgets_container) -> List[PdfWidget]:
        """Convert typed PdfWidgets container to list of PdfWidget objects."""
        return _to_widgets_from_decoder(widgets_container)

    def _to_hyperlinks_from_decoder(self, hyperlinks_container) -> List[PdfHyperlink]:
        """Convert typed PdfHyperlinks container to list of PdfHyperlink objects."""
        return _to_hyperlinks_from_decoder(hyperlinks_container)

    def _to_bitmap_resources_from_decoder(
        self, images_container
    ) -> List[BitmapResource]:
        """Convert typed PdfImages container to list of BitmapResource objects."""
        return _to_bitmap_resources_from_decoder(images_container)

    def _to_segmented_page_from_decoder(
        self,
        page_decoder,
    ) -> SegmentedPdfPage:
        """Convert typed PdfPageDecoder to SegmentedPdfPage (zero-copy path)."""
        return segmented_page_from_decoder(
            page_decoder=page_decoder,
            boundary_type=self._boundary_type,
        )

    def _get_page_typed(
        self,
        page_no: int,
        *,
        config: DecodePageConfig,
    ) -> SegmentedPdfPage:
        """Get page using typed API (zero-copy from C++, faster than get_page).

        This method uses direct typed bindings to C++ objects, avoiding JSON
        serialization/deserialization overhead. Use this for better performance.

        Args:
            page_no: Page number (1-indexed).
            config: Page decoding configuration.

        Returns:
            SegmentedPdfPage with the parsed page data.
        """
        if page_no in self._pages.keys():
            return self._pages[page_no]

        if 1 <= page_no <= self.number_of_pages():
            page_decoder = self._parser.get_page_decoder(
                key=self._key,
                page=page_no - 1,
                config=config,
            )

            if page_decoder is None:
                raise ValueError(f"Failed to decode page {page_no}")

            self._pages[page_no] = self._to_segmented_page_from_decoder(
                page_decoder=page_decoder,
            )
            return self._pages[page_no]

        raise ValueError(
            f"incorrect page_no: {page_no} for key={self._key} (min:1, max:{self.number_of_pages()})"
        )


class DoclingPdfParser:
    def __init__(self, loglevel: str = "fatal"):
        """
        Set the log level using a string label.

        Parameters:
            level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info']
        """
        self.parser = pdf_parser(level=loglevel)

    def set_loglevel(self, loglevel: str):
        """Set the log level using a string label.

        Parameters:
        level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info']
           )")
        """
        self.parser.set_loglevel_with_label(level=loglevel)

    def list_loaded_keys(self) -> List[str]:
        """List the keys of the loaded documents.

        Returns:
            List[str]: A list of keys for the currently loaded documents.
        """
        return self.parser.list_loaded_keys()

    def load(
        self,
        path_or_stream: Union[str, Path, BytesIO],
        lazy: bool = True,
        boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
        password: str | None = None,
    ) -> PdfDocument:

        if isinstance(path_or_stream, str):
            path_or_stream = Path(path_or_stream)

        if isinstance(path_or_stream, Path):
            key = f"key={path_or_stream!s}"  # use filepath as internal handle
            success = self._load_document(
                key=key, filename=str(path_or_stream), password=password
            )

        elif isinstance(path_or_stream, BytesIO):
            hasher = hashlib.sha256(usedforsecurity=False)

            while chunk := path_or_stream.read(8192):
                hasher.update(chunk)
            path_or_stream.seek(0)
            hash = hasher.hexdigest()

            key = f"key={hash}"  # use md5 hash as internal handle
            success = self._load_document_from_bytesio(key=key, data=path_or_stream)

        if success:
            result_doc = PdfDocument(
                parser=self.parser, key=key, boundary_type=boundary_type
            )
            if not lazy:  # eagerly parse the pages at init time if desired
                result_doc.load_all_pages()

            return result_doc
        else:
            raise RuntimeError(f"Failed to load document with key {key}")

    def _load_document(
        self, key: str, filename: str, password: str | None = None
    ) -> bool:
        """Load a document by key and filename.

        Parameters:
            key (str): The unique key to identify the document.
            filename (str): The path to the document file to load.
            password (str, optional): Optional password for password-protected files

        Returns:
            bool: True if the document was successfully loaded, False otherwise.)")
        """
        return self.parser.load_document(
            key=key, filename=filename.encode("utf8"), password=password
        )

    def _load_document_from_bytesio(self, key: str, data: BytesIO) -> bool:
        """Load a document by key from a BytesIO-like object.

        Parameters:
            key (str): The unique key to identify the document.
             bytes_io (Any): A BytesIO-like object containing the document data.

        Returns:
             bool: True if the document was successfully loaded, False otherwise.)")
        """
        return self.parser.load_document_from_bytesio(key=key, bytes_io=data)


class ThreadedPdfParserConfig(BaseModel):
    """Configuration for the threaded PDF parser.

    Attributes:
        loglevel: Logging level ('fatal', 'error', 'warning', 'info').
        threads: Number of worker threads for parallel page decoding.
        max_concurrent_results: Maximum results buffered before workers pause.
        boundary_type: Page boundary used for geometry conversion and page sizing.
        render_config: Optional render configuration for parse-and-render mode.
    """

    model_config = ConfigDict(arbitrary_types_allowed=True)

    loglevel: str = "fatal"
    threads: int = 4
    max_concurrent_results: int = 32
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX
    render_config: RenderConfig | None = None


class PageParseResult:
    """Outcome of one page processed by DoclingThreadedPdfParser."""

    def __init__(
        self,
        raw_result,
        *,
        boundary_type: PdfPageBoundaryType,
        render_config: RenderConfig | None,
    ):
        self._raw = raw_result
        self._boundary_type = boundary_type
        self._render_config = render_config
        self._page: SegmentedPdfPage | None = None
        self._page_decoder: PdfPageDecoder | None = None
        self._default_image: PILImage.Image | None = None

        self.doc_key: str = raw_result.doc_key
        self.page_number: int = raw_result.page_number + 1
        self.success: bool = raw_result.success

        if self.success:
            self._page_decoder, _ = raw_result.get()
            self._timings = _timings_from_decoder(self._page_decoder)
            self.page_width, self.page_height = _page_size_from_decoder(
                self._page_decoder, boundary_type
            )
        else:
            self._timings = Timings()
            self.page_width = 0.0
            self.page_height = 0.0

    @property
    def has_image(self) -> bool:
        """Whether get_image() can return a rendered image for this result."""
        return self._render_config is not None and self.success

    @property
    def error_message(self) -> str:
        """Error description; empty string when successful."""
        if self.success:
            return ""
        return self._raw.error()

    def _require_page_decoder(self) -> PdfPageDecoder:
        if not self.success:
            raise RuntimeError(
                f"Cannot access failed page {self.page_number} for {self.doc_key}: {self.error_message}"
            )
        assert self._page_decoder is not None
        return self._page_decoder

    def get_page(self) -> SegmentedPdfPage:
        """Return the parsed page, converting lazily on first access."""
        if self._page is None:
            self._page = segmented_page_from_decoder(
                page_decoder=self._require_page_decoder(),
                boundary_type=self._boundary_type,
            )
        return self._page

    def get_timings(self) -> Timings:
        """Return structured timing data for this page parse."""
        return self._timings

    def _rendering_config(self) -> RenderConfig:
        if self._render_config is None:
            raise RuntimeError(
                f"Rendered image not available for page {self.page_number} of {self.doc_key}"
            )
        return _copy_render_config(self._render_config)

    def _default_canvas_size(self) -> tuple[int, int]:
        self._require_page_decoder()
        self._rendering_config()
        height, width, _ = self._raw.image_shape
        return width, height

    def _scale_abs_tolerance(self) -> float:
        if self.page_width <= 0 or self.page_height <= 0:
            return 0.0
        return max(0.5 / self.page_width, 0.5 / self.page_height)

    @staticmethod
    def _image_from_bytes(
        raw_bytes: bytes, image_shape: Sequence[int]
    ) -> PILImage.Image:
        height, width, _ = image_shape
        return PILImage.frombuffer(
            "RGBA", (width, height), raw_bytes, "raw", "RGBA", 0, 1
        ).copy()

    def _get_default_image(self) -> PILImage.Image:
        self._require_page_decoder()
        self._rendering_config()

        if self._default_image is None:
            raw_bytes = self._raw.get_image()
            if not raw_bytes:
                raise RuntimeError(
                    f"Rendered image is empty for page {self.page_number} of {self.doc_key}"
                )
            self._default_image = self._image_from_bytes(
                raw_bytes, self._raw.image_shape
            )
        return self._default_image

    def _render_image_at_scale(self, scale: float) -> PILImage.Image:
        page_decoder = self._require_page_decoder()
        render_config = self._rendering_config()
        render_config.scale = scale
        render_config.canvas_width = -1
        render_config.canvas_height = -1
        raw_bytes, image_shape = page_decoder.render_image(render_config)
        if not raw_bytes:
            raise RuntimeError(
                f"Rendered image is empty for page {self.page_number} of {self.doc_key}"
            )
        return self._image_from_bytes(raw_bytes, image_shape)

    def _render_image_at_canvas_size(
        self, canvas_size: tuple[int, int]
    ) -> PILImage.Image:
        page_decoder = self._require_page_decoder()
        render_config = self._rendering_config()
        render_config.scale = -1.0
        render_config.canvas_width, render_config.canvas_height = canvas_size
        raw_bytes, image_shape = page_decoder.render_image(render_config)
        if not raw_bytes:
            raise RuntimeError(
                f"Rendered image is empty for page {self.page_number} of {self.doc_key}"
            )
        return self._image_from_bytes(raw_bytes, image_shape)

    def _crop_image(
        self, image: PILImage.Image, cropbox: BoundingBox | None
    ) -> PILImage.Image:
        if cropbox is None:
            return image
        if self.page_width <= 0 or self.page_height <= 0:
            return image

        cropbox_top_left = cropbox.to_top_left_origin(page_height=self.page_height)
        x_scale = image.width / self.page_width
        y_scale = image.height / self.page_height

        left = max(0, round(cropbox_top_left.l * x_scale))
        top = max(0, round(cropbox_top_left.t * y_scale))
        right = min(image.width, round(cropbox_top_left.r * x_scale))
        bottom = min(image.height, round(cropbox_top_left.b * y_scale))
        return image.crop((left, top, right, bottom))

    def get_image(
        self,
        scale: float | None = None,
        canvas_size: tuple[int, int] | None = None,
        cropbox: BoundingBox | None = None,
    ) -> PILImage.Image:
        """Return the rendered page image."""
        if scale is not None and canvas_size is not None:
            raise ValueError("Provide either scale or canvas_size, not both")

        if scale is None and canvas_size is None:
            image = self._get_default_image()
            return self._crop_image(image, cropbox)

        if scale is not None:
            if scale <= 0:
                raise ValueError(f"scale must be > 0, got {scale}")
            render_config = self._rendering_config()
            if math.isclose(
                scale,
                render_config.scale,
                rel_tol=0.0,
                abs_tol=self._scale_abs_tolerance(),
            ):
                image = self._get_default_image()
            else:
                image = self._render_image_at_scale(scale)
        else:
            assert canvas_size is not None
            if canvas_size[0] <= 0 or canvas_size[1] <= 0:
                raise ValueError(
                    f"canvas_size must contain positive integers, got {canvas_size}"
                )
            if canvas_size == self._default_canvas_size():
                image = self._get_default_image()
            else:
                image = self._render_image_at_canvas_size(canvas_size)

        return self._crop_image(image, cropbox)

    def _export_render_instructions_json(self) -> Dict[str, Any]:
        return self._require_page_decoder().export_render_instructions_json()

    def _export_bitmap_artifacts(self) -> List[Dict[str, Any]]:
        return self._require_page_decoder().export_bitmap_artifacts()


def _copy_decode_config(src: DecodePageConfig) -> DecodePageConfig:
    dst = DecodePageConfig()
    dst.page_boundary = src.page_boundary
    dst.do_sanitization = src.do_sanitization
    dst.keep_char_cells = src.keep_char_cells
    dst.keep_shapes = src.keep_shapes
    dst.keep_bitmaps = src.keep_bitmaps
    dst.max_num_lines = src.max_num_lines
    dst.max_num_bitmaps = src.max_num_bitmaps
    dst.create_word_cells = src.create_word_cells
    dst.create_line_cells = src.create_line_cells
    dst.enforce_same_font = src.enforce_same_font
    dst.horizontal_cell_tolerance = src.horizontal_cell_tolerance
    dst.word_space_width_factor_for_merge = src.word_space_width_factor_for_merge
    dst.line_space_width_factor_for_merge = src.line_space_width_factor_for_merge
    dst.line_space_width_factor_for_merge_with_space = (
        src.line_space_width_factor_for_merge_with_space
    )
    dst.do_thread_safe = src.do_thread_safe
    dst.release_native_memory_every_n_pages = src.release_native_memory_every_n_pages
    dst.keep_glyphs = src.keep_glyphs
    dst.keep_qpdf_warnings = src.keep_qpdf_warnings
    return dst


def _copy_render_config(src: RenderConfig) -> RenderConfig:
    dst = RenderConfig()
    dst.render_text = src.render_text
    dst.draw_text_bbox = src.draw_text_bbox
    dst.resolve_fonts = src.resolve_fonts
    dst.font_similarity_cutoff = src.font_similarity_cutoff
    dst.scale = src.scale
    dst.canvas_width = src.canvas_width
    dst.canvas_height = src.canvas_height
    return dst


def _validate_render_config(src: RenderConfig) -> None:
    have_scale = src.scale > 0
    have_width = src.canvas_width > 0
    have_height = src.canvas_height > 0

    if src.scale != -1.0 and src.scale <= 0:
        raise ValueError("render_config.scale must be > 0 or -1")
    if src.canvas_width != -1 and src.canvas_width <= 0:
        raise ValueError("render_config.canvas_width must be > 0 or -1")
    if src.canvas_height != -1 and src.canvas_height <= 0:
        raise ValueError("render_config.canvas_height must be > 0 or -1")
    if have_scale and (have_width or have_height):
        raise ValueError(
            "render_config.scale cannot be combined with canvas_width or canvas_height"
        )


def _validated_render_config(src: RenderConfig) -> RenderConfig:
    _validate_render_config(src)
    return _copy_render_config(src)


class DoclingThreadedPdfParser:
    """Threaded PDF parser that decodes pages from multiple documents in parallel."""

    def __init__(
        self,
        parser_config: ThreadedPdfParserConfig | None = None,
        decode_config: DecodePageConfig | None = None,
    ):
        if parser_config is None:
            parser_config = ThreadedPdfParserConfig()

        self._parser_config = parser_config
        if parser_config.render_config is not None:
            parser_config.render_config = _validated_render_config(
                parser_config.render_config
            )
        self._decode_config = (
            _copy_decode_config(decode_config)
            if decode_config is not None
            else DecodePageConfig()
        )
        self._decode_config.page_boundary = parser_config.boundary_type.value
        self._page_counts: Dict[str, int] = {}
        self._scheduled_page_counts: Dict[str, int] = {}

        if parser_config.render_config is None:
            self._parser = _threaded_pdf_parser(
                loglevel=parser_config.loglevel,
                num_threads=parser_config.threads,
                max_concurrent_results=parser_config.max_concurrent_results,
                config=self._decode_config,
            )
        else:
            self._parser = _threaded_pdf_renderer(
                loglevel=parser_config.loglevel,
                num_threads=parser_config.threads,
                max_concurrent_results=parser_config.max_concurrent_results,
                decode_config=self._decode_config,
                render_config=parser_config.render_config,
            )

    def load(
        self,
        path_or_stream: Union[str, Path, BytesIO],
        password: str | None = None,
        page_numbers: Sequence[int] | None = None,
    ) -> str:
        """Load a document for parallel processing.

        Parameters:
            path_or_stream: File path or BytesIO object.
            password: Optional password for protected files.
            page_numbers: Optional 1-indexed physical pages to schedule.

        Returns:
            str: The document key.
        """
        if isinstance(path_or_stream, str):
            path_or_stream = Path(path_or_stream)

        if isinstance(path_or_stream, Path):
            key = f"key={path_or_stream!s}"
            success = self._parser.load_document(
                key=key,
                filename=str(path_or_stream).encode("utf8"),
                password=password,
                page_numbers=list(page_numbers) if page_numbers is not None else None,
            )
        elif isinstance(path_or_stream, BytesIO):
            hasher = hashlib.sha256(usedforsecurity=False)
            while chunk := path_or_stream.read(8192):
                hasher.update(chunk)
            path_or_stream.seek(0)
            hash_val = hasher.hexdigest()

            key = f"key={hash_val}"
            success = self._parser.load_document_from_bytesio(
                key=key,
                bytes_io=path_or_stream,
                password=password,
                page_numbers=list(page_numbers) if page_numbers is not None else None,
            )
        else:
            raise TypeError(
                f"Expected str, Path, or BytesIO, got {type(path_or_stream)}"
            )

        if not success:
            raise RuntimeError(f"Failed to load document with key {key}")

        self._page_counts[key] = self._parser.number_of_pages(key)
        self._scheduled_page_counts[key] = self._parser.scheduled_number_of_pages(key)
        return key

    def page_count(self, doc_key: str) -> int:
        """Return the total page count for a loaded document."""
        if doc_key not in self._page_counts:
            raise ValueError(f"Document key not loaded: {doc_key}")
        return self._page_counts[doc_key]

    def scheduled_page_count(self, doc_key: str) -> int:
        """Return the number of pages scheduled for threaded emission."""
        if doc_key not in self._scheduled_page_counts:
            raise ValueError(f"Document key not loaded: {doc_key}")
        return self._scheduled_page_counts[doc_key]

    def unload(self, doc_key: str) -> bool:
        """Unload one document after threaded processing has completed."""
        unloaded = self._parser.unload_document(doc_key)
        self._page_counts.pop(doc_key, None)
        self._scheduled_page_counts.pop(doc_key, None)
        return unloaded

    def unload_all(self) -> None:
        """Unload all documents after threaded processing has completed."""
        self._parser.unload_all_documents()
        self._page_counts.clear()
        self._scheduled_page_counts.clear()

    def has_tasks(self) -> bool:
        """Check if there are remaining tasks to consume.

        On first call, builds the task queue and starts worker threads.

        Returns:
            bool: True if there are remaining results to consume.
        """
        return self._parser.has_tasks()

    def iterate_results(self) -> Iterator["PageParseResult"]:
        """Yield page results in completion order."""
        while self.has_tasks():
            yield self.get_task()

    def get_task(self) -> "PageParseResult":
        """Get the next completed page decode result.

        Blocks until a result is available.

        Returns:
            PageParseResult: Parsed page result with lazy page conversion and optional image access.
        """
        return PageParseResult(
            self._parser.get_task(),
            boundary_type=self._parser_config.boundary_type,
            render_config=self._parser_config.render_config,
        )
