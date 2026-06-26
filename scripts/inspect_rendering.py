#!/usr/bin/env python3
"""Side-by-side rendering inspector for docling-parse.

Builds a two-panel image for a single PDF page:

  * Left panel  : the page rasterized by **pdfium** (the reference renderer),
                  with the per-character bounding boxes from docling-parse's
                  ``text_instruction``s overlaid on top.
  * Right panel : the page rasterized by **docling-parse** (Blend2D renderer),
                  with the *same* per-character bounding boxes overlaid.

Both panels share the same character boxes (the ones emitted by the parser),
so any horizontal drift of a rendered glyph relative to its box is a
*renderer* discrepancy, while a box that does not sit on the pdfium glyph is a
*parser* discrepancy. This makes it easy to tell the two failure modes apart.

Usage:
    python scripts/inspect_rendering.py -i input.pdf [-p PAGE] [-o out.png]

The page number (``-p/--page``) is 1-indexed; it defaults to the first page.
"""

import argparse
import sys
from pathlib import Path

import pypdfium2 as pdfium
from PIL import Image, ImageDraw, ImageFont

from docling_parse.pdf_parser import (
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

# Box / label styling.
PDFIUM_BOX_COLOR = (220, 30, 30)        # red boxes on the pdfium panel
DOCLING_BOX_COLOR = (220, 30, 30)       # red boxes on the docling-parse panel
HEADER_HEIGHT = 34
GAP = 16                                # gap between the two panels
MARGIN = 12


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-i", "--input", required=True, type=Path,
                    help="Path to the input PDF.")
    ap.add_argument("-p", "--page", type=int, default=1,
                    help="1-indexed page number to inspect (default: 1).")
    ap.add_argument("-o", "--output", type=Path, default=None,
                    help="Output PNG path (default: <input>.render-inspect.p<page>.png).")
    ap.add_argument("--scale", type=float, default=2.0,
                    help="Raster scale in pixels-per-point for both panels (default: 2.0).")
    return ap.parse_args(argv)


def render_with_docling(input_path: Path, page: int, scale: float):
    """Render the page with docling-parse and return (PIL image, text_instructions, crop_bbox).

    ``text_instructions`` is the list of per-character text render instructions;
    ``crop_bbox`` is the page coordinate window [x0, y0, x1, y1] those boxes live in.
    """
    render_config = RenderConfig()
    render_config.render_text = True
    render_config.scale = scale

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            threads=1,
            render_config=render_config,
        ),
    )
    try:
        doc_key = parser.load(str(input_path), page_numbers=[page])
        result = None
        for res in parser.iterate_results():
            if res.page_number == page:
                result = res
                break
        if result is None:
            raise RuntimeError(f"page {page} was not emitted by the parser")
        if not result.success:
            raise RuntimeError(
                f"docling-parse failed on page {page}: {result.error_message}"
            )

        image = result.get_image(scale=scale).convert("RGB")
        render_info = result._export_render_instructions_json()
    finally:
        parser.unload_all()

    text_instructions = [
        instr for instr in render_info["instructions"]
        if instr.get("type") == "text"
    ]
    crop_bbox = render_info["size_instruction"]["crop_bbox"]
    return image, text_instructions, crop_bbox


def render_with_pdfium(input_path: Path, page: int, scale: float):
    """Render the page with pdfium and return the PIL image."""
    pdf = pdfium.PdfDocument(str(input_path))
    try:
        n_pages = len(pdf)
        if not (1 <= page <= n_pages):
            raise RuntimeError(
                f"page {page} out of range (document has {n_pages} page(s))"
            )
        bitmap = pdf[page - 1].render(scale=scale)
        return bitmap.to_pil().convert("RGB")
    finally:
        pdf.close()


def _page_to_pixel(x, y, crop_bbox, img_w, img_h):
    """Map a point in PDF page space (bottom-left origin) to image pixels (top-left).

    The character boxes are expressed in the same page coordinate window as
    ``crop_bbox``; we normalize by that window so the mapping is correct even
    when the crop box has a non-zero origin.
    """
    x0, y0, x1, y1 = crop_bbox
    span_w = (x1 - x0) or 1.0
    span_h = (y1 - y0) or 1.0
    px = (x - x0) / span_w * img_w
    py = (1.0 - (y - y0) / span_h) * img_h  # flip y: PDF origin is bottom-left
    return px, py


def overlay_boxes(image, text_instructions, crop_bbox, color):
    """Draw the character quads from the text instructions onto a copy of image."""
    out = image.copy()
    draw = ImageDraw.Draw(out)
    w, h = out.size
    for instr in text_instructions:
        quad = instr["quad"]
        corners = [
            _page_to_pixel(quad["r_x0"], quad["r_y0"], crop_bbox, w, h),
            _page_to_pixel(quad["r_x1"], quad["r_y1"], crop_bbox, w, h),
            _page_to_pixel(quad["r_x2"], quad["r_y2"], crop_bbox, w, h),
            _page_to_pixel(quad["r_x3"], quad["r_y3"], crop_bbox, w, h),
        ]
        draw.polygon(corners, outline=color)
    return out


def compose_panels(left_img, left_label, right_img, right_label):
    """Place two labelled panels side by side on a white canvas."""
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None

    panel_h = max(left_img.height, right_img.height)
    total_w = MARGIN + left_img.width + GAP + right_img.width + MARGIN
    total_h = MARGIN + HEADER_HEIGHT + panel_h + MARGIN

    canvas = Image.new("RGB", (total_w, total_h), (255, 255, 255))
    draw = ImageDraw.Draw(canvas)

    left_x = MARGIN
    right_x = MARGIN + left_img.width + GAP
    header_y = MARGIN + (HEADER_HEIGHT - 12) // 2
    panel_y = MARGIN + HEADER_HEIGHT

    draw.text((left_x, header_y), left_label, fill=(0, 0, 0), font=font)
    draw.text((right_x, header_y), right_label, fill=(0, 0, 0), font=font)

    canvas.paste(left_img, (left_x, panel_y))
    canvas.paste(right_img, (right_x, panel_y))
    return canvas


def main(argv=None):
    args = parse_args(argv)

    if not args.input.is_file():
        print(f"error: input not found: {args.input}", file=sys.stderr)
        return 1
    if args.page < 1:
        print(f"error: --page must be >= 1 (got {args.page})", file=sys.stderr)
        return 1

    print(f"Rendering '{args.input}' page {args.page} at scale {args.scale} ...")

    docling_img, text_instructions, crop_bbox = render_with_docling(
        args.input, args.page, args.scale
    )
    pdfium_img = render_with_pdfium(args.input, args.page, args.scale)

    print(f"  text instructions (character boxes): {len(text_instructions)}")
    print(f"  pdfium panel:        {pdfium_img.size}")
    print(f"  docling-parse panel: {docling_img.size}")

    left = overlay_boxes(pdfium_img, text_instructions, crop_bbox, PDFIUM_BOX_COLOR)
    right = overlay_boxes(docling_img, text_instructions, crop_bbox, DOCLING_BOX_COLOR)

    canvas = compose_panels(
        left, "pdfium + text_instruction boxes",
        right, "docling-parse + text_instruction boxes",
    )

    output = args.output or args.input.with_suffix(
        f".render-inspect.p{args.page}.png"
    )
    canvas.save(output)
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
