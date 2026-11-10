# Publish the consolidated Blend2D fork and switch docling-parse

## Current published state (2026-10-31)

The fork changes have now been committed and published as
`08d76290aded4885aafacd89c59303283a72a118`, the head of
`PeterStaar-IBM/blend2d:docling/0.21.2-patches` and upstream Blend2D PR #275.
There is no published release tag. docling-parse now pins that full commit
SHA directly; tag creation is optional and no longer blocks its build.

For docling-parse PR #433, the working-tree diff against its base changes only
`cmake/extlib_blend2d.cmake` under `cmake/`. The previously added four patch
files and unused helper are deleted, so they disappear from the PR's final
tree when those deletions are committed. The AsmJit pin remains unchanged.

The normal CMake remote-fetch path has been verified with
`FETCHCONTENT_SOURCE_DIR_BLEND2D` cleared: the populated source checkout has
the fork URL, the exact pinned SHA, and no local modifications.

The remote-fetched build also passed all five native CTest cases and the
Python unit/Honda tests (317 passed, 2 skipped). Its rebuilt Python extension
is byte-identical to the previously validated build, so the recorded corpus
comparison remains applicable without a new rendering run.

The sections below preserve the earlier release-preparation instructions.
**Steps 1 and 2 are already superseded by the published commit and current
full-SHA pin; do not rerun the old tag-to-SHA replacement script.** Use step 4
to record the docling-parse changes after review. No staging, commits, tags,
or pushes are performed by the assistant.

## Historical preparation (2026-10-30)

- Fork checkout: `.references/blend2d`, branch `docling/0.21.2-patches`.
- Base: upstream `6dbc2cefbc996379e07104e34519a440b49b15d7` (0.21.2).
- Four upstream changes, including their native tests, are applied as unstaged
  working-tree changes. No commits, staging, tags, or pushes were performed.
- `DOCLING_BACKPORTS.md` in the fork records each upstream commit and attribution.
- Planned immutable release tag: `docling-0.21.2-patches.1`.
- docling-parse now selects `PeterStaar-IBM/blend2d` and that tag. Its four
  downstream patch files and unused patch helper are removed; tests remain.
- AsmJit remains pinned to `0bd5787b54b575ed94bf32ac452153b34385c514`.

The final release commit SHA does not exist until you commit the fork changes.
The commands below publish the release, then replace the temporary tag-name
pin in docling-parse with the resulting full SHA. Run them from the
docling-parse project root after reviewing the diffs.

## 1. Commit and publish the fork

```bash
git -C .references/blend2d diff
git -C .references/blend2d add -- \
  CMakeLists.txt DOCLING_BACKPORTS.md \
  blend2d/core/path_test.cpp \
  blend2d/core/pathstroke.cpp \
  blend2d/core/pathstroke_test.cpp \
  blend2d/pipeline/jit/fillpart.cpp \
  blend2d/pipeline/reference/fillgeneric_p.h
git -C .references/blend2d commit -s \
  -m "fix: backport analytic fill and stroker fixes for Docling" \
  -m "Backports blend2d/blend2d#268, #270, #271, and #273 onto 6dbc2ce. Original commit IDs are recorded in DOCLING_BACKPORTS.md." \
  -m 'Co-authored-by: Jeff Witt <1848307+wittjeff@users.noreply.github.com>
Co-authored-by: Claude Opus 5.5 (1M context) <noreply@anthropic.com>'
git -C .references/blend2d tag -a docling-0.21.2-patches.1 \
  -m "Docling Blend2D 0.21.2 backports: upstream #268, #270, #271, #273"
git -C .references/blend2d push -u origin docling/0.21.2-patches
git -C .references/blend2d push origin refs/tags/docling-0.21.2-patches.1
```

The upstream fixes share `pathstroke.cpp`, so these commands make one
consolidated commit and retain the original authors' attribution in its
trailers and the provenance document. The fork's `master` branch is unchanged.
Do not move the published tag; subsequent releases get a new tag.

## 2. Resolve the release to its immutable SHA

This changes only the Blend2D `GIT_TAG` value, retaining the release name in
the adjacent CMake comment:

```bash
blend2d_release_sha="$(git -C .references/blend2d rev-parse 'docling-0.21.2-patches.1^{commit}')"
.venv/bin/python - "$blend2d_release_sha" <<'PY'
from pathlib import Path
import re
import sys

revision = sys.argv[1]
assert re.fullmatch(r"[0-9a-f]{40}", revision)
path = Path("cmake/extlib_blend2d.cmake")
old = "GIT_TAG        docling-0.21.2-patches.1"
text = path.read_text()
assert text.count(old) == 1, "expected the unpublished release-tag pin once"
path.write_text(text.replace(old, "GIT_TAG        " + revision))
print("Pinned Blend2D release to", revision)
PY
```

## 3. Verify the published source

Local validation used `FETCHCONTENT_SOURCE_DIR_BLEND2D` to consume the uncommitted
fork checkout. Clear that override after publishing so this check fetches the
fork release through the normal dependency declaration:

```bash
cmake -S . -B build -U FETCHCONTENT_SOURCE_DIR_BLEND2D
cmake --build build --target pdf_parsers blend2d_patch_regression --parallel 4
ctest --test-dir build --output-on-failure -R '^blend2d_'
```

No default remote-fetch verification is claimed before the commit/tag exist.
The configured local source override remains available until you clear it.

## 4. Commit the docling-parse migration

Review the paths first; these commands intentionally exclude `.references`
and the unrelated planning files in this checkout:

```bash
git diff
git add -- \
  cmake/extlib_blend2d.cmake \
  cmake/apply_patch.cmake \
  cmake/blend2d-fillanalytic-stale-i.patch \
  cmake/blend2d-stroke-offset-quad-stall.patch \
  cmake/blend2d-stroke-offset-degenerate-legs.patch \
  cmake/blend2d-stroke-flat-cubic-linear-cusp.patch \
  tests/README.md \
  tests/test_unit_affine_image_clip.py \
  tests/test_unit_subpixel_stroke_render.py \
  tests/test_unit_stroke_cusp_spike.py \
  tests/test_unit_flat_cubic_cusp_stroke.py \
  .plans/completed/blend2d-fork-migration.md \
  .plans/completed/blend2d-patches-consolidation.md \
  .plans/blend2d-evidence
git diff --cached --stat
git commit -s -m "build: consume versioned Blend2D fork release"
```

## Validation completed before your commits

- All runtime source files are byte-identical to the previously validated
  patched dependency. The only existing-file differences are upstream test
  cases and their CMake registration; the fork also adds `pathstroke_test.cpp`.
- The standalone Blend2D upstream test suite passes with the pinned AsmJit.
- docling-parse builds with the fork source override; five native CTest cases
  pass, and Python unit tests plus the retained Honda image regression pass
  (317 passed, 2 skipped).
- The rebuilt Python extension is byte-identical to the previous patch-based
  extension: SHA-256
  `c91bf876535546f5e8985b24b8bf63bc101a0ae5a347e1d3879a3a1a0fd1a843`.
  Therefore the recorded 3,370-page comparisons and 22 stored-ground-truth
  renderer test passes apply to the exact same executable code. A redundant
  full-corpus recapture was stopped once binary identity was verified; no
  partial capture is presented as a completed run.
- Ruff format/lint and diff whitespace checks pass in the modified sources.
- Full MyPy (84 source files), `uv lock --check`, and clean source-distribution
  checks pass. The sdist retains the fork dependency declaration and regression
  tests/runner and contains no downstream Blend2D patches or patch helper.

This migration changes how the fixes are supplied. It preserves the #407
renderer workaround and the previously documented real-document evidence
gaps for the three native stroker fixes.
