import os
from pathlib import Path

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


def _selection_needs_corpus(config) -> bool:
    """Whether the selected tests can reach the regression corpus.

    Only a selection naming `test_unit_*` files exclusively is known not to:
    those build the document they are about. Everything else -- a bare
    `pytest`, a directory, a `-k` expression -- is assumed to need the corpus,
    so the cheap case has to prove itself and a mistake here costs a download
    rather than a confusing failure.
    """
    paths = [arg for arg in config.args if not arg.startswith("-")]
    if not paths:
        return True

    return not all(
        Path(path.split("::")[0]).name.startswith("test_unit_") for path in paths
    )


def pytest_sessionstart(session) -> None:
    if not _selection_needs_corpus(session.config):
        return

    force = os.getenv("DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD", "").lower() in {
        "1",
        "true",
        "yes",
    }
    ensure_test_data_downloaded(force=force)
