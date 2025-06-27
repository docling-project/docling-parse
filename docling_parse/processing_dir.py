import argparse
import asyncio
import glob
import hashlib
import logging
import os
import time
from dataclasses import dataclass
from pathlib import Path
from queue import Queue

from docling_core.types.doc.page import PdfPageBoundaryType
from tabulate import tabulate

from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument

# from docling_parse import pdf_parser_v2  # type: ignore[attr-defined]

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

    if os.path.exists(directory) and os.path.isfile(directory):
        # Create a FileTask object
        hash_object = hashlib.sha256(directory.encode(), usedforsecurity=False)
        file_hash = hash_object.hexdigest()

        task = FileTask(folder_name=directory, file_name=directory, file_hash=file_hash)
        task_queue.put(task)

    for filename in sorted(glob.glob(os.path.join(directory, "*.pdf"))):

        file_name = str(Path(filename).resolve())

        hash_object = hashlib.sha256(filename.encode(), usedforsecurity=False)
        file_hash = hash_object.hexdigest()

        # Create a FileTask object
        task = FileTask(folder_name=directory, file_name=file_name, file_hash=file_hash)
        task_queue.put(task)

    task_queue.put(None)
    logging.info("Done with queue")


async def async_process_files_from_queue(
    file_queue: Queue, page_level: bool, loglevel: str, sync: bool
):

    parser = DoclingPdfParser(loglevel="fatal")

    overview = []

    while not file_queue.empty():

        task = file_queue.get()
        if task is None:  # End of queue signal
            break

        # logging.info(
        print(f"Queue-size [{file_queue.qsize()}], Processing task: {task.file_name}")

        try:
            start_time = time.time()

            # Load document asynchronously
            pdf_doc: PdfDocument = await parser.load_async(
                path_or_stream=task.file_name,
                lazy=True,
                boundary_type=PdfPageBoundaryType.CROP_BOX,
            )
            assert pdf_doc is not None

            num_pages = pdf_doc.number_of_pages()

            if sync:
                # Load pages sequentially using async
                pages = []
                for page_no in range(1, pdf_doc.number_of_pages() + 1):
                    page = await pdf_doc.get_page_async(
                        page_no=page_no, create_words=True, create_textlines=True
                    )
                    pages.append(page)

            else:
                # Load all pages in parallel using asyncio.gather
                page_numbers = list(range(1, pdf_doc.number_of_pages() + 1))

                # Create tasks for parallel page loading
                page_tasks = [
                    pdf_doc.get_page_async(
                        page_no=page_no, create_words=True, create_textlines=True
                    )
                    for page_no in page_numbers
                ]
                print(f"number of tasks: {len(page_tasks)}")

                # Execute all page loading tasks in parallel
                # pages = await asyncio.gather(*page_tasks)
                # pages = await asyncio.gather(page_tasks[0:4])

                STEP = 2
                for i in range(0, len(page_tasks), STEP):
                    print(i)
                    sublist = page_tasks[i : i + STEP]
                    pages = await asyncio.gather(*sublist)

                """
                for page_task in page_tasks:
                    print(page_task)
                    await page_task
                """

            end_time = time.time()

            elapsed_time = end_time - start_time
            print(f"Elapsed time on tasks: {elapsed_time:.2f} seconds")

            overview.append(
                [os.path.basename(str(task.file_name)), num_pages, elapsed_time, True]
            )

        except Exception as exc:
            logging.error(exc)
            overview.append([os.path.basename(str(task.file_name)), -1, -1, False])

    return overview


def process_files_from_queue(
    file_queue: Queue, page_level: bool, loglevel: str, sync: bool
):
    return asyncio.run(
        async_process_files_from_queue(file_queue, page_level, loglevel, sync=sync)
    )


def main():

    directory, recursive, loglevel, page_level_parsing = parse_arguments()

    """
    task_queue = Queue()
    fetch_files_from_disk(directory, recursive, task_queue)

    overview_sync = process_files_from_queue(task_queue, page_level_parsing, loglevel, sync=True)
    print(tabulate(overview_sync, headers=["filename", "#-pages", "total-time", "success"]))
    """

    task_queue = Queue()
    fetch_files_from_disk(directory, recursive, task_queue)

    overview_async = process_files_from_queue(
        task_queue, page_level_parsing, loglevel, sync=False
    )
    print(
        tabulate(
            overview_async, headers=["filename", "#-pages", "total-time", "success"]
        )
    )

    logging.info("All files processed successfully.")


if __name__ == "__main__":
    main()
