"""Parser for PDF files"""

import hashlib
from io import BytesIO
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Tuple, Union

from docling_core.types.doc.base import BoundingBox, CoordOrigin
from docling_core.types.doc.page import (
    BitmapResource,
    BoundingRectangle,
    Coord2D,
    ParsedPdfDocument,
    PdfLine,
    PdfMetaData,
    PdfPageBoundaryType,
    PdfPageGeometry,
    PdfTableOfContents,
    PdfTextCell,
    SegmentedPdfPage,
    TextCell,
    TextDirection,
)

from docling_parse.pdf_parsers import pdf_parser_v2  # type: ignore[import]
from docling_parse.pdf_parsers import pdf_sanitizer  # type: ignore[import]


class PdfDocument:

    def iterate_pages(
        self,
    ) -> Iterator[Tuple[int, SegmentedPdfPage]]:
        for page_no in range(self.number_of_pages()):
            yield page_no + 1, self.get_page(page_no + 1)

    def __init__(
        self,
        parser: "pdf_parser_v2",
        key: str,
        boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
    ):
        self._parser: pdf_parser_v2 = parser
        self._key = key
        self._boundary_type = boundary_type
        self._pages: Dict[int, SegmentedPdfPage] = {}
        self._toc: Optional[PdfTableOfContents] = None
        self._meta: Optional[PdfMetaData] = None

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

    def _to_table_of_contents(self, toc: dict) -> List[PdfTableOfContents]:

        result = []
        for item in toc:

            subtoc = PdfTableOfContents(text=item["title"])
            if "children" in item:
                subtoc.children = self._to_table_of_contents(toc=item["children"])
            result.append(subtoc)

        return result

    def get_page(
        self,
        page_no: int,
        *,
        create_words: bool = True,
        create_textlines: bool = True,
        enforce_same_font: bool = True,
    ) -> SegmentedPdfPage:
        if page_no in self._pages.keys():
            return self._pages[page_no]
        else:
            if 1 <= page_no <= self.number_of_pages():
                doc_dict = self._parser.parse_pdf_from_key_on_page(
                    key=self._key,
                    page=page_no - 1,
                    page_boundary=self._boundary_type,
                    do_sanitization=False,
                )
                for pi, page in enumerate(
                    doc_dict["pages"]
                ):  # only one page is expected
                    self._pages[page_no] = self._to_segmented_page(
                        page=page["original"],
                        create_words=create_words,
                        create_textlines=create_textlines,
                        enforce_same_font=enforce_same_font,
                    )  # put on cache
                    return self._pages[page_no]

        raise ValueError(
            f"incorrect page_no: {page_no} for key={self._key} (min:1, max:{self.number_of_pages()})"
        )

        return SegmentedPdfPage()

    def load_all_pages(self, create_words: bool = True, create_lines: bool = True):
        doc_dict = self._parser.parse_pdf_from_key(
            key=self._key, page_boundary=self._boundary_type, do_sanitization=False
        )
        for pi, page in enumerate(doc_dict["pages"]):
            assert "original" in page, "'original' in page"

            # will need to be changed once we remove the original/sanitized from C++
            self._pages[pi + 1] = self._to_segmented_page(
                page["original"],
                create_words=create_words,
                create_textlines=create_lines,
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

        result: List[Union[PdfTextCell, TextCell]] = []
        for ind, row in enumerate(data):
            rect = BoundingRectangle(
                r_x0=row[header.index(f"r_x0")],
                r_y0=row[header.index(f"r_y0")],
                r_x1=row[header.index(f"r_x1")],
                r_y1=row[header.index(f"r_y1")],
                r_x2=row[header.index(f"r_x2")],
                r_y2=row[header.index(f"r_y2")],
                r_x3=row[header.index(f"r_x3")],
                r_y3=row[header.index(f"r_y3")],
            )
            cell = PdfTextCell(
                rect=rect,
                text=row[header.index(f"text")],
                orig=row[header.index(f"text")],
                font_key=row[header.index(f"font-key")],
                font_name=row[header.index(f"font-name")],
                widget=row[header.index(f"widget")],
                text_direction=(
                    TextDirection.LEFT_TO_RIGHT
                    if row[header.index(f"left_to_right")]
                    else TextDirection.RIGHT_TO_LEFT
                ),
                index=ind,
                rendering_mode=row[header.index(f"rendering-mode")],
            )
            result.append(cell)

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

    def _to_lines(self, data: dict) -> List[PdfLine]:

        result: List[PdfLine] = []
        for ind, item in enumerate(data):

            for l in range(0, len(item["i"]), 2):
                i0: int = item["i"][l + 0]
                i1: int = item["i"][l + 1]

                points: List[Coord2D] = []
                for k in range(i0, i1):
                    points.append(Coord2D(item["x"][k], item["y"][k]))

                line = PdfLine(
                    index=ind,
                    parent_id=l,
                    points=points,
                )
                result.append(line)

        return result

    def _to_segmented_page(
        self,
        page: dict,
        *,
        create_words: bool,
        create_textlines: bool,
        enforce_same_font: bool = True,
    ) -> SegmentedPdfPage:

        char_cells = self._to_cells(page["cells"])
        segmented_page = SegmentedPdfPage(
            dimension=self._to_page_geometry(page["dimension"]),
            char_cells=char_cells,
            word_cells=[],
            textline_cells=[],
            has_chars=len(char_cells) > 0,
            bitmap_resources=self._to_bitmap_resources(page["images"]),
            lines=self._to_lines(page["lines"]),
        )

        if create_words:
            self._create_word_cells(segmented_page, enforce_same_font=enforce_same_font)

        if create_textlines:
            self._create_textline_cells(
                segmented_page, enforce_same_font=enforce_same_font
            )

        return segmented_page

    def _create_word_cells(
        self,
        segmented_page: SegmentedPdfPage,
        *,
        space_width_factor_for_merge: float = 0.33,
        enforce_same_font: bool = True,
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

        # data = sanitizer.create_word_cells(space_width_factor_for_merge=0.33)
        data = sanitizer.create_word_cells(
            space_width_factor_for_merge=space_width_factor_for_merge,
            enforce_same_font=enforce_same_font,
        )

        segmented_page.word_cells = []
        for item in data:
            cell = PdfTextCell.model_validate(item)
            segmented_page.word_cells.append(cell)

        segmented_page.has_words = len(segmented_page.word_cells) > 0

    def _create_textline_cells(
        self,
        segmented_page: SegmentedPdfPage,
        *,
        space_width_factor_for_merge: float = 1.0,
        space_width_factor_for_merge_with_space: float = 0.33,
        enforce_same_font: bool = True,
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

        # data = sanitizer.create_line_cells()
        data = sanitizer.create_line_cells(
            space_width_factor_for_merge=space_width_factor_for_merge,
            space_width_factor_for_merge_with_space=space_width_factor_for_merge_with_space,
            enforce_same_font=enforce_same_font,
        )

        segmented_page.textline_cells = []
        for item in data:
            cell = PdfTextCell.model_validate(item)
            segmented_page.textline_cells.append(cell)

        segmented_page.has_lines = len(segmented_page.textline_cells) > 0

    def _to_parsed_document(
        self,
        doc_dict: dict,
        page_no: int = 1,
        create_words: bool = False,
        create_lines: bool = True,
    ) -> ParsedPdfDocument:

        parsed_doc = ParsedPdfDocument()

        for pi, page in enumerate(doc_dict["pages"]):
            parsed_doc.pages[page_no + pi] = self._to_segmented_page(
                page["original"],
                create_words=create_words,
                create_textlines=create_lines,
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
        self.parser = pdf_parser_v2(level=loglevel)

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
    ) -> PdfDocument:

        if isinstance(path_or_stream, str):
            path_or_stream = Path(path_or_stream)

        if isinstance(path_or_stream, Path):
            key = f"key={str(path_or_stream)}"  # use filepath as internal handle
            success = self._load_document(key=key, filename=str(path_or_stream))

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

    def _load_document(self, key: str, filename: str) -> bool:
        """Load a document by key and filename.

        Parameters:
            key (str): The unique key to identify the document.
            filename (str): The path to the document file to load.

        Returns:
            bool: True if the document was successfully loaded, False otherwise.)")
        """
        return self.parser.load_document(key=key, filename=filename.encode("utf8"))

    def _load_document_from_bytesio(self, key: str, data: BytesIO) -> bool:
        """Load a document by key from a BytesIO-like object.

        Parameters:
            key (str): The unique key to identify the document.
             bytes_io (Any): A BytesIO-like object containing the document data.

        Returns:
             bool: True if the document was successfully loaded, False otherwise.)")
        """
        return self.parser.load_document_from_bytesio(key=key, bytes_io=data)
