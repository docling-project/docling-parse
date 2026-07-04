import os

import pytest

from tests.data_utils import ensure_test_data_downloaded


def pytest_addoption(parser) -> None:
    parser.addoption(
        "--update-groundtruth",
        action="store_true",
        default=False,
        help="Rewrite parser and renderer groundtruth fixtures.",
    )


def pytest_configure(config) -> None:
    config.addinivalue_line(
        "markers",
        "groundtruth: tests that compare or update checked-in groundtruth fixtures",
    )


@pytest.fixture
def update_groundtruth(request) -> bool:
    return request.config.getoption("--update-groundtruth")


def pytest_sessionstart(session) -> None:
    force = os.getenv("DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD", "").lower() in {
        "1",
        "true",
        "yes",
    }
    ensure_test_data_downloaded(force=force)
