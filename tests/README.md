# Test Suite

This directory contains the parser, renderer, threading, font, locale, and
bitmap regression tests for `docling-parse`.

## Running Tests

Run the full test suite:

```bash
uv run pytest
```

Run focused parser and renderer regression tests:

```bash
uv run pytest tests/test_parse.py::test_reference_documents_from_filenames -q
uv run pytest tests/test_threaded_render.py::test_rendered_pages_match_groundtruth -q
```

The test suite downloads regression data automatically at pytest session start.
To force a fresh download of the pinned Hugging Face snapshot:

```bash
DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD=1 uv run pytest
```

## Updating Groundtruth

Normal test runs are read-only. To intentionally refresh parser and renderer
groundtruth artifacts, pass `--update-groundtruth`:

```bash
uv run pytest tests/test_parse.py::test_reference_documents_from_filenames --update-groundtruth -q
uv run pytest tests/test_threaded_render.py::test_rendered_pages_match_groundtruth --update-groundtruth -q
```

The test-local wrapper adds `--update-groundtruth` automatically:

```bash
uv run python tests/update_groundtruth.py
uv run python tests/update_groundtruth.py tests/test_threaded_render.py::test_rendered_pages_match_groundtruth
```

This flag updates checked regression artifacts under `tests/data`, not source
code in the main repository.

## Main Test Areas

- `test_parse.py`: single-threaded parser regression tests. These compare parsed
  `SegmentedPdfPage` JSON and text-line exports against `tests/data/groundtruth`.
- `test_threaded_parse.py`: threaded parser behavior and content materialization.
- `test_threaded_render.py`: threaded parse-and-render behavior using
  `DoclingThreadedPdfParser`.
- `rendering_regression.py`: shared helpers for renderer groundtruth comparison
  and update logic.
- `test_embedded_fonts.py`: embedded-font and font-resolution renderer behavior.
- `test_locale_safety.py`: locale-sensitive parsing and rendering behavior.

## Renderer Regression Checks

Renderer regression tests use the Blend2D rendering path exposed through
`DoclingThreadedPdfParser`, not the `docling-core` visualizer.

For each selected rendered page, the test can compare:

- full-page PNG output from `PageParseResult.get_image()`;
- render instruction JSON from `_export_render_instructions_json()`;
- bitmap artifact metadata and exported bitmap image bytes from
  `_export_bitmap_artifacts()`.

Full-page image comparison is intentionally tolerant rather than exact. The
comparison requires identical dimensions, then checks mean absolute error and
changed-pixel ratio after applying a per-pixel threshold. This avoids making CI
too sensitive to small rasterization differences while still catching material
rendering regressions.

On image comparison failure, diagnostic files are written under:

```text
tests/data/render_deltas/
```

The generated files include actual, expected, and amplified diff PNGs.

## Regression Data

The test data lives in a Hugging Face dataset repository:

```text
docling-project/regression-dataset-for-docling-parse
```

The pinned revision is defined in `tests/data_utils.py` as
`HF_DATASET_REVISION`. Pytest calls `ensure_test_data_downloaded()` from
`tests/conftest.py` before tests start.

Current behavior:

- `tests/data` is created locally if missing.
- The pinned dataset snapshot is downloaded with `huggingface_hub.snapshot_download`.
- If `tests/data` already contains files, it is reused.
- `DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD=1` forces a redownload.

`tests/data` is intentionally not treated as source code in this repository.
It is populated from the external dataset and ignored by the main repository.

## Data Layout

The downloaded dataset is organized as follows:

```text
tests/data/
  regression/             source PDFs used by parser and renderer regressions
  groundtruth/            parser JSON and text-line groundtruth
  groundtruth_renderer/   renderer PNGs, instruction JSON, and bitmap artifacts
  cases/                  focused case fixtures
  errors/                 failure and error-handling fixtures
  synthetic/              synthetic PDF fixtures
```

Renderer artifact naming follows this pattern:

```text
<pdf-name>.page_no_<n>.full_page.png
<pdf-name>.page_no_<n>.instructions.json
<pdf-name>.page_no_<n>.bitmap_<i>.json
<pdf-name>.page_no_<n>.bitmap_<i>.<png|jpg|bin>
```

Bitmap JSON stores metadata and hashes. Raw bitmap bytes are stored separately
as image or binary artifact files.

## Working With Dataset Changes

Because `tests/data` is a downloaded snapshot, it is not a nested Git checkout
by default. After running tests with `--update-groundtruth`, inspect changed
files in `tests/data` before publishing a new dataset revision.

Recommended review flow:

```bash
find tests/data/groundtruth tests/data/groundtruth_renderer -type f -newer <marker>
```

or use a managed local checkout of the Hugging Face dataset when doing larger
dataset updates.

Do not commit Hugging Face credentials, authenticated remotes, or tokens into
this repository. Publishing dataset changes should use local credentials from
the environment, for example `HF_TOKEN` or `HUGGINGFACE_HUB_TOKEN`.

After publishing a new dataset revision, update `HF_DATASET_REVISION` in
`tests/data_utils.py` so CI and local runs use the intended snapshot.
