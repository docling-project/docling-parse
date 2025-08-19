import argparse
import logging
import os
from pathlib import Path

from docling_core.types.doc.page import SegmentedPdfPage, TextCellUnit

from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)


def parse_args():
    parser = argparse.ArgumentParser(description="Process a PDF file.")

    # Restrict log-level to specific values
    parser.add_argument(
        "-l",
        "--log-level",
        type=str,
        choices=["info", "warning", "error", "fatal"],
        required=False,
        default="error",
        help="Log level [info, warning, error, fatal]",
    )

    # Restrict page-boundary
    parser.add_argument(
        "-b",
        "--page-boundary",
        type=str,
        choices=["crop_box", "media_box"],
        required=False,
        default="crop_box",
        help="page-boundary [crop_box, media_box]",
    )

    # Restrict page-boundary
    parser.add_argument(
        "-c",
        "--category",
        type=str,
        # choices=["both", "original", "sanitized"],
        choices=["all", "char", "word", "line"],
        required=True,
        default="both",
        help="category [`all`, `char`, `word`, `line`]",
    )

    # Add an argument for the path to the PDF file
    parser.add_argument(
        "-i", "--input-pdf", type=str, help="Path to the PDF file", required=True
    )

    # Add an optional boolean argument for interactive mode
    parser.add_argument(
        "--interactive",
        action="store_true",
        help="Enable interactive mode (default: False)",
    )

    # Add an optional boolean argument for interactive mode
    parser.add_argument(
        "--display-text",
        action="store_true",
        help="Enable interactive mode (default: False)",
    )

    # Add an optional boolean argument for interactive mode
    parser.add_argument(
        "--log-text",
        action="store_true",
        help="Enable interactive mode (default: False)",
    )

    # Add an optional boolean argument for enforcing same-font
    parser.add_argument(
        "--enforce-same-font",
        action="store_true",
        help="Enable interactive mode (default: False)",
    )

    # Add an argument for the output directory, defaulting to "./tmp"
    parser.add_argument(
        "-o",
        "--output-dir",
        type=str,
        required=False,
        default=None,
        help="Path to the output directory (default: None)",
    )

    # Add an argument for the output directory, defaulting to "./tmp"
    parser.add_argument(
        "-p",
        "--page",
        type=int,
        required=False,
        default=-1,
        help="page to be displayed (default: -1 -> all)",
    )

    # Parse the command-line arguments
    args = parser.parse_args()

    # Check if the PDF file exists
    assert os.path.exists(args.input_pdf), f"PDF file does not exist: {args.input_pdf}"

    # Check if the output directory exists, create it if not
    if (args.output_dir is not None) and (not os.path.exists(args.output_dir)):
        os.makedirs(args.output_dir)
        logging.info(f"Output directory '{args.output_dir}' created.")

    return (
        args.log_level,
        args.input_pdf,
        args.interactive,
        args.output_dir,
        int(args.page),
        args.display_text,
        args.log_text,
        args.enforce_same_font,
        args.page_boundary,
        args.category,
    )


def visualise_py(
    *,
    log_level: str,
    pdf_path: str,
    interactive: str,
    output_dir: Path,
    display_text: bool,
    log_text: bool,
    enforce_same_font: bool,
    page_boundary: str = "crop_box",  # media_box
    category: str = "char",  # "both", "sanitized", "original"
    page_num: int = -1,
):
    parser = DoclingPdfParser(loglevel=log_level)

    pdf_doc: PdfDocument = parser.load(path_or_stream=pdf_path, lazy=True)

    page_nos = [page_num]
    if page_num == -1:
        page_nos = [(page_ind + 1) for page_ind in range(0, pdf_doc.number_of_pages())]

    for page_no in page_nos:
        print(f"parsing {pdf_path} on page: {page_no}")

        pdf_page: SegmentedPdfPage = pdf_doc.get_page(
            page_no=page_no,
            create_words=True,
            create_textlines=True,
            enforce_same_font=enforce_same_font,
        )

        if os.path.exists(str(output_dir)):
            pdf_page.save_as_json(
                Path(f"{output_dir}/{os.path.basename(pdf_path)}.page_{page_no}.json")
            )

        if category in ["all", "char"]:

            img = pdf_page.render_as_image(
                cell_unit=TextCellUnit.CHAR,
                draw_cells_bbox=(not display_text),
                draw_cells_text=display_text,
            )

            if os.path.exists(str(output_dir)):
                img.save(
                    f"{output_dir}/{os.path.basename(pdf_path)}.page_{page_no}.char.png"
                )

            if interactive:
                img.show()

            if log_text:
                lines = pdf_page.export_to_textlines(
                    cell_unit=TextCellUnit.CHAR,
                    add_fontkey=True,
                    add_fontname=False,
                )
                print(f"text-lines (original, page_no: {page_no}):")
                print("\n".join(lines))

        if category in ["all", "word"]:
            img = pdf_page.render_as_image(
                cell_unit=TextCellUnit.WORD,
                draw_cells_bbox=(not display_text),
                draw_cells_text=display_text,
            )

            if os.path.exists(str(output_dir)):
                img.save(
                    f"{output_dir}/{os.path.basename(pdf_path)}.page_{page_no}.word.png"
                )

            if interactive:
                img.show()

            if log_text:
                lines = pdf_page.export_to_textlines(
                    cell_unit=TextCellUnit.WORD,
                    add_fontkey=True,
                    add_fontname=False,
                )
                print(f"text-words (sanitized, page_no: {page_no}):")
                print("\n".join(lines))

        if category in ["all", "line"]:
            img = pdf_page.render_as_image(
                cell_unit=TextCellUnit.LINE,
                draw_cells_bbox=(not display_text),
                draw_cells_text=display_text,
            )

            if os.path.exists(str(output_dir)):
                img.save(
                    f"{output_dir}/{os.path.basename(pdf_path)}.page_{page_no}.line.png"
                )

            if interactive:
                img.show()

            if log_text:
                lines = pdf_page.export_to_textlines(
                    cell_unit=TextCellUnit.LINE,
                    add_fontkey=True,
                    add_fontname=False,
                )
                print(f"text-lines (sanitized, page_no: {page_no}):")
                print("\n".join(lines))

        pdf_doc.unload_pages(page_range=(page_no, page_no + 1))


def main():

    (
        log_level,
        # version,
        pdf_path,
        interactive,
        output_dir,
        page_num,
        display_text,
        log_text,
        enforce_same_font,
        page_boundary,
        category,
    ) = parse_args()

    logging.info(f"page_boundary: {page_boundary}")

    visualise_py(
        log_level=log_level,
        pdf_path=pdf_path,
        interactive=interactive,
        output_dir=output_dir,
        display_text=display_text,
        log_text=log_text,
        enforce_same_font=enforce_same_font,
        page_boundary=page_boundary,
        category=category,
        page_num=page_num,
    )


if __name__ == "__main__":
    main()
