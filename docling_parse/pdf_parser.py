"""Parser for PDF files"""

import hashlib
import logging
from enum import Enum
from io import BytesIO
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Tuple, Union

from docling_core.types.doc.base import BoundingBox, CoordOrigin, ImageRefMode
from docling_core.types.doc.document import ImageRef
from docling_core.types.doc.page import (
    BitmapResource,
    BoundingRectangle,
    ColorRGBA,
    Coord2D,
    ParsedPdfDocument,
    PdfMetaData,
    PdfPageBoundaryType,
    PdfPageGeometry,
    PdfShape,
    PdfTableOfContents,
    PdfTextCell,
    SegmentedPdfPage,
    TextCell,
    TextDirection,
)
from PIL import Image as PILImage
from pydantic import BaseModel, ConfigDict

from docling_parse.pdf_parsers import DecodePageConfig  # type: ignore[import]
from docling_parse.pdf_parsers import pdf_parser  # type: ignore[import]
from docling_parse.pdf_parsers import pdf_sanitizer  # type: ignore[import]
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
    TIMING_KEY_DECODE_PAGE,
    TIMING_KEY_DECODE_RESOURCES,
    TIMING_KEY_DECODE_XOBJECTS,
    TIMING_KEY_EXTRACT_ANNOTS_JSON,
    TIMING_KEY_PROCESS_DOCUMENT_FROM_BYTESIO,
    TIMING_KEY_PROCESS_DOCUMENT_FROM_FILE,
    TIMING_KEY_ROTATE_CONTENTS,
    TIMING_KEY_SANITISE_CONTENTS,
    TIMING_KEY_SANITIZE_CELLS,
    TIMING_KEY_SANITIZE_ORIENTATION,
    TIMING_KEY_TO_JSON_PAGE,
    TIMING_PREFIX_DECODE_FONT,
    TIMING_PREFIX_DECODE_PAGE,
    TIMING_PREFIX_DECODING_PAGE,
    get_decode_page_timing_keys,
    get_static_timing_keys,
    is_static_timing_key,
)

# Configure logging
_log = logging.getLogger(__name__)


class CONVERSION_MODE(Enum):
    JSON = "JSON"
    TYPED = "TYPED"


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
    level: Optional[int] = None
    page: Optional[int] = None
    children: Optional[List["PdfTocEntry"]] = None


class PdfAnnotations(BaseModel):
    """PDF document annotations including form fields, language, metadata, and table of contents.

    Attributes:
        form: AcroForm data containing interactive form fields (raw dict structure). None if no forms present.
        language: Document language code (e.g., 'en-US', 'fr-FR'). None if not specified.
        meta_xml: XMP metadata as XML string. None if no metadata present.
        table_of_contents: Document outline/bookmark structure as list of entries. None if no TOC.
    """

    model_config = ConfigDict(validate_assignment=True, extra="allow")

    form: Optional[Dict[str, Any]] = None
    language: Optional[str] = None
    meta_xml: Optional[str] = None
    table_of_contents: Optional[List[PdfTocEntry]] = None


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
        self._toc: Optional[PdfTableOfContents] = None
        self._meta: Optional[PdfMetaData] = None
        self._annotations: Optional[PdfAnnotations] = None

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
            if page_no in self._pages:
                self._parser.unload_document_page(key=self._key, page=page_no)
                del self._pages[page_no]

    def number_of_pages(self) -> int:
        if self.is_loaded():
            return self._parser.number_of_pages(key=self._key)
        else:
            raise RuntimeError("This document is not loaded.")

    def get_meta(self) -> Optional[PdfMetaData]:

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

    def get_table_of_contents(self) -> Optional[PdfTableOfContents]:
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
        mode: CONVERSION_MODE = CONVERSION_MODE.TYPED,
        config: Optional[DecodePageConfig] = None,
    ) -> Iterator[Tuple[int, SegmentedPdfPage]]:
        if config is None:
            config = self._default_config()
        for page_no in range(self.number_of_pages()):
            yield page_no + 1, self.get_page(
                page_no + 1,
                mode=mode,
                config=config,
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
            if "children" in item and item["children"]:
                entry.children = self._to_pdf_toc_entry(item["children"])
            result.append(entry)
        return result

    def get_annotations(self) -> Optional[PdfAnnotations]:
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
        mode: CONVERSION_MODE = CONVERSION_MODE.TYPED,
        config: Optional[DecodePageConfig] = None,
    ) -> SegmentedPdfPage:
        """Unified page getter. Dispatches to JSON or TYPED pipeline based on mode."""
        if config is None:
            config = self._default_config()
        if mode == CONVERSION_MODE.JSON:
            return self._get_page_json(page_no, config=config)
        else:
            return self._get_page_typed(page_no, config=config)

    def get_page_with_timings(
        self,
        page_no: int,
        *,
        mode: CONVERSION_MODE = CONVERSION_MODE.TYPED,
        config: Optional[DecodePageConfig] = None,
    ) -> Tuple[SegmentedPdfPage, Timings]:
        """Get page along with timing information.

        Similar to get_page() but also returns timing data from the parsing process.
        Useful for performance analysis and benchmarking.

        Note: This method does NOT use the page cache to ensure fresh timing data.

        Args:
            page_no: Page number (1-indexed).
            mode: Conversion mode (JSON or TYPED).
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

        if mode == CONVERSION_MODE.TYPED:
            return self._get_page_with_timings_typed(page_no, config=config)
        else:
            return self._get_page_with_timings_json(page_no, config=config)

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
            config=config,
        )

        # Get timings from the page decoder
        timings_dict = page_decoder.get_timings()
        raw_timings_dict = page_decoder.get_timings_raw()
        timings = Timings(data=dict(timings_dict), raw_data=dict(raw_timings_dict))

        return segmented_page, timings

    def _get_page_with_timings_json(
        self,
        page_no: int,
        *,
        config: DecodePageConfig,
    ) -> Tuple[SegmentedPdfPage, Timings]:
        """Get page with timings using JSON API."""
        doc_dict = self._parser.parse_pdf_from_key_on_page(
            key=self._key,
            page=page_no - 1,
            page_boundary=config.page_boundary,
            do_sanitization=config.do_sanitization,
            keep_char_cells=config.keep_char_cells,
            keep_shapes=config.keep_shapes,
            keep_bitmaps=config.keep_bitmaps,
            create_word_cells=config.create_word_cells,
            create_line_cells=config.create_line_cells,
        )

        # Extract page and timings from doc_dict
        timings_data: Dict[str, float] = {}

        # Get document-level timings
        if "timings" in doc_dict:
            timings_data.update(doc_dict["timings"])

        for page in doc_dict["pages"]:
            # Get page-level timings
            if "timings" in page:
                timings_data.update(page["timings"])

            segmented_page = self._to_segmented_page(
                page=page["original"],
                config=config,
            )

            # Note: JSON mode only provides summed timings, not raw timing vectors
            return segmented_page, Timings(data=timings_data)

        raise ValueError(f"No pages found in document for page {page_no}")

    def _get_page_json(
        self,
        page_no: int,
        *,
        config: DecodePageConfig,
    ) -> SegmentedPdfPage:
        if page_no in self._pages.keys():
            return self._pages[page_no]
        else:
            if 1 <= page_no <= self.number_of_pages():

                doc_dict = self._parser.parse_pdf_from_key_on_page(
                    key=self._key,
                    page=page_no - 1,
                    page_boundary=config.page_boundary,
                    do_sanitization=config.do_sanitization,
                    keep_char_cells=config.keep_char_cells,
                    keep_shapes=config.keep_shapes,
                    keep_bitmaps=config.keep_bitmaps,
                    create_word_cells=config.create_word_cells,
                    create_line_cells=config.create_line_cells,
                )
                for pi, page in enumerate(
                    doc_dict["pages"]
                ):  # only one page is expected
                    print(page.keys())

                    self._pages[page_no] = self._to_segmented_page(
                        page=page["original"],
                        config=config,
                    )  # put on cache
                    return self._pages[page_no]

        raise ValueError(
            f"incorrect page_no: {page_no} for key={self._key} (min:1, max:{self.number_of_pages()})"
        )

        return SegmentedPdfPage()

    def load_all_pages(self, config: Optional[DecodePageConfig] = None):
        if config is None:
            config = self._default_config()
        doc_dict = self._parser.parse_pdf_from_key(
            key=self._key,
            page_boundary=config.page_boundary,
            do_sanitization=config.do_sanitization,
        )
        for pi, page in enumerate(doc_dict["pages"]):
            assert "original" in page, "'original' in page"

            # will need to be changed once we remove the original/sanitized from C++
            self._pages[pi + 1] = self._to_segmented_page(
                page["original"],
                config=config,
            )  # put on cache

    def _to_page_geometry(self, dimension: dict) -> PdfPageGeometry:

        boundary_type: PdfPageBoundaryType = PdfPageBoundaryType(
            dimension["page_boundary"]
        )

        art_bbox = BoundingBox(
            l=dimension["rectangles"]["art-bbox"][0],
            b=dimension["rectangles"]["art-bbox"][1],
            r=dimension["rectangles"]["art-bbox"][2],
            t=dimension["rectangles"]["art-bbox"][3],
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        media_bbox = BoundingBox(
            l=dimension["rectangles"]["media-bbox"][0],
            b=dimension["rectangles"]["media-bbox"][1],
            r=dimension["rectangles"]["media-bbox"][2],
            t=dimension["rectangles"]["media-bbox"][3],
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        bleed_bbox = BoundingBox(
            l=dimension["rectangles"]["bleed-bbox"][0],
            b=dimension["rectangles"]["bleed-bbox"][1],
            r=dimension["rectangles"]["bleed-bbox"][2],
            t=dimension["rectangles"]["bleed-bbox"][3],
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        trim_bbox = BoundingBox(
            l=dimension["rectangles"]["trim-bbox"][0],
            b=dimension["rectangles"]["trim-bbox"][1],
            r=dimension["rectangles"]["trim-bbox"][2],
            t=dimension["rectangles"]["trim-bbox"][3],
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        crop_bbox = BoundingBox(
            l=dimension["rectangles"]["crop-bbox"][0],
            b=dimension["rectangles"]["crop-bbox"][1],
            r=dimension["rectangles"]["crop-bbox"][2],
            t=dimension["rectangles"]["crop-bbox"][3],
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        # Fixme: The boundary type to which this rect refers should accept a user argument
        # TODO: Why is this a BoundingRectangle not a BoundingBox?
        rect = BoundingRectangle(
            r_x0=crop_bbox.l,
            r_y0=crop_bbox.b,
            r_x1=crop_bbox.r,
            r_y1=crop_bbox.b,
            r_x2=crop_bbox.r,
            r_y2=crop_bbox.t,
            r_x3=crop_bbox.l,
            r_y3=crop_bbox.t,
            coord_origin=CoordOrigin.BOTTOMLEFT,
        )

        return PdfPageGeometry(
            angle=dimension["angle"],
            boundary_type=boundary_type,
            rect=rect,
            art_bbox=art_bbox,
            media_bbox=media_bbox,
            trim_bbox=trim_bbox,
            crop_bbox=crop_bbox,
            bleed_bbox=bleed_bbox,
        )

    def _to_cells(self, cells: dict) -> List[Union[PdfTextCell, TextCell]]:
        assert "data" in cells, '"data" in cells'
        assert "header" in cells, '"header" in cells'

        data = cells["data"]
        header = cells["header"]

        # Pre-compute header indices as local variables
        r_x0_idx = header.index("r_x0")
        r_y0_idx = header.index("r_y0")
        r_x1_idx = header.index("r_x1")
        r_y1_idx = header.index("r_y1")
        r_x2_idx = header.index("r_x2")
        r_y2_idx = header.index("r_y2")
        r_x3_idx = header.index("r_x3")
        r_y3_idx = header.index("r_y3")
        text_idx = header.index("text")
        font_key_idx = header.index("font-key")
        font_name_idx = header.index("font-name")
        widget_idx = header.index("widget")
        left_to_right_idx = header.index("left_to_right")
        rendering_mode_idx = header.index("rendering-mode")

        # Pre-allocate list with exact size
        data_len = len(data)
        result: List[Union[PdfTextCell, TextCell]] = [None] * data_len  # type: ignore

        for ind, row in enumerate(data):
            rect = BoundingRectangle(
                r_x0=row[r_x0_idx],
                r_y0=row[r_y0_idx],
                r_x1=row[r_x1_idx],
                r_y1=row[r_y1_idx],
                r_x2=row[r_x2_idx],
                r_y2=row[r_y2_idx],
                r_x3=row[r_x3_idx],
                r_y3=row[r_y3_idx],
            )

            result[ind] = PdfTextCell(
                rect=rect,
                text=row[text_idx],
                orig=row[text_idx],
                font_key=row[font_key_idx],
                font_name=row[font_name_idx],
                widget=row[widget_idx],
                text_direction=(
                    TextDirection.LEFT_TO_RIGHT
                    if row[left_to_right_idx]
                    else TextDirection.RIGHT_TO_LEFT
                ),
                index=ind,
                rendering_mode=row[rendering_mode_idx],
            )

        return result

    def _to_bitmap_resources(self, images: dict) -> List[BitmapResource]:

        assert "data" in images, '"data" in images'
        assert "header" in images, '"header" in images'

        data = images["data"]
        header = images["header"]

        result: List[BitmapResource] = []
        for ind, row in enumerate(data):
            rect = BoundingRectangle(
                r_x0=row[header.index(f"x0")],
                r_y0=row[header.index(f"y0")],
                r_x1=row[header.index(f"x1")],
                r_y1=row[header.index(f"y0")],
                r_x2=row[header.index(f"x1")],
                r_y2=row[header.index(f"y1")],
                r_x3=row[header.index(f"x0")],
                r_y3=row[header.index(f"y1")],
            )
            image = BitmapResource(index=ind, rect=rect, uri=None)
            result.append(image)

        return result

    def _to_shapes(self, data: dict) -> List[PdfShape]:

        result: List[PdfShape] = []
        for ind, item in enumerate(data):

            for l in range(0, len(item["i"]), 2):
                i0: int = item["i"][l + 0]
                i1: int = item["i"][l + 1]

                points: List[Coord2D] = []
                for k in range(i0, i1):
                    points.append(Coord2D(item["x"][k], item["y"][k]))

                has_gs = item.get("has-graphics-state", False)
                rgb_s = item.get("rgb-stroking", [0, 0, 0])
                rgb_f = item.get("rgb-filling", [0, 0, 0])

                shape = PdfShape(
                    index=ind,
                    parent_id=l,
                    points=points,
                    has_graphics_state=has_gs,
                    line_width=item.get("line-width", -1.0),
                    miter_limit=item.get("miter-limit", -1.0),
                    line_cap=item.get("line-cap", -1),
                    line_join=item.get("line-join", -1),
                    dash_phase=item.get("dash-phase", 0.0),
                    dash_array=item.get("dash-array", []),
                    flatness=item.get("flatness", -1.0),
                    rgb_stroking=ColorRGBA(r=rgb_s[0], g=rgb_s[1], b=rgb_s[2]),
                    rgb_filling=ColorRGBA(r=rgb_f[0], g=rgb_f[1], b=rgb_f[2]),
                )
                result.append(shape)

        return result

    def _to_segmented_page(
        self,
        page: dict,
        *,
        config: DecodePageConfig,
    ) -> SegmentedPdfPage:

        char_cells = self._to_cells(page["cells"]) if "cells" in page else []
        shapes = self._to_shapes(page["shapes"]) if "shapes" in page else []
        bitmap_resources = (
            self._to_bitmap_resources(page["images"]) if "images" in page else []
        )

        segmented_page = SegmentedPdfPage(
            dimension=self._to_page_geometry(page["dimension"]),
            char_cells=char_cells,
            word_cells=[],
            textline_cells=[],
            has_chars=len(char_cells) > 0,
            bitmap_resources=bitmap_resources,
            shapes=shapes,
        )

        if config.create_word_cells and ("word_cells" in page):
            segmented_page.word_cells = self._to_cells(page["word_cells"])
            segmented_page.has_words = len(segmented_page.word_cells) > 0
        elif config.create_word_cells and config.keep_char_cells:
            _log.warning(
                "`words` will be created for segmented_page in an inefficient way!"
            )
            self._create_word_cells(segmented_page, config)

        if config.create_line_cells and ("line_cells" in page):
            segmented_page.textline_cells = self._to_cells(page["line_cells"])
            segmented_page.has_lines = len(segmented_page.textline_cells) > 0
        elif config.create_line_cells and config.keep_char_cells:
            _log.warning(
                "`text_lines` will be created for segmented_page in an inefficient way!"
            )
            self._create_textline_cells(segmented_page, config)

        return segmented_page

    # ============= Typed API Methods (zero-copy from C++) =============

    def _to_page_geometry_from_decoder(self, page_dim) -> PdfPageGeometry:
        """Convert typed PdfPageDimension to PdfPageGeometry."""
        crop_bbox = page_dim.get_crop_bbox()
        media_bbox = page_dim.get_media_bbox()
        angle = page_dim.get_angle()

        # Use crop_box as default boundary
        bbox = crop_bbox
        # Build page rectangle as a BoundingRectangle (typed API expects this)
        rect = BoundingRectangle(
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
        art_bbox_obj = BoundingBox(
            l=crop_bbox[0], b=crop_bbox[1], r=crop_bbox[2], t=crop_bbox[3]
        )
        media_bbox_obj = BoundingBox(
            l=media_bbox[0], b=media_bbox[1], r=media_bbox[2], t=media_bbox[3]
        )
        crop_bbox_obj = BoundingBox(
            l=crop_bbox[0], b=crop_bbox[1], r=crop_bbox[2], t=crop_bbox[3]
        )

        return PdfPageGeometry(
            angle=angle,
            boundary_type=PdfPageBoundaryType(self._boundary_type),
            rect=rect,
            art_bbox=art_bbox_obj,
            media_bbox=media_bbox_obj,
            trim_bbox=crop_bbox_obj,
            crop_bbox=crop_bbox_obj,
            bleed_bbox=crop_bbox_obj,
        )

    def _to_cells_from_decoder(
        self, cells_container
    ) -> List[Union[PdfTextCell, TextCell]]:
        """Convert typed PdfCells container to list of PdfTextCell objects."""
        result: List[Union[PdfTextCell, TextCell]] = []

        for ind, cell in enumerate(cells_container):
            rect = BoundingRectangle(
                r_x0=cell.r_x0,
                r_y0=cell.r_y0,
                r_x1=cell.r_x1,
                r_y1=cell.r_y1,
                r_x2=cell.r_x2,
                r_y2=cell.r_y2,
                r_x3=cell.r_x3,
                r_y3=cell.r_y3,
            )

            result.append(
                PdfTextCell(
                    rect=rect,
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

    def _to_shapes_from_decoder(self, shapes_container) -> List[PdfShape]:
        """Convert typed PdfShapes container to list of PdfShape objects."""
        result: List[PdfShape] = []

        for ind, shape in enumerate(shapes_container):
            x_coords = shape.get_x()
            y_coords = shape.get_y()
            indices = shape.get_i()

            for l in range(0, len(indices), 2):
                i0: int = indices[l + 0]
                i1: int = indices[l + 1]

                points: List[Coord2D] = []
                for k in range(i0, i1):
                    points.append(Coord2D(x_coords[k], y_coords[k]))

                rgb_s = shape.get_rgb_stroking_ops()
                rgb_f = shape.get_rgb_filling_ops()

                pdf_shape = PdfShape(
                    index=ind,
                    parent_id=l,
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
                result.append(pdf_shape)

        return result

    def _to_bitmap_resources_from_decoder(
        self, images_container
    ) -> List[BitmapResource]:
        """Convert typed PdfImages container to list of BitmapResource objects."""
        result: List[BitmapResource] = []

        for ind, image in enumerate(images_container):
            rect = BoundingRectangle(
                r_x0=image.x0,
                r_y0=image.y0,
                r_x1=image.x1,
                r_y1=image.y0,
                r_x2=image.x1,
                r_y2=image.y1,
                r_x3=image.x0,
                r_y3=image.y1,
            )

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
                            pil_image = PILImage.frombytes(
                                pil_mode, (w, h), image_bytes
                            )

                    if pil_image is not None:
                        # Normalize to RGBA for consistent downstream handling
                        if pil_image.mode != "RGBA":
                            pil_image = pil_image.convert("RGBA")

                        # Compute DPI from pixel dimensions and PDF bbox
                        bbox_width = abs(image.x1 - image.x0)
                        if bbox_width > 0 and image.image_width > 0:
                            dpi = int(round(image.image_width * 72.0 / bbox_width))
                        else:
                            dpi = 72

                        image_ref = ImageRef.from_pil(pil_image, dpi=dpi)
                        mode = ImageRefMode.EMBEDDED

            except Exception:
                _log.debug(
                    "Failed to extract image data for bitmap %d, "
                    "falling back to placeholder",
                    ind,
                    exc_info=True,
                )

            bitmap = BitmapResource(
                index=ind, rect=rect, uri=None, image=image_ref, mode=mode
            )
            result.append(bitmap)

        return result

    def _to_segmented_page_from_decoder(
        self,
        page_decoder,
        *,
        config: DecodePageConfig,
    ) -> SegmentedPdfPage:
        """Convert typed PdfPageDecoder to SegmentedPdfPage (zero-copy path)."""

        char_cells = self._to_cells_from_decoder(page_decoder.get_char_cells())
        shapes = self._to_shapes_from_decoder(page_decoder.get_page_shapes())
        bitmap_resources = self._to_bitmap_resources_from_decoder(
            page_decoder.get_page_images()
        )

        segmented_page = SegmentedPdfPage(
            dimension=self._to_page_geometry_from_decoder(
                page_decoder.get_page_dimension()
            ),
            char_cells=char_cells,
            word_cells=[],
            textline_cells=[],
            has_chars=len(char_cells) > 0,
            bitmap_resources=bitmap_resources,
            shapes=shapes,
        )

        if page_decoder.has_word_cells():
            segmented_page.word_cells = self._to_cells_from_decoder(
                page_decoder.get_word_cells()
            )
            segmented_page.has_words = len(segmented_page.word_cells) > 0

        if page_decoder.has_line_cells():
            segmented_page.textline_cells = self._to_cells_from_decoder(
                page_decoder.get_line_cells()
            )
            segmented_page.has_lines = len(segmented_page.textline_cells) > 0

        return segmented_page

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
                config=config,
            )
            return self._pages[page_no]

        raise ValueError(
            f"incorrect page_no: {page_no} for key={self._key} (min:1, max:{self.number_of_pages()})"
        )

    def _create_word_cells(
        self,
        segmented_page: SegmentedPdfPage,
        config: DecodePageConfig,
        _loglevel: str = "fatal",
    ):
        if len(segmented_page.word_cells) > 0:
            return

        sanitizer = pdf_sanitizer(level=_loglevel)

        char_data = []
        for item in segmented_page.char_cells:
            item_dict = item.model_dump(mode="json", by_alias=True, exclude_none=True)
            item_dict["left_to_right"] = (
                item.text_direction == TextDirection.LEFT_TO_RIGHT
            )
            char_data.append(item_dict)

        sanitizer.set_char_cells(data=char_data)

        data = sanitizer.create_word_cells(config=config)

        segmented_page.word_cells = []
        for item in data:
            cell = PdfTextCell.model_validate(item)
            segmented_page.word_cells.append(cell)

        segmented_page.has_words = len(segmented_page.word_cells) > 0

    def _create_textline_cells(
        self,
        segmented_page: SegmentedPdfPage,
        config: DecodePageConfig,
        _loglevel: str = "fatal",
    ):
        if len(segmented_page.textline_cells) > 0:
            return

        sanitizer = pdf_sanitizer(level=_loglevel)

        char_data = []
        for item in segmented_page.char_cells:
            item_dict = item.model_dump(mode="json", by_alias=True, exclude_none=True)

            # TODO changing representation for the C++ parser, need to update on C++ code.
            item_dict["left_to_right"] = (
                item.text_direction == TextDirection.LEFT_TO_RIGHT
            )
            item_dict["id"] = item.index

            char_data.append(item_dict)

        sanitizer.set_char_cells(data=char_data)

        data = sanitizer.create_line_cells(config=config)

        segmented_page.textline_cells = []
        for item in data:
            cell = PdfTextCell.model_validate(item)
            segmented_page.textline_cells.append(cell)

        segmented_page.has_lines = len(segmented_page.textline_cells) > 0

    def _to_parsed_document(
        self,
        doc_dict: dict,
        page_no: int = 1,
        config: Optional[DecodePageConfig] = None,
    ) -> ParsedPdfDocument:

        if config is None:
            config = self._default_config()

        parsed_doc = ParsedPdfDocument()

        for pi, page in enumerate(doc_dict["pages"]):
            parsed_doc.pages[page_no + pi] = self._to_segmented_page(
                page["original"],
                config=config,
            )

        return parsed_doc


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
        password: Optional[str] = None,
    ) -> PdfDocument:

        if isinstance(path_or_stream, str):
            path_or_stream = Path(path_or_stream)

        if isinstance(path_or_stream, Path):
            key = f"key={str(path_or_stream)}"  # use filepath as internal handle
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
        self, key: str, filename: str, password: Optional[str] = None
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
