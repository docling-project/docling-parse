#!/usr/bin/env python3
"""Export a PDF to a DocLang archive (.dclx) using docling-parse.

For every page the script takes the ``SegmentedPdfPage`` produced by
docling-parse plus the rendered page image and assembles a
``DoclingDocument``:

  * each char / word / text cell (selected via ``-m/--mode``) becomes a
    ``TextItem`` with a ``ProvenanceItem`` pointing at the cell's bounding box;
  * the rendered page image is attached as the page's image in ``doc.pages``.

The document is then serialized with ``save_as_doclang_archive`` to a ``.dclx``.

Usage:
    python scripts/export_dclx.py -i input.pdf [-m word] [-p 1] [-o out.dclx]

Modes:
    char  -> one TextItem per character cell
    word  -> one TextItem per word cell
    text  -> one TextItem per text line cell
"""

import argparse
import sys
from pathlib import Path

from docling_core.types.doc.base import Size
from docling_core.types.doc.document import (
    DoclingDocument,
    ImageRef,
    ProvenanceItem,
)
from docling_core.types.doc.labels import DocItemLabel
from docling_core.types.doc.page import TextCellUnit

from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DoclingThreadedPdfParser,
    RenderConfig,
    ThreadedPdfParserConfig,
)

# Map the CLI mode onto the cell unit it materializes.
MODE_TO_UNIT = {
    "char": TextCellUnit.CHAR,
    "word": TextCellUnit.WORD,
    "text": TextCellUnit.LINE,
}


def parse_args(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("-i", "--input", required=True, type=Path,
                    help="Path to the input PDF.")
    ap.add_argument("-m", "--mode", choices=sorted(MODE_TO_UNIT),
                    default="word",
                    help="Cell granularity exported as TextItems (default: word).")
    ap.add_argument("-o", "--output", type=Path, default=None,
                    help="Output .dclx path (default: <input>.<mode>.dclx).")
    ap.add_argument("-p", "--page", type=int, default=None,
                    help="1-indexed page number to export (default: all pages).")
    ap.add_argument("--scale", type=float, default=2.0,
                    help="Page-image raster scale in pixels-per-point (default: 2.0).")
    ap.add_argument("--threads", type=int, default=4,
                    help="Worker threads for the parser (default: 4).")
    return ap.parse_args(argv)


def _content_config_for(mode: str) -> ContentConfig:
    """Materialize the cell unit for the mode plus the page bitmaps."""
    materialize = ContentLevel.COMPUTE_AND_MATERIALIZE
    skip = ContentLevel.SKIP
    return ContentConfig(
        char_cells_content_level=materialize if mode == "char" else skip,
        word_cells_content_level=materialize if mode == "word" else skip,
        line_cells_content_level=materialize if mode == "text" else skip,
        bitmaps_content_level=materialize,
        include_bitmap_bytes=True,
    )


def _add_page_to_doc(doc, page_no, page, image, dpi, unit):
    """Attach one page (image + text cells + picture bitmaps) to the doc.

    Returns (n_text_cells, n_pictures).
    """
    doc.add_page(
        page_no=page_no,
        size=Size(width=page.dimension.width, height=page.dimension.height),
        image=ImageRef.from_pil(image, dpi=dpi),
    )

    n_cells = 0
    for cell in page.iterate_cells(unit):
        text = cell.text
        if not text:
            continue
        prov = ProvenanceItem(
            page_no=page_no,
            bbox=cell.rect.to_bounding_box(),
            charspan=(0, len(text)),
        )
        doc.add_text(label=DocItemLabel.TEXT, text=text, orig=text, prov=prov)
        n_cells += 1

    n_pictures = 0
    for bitmap in page.bitmap_resources:
        if bitmap.image is None:
            continue
        prov = ProvenanceItem(
            page_no=page_no,
            bbox=bitmap.rect.to_bounding_box(),
            charspan=(0, 0),
        )
        doc.add_picture(image=bitmap.image, prov=prov)
        n_pictures += 1

    return n_cells, n_pictures


def export(input_path: Path, mode: str, scale: float, threads: int,
           page: int | None = None) -> DoclingDocument:
    content_config = _content_config_for(mode)
    page_numbers = [page] if page is not None else None

    render_config = RenderConfig()
    render_config.render_text = True
    render_config.scale = scale

    parser = DoclingThreadedPdfParser(
        parser_config=ThreadedPdfParserConfig(
            threads=threads,
            render_config=render_config,
            page_content_config=content_config,
        ),
    )

    # Results arrive in completion order, not page order; collect then sort so
    # the DoclingDocument is assembled in natural reading order.
    pages = {}
    try:
        parser.load(str(input_path), page_numbers=page_numbers)
        for result in parser.iterate_results():
            if not result.success:
                print(f"  warning: page {result.page_number} failed: "
                      f"{result.error_message}", file=sys.stderr)
                continue
            pages[result.page_number] = (
                result.get_page(content_config),
                result.get_image(scale=scale),
            )
    finally:
        parser.unload_all()

    doc = DoclingDocument(name=input_path.stem)
    unit = MODE_TO_UNIT[mode]
    dpi = int(round(72 * scale))

    total_cells = 0
    total_pictures = 0
    for page_no in sorted(pages):
        page, image = pages[page_no]
        n_cells, n_pics = _add_page_to_doc(doc, page_no, page, image, dpi, unit)
        total_cells += n_cells
        total_pictures += n_pics
        print(f"  page {page_no}: {n_cells} {mode} cell(s), {n_pics} picture(s)")

    print(f"Assembled DoclingDocument: {len(pages)} page(s), "
          f"{total_cells} TextItem(s), {total_pictures} PictureItem(s)")
    return doc


def main(argv=None):
    args = parse_args(argv)

    if not args.input.is_file():
        print(f"error: input not found: {args.input}", file=sys.stderr)
        return 1

    if args.page is not None and args.page < 1:
        print(f"error: --page must be >= 1 (got {args.page})", file=sys.stderr)
        return 1

    page_label = f", page={args.page}" if args.page is not None else ""
    print(f"Exporting '{args.input}' (mode={args.mode}, scale={args.scale}{page_label}) ...")
    doc = export(args.input, args.mode, args.scale, args.threads, args.page)

    default_suffix = (
        f".{args.mode}.p{args.page}.dclx"
        if args.page is not None
        else f".{args.mode}.dclx"
    )
    output = args.output or args.input.with_suffix(default_suffix)
    doc.save_as_doclang_archive(output)
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
