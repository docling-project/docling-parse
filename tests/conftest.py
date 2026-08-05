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
    parser.addoption(
        "--render-visualizations",
        choices=("above-tolerance", "all", "none"),
        default="above-tolerance",
        help=(
            "Which three-panel renderer comparison images to write to "
            "tests/data/visualizations (above-tolerance)."
        ),
    )


def pytest_configure(config) -> None:
    config.addinivalue_line(
        "markers",
        "groundtruth: tests that compare or update checked-in groundtruth fixtures",
    )
    config.addinivalue_line(
        "markers",
        "pypdfium: tests that compare rendering against pypdfium2 without failing",
    )


@pytest.fixture
def update_groundtruth(request) -> bool:
    return request.config.getoption("--update-groundtruth")


@pytest.fixture
def render_visualizations(request) -> str:
    return request.config.getoption("--render-visualizations")


def pytest_sessionstart(session) -> None:
    force = os.getenv("DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD", "").lower() in {
        "1",
        "true",
        "yes",
    }
    ensure_test_data_downloaded(force=force)
