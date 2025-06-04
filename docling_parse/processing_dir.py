import argparse
import glob
import hashlib
import logging
import os
from dataclasses import dataclass
from pathlib import Path
from queue import Queue

from tabulate import tabulate

from docling_parse import pdf_parser_v2  # type: ignore[attr-defined]

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)


@dataclass
class FileTask:
    folder_name: str

    file_name: str  # Local path where the file will be processed or saved
    file_hash: str


def parse_arguments():
    """Parse arguments for directory parsing."""

    parser = argparse.ArgumentParser(
        description="Process S3 files using multithreading."
    )
    parser.add_argument(
        "-d", "--directory", help="input directory with pdf files", required=True
    )
    parser.add_argument(
        "-r",
        "--recursive",
        help="recursively finding pdf-files",
        required=False,
        default=False,
    )
    parser.add_argument(
        "-p",
        "--page-level-parsing",
        help="parse pdf-files page-by-page",
        required=False,
        default=True,
    )

    # Restrict log-level to specific values
    parser.add_argument(
        "-l",
        "--log-level",
        type=str,
        choices=["info", "warning", "error", "fatal"],
        required=False,
        default="fatal",
        help="Log level [info, warning, error, fatal]",
    )

    args = parser.parse_args()

    return args.directory, args.recursive, args.log_level, args.page_level_parsing


def fetch_files_from_disk(directory, recursive, task_queue):
    """Recursively fetch files from disk and add them to the queue."""
    logging.info(f"Fetching file keys from disk: {directory}")

    for filename in sorted(glob.glob(os.path.join(directory, "*.pdf"))):

        file_name = str(Path(filename).resolve())

        hash_object = hashlib.sha256(filename.encode(), usedforsecurity=False)
        file_hash = hash_object.hexdigest()

        # Create a FileTask object
        task = FileTask(folder_name=directory, file_name=file_name, file_hash=file_hash)
        task_queue.put(task)

    task_queue.put(None)
    logging.info("Done with queue")


def process_files_from_queue(file_queue: Queue, page_level: bool, loglevel: str):
    """Process files from the queue."""

    overview = []

    while not file_queue.empty():

        task = file_queue.get()
        if task is None:  # End of queue signal
            break

        logging.info(
            f"Queue-size [{file_queue.qsize()}], Processing task: {task.file_name}"
        )

        try:
            parser = pdf_parser_v2(loglevel)

            parser.load_document(task.file_hash, str(task.file_name))

            num_pages = parser.number_of_pages(task.file_hash)
            logging.info(f" => #-pages of {task.file_name}: {num_pages}")

            overview.append([str(task.file_name), num_pages, -1, True])

            if page_level:
                # Parse page by page to minimize memory footprint
                for page in range(0, num_pages):
                    fname = f"{task.file_name}-page-{page:03}.json"

                    try:
                        json_doc = parser.parse_pdf_from_key_on_page(
                            task.file_hash, page
                        )

                        """
                        with open(os.path.join(directory, fname), "w") as fw:
                            fw.write(json.dumps(json_doc, indent=2))
                        """

                        overview.append([fname, num_pages, page, True])
                    except Exception as exc:
                        overview.append([fname, num_pages, page, False])
                        logging.error(
                            f"problem with parsing {task.file_name} on page {page}: {exc}"
                        )
            else:

                parser.parse_pdf_from_key(task.file_hash)

                """
                # with open(os.path.join(task.folder_name, f"{task.file_name}.json"), "w") as fw:
                with open(f"{task.file_name}.json", "w") as fw:
                    fw.write(json.dumps(json_doc, indent=2))
                """

                overview.append([str(task.file_name), num_pages, -1, True])

            # Unload the (QPDF) document and buffers
            parser.unload_document(task.file_hash)

        except Exception as exc:
            logging.error(exc)
            overview.append([str(task.file_name), -1, -1, False])

    return overview


def main():

    directory, recursive, loglevel, page_level_parsing = parse_arguments()

    task_queue = Queue()

    fetch_files_from_disk(directory, recursive, task_queue)

    overview = process_files_from_queue(task_queue, page_level_parsing, loglevel)

    print(tabulate(overview, headers=["filename", "success", "page-number", "#-pages"]))

    logging.info("All files processed successfully.")


if __name__ == "__main__":
    main()
