# Test Suite

This directory contains the parser, renderer, threading, font, locale, and
bitmap tests for `docling-parse`.

## Naming Convention

Every test file declares in its name what it needs to run:

- `test_unit_*.py` — self-contained. The document under test is built in the
  file (see `pdf_builder.py`) or in memory, and the assertions are about the
  parser's own output. These need nothing but the extension module, so they
  run on a bare checkout in seconds.
- `test_regression_*.py` — driven by the regression corpus under
  `tests/data/regression`, and mostly compared against the stored groundtruth
  in `tests/data/groundtruth`. These need the Hugging Face snapshot, which the
  suite downloads at session start.

A new test belongs in the first group unless it genuinely needs the corpus: a
failure there names the operator that broke, while a corpus failure only names
a page.

## Running Tests

Run the full test suite:

```bash
uv run pytest
```

Run only the self-contained tests (no corpus download, a few seconds):

```bash
uv run pytest tests/test_unit_*.py -q
```

Run focused parser and renderer regression tests:

```bash
uv run pytest tests/test_regression_parse.py::test_reference_documents_from_filenames -q
uv run pytest tests/test_regression_threaded_render.py::test_rendered_pages_match_groundtruth -q
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
uv run pytest tests/test_regression_parse.py::test_reference_documents_from_filenames --update-groundtruth -q
uv run pytest tests/test_regression_threaded_render.py::test_rendered_pages_match_groundtruth --update-groundtruth -q
```

The test-local wrapper adds `--update-groundtruth` automatically:

```bash
uv run python tests/update_groundtruth.py
uv run python tests/update_groundtruth.py tests/test_regression_threaded_render.py::test_rendered_pages_match_groundtruth
```

This flag updates checked regression artifacts under `tests/data`, not source
code in the main repository.

### Artifact encoding

Groundtruth JSON and text-line artifacts are written compact and gzipped
(`<name>.json.gz`, `<name>.txt.gz`). Reads accept either encoding and prefer the
gzipped one when both are present, so a dataset revision can be migrated one
artifact family at a time and an old checkout keeps working.

`DOCLING_PARSE_GT_FORMAT` steers writes only:

| value | writes |
|---|---|
| `gz` (default) | compact JSON, gzipped |
| `plain` | the legacy `indent=2`, uncompressed form, byte for byte |

Compression is worth it: on this corpus the parser pages shrink 4.6x and the
render instructions 16.6x, taking the tree from ~6.0 GB to ~1.7 GB. Reading gets
slightly *faster* rather than slower, because gunzip plus parse beats parsing
2.5x more indented text.

Two families are deliberately left uncompressed:

- **`*.char.txt`, `*.word.txt`, `*.line.txt`** -- the textline exports, so a
  refresh is reviewable as a text diff rather than an opaque pointer swap. They
  are line-oriented, so they diff cell by cell. This costs ~223 MB (264 MB plain
  against 40 MB gzipped) and only pays off alongside the `.gitattributes` change
  described below.
- **`*.delta.txt`** -- not groundtruth; written next to a failing artifact for a
  human to read and deleted once the page passes.

`render/pages` and `render/bitmap_data` are never touched either: PNG and JPEG
payloads are already compressed.

The list lives in `NEVER_COMPRESS_SUFFIXES` in `tests/groundtruth_io.py`;
`compresses(path)` answers it for a single artifact.

`tests/groundtruth_io.py` is the single place that knows about this. Use its
helpers rather than `open()` when touching groundtruth:

```python
from tests.groundtruth_io import (
    groundtruth_exists,      # true for either encoding
    load_groundtruth_json,   # read, auto-detecting encoding
    dump_groundtruth_json,   # write, following DOCLING_PARSE_GT_FORMAT
    load_segmented_page,     # SegmentedPdfPage from either encoding
    read_groundtruth_text,
    write_groundtruth_text,
    resolve_groundtruth_path,  # the path actually on disk, for error messages
)
```

`SegmentedPdfPage.load_from_json()` opens the path itself and so cannot read a
gzipped artifact; `load_segmented_page()` is the drop-in replacement.

### Re-encoding without re-parsing

Converting between the two encodings is a pure format change, so it does not
need a parser run:

```bash
uv run python tests/tools/recompress_groundtruth.py --to gz --dry-run
uv run python tests/tools/recompress_groundtruth.py --to gz
uv run python tests/tools/recompress_groundtruth.py --to plain   # undo
```

Keep a re-encoding revision separate from a content revision. If both land at
once, every file changes and a real regression becomes indistinguishable from a
re-serialization:

```bash
# 1. content only -- same encoding, so the diff is genuine content
DOCLING_PARSE_GT_FORMAT=plain uv run python tests/update_groundtruth.py
#    review, publish, bump HF_DATASET_REVISION

# 2. re-encode as its own revision
uv run python tests/tools/recompress_groundtruth.py --to gz
#    publish, bump HF_DATASET_REVISION again
```

Gzip output is written with `mtime=0`. Without that, gzip stamps the current
time into the header and unchanged content would produce different bytes on
every run, defeating Git LFS deduplication in the dataset repo.

### Reviewing a refresh in the dataset repo

Keeping the textline exports uncompressed only helps if the dataset's
`.gitattributes` also stops routing them through Git LFS. As published it ends
with

```gitattributes
*.txt filter=lfs diff=lfs merge=lfs -text
```

and since the last matching line wins, every `.txt` is an LFS pointer. `git
diff` then shows only an oid and a size changing -- no more informative than the
gzipped form:

```diff
-oid sha256:e9024f1a07d29d52ad3aa5e1a18e94db1f3a9fd32b89e39d47c472cd99071e13
-size 18
+oid sha256:832ba8615786d74d17e69c3efe65fa35ea5404f7a2c1e260824633446e76abe1
+size 26
```

To get real text diffs, append a line that takes the textline exports back out
of LFS (after the catch-all, so it wins):

```gitattributes
groundtruth/parser/*.py.json.char.txt !filter !diff !merge text
groundtruth/parser/*.py.json.word.txt !filter !diff !merge text
groundtruth/parser/*.py.json.line.txt !filter !diff !merge text
```

The largest such file is 3.5 MB, comfortably inside what the Hub accepts outside
LFS. Storing them as ordinary Git blobs also grows the repo more slowly than LFS
does across revisions: Git delta-compresses successive versions of a text file,
whereas LFS stores a full copy of every version.

Note that this rewrites how those paths are stored, so it takes effect for
commits made after the change.

## Main Test Areas

### Corpus-driven (`test_regression_*.py`)

- `test_regression_parse.py`: single-threaded parser. Compares parsed
  `SegmentedPdfPage` JSON and text-line exports against `tests/data/groundtruth`.
- `test_regression_threaded_parse.py`: threaded parser behavior and content
  materialization, against the same groundtruth.
- `test_regression_threaded_render.py`: threaded parse-and-render through
  `DoclingThreadedPdfParser`, against the renderer groundtruth.
- `test_regression_pypdfium_render.py`: non-failing comparison of the render
  output against pypdfium2, with three-panel visualizations.
- `test_regression_embedded_fonts.py`: embedded-font and font-resolution
  behavior, on a corpus document that carries both kinds of font program.
- `test_regression_locale_safety.py`: locale-sensitive parsing and rendering,
  swept over the whole corpus.

### Self-contained (`test_unit_*.py`)

Each builds the document it is about and asserts on the result:

- colour and images: `colorspace`, `colorspaces`, `indexed_images`,
  `jpeg_images`, `ccitt_images`, `transparency`
- vector painting: `shading`, `shadings`, `patterns`, `clipping`
- text and fonts: `actual_text`, `cjk_fonts`, `cmap_encoding`,
  `font_fallback`, `font_name_resolution`, `gid_glyph_names`,
  `glyph_proportions`, `standard_font_widths`, `text_render_modes`,
  `tounicode_fallback`, `type3_fonts`, `vertical_writing`
- documents and annotations: `load_failure`, `widget_appearances`

### Support modules (not collected)

- `pdf_builder.py`: writes the small PDFs the self-contained tests are about.
- `groundtruth_io.py`: the only module that knows how groundtruth artifacts are
  encoded on disk. Stdlib-only, so `tools/` can import it too. See
  [Artifact encoding](#artifact-encoding).
- `rendering_regression.py`: shared helpers for image comparison, renderer
  groundtruth and update logic. The self-contained tests use its measurement
  helpers (`region_image`, `coverage_ratio`, `center_color`) without touching
  groundtruth.
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

### Substituted system fonts

Nine documents in the corpus name CJK fonts -- `/Batang`, `/Gulim`, `/宋体`,
`/楷体_GB2312`, `/HGMarugothicMPRO` -- and embed none of them. There is nothing
in the PDF to draw with, so the renderer substitutes whatever CJK face the host
has, and *that* is what the comparison sees: macOS, where this groundtruth was
written, substitutes Hiragino / AppleGothic / PingFang; Linux substitutes Noto
CJK. Two different typefaces drawing the same page differ far more than any
rendering change would.

Those documents are listed in `FONT_SUBSTITUTED_DOCUMENTS` in
`test_regression_threaded_render.py` and compared at
`SUBSTITUTED_FONT_IMAGE_TOLERANCE` instead of `RENDERER_IMAGE_TOLERANCE`: wide
enough for the substitution, and no wider, so a host that renders their CJK as
`.notdef` boxes still fails. Every other page keeps the tight tolerance --
loosening it globally would have retired the signal on the ~200 pages that
substitute nothing.

To run them locally the way CI does, install a CJK font and pin it:

```sh
# Debian / Ubuntu
sudo apt-get install -y fonts-noto-cjk
export DOCLING_PARSE_CJK_FALLBACK_FONT=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc

# Fedora / RHEL  (the package WITHOUT -vf-: Blend2D cannot read CFF2 outlines)
sudo dnf install -y google-noto-sans-cjk-fonts
export DOCLING_PARSE_CJK_FALLBACK_FONT=/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc
```

`DOCLING_PARSE_FALLBACK_FONT` and `DOCLING_PARSE_ARABIC_FALLBACK_FONT` pin the
Latin and Arabic substitutions the same way; see the "System fonts" section of
the top-level [README](../README.md). `.github/workflows/checks.yml` installs
the packages and sets `DOCLING_PARSE_CJK_FALLBACK_FONT` so a runner-image
change cannot quietly become a rendering change.

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

With one exception: an artifact whose `filters` name `/JPXDecode`. JPEG 2000's
irreversible 9/7 wavelet is a floating-point transform, and OpenJPEG runs it in
single precision, so the same codestream decodes to samples that differ by one
level here and there between an arm64 macOS build and an x86-64 Linux one
(most likely FMA contraction in the inverse DWT; two independent x86-64 builds
agree byte for byte). That is far below what the exported JPEG quantises away --
the container re-encoded from either came out identical byte for byte on the
image that surfaced this -- but it changes a hash, so a byte-exact groundtruth
for such an image only ever holds on the machine that wrote it, and under
`USE_SYSTEM_DEPS=ON` not even there. Those artifacts drop both hashes and the byte comparison of their
retained bytes, and are compared on a `raw_profile` instead: their samples
averaged down to an 8x8 grid per channel, which the one-level noise disappears
into and a decode that actually changed does not. Nothing else in the decode
path is float -- libjpeg's islow IDCT, CCITT, JBIG2 and Flate are integer -- so
nothing else is excused. `test_unit_bitmap_artifacts.py` holds both halves of
this.

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

`test_regression_pypdfium_render.py::test_rendered_pages_match_pypdfium` is structured like
`test_rendered_pages_match_groundtruth`: same regression PDF set, same
`PARSER_PAGE_RESTRICTIONS`, same threaded parser at scale 2.0. The difference is
where the reference image comes from: pypdfium2 renders it on the spot instead
of it being read from the Hugging Face dataset.

```bash
uv run pytest tests/test_regression_pypdfium_render.py -q -s
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
uv run pytest tests/test_regression_pypdfium_render.py -q -s --render-visualizations above-tolerance
# every compared page
uv run pytest tests/test_regression_pypdfium_render.py -q -s --render-visualizations all
# none
uv run pytest tests/test_regression_pypdfium_render.py -q -s --render-visualizations none
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

The pinned revision is defined in `tests/constants.py` as
`HF_DATASET_REVISION`, and consumed by `ensure_test_data_downloaded()` in
`tests/data_utils.py`, which pytest calls from `tests/conftest.py` before tests
start.

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
  groundtruth/parser/     parser JSON (gzipped) and text-line exports (plain)
  groundtruth/render/     renderer PNGs, instruction JSON (gzipped), bitmap metadata
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
instructions/<pdf-name>.page_no_<n>.instructions.json.gz
bitmaps/<pdf-name>.page_no_<n>.bitmaps.json.gz
bitmap_data/<pdf-name>.page_no_<n>.bitmap_<i>.<png|jpg|bin>
```

The `.gz` suffix is present under the default `DOCLING_PARSE_GT_FORMAT=gz`; the
`.png`, `.jpg` and `.bin` payloads and the parser textline exports are never
gzipped. `prune_groundtruth.py` matches on the `<pdf-name>.page_no_<n>` prefix,
so it handles either encoding without special-casing.

The per-page bitmap JSON stores metadata and hashes for every bitmap; the
`bitmap_data/` files hold the bytes of the retained subset only.

## Working With Dataset Changes

Because `tests/data` is a downloaded snapshot, it is not a nested Git checkout
by default -- it is gitignored and has no `.git` of its own, so `git status` and
`git diff` cannot tell you what a refresh changed. Inspect it explicitly before
publishing a new dataset revision.

Do **not** review by modification time. `--update-groundtruth` rewrites every
artifact it covers whether or not the content changed, so `find -newer` reports
all of them and tells you nothing about the blast radius.

Compare content hashes instead:

```bash
S=/tmp/gt-review; mkdir -p $S

# before
find tests/data/groundtruth/parser -type f | sort | xargs shasum -a 1 > $S/before.sha1

uv run python tests/update_groundtruth.py

# after
find tests/data/groundtruth/parser -type f | sort | xargs shasum -a 1 > $S/after.sha1

# what genuinely changed (CHG), and what is new (NEW)
join -j 2 -a 2 -e MISSING -o 0,1.1,2.1 \
     <(sort -k2 $S/before.sha1) <(sort -k2 $S/after.sha1) \
  | awk '$2!=$3 {print ($2=="MISSING"?"NEW  ":"CHG  ") $1}'
```

The `-e MISSING` matters: without it `awk` collapses the empty field and
misreports new files as changed. Run the same over
`tests/data/groundtruth/render` when refreshing renderer groundtruth.

A run that changes far more files than the fix you made would explain is the
signal to stop and look, not to publish.

There is no local undo -- the manifest says what changed but restores nothing.
Recovery is a re-download of the still-pinned revision
(`DOCLING_PARSE_TEST_DATA_FORCE_DOWNLOAD=1`), which is cheaper than keeping a
copy of a multi-gigabyte tree. Alternatively, use a managed local checkout of
the Hugging Face dataset when doing larger dataset updates.

Do not commit Hugging Face credentials, authenticated remotes, or tokens into
this repository. Publishing dataset changes should use local credentials from
the environment, for example `HF_TOKEN` or `HUGGINGFACE_HUB_TOKEN`.

After publishing a new dataset revision, update `HF_DATASET_REVISION` in
`tests/constants.py` so CI and local runs use the intended snapshot.
`tests/data_utils.py` imports it from there and does the download.
