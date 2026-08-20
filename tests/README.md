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

## Page Selection

The regression corpus grows by whole documents, but consecutive pages of one
document mostly repeat each other's code paths, so the suite decodes and
verifies only a sample of each document. The sample lives in
`tests/regression_page_selection.py` and is exposed as
`PARSER_PAGE_RESTRICTIONS` in `tests/constants.py`; every parser and renderer
regression test honours it.

Regenerate it after adding documents to `tests/data/regression`:

```bash
uv run python tests/tools/select_regression_pages.py
uv run python tests/tools/select_regression_pages.py --check   # CI-style verification
```

At most five pages per document are kept, drawn from a generator seeded with
the document name, so the sample is reproducible and adding one document never
reshuffles another. Two sets of pages override the sample: `PINNED_PAGES`, for
documents that are in the corpus for one specific page or are too large to
sample blindly, and `ALWAYS_KEEP_PAGES`, the pages that once exposed a parser
defect. A document may therefore end up with more than five pages.

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
- `test_pypdfium_render.py`: non-failing comparison of the render output against
  pypdfium2, with three-panel visualizations.
- `rendering_regression.py`: shared helpers for renderer groundtruth comparison
  and update logic.
- `test_embedded_fonts.py`: embedded-font and font-resolution renderer behavior.
- `test_locale_safety.py`: locale-sensitive parsing and rendering behavior.
- `tools/`: maintenance entry points for the test data itself, not tests. They
  are plain scripts (pytest never collects them, as their names do not match
  `test_*.py`) and are the only place outside the suite allowed to import from
  `tests`.

## Renderer Regression Checks

Renderer regression tests use the Blend2D rendering path exposed through
`DoclingThreadedPdfParser`, not the `docling-core` visualizer.

For each selected rendered page, the test can compare:

- full-page PNG output from `PageParseResult.get_image()`;
- render instruction JSON from `_export_render_instructions_json()`;
- bitmap artifact metadata and exported bitmap image bytes from
  `_export_bitmap_artifacts()`.

### Bitmap artifacts

Each bitmap artifact reports a `source`: `xobject` (an image XObject painted by
`Do`), `inline` (a `BI ... ID ... EI` image) or `type3_glyph` (one rasterised
Type3 glyph).

Type3 glyphs are **not stored**. One is emitted per painted character, so a
single page of Type3 text yields hundreds of near-identical masks that add
nothing the full-page image comparison does not already cover. Only their count
is recorded, so a page that stops emitting them, or suddenly emits twice as
many, is still noticed.

The remaining bitmaps are described in one file per page,
`bitmaps/<pdf-name>.page_no_<n>.bitmaps.json`, holding the metadata of every
bitmap the page painted together with a `raw_sha256` of its decoded samples and
an `encoded_sha256` of the exported container. Every artifact is therefore
compared exactly, whether or not its bytes are kept.

The bytes are kept only for a subset, written to `bitmap_data/`.
`select_retained_bitmaps()` keeps the first artifact of each distinct
`(source, pixel_format, image_mask, extension)` signature on the page: those
four fields pick the decode path, so one sample per signature per page leaves a
byte-level example of every path that page exercises. Across the corpus this is
around 600 files instead of roughly 47000, and the retained ones are compared
byte for byte so a mismatch can be looked at rather than only reported as a
hash.

Both limits matter because the dataset lives in Git LFS, which does not cope
with more than about 10k entries in one directory.

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

## Cross-Renderer Comparison Against pypdfium2

`test_pypdfium_render.py::test_rendered_pages_match_pypdfium` is structured like
`test_rendered_pages_match_groundtruth`: same regression PDF set, same
`PARSER_PAGE_RESTRICTIONS`, same threaded parser at scale 2.0. The difference is
where the reference image comes from: pypdfium2 renders it on the spot instead
of it being read from the Hugging Face dataset.

```bash
uv run pytest tests/test_pypdfium_render.py -q -s
```

Both renders are composited onto opaque white before they are compared, because
the two renderers disagree on what an untouched page pixel is: pypdfium2 paints
an opaque page, while the Blend2D path can leave the background transparent.

### This test never fails on pixel differences

pypdfium2 is an independent implementation, so the per-page numbers are a
report, not a contract. The test prints the metric table and the list of pages
above `PYPDFIUM_IMAGE_TOLERANCE` (reported, not enforced), and only fails when
no page could be compared at all. It is skipped when pypdfium2 is not installed.

Consequently `PYPDFIUM_IMAGE_TOLERANCE` is not a regression limit. It is only
the cut-off that decides which pages are called out in the report and, by
default, which ones get a visualization written.

### Three-panel visualizations

Comparison images are written to:

```text
tests/data/visualizations/delta_<mean_abs_error>_<pdf-name>.page_no_<n>.png
```

Left to right, the panels are pypdfium2, docling-parse, and their difference
amplified by `DIFFERENCE_AMPLIFICATION`. Each panel carries a label whose font
scales with the page width, so the image stays readable when the full
three-panel png is scaled down to fit on screen.

`--render-visualizations` selects which pages get one:

```bash
# only pages above tolerance (default)
uv run pytest tests/test_pypdfium_render.py -q -s --render-visualizations above-tolerance
# every compared page
uv run pytest tests/test_pypdfium_render.py -q -s --render-visualizations all
# none
uv run pytest tests/test_pypdfium_render.py -q -s --render-visualizations none
```

A full run over the regression set writes tens of megabytes of png into
`tests/data/visualizations`. That directory is not part of the downloaded
dataset and can be deleted at any time.

### Reading the result

A run after page `/Rotate` was implemented in the Blend2D renderer compared 93
pages, of which 51 were reported above tolerance. The large deltas are the
interesting ones, and the visualization usually makes the cause obvious at a
glance.

Page rotation is applied: `rotated_page_01`, `rotated_image` and
`jbig2_test_01` render landscape at 1584x1224 like pypdfium2, with mean
absolute errors of 1.21, 0.22 and 0.22, and the
`ocr_test_rotated_{000,090,180,270}` set agrees with pypdfium2 on orientation
as well.

What is left in the report is mostly one of three things:

- A page size that is off by one or two pixels. 33 of the 93 pages report
  `size_match False`, with the pypdfium2 render one or two pixels wider and/or
  taller (for example 1190x1682 against 1191x1684). A size mismatch on its own
  already puts a page above tolerance, and the images are then compared on
  their common top-left area, so the accumulated sub-pixel scale difference
  shifts glyph and image edges towards the far side of the page. Such a page
  can report a large `mean_abs_error` while looking identical side by side:
  `complex_invisible_fonts_01` at 24.80 is a full-page photograph that differs
  only by that offset.
- Content one renderer draws and the other does not. `right_to_left_02` (45.28)
  is a tiling pattern that docling-parse paints over the whole page.
  `device_n_black` used to be the other direction — pypdfium2 drew a
  gradient-filled banner that docling-parse left blank — because the banner's
  `sh` shading sits under a hexagonal clip and the renderer only honoured
  rectangular clips. It now fills the clip outline, and with the DeviceCMYK
  conversion replaced the page is down from 48.37 to 0.79. `form_fields` and
  `fillable_form` were the widget-bounds overlay, which is now opt-in.
- Anti-aliasing, hinting and glyph rasterization, which differ between the two
  renderers on nearly every page. The size-matching pages sit around a mean
  absolute error of 2.6, and those small deltas are not worth chasing.

### Shared helpers

The comparison machinery lives in `rendering_regression.py` and is shared with
the groundtruth test:

- `measure_image_pair()` computes the pixel metrics for any two images;
  `measure_image_comparison()` is the wrapper that loads the groundtruth png.
- `render_pypdfium_page()` renders the reference page.
- `flatten_on_white()` puts both renders on the same page background.
- `amplified_difference()` builds the difference panel, and is also used for the
  `render_deltas` diff artifacts.
- `write_comparison_visualization()` assembles and writes the three-panel png.

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
  groundtruth/parser/     parser JSON and text-line groundtruth
  groundtruth/render/     renderer PNGs, instruction JSON, and bitmap metadata
  groundtruth-legacy/     groundtruth of documents that left the corpus
  cases/                  focused case fixtures
  errors/                 failure and error-handling fixtures
  synthetic/              synthetic PDF fixtures
  render_deltas/          diff artifacts written on image comparison failure
  visualizations/         three-panel renderer comparison images
```

`render_deltas/` and `visualizations/` are produced by test runs and are not
part of the downloaded dataset.

Groundtruth files are named after the document they came from, so they linger
after a document leaves `tests/data/regression`. `prune_groundtruth.py` moves
those orphans into `groundtruth-legacy/`, mirroring the directory layout, so
they are archived rather than deleted:

```bash
uv run python tests/tools/prune_groundtruth.py --dry-run
uv run python tests/tools/prune_groundtruth.py
uv run python tests/tools/prune_groundtruth.py --pages   # also drop unselected pages
```

Renderer artifact naming follows this pattern:

```text
pages/<pdf-name>.page_no_<n>.full_page.png
instructions/<pdf-name>.page_no_<n>.instructions.json
bitmaps/<pdf-name>.page_no_<n>.bitmaps.json
bitmap_data/<pdf-name>.page_no_<n>.bitmap_<i>.<png|jpg|bin>
```

The per-page bitmap JSON stores metadata and hashes for every bitmap; the
`bitmap_data/` files hold the bytes of the retained subset only.

## Working With Dataset Changes

Because `tests/data` is a downloaded snapshot, it is not a nested Git checkout
by default. After running tests with `--update-groundtruth`, inspect changed
files in `tests/data` before publishing a new dataset revision.

Recommended review flow:

```bash
find tests/data/groundtruth -type f -newer <marker>
```

or use a managed local checkout of the Hugging Face dataset when doing larger
dataset updates.

Do not commit Hugging Face credentials, authenticated remotes, or tokens into
this repository. Publishing dataset changes should use local credentials from
the environment, for example `HF_TOKEN` or `HUGGINGFACE_HUB_TOKEN`.

After publishing a new dataset revision, update `HF_DATASET_REVISION` in
`tests/data_utils.py` so CI and local runs use the intended snapshot.
