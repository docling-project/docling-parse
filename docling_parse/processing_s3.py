import argparse
import hashlib
import logging
import os
import queue
import threading
from dataclasses import dataclass, field
from io import BytesIO
from typing import Dict, List

try:
    import botocore
except ImportError as e:
    raise ImportError(
        "botocore is required but not installed. Install it with `pip install botocore`."
    ) from e

try:
    import boto3
except ImportError as e:
    raise ImportError(
        "boto3 is required but not installed. Install it with `pip install boto3`."
    ) from e

# import boto3
# import botocore

from docling_parse import pdf_parser_v2  # type: ignore[attr-defined]

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)

queue_lock = threading.Lock()


@dataclass
class S3Config:

    url: str
    region: str

    verify: bool

    bucket_name: str
    prefix: str

    access_key: str
    secret_key: str

    scratch_dir: str


@dataclass
class FileTask:
    bucket_name: str

    file_key: str
    file_name: str  # Local path where the file will be processed or saved
    file_hash: str

    data: BytesIO = field(default_factory=BytesIO)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Process S3 files using multithreading."
    )
    parser.add_argument(
        "-u",
        "--endpoint-url",
        help="S3 url",
        default="https://s3.eu-de.cloud-object-storage.appdomain.cloud/",
    )
    parser.add_argument("-a", "--access-key", required=True, help="Access Key")
    parser.add_argument("-s", "--secret-key", required=True, help="Secret Key")
    parser.add_argument(
        "-r",
        "--region",
        help="region (eg: us-east-1,eu-de-standard)",
        required=False,
        default="eu-de-standard",
    )
    parser.add_argument(
        "-b",
        "--bucket-name",
        required=False,
        help="S3 Bucket name",
        default="doclaynet",
    )
    parser.add_argument(
        "-p",
        "--bucket-prefix",
        required=False,
        help="S3 Bucket prefix",
        default="source_docs/Extra/",
    )
    parser.add_argument(
        "-d",
        "--local-scratch-dir",
        required=False,
        help="local scratch directory",
        default="./scratch",
    )
    parser.add_argument(
        "-t",
        "--threads",
        required=False,
        help="processing threads",
        default=4,
    )
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

    args = parser.parse_args()

    s3config = S3Config(
        url=args.endpoint_url,
        region=args.region,
        verify=True,
        bucket_name=args.bucket_name,
        prefix=args.bucket_prefix,
        access_key=args.access_key,
        secret_key=args.secret_key,
        scratch_dir=args.local_scratch_dir,
    )

    return s3config, int(args.threads), args.log_level


def list_buckets(s3_client):

    # List the buckets
    response = s3_client.list_buckets()

    # Print the bucket names
    logging.info("------------------------\nBuckets:")
    for bucket in response.get("Buckets", []):
        logging.info(f"- {bucket['Name']}")


def fetch_files_from_s3(
    s3_client, task_queue, local_dir, bucket_name, top_level_prefix, prefix
):
    """Recursively fetch file keys from the S3 bucket and add them to the queue."""
    logging.info(f"Fetching file keys from S3 bucket with prefix: {prefix}")
    # local_files = glob.glob(os.path.join(args.local_scratch, "*.pdf"))

    paginator = s3_client.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=bucket_name, Prefix=prefix, Delimiter="/"):
        # Handle files in the current prefix
        for obj in page.get("Contents", []):

            filename = os.path.basename(obj["Key"])
            # logging.info(filename)

            if filename.endswith(".pdf"):

                """
                # Download and process file (placeholder for actual processing logic)
                response = s3_client.get_object(Bucket=bucket_name, Key=obj["Key"])
                data = response["Body"].read()

                # Generate a hash (e.g., SHA-256)
                hash_object = hashlib.sha256(data, usedforsecurity=False)
                file_hash = hash_object.hexdigest()

                local_file = os.path.join(local_dir, f"{file_hash}.pdf")

                # Save the data
                logging.info(f"processing file: {filename} -> {local_file}")
                with open(local_file, "wb") as fw:
                    fw.write(data)

                # file_queue.put(obj["Key"])
                file_queue.put((filename, file_hash, local_file))
                """

                # Create a FileTask object
                task = FileTask(
                    bucket_name=bucket_name,
                    file_key=obj["Key"],
                    file_name=filename,
                    file_hash="",
                    data=None,
                )

                with queue_lock:
                    task_queue.put(task)

                # logging.info(task)

        # Recursively handle sub-prefixes (folders)
        for common_prefix in page.get("CommonPrefixes", []):
            sub_prefix = common_prefix["Prefix"]
            logging.info(f"Entering sub-prefix: {sub_prefix}")
            fetch_files_from_s3(
                s3_client,
                task_queue,
                local_dir,
                bucket_name,
                top_level_prefix,
                sub_prefix,
            )

    # Signal the end of fetching (only add `None` at the top level)
    if top_level_prefix == prefix:  # Only at the top-level call
        task_queue.put(None)
        logging.info("Done with queue")


def print_toc(toc):

    if isinstance(toc, List):
        for _ in toc:
            print_toc(_)

    elif isinstance(toc, Dict):
        if "title" in toc and "level" in toc:
            print(" " * (4 * toc["level"]), toc["title"])

        if "children" in toc:
            for _ in toc["children"]:
                print_toc(_)


def get_logfile(scratch_dir, thread_id):

    logfile = os.path.join(scratch_dir, f"_cache_thread_{thread_id}.csv")
    return logfile


def retrieve_file(s3_client, task):

    # logging.info(task)

    # Download and process file (placeholder for actual processing logic)
    response = s3_client.get_object(Bucket=task.bucket_name, Key=task.file_key)
    task.data = BytesIO(response["Body"].read())  # response["Body"].read()

    # Generate a hash (e.g., SHA-256)
    hash_object = hashlib.sha256(task.data.read(), usedforsecurity=False)
    task.file_hash = str(hash_object.hexdigest())

    return task


def save_file(local_dir, task):
    local_file = os.path.join(local_dir, f"{task.file_hash}.pdf")

    # Save the data
    logging.info(f"saving file: {task.file_name} -> {local_file}")
    with open(local_file, "wb") as fw:
        fw.write(task.data.getvalue())


def process_files_from_queue(
    thread_id: int,
    s3_client,
    file_queue,
    bucket_name: str,
    scratch_dir: str,
    cache: set,
    loglevel: str,
):
    """Process files from the queue."""

    logfile = get_logfile(scratch_dir, thread_id)

    fw = None
    if os.path.exists(logfile):
        fw = open(logfile, "a")
    else:
        fw = open(logfile, "w")

    while True:

        if file_queue.empty():
            continue

        task = None
        with queue_lock:
            task = file_queue.get()

        if task is None:  # End of queue signal
            break

        if task.file_name in cache:
            logging.info(f" => skipping due to cache: {task.file_name}")
            continue

        try:
            task = retrieve_file(s3_client, task)
            logging.info(
                f"Thread: {thread_id}, Queue-size [{file_queue.qsize()}], Processing task: {task.file_name}"
            )

            ##save_file(scratch_dir, task)

            parser = pdf_parser_v2(loglevel)

            success = parser.load_document_from_bytesio(task.file_hash, task.data)

            if success:

                # Get number of pages
                num_pages = parser.number_of_pages(task.file_hash)
                logging.info(f" => #-pages of {task.file_name}: {num_pages}")

                # Parse page by page to minimize memory footprint
                for page in range(0, num_pages):
                    try:
                        json_doc = parser.parse_pdf_from_key_on_page(
                            task.file_hash, page
                        )
                    except:
                        save_file(scratch_dir, task)
                        logging.error(
                            f"problem with parsing {task.file_name} on page {page}"
                        )

                fw.write(f"{task.file_name},{num_pages},{task.file_hash}\n")
                fw.flush()
            else:
                save_file(scratch_dir, task)
                logging.error(f"problem with loading {task.file_name}")

            # Unload the (QPDF) document and buffers
            parser.unload_document(task.file_hash)

        except:
            logging.error(f"Error on file: {task.file_name}")


def main():

    s3_config, threads, loglevel = parse_arguments()

    os.makedirs(s3_config.scratch_dir, exist_ok=True)

    cache = set()
    for tid in range(0, 1024):
        logfile = get_logfile(s3_config.scratch_dir, tid)
        if os.path.exists(logfile):
            fr = open(logfile)
            for line in fr:
                parts = line.strip().split(",")
                cache.add(parts[0])
            fr.close()
        else:
            break

    logging.info(f"#-cached files: {len(cache)}")

    session = boto3.session.Session()

    config = botocore.config.Config(connect_timeout=60, signature_version="s3v4")

    # Initialize the S3 client
    s3_client = session.client(
        "s3",
        endpoint_url=s3_config.url,
        verify=True,
        aws_access_key_id=s3_config.access_key,
        aws_secret_access_key=s3_config.secret_key,
        region_name=s3_config.region,
        config=config,
    )

    # list_buckets(s3_client)

    file_queue = queue.Queue()

    # Create threads
    fetch_thread = threading.Thread(
        target=fetch_files_from_s3,
        args=(
            s3_client,
            file_queue,
            s3_config.scratch_dir,
            s3_config.bucket_name,
            s3_config.prefix,
            s3_config.prefix,
        ),
    )
    # Start threads
    fetch_thread.start()
    fetch_thread.join()

    process_threads = []
    for tid in range(0, threads):
        process_threads.append(
            threading.Thread(
                target=process_files_from_queue,
                args=(
                    tid,
                    s3_client,
                    file_queue,
                    s3_config.bucket_name,
                    s3_config.scratch_dir,
                    cache,
                    loglevel,
                ),
            )
        )

    for _ in process_threads:
        _.start()

    # Wait for threads to complete

    for _ in process_threads:
        _.join()

    logging.info("All files processed successfully.")


if __name__ == "__main__":
    main()
