import os

from tests.data_utils import ensure_test_data_downloaded


def pytest_sessionstart(session) -> None:
    force = os.getenv("DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD", "").lower() in {
        "1",
        "true",
        "yes",
    }
    ensure_test_data_downloaded(force=force)
