# SPDX-FileCopyrightText: The Docling Contributors
# SPDX-License-Identifier: MIT
# Standalone backend-default replay. No imports from the docling package.
# Inspected Docling revision: 07154cdc82500058ad9377bac92beed3de820458
# Source links (also available under .references/docling/docling/):
# https://github.com/docling-project/docling/blob/07154cdc82500058ad9377bac92beed3de820458/docling/backend/pypdfium2_backend.py
# https://github.com/docling-project/docling/blob/07154cdc82500058ad9377bac92beed3de820458/docling/backend/docling_parse_backend.py
# Settings: docling/document_converter.py (PdfFormatOption),
# docling/datamodel/{backend_options,pipeline_options,accelerator_options}.py
# Sequence: docling/models/stages/page_preprocessing/page_preprocessing_model.py
#
# Setting                         | Docling-default workload
# Requested page-image scale      | 1.0
# PDFium rendering                | scale * 1.5, copy PIL image, resize to request
# docling-parse rendering         | enabled; native defaults, scale 1.0; RGB conversion
# Character cells                 | compute; do not materialize character models
# Word and line cells             | compute and materialize both
# Shapes                          | compute while rendering; do not materialize
# Bitmaps                         | materialize bounds; include_bitmap_images=False
# Font-boundary enforcement       | enforce_same_font=True
# Parser threads                  | accelerator environment, otherwise 4
# Result queue                    | inherit parser default (inspected: 32)
# Native-memory release           | every 128 pages
# Reading-order separators        | shape-line and connected-shape queries enabled
# Outline/structure               | heading hierarchy disabled; no request by default
#
# Sequence: acquire page, size, cached image(scale=1), separator geometry,
# segmented page; bitmap bounds are an explicit extra output-contract request.
# Repeated same-scale images reuse the page cache. Only one page is retained.
# Native decoding needed for rendering stays timed even without image-byte export.
# Native word/line normalization, coordinates and RGB conversion are timed.
# PDFium character selection/merging and page models below are adapted from the
# linked MIT source. Its default shape-line query returns None; connected shapes
# use path-object bounds. Segmentation and raster pixels differ between engines.
#
# Boundary: backend work only, not full conversion or model inference. Conditional
# calls outside this trace include OCR image crops/bitmap queries, content
# intersections, and repeated table/text getters. These require a measured trace
# before claiming full-pipeline backend coverage. Framework construction overhead
# is excluded; this standalone replay is not validated as exact backend parity.
# Defaults are pinned to the revision above, not resolved by importing Docling.
# Imported parser defaults and relevant environment overrides are exported.
# Benchmark CLI defaults: HF performance dataset; PDFium once; native threads 1,4,8.
# --threads 1,4,8 changes only native thread count (scaling-override label).
# --threads default replays accelerator environment resolution (otherwise 4).
# Legacy scale-2/queue-64/no-release/no-words/no-shapes results remain diagnostic
# and belong to run_performance_benchmarking.py, which is deliberately unchanged.
# Run each backend/profile in a fresh process for comparable peak RSS.
# Imports/provenance are outside timing; metadata consumption/streaming is inside.
# Multiple --threads values share warmed state and cumulative process peak RSS.
# Example: python this_script.py --selection selection.json --dataset-root /pdfs \
#   --backend threaded --threads 4 --dataset-revision REV --output result.json

from __future__ import annotations

import argparse
import collections
import ctypes
import hashlib
import importlib.metadata
import json
import logging
import os
import platform
import random
import resource
import subprocess
import sys
import threading
import time
from bisect import bisect_left, bisect_right
from collections.abc import Iterable
from dataclasses import dataclass
from datetime import datetime
from importlib.metadata import version
from pathlib import Path
from typing import Any, Dict, List, Optional

import pypdfium2 as pdfium
import pypdfium2.raw as pdfium_c
from docling_core.types.doc import BoundingBox, CoordOrigin, Size
from docling_core.types.doc.page import (
    BoundingRectangle,
    PdfCellRenderingMode,
    PdfPageBoundaryType,
    PdfPageGeometry,
    PdfTextCell,
    SegmentedPdfPage,
    TextCell,
)
from PIL import Image
from pypdfium2 import PdfTextPage
from pypdfium2._helpers.misc import PdfiumError
from rtree import index
from tqdm import tqdm

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingThreadedPdfParser,
    PageParseResult,
    RenderConfig,
    ThreadedPdfParserConfig,
)

pypdfium2_lock = threading.Lock()
_log = logging.getLogger(__name__)
_PYPDFIUM2_MAJOR_VERSION = int(version("pypdfium2").split(".")[0])
_INVISIBLE_TEXT_RENDER_MODES = frozenset(
    {
        pdfium_c.FPDF_TEXTRENDERMODE_INVISIBLE,
        pdfium_c.FPDF_TEXTRENDERMODE_CLIP,
    }
)
_INVISIBLE_RENDERING_MODES = frozenset(
    {
        PdfCellRenderingMode.INVISIBLE,
        PdfCellRenderingMode.ONLY_CLIPPING,
    }
)


class ManagedPdfiumPageBackend:
    def unload(self):
        self._close_native_page()

    def get_shape_lines(self):
        return None


class PdfPageBackend:
    pass


def _merge_overlapping_boxes(
    boxes: List[BoundingBox], tolerance: float
) -> List[BoundingBox]:
    """Merge boxes that overlap (within ``tolerance``) into their connected components.

    All boxes must share the top-left origin. An R-tree keeps this near-linear: pages of
    vector art routinely carry thousands of path objects.
    """
    if not boxes:
        return []

    def _query(bbox: BoundingBox) -> tuple[float, float, float, float]:
        return (
            bbox.l - tolerance,
            bbox.t - tolerance,
            bbox.r + tolerance,
            bbox.b + tolerance,
        )

    prop = index.Property()
    prop.dimension = 2
    tree = index.Index(properties=prop)
    for i, bbox in enumerate(boxes):
        tree.insert(i, (bbox.l, bbox.t, bbox.r, bbox.b))

    merged: List[BoundingBox] = []
    visited: set[int] = set()
    for start in range(len(boxes)):
        if start in visited:
            continue

        visited.add(start)
        stack = [start]
        left, top, right, bottom = (
            boxes[start].l,
            boxes[start].t,
            boxes[start].r,
            boxes[start].b,
        )
        while stack:
            current = boxes[stack.pop()]
            left = min(left, current.l)
            top = min(top, current.t)
            right = max(right, current.r)
            bottom = max(bottom, current.b)
            for neighbor in tree.intersection(_query(current)):
                if neighbor not in visited:
                    visited.add(neighbor)
                    stack.append(neighbor)

        merged.append(
            BoundingBox(
                l=left, t=top, r=right, b=bottom, coord_origin=CoordOrigin.TOPLEFT
            )
        )

    return merged


def get_pdf_page_geometry(
    ppage: pdfium.PdfPage,
    angle: float = 0.0,
    boundary_type: PdfPageBoundaryType = PdfPageBoundaryType.CROP_BOX,
) -> PdfPageGeometry:
    """
    Create PdfPageGeometry from a pypdfium2 PdfPage object.

    Args:
        ppage: pypdfium2 PdfPage object
        angle: Page rotation angle in degrees (default: 0.0)
        boundary_type: The boundary type for the page (default: CROP_BOX)

    Returns:
        PdfPageGeometry with all the different bounding boxes properly set
    """
    with pypdfium2_lock:
        # Get the main bounding box (intersection of crop_box and media_box)
        bbox_tuple = ppage.get_bbox()
        bbox = BoundingBox.from_tuple(bbox_tuple, CoordOrigin.BOTTOMLEFT)

        # Get all the different page boxes from pypdfium2
        media_box_tuple = ppage.get_mediabox()
        crop_box_tuple = ppage.get_cropbox()
        art_box_tuple = ppage.get_artbox()
        bleed_box_tuple = ppage.get_bleedbox()
        trim_box_tuple = ppage.get_trimbox()

        # Convert to BoundingBox objects using existing from_tuple method
        # pypdfium2 returns (x0, y0, x1, y1) in PDF coordinate system (bottom-left origin)
        # Use bbox as fallback when specific box types are not defined
        media_bbox = (
            BoundingBox.from_tuple(media_box_tuple, CoordOrigin.BOTTOMLEFT)
            if media_box_tuple
            else bbox
        )
        crop_bbox = (
            BoundingBox.from_tuple(crop_box_tuple, CoordOrigin.BOTTOMLEFT)
            if crop_box_tuple
            else bbox
        )
        art_bbox = (
            BoundingBox.from_tuple(art_box_tuple, CoordOrigin.BOTTOMLEFT)
            if art_box_tuple
            else bbox
        )
        bleed_bbox = (
            BoundingBox.from_tuple(bleed_box_tuple, CoordOrigin.BOTTOMLEFT)
            if bleed_box_tuple
            else bbox
        )
        trim_bbox = (
            BoundingBox.from_tuple(trim_box_tuple, CoordOrigin.BOTTOMLEFT)
            if trim_box_tuple
            else bbox
        )

        return PdfPageGeometry(
            angle=angle,
            rect=BoundingRectangle.from_bounding_box(bbox),
            boundary_type=boundary_type,
            art_bbox=art_bbox,
            bleed_bbox=bleed_bbox,
            crop_bbox=crop_bbox,
            media_bbox=media_bbox,
            trim_bbox=trim_bbox,
        )


def _rect_to_display_frame(
    rect: tuple[float, float, float, float],
    rotation: int,
    page_size: Size,
    origin: tuple[float, float],
) -> tuple[float, float, float, float]:
    """Map a rect from the page's unrotated frame to its rotated display frame.

    PDFium reports page-object and text coordinates in the unrotated PDF user
    space, ignoring the page's ``/Rotate`` entry and the position of its visible
    box. ``PdfPage.get_size()`` and the rendered page bitmap, on the other hand,
    are relative to the visible box (``PdfPage.get_bbox()``) and already in the
    rotated display frame. Moving the rect to the box origin and applying the
    rotation here puts everything the backend returns into that single frame.

    Args:
        rect: ``(x0, y0, x1, y1)`` in the unrotated frame, bottom-left origin.
        rotation: page rotation in degrees (``PdfPage.get_rotation()``).
        page_size: page size in the display frame (``get_size()``).
        origin: lower-left corner of the visible page box in the unrotated
            frame (``PdfPage.get_bbox()[:2]``).

    Returns:
        ``(x0, y0, x1, y1)`` in the display frame, bottom-left origin.
    """
    x0, y0, x1, y1 = rect
    ox, oy = origin
    x0, y0, x1, y1 = x0 - ox, y0 - oy, x1 - ox, y1 - oy
    if rotation == 90:
        return (y0, page_size.height - x1, y1, page_size.height - x0)
    elif rotation == 180:
        return (
            page_size.width - x1,
            page_size.height - y1,
            page_size.width - x0,
            page_size.height - y0,
        )
    elif rotation == 270:
        return (page_size.width - y1, x0, page_size.width - y0, x1)
    return (x0, y0, x1, y1)


def _rect_to_pdf_frame(
    rect: tuple[float, float, float, float],
    rotation: int,
    page_size: Size,
    origin: tuple[float, float],
) -> tuple[float, float, float, float]:
    """Map a rect from the rotated display frame back to the unrotated frame.

    Inverse of :func:`_rect_to_display_frame`, needed whenever coordinates are
    handed back to PDFium (e.g. ``PdfTextPage.get_text_bounded()``), which only
    understands the unrotated frame.

    Args:
        rect: ``(x0, y0, x1, y1)`` in the display frame, bottom-left origin.
        rotation: page rotation in degrees (``PdfPage.get_rotation()``).
        page_size: page size in the display frame (``get_size()``).
        origin: lower-left corner of the visible page box in the unrotated
            frame (``PdfPage.get_bbox()[:2]``).

    Returns:
        ``(x0, y0, x1, y1)`` in the unrotated frame, bottom-left origin.
    """
    x0, y0, x1, y1 = rect
    if rotation == 90:
        x0, y0, x1, y1 = (page_size.height - y1, x0, page_size.height - y0, x1)
    elif rotation == 180:
        x0, y0, x1, y1 = (
            page_size.width - x1,
            page_size.height - y1,
            page_size.width - x0,
            page_size.height - y0,
        )
    elif rotation == 270:
        x0, y0, x1, y1 = (y0, page_size.width - x1, y1, page_size.width - x0)
    ox, oy = origin
    return (x0 + ox, y0 + oy, x1 + ox, y1 + oy)


@dataclass(frozen=True)
class _PageChars:
    """The characters of a PDFium text page, to select them by area.

    ``PdfTextPage.get_text_bounded()`` selects every character whose box touches
    the query rect. In tightly set lines the boxes of neighbouring lines overlap:
    descenders (g, j, p, y, ą, ę) reach down into the next line and accents on
    capitals (Ó, Ś, Ć, Ź) reach up into the previous one. A query for one line then
    also returns these letters of its neighbours. ``text_in_rect`` selects a
    character only when the centre of its box lies in the rect, and builds the
    text with the same spacing and line-break rules as PDFium.

    Attributes:
        units: UTF-16 code unit of each character, by PDFium character index.
        baselines: y of each character's origin, by PDFium character index.
        centres: ``(y, x, index)`` of the box centre of each character whose box
            is not empty, sorted by y.
        centre_ys: The y of each entry in ``centres``, for bisection.
    """

    units: list[int]
    baselines: list[float]
    centres: list[tuple[float, float, int]]
    centre_ys: list[float]

    @classmethod
    def from_text_page(cls, text_page: PdfTextPage) -> _PageChars:
        """Read every character of ``text_page``. The caller holds the PDFium lock."""
        units: list[int] = []
        baselines: list[float] = []
        centres: list[tuple[float, float, int]] = []
        origin_x = ctypes.c_double()
        origin_y = ctypes.c_double()
        for char_index in range(text_page.count_chars()):
            units.append(pdfium_c.FPDFText_GetUnicode(text_page.raw, char_index))
            pdfium_c.FPDFText_GetCharOrigin(
                text_page.raw, char_index, origin_x, origin_y
            )
            baselines.append(origin_y.value)
            left, bottom, right, top = text_page.get_charbox(char_index)
            # PDFium never selects a character with an empty box (e.g. a
            # generated line break), so it gets no centre here.
            if right > left and top > bottom:
                centres.append(((bottom + top) / 2, (left + right) / 2, char_index))
        centres.sort()
        return cls(
            units=units,
            baselines=baselines,
            centres=centres,
            centre_ys=[y for y, _, _ in centres],
        )

    def text_in_rect(self, rect: tuple[float, float, float, float]) -> str:
        """Text of the characters whose box centre lies in ``rect``.

        Args:
            rect: ``(left, bottom, right, top)`` in the unrotated PDF frame, as
                ``PdfTextPage.get_text_bounded()`` takes it.

        Returns:
            The text in character order. As in ``FPDFText_GetBoundedText``, a
            space that directly follows a selected character is kept, and CRLF
            separates selected characters on different baselines.
        """
        left, bottom, right, top = rect
        candidates = self.centres[
            bisect_left(self.centre_ys, bottom) : bisect_right(self.centre_ys, top)
        ]
        selected = {char_index for _, x, char_index in candidates if left <= x <= right}
        if not selected:
            return ""

        first = min(selected)
        pieces: list[str] = []
        kept_previous = False
        # PDFium walks the page from its first character: the characters before
        # `first` are not selected, so they request a line break.
        line_break = first > 0
        line_y = 0.0
        for char_index in range(first, min(max(selected) + 2, len(self.units))):
            unit = self.units[char_index]
            if char_index in selected:
                if (
                    line_break
                    and not kept_previous
                    and self.baselines[char_index] != line_y
                ):
                    line_y = self.baselines[char_index]
                    if pieces:
                        pieces.append("\r\n")
                kept_previous = True
                line_break = False
                if unit:
                    pieces.append(chr(unit))
            elif unit == ord(" "):
                if kept_previous:
                    pieces.append(" ")
                    kept_previous = False
                    line_break = False
            else:
                kept_previous = False
                line_break = True
        # A character outside the BMP arrives as two surrogate code units.
        return (
            "".join(pieces)
            .encode("utf-16-le", "surrogatepass")
            .decode("utf-16-le", "ignore")
        )


class PyPdfiumPageBackend(ManagedPdfiumPageBackend):
    def __init__(
        self,
        pdfium_doc: pdfium.PdfDocument,
        document_hash: str,
        page_no: int,
    ):
        super().__init__()
        self._page_no = page_no
        # Note: lock applied by the caller
        self.valid = True  # No better way to tell from pypdfium.
        self._ppage: pdfium.PdfPage | None = None
        try:
            self._ppage = pdfium_doc[page_no]
        except PdfiumError:
            _log.info(
                f"An exception occurred when loading page {page_no} of document {document_hash}.",
                exc_info=True,
            )
            self.valid = False
        self.text_page: PdfTextPage | None = None
        self._seg_page: SegmentedPdfPage | None = None
        # Page-object bboxes keyed by FPDF_PAGEOBJ_* type. Each type's list is built
        # on its first request by ``_get_object_bucket`` and then shared by every
        # object-rect query, so a caller that only asks for images never pays for
        # the text or path objects.
        self._object_index: dict[int, List[tuple[BoundingBox, bool]]] = {}

    def is_valid(self) -> bool:
        return self.valid

    @property
    def page_no(self) -> int:
        return self._page_no + 1

    def _require_page(self) -> pdfium.PdfPage:
        assert self._ppage is not None, "Page backend was unloaded."
        return self._ppage

    def _compute_text_cells(self) -> List[TextCell]:
        """Compute text cells from pypdfium."""
        with pypdfium2_lock:
            page = self._require_page()
            if not self.text_page:
                self.text_page = page.get_textpage()
            rotation = page.get_rotation()
            origin = page.get_bbox()[:2]

        cells = []
        cell_counter = 0

        page_size = self.get_size()

        with pypdfium2_lock:
            chars = _PageChars.from_text_page(self.text_page)
            for i in range(self.text_page.count_rects()):
                rect = self.text_page.get_rect(i)
                text_piece = chars.text_in_rect(rect)
                # `rect` is in the unrotated frame, `page_size` in the rotated
                # display frame: bring the rect over before converting origin.
                x0, y0, x1, y1 = _rect_to_display_frame(
                    rect, rotation, page_size, origin
                )
                cells.append(
                    TextCell(
                        index=cell_counter,
                        text=text_piece,
                        orig=text_piece,
                        from_ocr=False,
                        rect=BoundingRectangle.from_bounding_box(
                            BoundingBox(
                                l=x0,
                                b=y0,
                                r=x1,
                                t=y1,
                                coord_origin=CoordOrigin.BOTTOMLEFT,
                            )
                        ).to_top_left_origin(page_size.height),
                    )
                )
                cell_counter += 1

        # PyPdfium2 produces very fragmented cells, with sub-word level boundaries, in many PDFs.
        # The cell merging code below is to clean this up.
        def merge_horizontal_cells(
            cells: List[TextCell],
            horizontal_threshold_factor: float = 1.0,
            vertical_threshold_factor: float = 0.5,
        ) -> List[TextCell]:
            if not cells:
                return []

            def group_rows(cells: List[TextCell]) -> List[List[TextCell]]:
                rows = []
                current_row = [cells[0]]
                row_top = cells[0].rect.to_bounding_box().t
                row_bottom = cells[0].rect.to_bounding_box().b
                row_height = cells[0].rect.to_bounding_box().height

                for cell in cells[1:]:
                    vertical_threshold = row_height * vertical_threshold_factor
                    if (
                        abs(cell.rect.to_bounding_box().t - row_top)
                        <= vertical_threshold
                        and abs(cell.rect.to_bounding_box().b - row_bottom)
                        <= vertical_threshold
                    ):
                        current_row.append(cell)
                        row_top = min(row_top, cell.rect.to_bounding_box().t)
                        row_bottom = max(row_bottom, cell.rect.to_bounding_box().b)
                        row_height = row_bottom - row_top
                    else:
                        rows.append(current_row)
                        current_row = [cell]
                        row_top = cell.rect.to_bounding_box().t
                        row_bottom = cell.rect.to_bounding_box().b
                        row_height = cell.rect.to_bounding_box().height

                if current_row:
                    rows.append(current_row)

                return rows

            def merge_row(row: List[TextCell]) -> List[TextCell]:
                merged = []
                current_group = [row[0]]

                for cell in row[1:]:
                    prev_cell = current_group[-1]
                    avg_height = (
                        prev_cell.rect.height + cell.rect.to_bounding_box().height
                    ) / 2
                    if (
                        cell.rect.to_bounding_box().l
                        - prev_cell.rect.to_bounding_box().r
                        <= avg_height * horizontal_threshold_factor
                    ):
                        current_group.append(cell)
                    else:
                        merged.append(merge_group(current_group))
                        current_group = [cell]

                if current_group:
                    merged.append(merge_group(current_group))

                return merged

            def merge_group(group: List[TextCell]) -> TextCell:
                if len(group) == 1:
                    return group[0]

                merged_bbox = BoundingBox(
                    l=min(cell.rect.to_bounding_box().l for cell in group),
                    t=min(cell.rect.to_bounding_box().t for cell in group),
                    r=max(cell.rect.to_bounding_box().r for cell in group),
                    b=max(cell.rect.to_bounding_box().b for cell in group),
                )

                bbox = merged_bbox.to_bottom_left_origin(page_size.height)
                # Cells are stored in the display frame; PDFium only understands
                # the unrotated one, so undo the rotation before querying it.
                pdf_rect = _rect_to_pdf_frame(
                    bbox.as_tuple(), rotation, page_size, origin
                )
                merged_text = chars.text_in_rect(pdf_rect)

                return TextCell(
                    index=group[0].index,
                    text=merged_text,
                    orig=merged_text,
                    rect=BoundingRectangle.from_bounding_box(merged_bbox),
                    from_ocr=False,
                )

            rows = group_rows(cells)
            merged_cells = [cell for row in rows for cell in merge_row(row)]

            for i, cell in enumerate(merged_cells, 1):
                cell.index = i

            return merged_cells

        return merge_horizontal_cells(cells)

    def _get_object_bucket(self, obj_type: int) -> List[tuple[BoundingBox, bool]]:
        """Return the bbox list for the page objects of ``obj_type``, building it once.

        Each list holds ``(bbox, invisible_text)`` pairs, where ``bbox`` is already in
        the rotated display frame with a top-left origin and ``invisible_text`` flags
        text objects drawn in a no-ink rendering mode. Only the requested type is
        walked, so the cost and retained memory of a page scale with the object types
        a caller actually asks for. Because page rotation and size are fixed for a
        loaded page, a list is valid for the page's whole lifetime; the memo is cleared
        in ``_close_native_page``.

        ``pypdfium2_lock`` is not reentrant, so ``get_size`` (which takes the lock
        itself) runs before the block. The memo is re-checked and written inside the
        block, so concurrent callers sharing a page backend build each list once.
        Callers iterate the returned list with the lock released.

        Args:
            obj_type: an ``FPDF_PAGEOBJ_*`` constant.

        Returns:
            The cached ``(bbox, invisible_text)`` list for ``obj_type``.
        """
        bucket = self._object_index.get(obj_type)
        if bucket is not None:
            return bucket

        page_size = self.get_size()  # acquires the lock; must be outside the block

        with pypdfium2_lock:
            bucket = self._object_index.get(obj_type)
            if bucket is not None:
                return bucket

            bucket = []
            page = self._require_page()
            rotation = page.get_rotation()
            origin = page.get_bbox()[:2]
            for obj in page.get_objects(filter=[obj_type]):
                invisible = (
                    obj_type == pdfium_c.FPDF_PAGEOBJ_TEXT
                    and pdfium_c.FPDFTextObj_GetTextRenderMode(obj.raw)
                    in _INVISIBLE_TEXT_RENDER_MODES
                )

                if _PYPDFIUM2_MAJOR_VERSION >= 5:
                    pos = obj.get_bounds()  # pypdfium2 >= 5.x
                else:
                    pos = obj.get_pos()  # pypdfium2 <= 4.x
                pos = _rect_to_display_frame(pos, rotation, page_size, origin)

                bbox = BoundingBox.from_tuple(
                    pos, origin=CoordOrigin.BOTTOMLEFT
                ).to_top_left_origin(page_height=page_size.height)
                bucket.append((bbox, invisible))

            self._object_index[obj_type] = bucket

        return bucket

    def _object_rects(
        self, obj_type: int, *, skip_invisible_text: bool = False
    ) -> Iterable[BoundingBox]:
        """Yield the bboxes of the page objects of ``obj_type``, in top-left origin.

        Served from the per-type list kept by ``_get_object_bucket``, so repeated
        calls do not re-walk the page. With ``skip_invisible_text``, text objects
        drawn in a rendering mode that paints no ink are left out, matching what
        docling-parse does natively.
        """
        for bbox, invisible in self._get_object_bucket(obj_type):
            if skip_invisible_text and invisible:
                continue
            yield bbox

    def get_bitmap_rects(self, scale: float = 1) -> Iterable[BoundingBox]:
        AREA_THRESHOLD = 0  # 32 * 32

        for cropbox in self._object_rects(pdfium_c.FPDF_PAGEOBJ_IMAGE):
            if cropbox.area() > AREA_THRESHOLD:
                yield cropbox.scaled(scale=scale)

    def has_content_in(
        self,
        *,
        bbox: BoundingBox,
        chars: bool = False,
        shapes: bool = True,
        bitmaps: bool = True,
    ) -> bool | None:
        """Best-effort content-intersection test built from page-object bboxes.

        pypdfium2 exposes no clip state, so this approximates the docling-parse query: an
        object counts as intersecting when its bounding box does, even if the object is
        clipped away or fully transparent. Text rendering mode is the one visibility signal
        it does expose, and invisible text is skipped just like docling-parse does.
        """
        if not self.valid:
            return False

        page_size = self.get_size()
        probe = bbox.to_top_left_origin(page_height=page_size.height)

        obj_types = []
        if shapes:
            obj_types.append(pdfium_c.FPDF_PAGEOBJ_PATH)
        if bitmaps:
            obj_types.append(pdfium_c.FPDF_PAGEOBJ_IMAGE)
        if chars:
            obj_types.append(pdfium_c.FPDF_PAGEOBJ_TEXT)

        for obj_type in obj_types:
            for rect in self._object_rects(obj_type, skip_invisible_text=True):
                # Plain overlap, so that degenerate (zero-area) rules still count.
                if (
                    rect.l <= probe.r
                    and probe.l <= rect.r
                    and rect.t <= probe.b
                    and probe.t <= rect.b
                ):
                    return True

        return False

    def get_connected_shape_bounding_boxes(
        self, *, tolerance: float = 0.0
    ) -> List[BoundingBox] | None:
        """Best-effort connected shape regions, merged from path-object bboxes.

        Unlike the docling-parse implementation this sees neither clip state nor stroke
        width, so the regions are the union of raw path bounding boxes.
        """
        if not self.valid:
            return []

        return _merge_overlapping_boxes(
            list(self._object_rects(pdfium_c.FPDF_PAGEOBJ_PATH)), tolerance
        )

    def get_text_in_rect(self, bbox: BoundingBox) -> str:
        with pypdfium2_lock:
            page = self._require_page()
            if not self.text_page:
                self.text_page = page.get_textpage()
            rotation = page.get_rotation()
            origin = page.get_bbox()[:2]

        page_size = self.get_size()

        if bbox.coord_origin != CoordOrigin.BOTTOMLEFT:
            bbox = bbox.to_bottom_left_origin(page_size.height)

        # `bbox` is expressed in the rotated display frame, PDFium expects the
        # unrotated one.
        pdf_rect = _rect_to_pdf_frame(bbox.as_tuple(), rotation, page_size, origin)

        with pypdfium2_lock:
            text_piece = self.text_page.get_text_bounded(*pdf_rect)

        return text_piece

    def get_segmented_page(self) -> SegmentedPdfPage | None:
        if not self.valid:
            return None

        # Cached like the docling-parse backends do: rebuilding meant re-running
        # the whole text extraction, and callers reach for this once per table.
        if self._seg_page is None:
            text_cells = self._compute_text_cells()

            # Get the PDF page geometry from pypdfium2
            dimension = get_pdf_page_geometry(self._require_page())

            # Create SegmentedPdfPage
            self._seg_page = SegmentedPdfPage(
                dimension=dimension,
                textline_cells=text_cells,
                char_cells=[],
                word_cells=[],
                has_textlines=len(text_cells) > 0,
                has_words=False,
                has_chars=False,
            )
        return self._seg_page

    def get_text_cells(self) -> Iterable[TextCell]:
        return self._compute_text_cells()

    def get_page_image(
        self, scale: float = 1, cropbox: BoundingBox | None = None
    ) -> Image.Image:
        page_size = self.get_size()

        if not cropbox:
            cropbox = BoundingBox(
                l=0,
                r=page_size.width,
                t=0,
                b=page_size.height,
                coord_origin=CoordOrigin.TOPLEFT,
            )
            padbox = BoundingBox(
                l=0, r=0, t=0, b=0, coord_origin=CoordOrigin.BOTTOMLEFT
            )
        else:
            padbox = cropbox.to_bottom_left_origin(page_size.height).model_copy()
            padbox.r = page_size.width - padbox.r
            padbox.t = page_size.height - padbox.t

        with pypdfium2_lock:
            bitmap = self._require_page().render(
                scale=scale * 1.5,
                rotation=0,  # no additional rotation
                crop=padbox.as_tuple(),
            )
            image = bitmap.to_pil().copy()
            bitmap.close()
        # We resize the image from 1.5x the given scale to make it sharper.
        image = image.resize(
            size=(round(cropbox.width * scale), round(cropbox.height * scale))
        )

        return image

    def get_size(self) -> Size:
        with pypdfium2_lock:
            page = self._require_page()
            return Size(width=page.get_width(), height=page.get_height())

    def _close_native_page(self) -> None:
        with pypdfium2_lock:
            if self.text_page is not None:
                self.text_page.close()
            if self._ppage is not None:
                self._ppage.close()

        self.text_page = None
        self._ppage = None
        self._seg_page = None
        self._object_index = {}


def _visible_text_cells(cells: Iterable[TextCell]) -> list[TextCell]:
    """Keep only the cells that paint ink on the page"""
    return [
        cell
        for cell in cells
        if not isinstance(cell, PdfTextCell)
        or cell.rendering_mode not in _INVISIBLE_RENDERING_MODES
    ]


class ThreadedDoclingParsePageBackend(PdfPageBackend):
    def __init__(self, result: PageParseResult, rendered: bool = True):
        self._result = result
        self._rendered = rendered
        self._seg_page: SegmentedPdfPage | None = None

    @property
    def page_no(self) -> int:
        return self._result.page_number

    def is_valid(self) -> bool:
        return self._result.success

    def get_error_message(self) -> str:
        return self._result.error_message

    def get_text_in_rect(self, bbox: BoundingBox) -> str:
        segmented_page = self.get_segmented_page()
        if segmented_page is None:
            return ""

        text_piece = ""
        for cell in segmented_page.textline_cells:
            cell_bbox = cell.rect.to_bounding_box()
            overlap_frac = cell_bbox.intersection_over_self(bbox)
            if overlap_frac > 0.5:
                if text_piece:
                    text_piece += " "
                text_piece += cell.text

        return text_piece

    def get_segmented_page(self) -> SegmentedPdfPage | None:
        if not self.is_valid():
            return None
        if self._seg_page is None:
            seg_page = self._result.get_page()
            page_height = seg_page.dimension.height
            for tc in (
                *seg_page.textline_cells,
                *seg_page.char_cells,
                *seg_page.word_cells,
            ):
                tc.to_top_left_origin(page_height)
                # docling-parse emits U+0000 for a glyph it cannot map to Unicode.
                # U+FFFD keeps the loss visible and counted by is_broken_text_layer,
                # and does not truncate text in C-string based storage.
                tc.text = tc.text.replace("\x00", "\ufffd")
                tc.orig = tc.orig.replace("\x00", "\ufffd")
            self._seg_page = seg_page
        return self._seg_page

    def get_text_cells(self) -> Iterable[TextCell]:
        segmented_page = self.get_segmented_page()
        if segmented_page is None:
            return []
        return segmented_page.textline_cells

    def get_visible_text_cells(self) -> list[TextCell] | None:
        segmented_page = self.get_segmented_page()
        if segmented_page is None:
            return []
        return _visible_text_cells(segmented_page.textline_cells)

    def get_bitmap_rects(self, scale: float = 1) -> Iterable[BoundingBox]:
        segmented_page = self.get_segmented_page()
        if segmented_page is None:
            return []

        page_height = self.get_size().height
        cropboxes: list[BoundingBox] = []
        for image_resource in segmented_page.bitmap_resources:
            cropbox = image_resource.rect.to_bounding_box().to_top_left_origin(
                page_height
            )
            if cropbox.area() > 0:
                cropboxes.append(cropbox.scaled(scale=scale))
        return cropboxes

    def has_content_in(
        self,
        *,
        bbox: BoundingBox,
        chars: bool = False,
        shapes: bool = True,
        bitmaps: bool = True,
    ) -> bool | None:
        if not self.is_valid():
            return False
        return self._result.intersects_with(
            bbox=bbox, chars=chars, shapes=shapes, bitmaps=bitmaps
        )

    def get_shape_lines(
        self,
        *,
        horizontal: bool = True,
        vertical: bool = True,
        tolerance: float = 1e-3,
    ) -> list[BoundingBox] | None:
        if not self.is_valid():
            return []

        page_height = self.get_size().height
        return [
            bbox.to_top_left_origin(page_height)
            for bbox in self._result.get_shape_lines(
                horizontal=horizontal, vertical=vertical, tolerance=tolerance
            )
        ]

    def get_connected_shape_bounding_boxes(
        self, *, tolerance: float = 0.0
    ) -> list[BoundingBox] | None:
        if not self.is_valid():
            return []

        page_height = self.get_size().height
        return [
            bbox.to_top_left_origin(page_height)
            for bbox in self._result.get_connected_shape_bounding_boxes(
                tolerance=tolerance
            )
        ]

    def get_thin_shape_boxes(self, *, max_thickness: float) -> list[BoundingBox] | None:
        if not self.is_valid():
            return []

        page_height = self.get_size().height
        boxes = []
        # docling-parse exposes the individual painted paths only through its
        # page decoder; get_connected_shape_bounding_boxes merges them first.
        for shape in self._result._require_page_decoder().get_page_shapes():
            xs, ys = list(shape.get_x()), list(shape.get_y())
            if not xs or not ys:
                continue
            box = BoundingBox(
                l=min(xs),
                b=min(ys),
                r=max(xs),
                t=max(ys),
                coord_origin=CoordOrigin.BOTTOMLEFT,
            )
            if min(box.width, box.height) <= max_thickness:
                boxes.append(box.to_top_left_origin(page_height))
        return boxes

    def get_page_image(
        self, scale: float = 1, cropbox: BoundingBox | None = None
    ) -> Image.Image:
        if not self._rendered:
            raise RuntimeError(
                "This backend was configured with render_pages=False, so page "
                f"{self.page_no} was parsed but never rendered and no page image exists."
            )
        return self._result.get_image(scale=scale, cropbox=cropbox).convert("RGB")

    def get_size(self) -> Size:
        return Size(width=self._result.page_width, height=self._result.page_height)

    def unload(self) -> None:
        return None


def accelerator_threads():
    value = os.getenv("DOCLING_NUM_THREADS")
    if value is not None:
        return int(value)
    try:
        return int(os.getenv("OMP_NUM_THREADS", "4"))
    except ValueError:
        return 4


def configurations(threads):
    render = RenderConfig()
    render.scale = 1.0
    content = ContentConfig(
        char_cells_content_level=ContentLevel.COMPUTE,
        word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        shapes_content_level=ContentLevel.COMPUTE,
        bitmaps_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        include_bitmap_bytes=False,
    )
    decode = DecodeConfig(
        enforce_same_font=True, release_native_memory_every_n_pages=128
    )
    parser = ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=threads,
        render_config=render,
        page_content_config=content,
    )
    return parser, decode


def serialize(value):
    if hasattr(value, "model_dump"):
        return value.model_dump(mode="python")
    if hasattr(value, "__dict__"):
        return {k: v for k, v in vars(value).items() if not k.startswith("_")}
    # pybind configuration objects expose properties rather than __dict__.
    properties = {
        name: getattr(value, name)
        for name in dir(value)
        if not name.startswith("_") and not callable(getattr(value, name))
    }
    return properties or str(value)


def git_state(path):
    def command(*args):
        result = subprocess.run(
            ["git", "-C", str(path), *args], capture_output=True, text=True
        )
        return result.stdout.strip() if result.returncode == 0 else None

    return {
        "revision": command("rev-parse", "HEAD"),
        "working_tree": command("status", "--porcelain"),
        "diff_sha256": hashlib.sha256(
            (command("diff", "HEAD") or "").encode()
        ).hexdigest(),
    }


def provenance():
    import docling_core
    import docling_parse.pdf_parsers as extension

    import docling_parse.pdf_parser as parser_module

    packages = {}
    for name in ("docling-parse", "docling-core", "pypdfium2", "pillow", "rtree"):
        packages[name] = version(name)
    return {
        "python": sys.version,
        "platform": platform.platform(),
        "python_compiler": platform.python_compiler(),
        "native_compiler": "not exposed by binding; record build configuration externally",
        "packages": packages,
        "imports": {
            "parser": parser_module.__file__,
            "extension": extension.__file__,
            "pypdfium2": pdfium.__file__,
            "docling_core": docling_core.__file__,
        },
        "parse_git": git_state(Path(parser_module.__file__).parent),
        "reference_git": git_state(
            Path(__file__).resolve().parents[2] / ".references/docling"
        ),
        "environment": {
            k: v
            for k, v in os.environ.items()
            if k.startswith(("DOCLING_", "OMP_", "BLEND2D_"))
        },
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }


def consume(page, calls):
    def call(name, *args, **kwargs):
        calls[name] += 1
        return getattr(page, name)(*args, **kwargs)

    size = call("get_size")
    # Replay Page.get_image's scale cache without importing Docling's Page.
    cache = {}

    def get_image(scale=1.0):
        calls["Page.get_image"] += 1
        if scale not in cache:
            cache[scale] = call("get_page_image", scale=scale)
        return cache[scale]

    image = get_image()
    assert get_image() is image
    lines = call("get_shape_lines")
    shapes = call("get_connected_shape_bounding_boxes")
    segmented = call("get_segmented_page")
    if segmented is None:
        raise RuntimeError("No segmented page returned")
    assert call("get_segmented_page") is segmented
    bitmaps = list(call("get_bitmap_rects"))
    row = {
        "page": page.page_no,
        "width": size.width,
        "height": size.height,
        "image_size": list(image.size),
        "image_mode": image.mode,
        "textlines": len(segmented.textline_cells),
        "words": len(segmented.word_cells),
        "chars": len(segmented.char_cells),
        "text_characters": sum(len(c.text) for c in segmented.textline_cells),
        "bitmaps": len(bitmaps),
        "shape_lines": len(lines or []),
        "connected_shapes": len(shapes or []),
    }
    image.close()
    return row


def measure(path, pages, backend, parser_config, decode, output, progress=None):
    calls = collections.Counter()
    totals = collections.Counter()
    images = collections.Counter()
    start = time.perf_counter()
    parser = document = None
    rows = 0
    first = None
    if backend == "threaded":
        parser = DoclingThreadedPdfParser(
            parser_config=parser_config, decode_config=decode
        )
        key = parser.load(path, page_numbers=pages)
        iterator = (
            ThreadedDoclingParsePageBackend(result)
            for result in parser.iterate_results()
        )
    else:
        with pypdfium2_lock:
            document = pdfium.PdfDocument(path)
            selected = pages if pages is not None else range(1, len(document) + 1)

        def pdfium_pages():
            for number in selected:
                with pypdfium2_lock:
                    yield_page = PyPdfiumPageBackend(document, str(path), number - 1)
                yield yield_page

        iterator = pdfium_pages()
    loaded = time.perf_counter()
    try:
        for page in iterator:
            if first is None:
                first = time.perf_counter()
            try:
                if not page.is_valid():
                    raise RuntimeError(f"Invalid page {page.page_no}")
                row = consume(page, calls)
                rows += 1
                totals.update(
                    {
                        k: row[k]
                        for k in (
                            "textlines",
                            "words",
                            "chars",
                            "text_characters",
                            "bitmaps",
                            "shape_lines",
                            "connected_shapes",
                        )
                    }
                )
                images[f"{row['image_mode']}:{row['image_size']}"] += 1
            finally:
                page.unload()
                calls["page.unload"] += 1
            # Stream metadata to disk; never retain page results across documents.
            output.write(json.dumps({"document": str(path), **row}) + "\n")
            if progress is not None:
                progress.update(1)
            del page
    finally:
        processed = time.perf_counter()
        if parser is not None:
            while parser.has_tasks():
                parser.get_task()
            parser.unload(key)
        if document is not None:
            with pypdfium2_lock:
                document.close()
    ended = time.perf_counter()
    return {
        "document": str(path),
        "pages": rows,
        "counts": totals,
        "image_modes_dimensions": images,
        "api_calls": calls,
        "construction_load_s": loaded - start,
        "page_processing_s": processed - loaded,
        "unload_s": ended - processed,
        "total_s": ended - start,
        "first_result_s": (first - start) if first is not None else None,
    }


# Match scripts/benchmarking/_common.py without requiring sibling imports.
PROGRESS_BAR_FORMAT = (
    "{desc}: [{bar:20}] {n_fmt}/{total_fmt} {percentage:5.1f}% "
    "{rate_text} elapsed: {elapsed_text} [sec] total: {projected_total} [sec]"
)


def format_duration(seconds: float) -> str:
    """Format a duration like the C++ scaling benchmark."""
    total_seconds = int(seconds)
    hours, remainder = divmod(total_seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes}:{secs:02d}"


def _average_rate(values: Dict[str, Any]) -> float | None:
    """Items per second, mirroring the fallback `tqdm.format_meter` applies.

    With `smoothing=0` tqdm leaves `rate` unset in `format_dict` and works the
    average out only while formatting, so the custom output must derive the
    same rate for its `page/s` field.
    """
    rate = values.get("rate")
    if rate:
        return float(rate)
    elapsed = values.get("elapsed") or 0.0
    if elapsed <= 0:
        return None
    done = values.get("n", 0) - (values.get("initial") or 0)
    return done / elapsed if done > 0 else None


class ProgressBar(tqdm):
    """C++-style benchmark bar with average rate and projected total duration."""

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        kwargs.setdefault("bar_format", PROGRESS_BAR_FORMAT)
        kwargs.setdefault("smoothing", 0.0)
        kwargs.setdefault("ascii", "-#")
        super().__init__(*args, **kwargs)

    @property
    def format_dict(self) -> Dict[str, Any]:
        values = dict(super().format_dict)
        rate = _average_rate(values)
        total = values.get("total")
        current = values.get("n", 0)
        elapsed = values.get("elapsed") or 0.0
        projected_total = elapsed * total / current if total and current else 0.0
        unit = values.get("unit") or "it"
        values["rate_text"] = (
            f"{rate:.1f}{unit}/s" if rate is not None else f"0.0{unit}/s"
        )
        values["elapsed_text"] = format_duration(elapsed)
        values["projected_total"] = format_duration(projected_total)
        return values


DEFAULT_HF_REPO_ID = "docling-project/performance-dataset-bo767"


def resolve_inputs(args, cli):
    if args.selection and args.pdf:
        cli.error("Use either positional inputs or --selection")
    if args.selection:
        selection = json.loads(args.selection.read_text())
        if not isinstance(selection, dict):
            cli.error("--selection must contain a JSON object")
        info = {
            "source": "selection",
            "path": str(args.selection.resolve()),
            "revision": args.dataset_revision,
        }
    else:
        inputs = args.pdf or [DEFAULT_HF_REPO_ID]
        selection = {}
        sources = []
        for name in inputs:
            path = Path(name)
            if path.exists():
                pdfs = sorted(path.rglob("*.pdf")) if path.is_dir() else [path]
                sources.append(
                    {
                        "source": "local",
                        "path": str(path.resolve()),
                        "revision": args.dataset_revision,
                    }
                )
            else:
                if name.lower().endswith(".pdf") or name.startswith(("/", ".")):
                    cli.error(f"Local input does not exist: {name}")
                from huggingface_hub import snapshot_download

                print(f"Resolving HF dataset {name!r} (pdf/**) ...", flush=True)
                snapshot = Path(
                    snapshot_download(
                        repo_id=name,
                        repo_type="dataset",
                        allow_patterns=["pdf/**"],
                        revision=args.dataset_revision,
                    )
                )
                pdfs = sorted((snapshot / "pdf").rglob("*.pdf"))
                sources.append(
                    {
                        "source": "huggingface",
                        "name": name,
                        "path": str(snapshot / "pdf"),
                        "revision": snapshot.name,
                    }
                )
            selection.update({str(pdf.resolve()): None for pdf in pdfs})
        info = {
            "sources": sources,
            "revision": sources[0]["revision"] if len(sources) == 1 else None,
        }
    if not selection:
        cli.error("No PDFs found")
    for path, pages in selection.items():
        if pages is not None and (
            not isinstance(pages, list)
            or not pages
            or any(type(page) is not int or page < 1 for page in pages)
            or len(pages) != len(set(pages))
        ):
            cli.error(f"Invalid 1-based page selection for {path}")
    if args.dataset_root:
        selection = {
            str(args.dataset_root / Path(name).name): pages
            for name, pages in selection.items()
        }
    for name in selection:
        if not Path(name).is_file():
            cli.error(f"PDF does not exist: {name}")
    return selection, info


def main():
    cli = argparse.ArgumentParser(
        description="Standalone Docling-default backend replay",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        epilog=(
            "Workload: requested image scale=1.0; PDFium oversampling=1.5 then resize; "
            "native render scale=1.0 with RGB conversion; character cells computed; "
            "words/lines materialized; shapes computed; bitmap bounds materialized "
            "without byte export; enforce_same_font=True; parser queue inherited "
            "(currently 32); native memory release every 128 pages; separator geometry "
            "enabled; heading hierarchy disabled. Native thread counts are benchmark "
            "scaling overrides; --threads default uses DOCLING_NUM_THREADS, then valid "
            "OMP_NUM_THREADS, otherwise 4. PDFium runs once, synchronized on one worker. "
            "OCR/layout/table inference is excluded."
        ),
    )
    cli.add_argument(
        "pdf",
        nargs="*",
        metavar="INPUT",
        default=[DEFAULT_HF_REPO_ID],
        help="Local PDFs/directories or Hugging Face dataset IDs (pdf/ subfolder)",
    )
    cli.add_argument(
        "--selection",
        type=Path,
        help="JSON mapping PDF paths to 1-based page lists; replaces default dataset",
    )
    cli.add_argument(
        "--dataset-root",
        type=Path,
        help="Resolve selection keys by filename under this directory",
    )
    cli.add_argument(
        "--dataset-revision",
        help="HF revision to download; latest when omitted, resolved commit recorded automatically. For local inputs, a provenance label",
    )
    cli.add_argument(
        "--backend",
        choices=("both", "pdfium", "threaded"),
        default="both",
        help="Backends to benchmark",
    )
    cli.add_argument(
        "--threads",
        default="1,4,8",
        help="Comma-separated native thread counts, or 'default' for accelerator settings; ignored by PDFium",
    )
    cli.add_argument(
        "--output-dir",
        type=Path,
        default=Path(
            f"scratch_perf_docling_benchmark_{datetime.now():%Y_%m_%d_%H_%M_%S}"
        ),
        help="Directory for report.json and streamed per-page JSONL files",
    )
    cli.add_argument(
        "--output",
        type=Path,
        help="Override report JSON path and place page files beside it; otherwise <output-dir>/report.json",
    )
    args = cli.parse_args()
    # argparse uses the positional default even when --selection was supplied.
    if args.selection and args.pdf == [DEFAULT_HF_REPO_ID]:
        args.pdf = []
    try:
        threads = (
            [accelerator_threads()]
            if args.threads == "default"
            else [int(n) for n in args.threads.split(",")]
        )
    except ValueError:
        cli.error(
            "--threads must be positive integers separated by commas, or 'default'"
        )
    if any(n < 1 for n in threads) or len(threads) != len(set(threads)):
        cli.error("Thread counts must be positive and unique")
    selection, dataset_info = resolve_inputs(args, cli)
    if args.output is None:
        args.output = args.output_dir / "report.json"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    print(f"Output: {args.output.parent.resolve()}", flush=True)
    revision = dataset_info["revision"]
    revision_label = f" (revision {revision})" if revision else ""
    print(
        f"Dataset: {len(selection)} PDF{'s' if len(selection) != 1 else ''}{revision_label}",
        flush=True,
    )
    total_pages = 0
    for name, pages in ProgressBar(
        selection.items(), desc="counting pages", unit="doc"
    ):
        if pages is not None:
            total_pages += len(pages)
        else:
            with pypdfium2_lock:
                with pdfium.PdfDocument(name) as document:
                    total_pages += len(document)
    meta = provenance()
    reports = []
    schedule = []
    if args.backend in ("both", "pdfium"):
        schedule.append(("pdfium", 1, "backend-default"))
    if args.backend in ("both", "threaded"):
        schedule.extend(
            (
                "threaded",
                count,
                "backend-default" if args.threads == "default" else "scaling-override",
            )
            for count in threads
        )
    for backend, count, profile in schedule:
        config, decode = configurations(count)
        settings = {
            "profile": profile,
            "backend": backend,
            "parser": config,
            "decode": decode,
            "requested_scale": 1.0,
            "pdfium_oversampling": 1.5,
            "native_rgb_conversion": backend == "threaded",
            "native_settings_active": backend == "threaded",
            "cache_probe_extra_requests": ["Page.get_image", "get_segmented_page"],
            "reading_order_separators": True,
            "heading_hierarchy": False,
            "bitmap_bounds_extra_request": True,
            "settings_source": "pinned standalone replay of Docling 07154cdc",
        }
        suffix = f".{backend}.{profile}.{count}"
        rows_path = args.output.with_name(args.output.name + suffix + ".pages.jsonl")
        documents = []
        label = (
            "  pypdfium2-render"
            if backend == "pdfium"
            else f"  docling-parse-render ({count} thread{'s' if count != 1 else ''})"
        )
        with (
            rows_path.open("w") as output,
            ProgressBar(total=total_pages, desc=label, unit="page") as progress,
        ):
            for i, (name, pages) in enumerate(selection.items()):
                doc = measure(
                    Path(name), pages, backend, config, decode, output, progress
                )
                doc["initialization"] = (
                    "first-document-in-run" if i == 0 else "steady-state"
                )
                documents.append(doc)
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        reports.append(
            {
                "settings": settings,
                "documents": documents,
                "pages_file": str(rows_path),
                "peak_rss_mib": peak / (1024**2 if sys.platform == "darwin" else 1024),
                "rss_scope": "process lifetime; use fresh process for each profile",
            }
        )
    report = {
        "provenance": meta,
        "dataset_revision": dataset_info["revision"],
        "dataset": dataset_info,
        "selection": selection,
        "runs": reports,
        "measurement": "standalone backend replay; excludes model inference and Docling framework overhead",
        "parity": "not independently validated against actual Docling backends",
        "expected_differences": "PDFium merges rectangle text; native produces word/line cells; raster pixels differ",
    }
    args.output.write_text(json.dumps(report, default=serialize, indent=2) + "\n")
    print(f"Report saved: {args.output.resolve()}", flush=True)


if __name__ == "__main__":
    main()
