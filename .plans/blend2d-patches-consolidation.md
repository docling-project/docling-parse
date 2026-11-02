# Consolidated Blend2D library patches

## Current delivery approach

The user subsequently chose a versioned `PeterStaar-IBM/blend2d` fork instead
of downstream patch files. The original implementation sequence below is
retained as the historical plan. The active migration and exact commands for
the user to commit/tag/publish are in `blend2d-fork-migration.md`.

## Objective and authorization

Implement the Blend2D topic from docling-project/docling-parse#428, consolidating
#387, #396, #400, and #401 on the existing `feat/consolidate-rendering-prs`
branch. The user approved writing this plan and implementing it on 2026-10-30.
Preserve the pre-existing `app/render.cpp` change and unrelated untracked files.
Do not include them in the replacement PR.

## Source changes

The PRs are stacked: #401 contains all four logical fixes, in this order:

| Source PR | Original commit | Fix | Upstream |
| --- | --- | --- | --- |
| #387 | d91aea1ed3ed1083e72c194998524cc7aec47786 | Reset stale analytic-fill span length in JIT and reference pipelines | blend2d/blend2d#267, #268 |
| #396 | c4a0d00fd19fd7886646c8d4e26b0497c556ac26 | Stop quadratic offset subdivision when a split makes no progress | blend2d/blend2d#269, #270 |
| #400 | 9ebeeeee45d41fdffb84d567847006d51c8743e2 | Handle degenerate offset legs and join curve pieces | blend2d/blend2d#271 |
| #401 | 93b4e33f232b24a41ce4b78587d640f8baf85ba4 | Find flat-cubic cusps with stable quadratic roots | blend2d/blend2d#272, #273 |

Original author: Jeff Witt. Preserve attribution in the replacement PR and any
commits derived from these changes. Upstream fixes were still open when read.

## Implementation sequence

1. Record the starting branch, HEAD, and dirty files; compare with current main.
   Work on the user's current branch, without merge commits or resetting edits.
2. Import the reviewed #401 diff (the full stack), adding:
   - `cmake/apply_patch.cmake`: apply once; skip a successfully reversed dry run;
     fail configuration if a patch cannot be applied.
   - Four `cmake/blend2d-*.patch` files containing the original fixes.
   - Four ordered FetchContent patch commands in `cmake/extlib_blend2d.cmake`,
     with quoted arguments, upstream references, and removal conditions.
   - `tests/test_unit_affine_image_clip.py`,
     `tests/test_unit_subpixel_stroke_render.py`,
     `tests/test_unit_stroke_cusp_spike.py`, and
     `tests/test_unit_flat_cubic_cusp_stroke.py`.
   Retain existing dependency pins and the system-dependency path. The current
   MANIFEST.in already packages all cmake files.
3. Capture baseline failures before rebuilding. Run crash/hang reproducers in
   child processes with timeouts so they cannot kill or stall pytest.
4. Verify patch application on pristine pinned Blend2D, reapplication, and a
   fresh/repeated CMake configuration. Investigate failures rather than hiding
   them with relaxed tests.
5. Build the patched extension; run the focused tests, unit suite, applicable
   renderer regressions, lint, and source distribution packaging checks.
6. Add a reproducible before/after comparison runner using the existing image
   metrics against pypdfium2, with per-page timeouts and explicit failure records.
   Produce per-page JSON/Markdown, corpus aggregates, and reference/before/after
   panels. Cover all pages in the pinned corpus for evidence rather than silently
   relying only on the CI page sample. Preserve dataset revision and ground truth.
7. Search existing real PDFs for affected content, including pages whose pixels
   change with #400. Validate #387 on Honda manual 31ZH7600.pdf page 7 at scale 3;
   locate Nagra NAB 22-29 page 29 for #396. Add enforced image comparisons for
   available reproducible pages, separately from the report-only corpus test.
8. Record measured results and remaining evidence gaps here and in the PR body.
   Prepare/create the consolidated PR as a draft if #428 evidence is incomplete.
   Link all source/upstream PRs. Do not close another contributor's PRs until
   the replacement is ready and the maintainers agree on retirement.

## Acceptance criteria

- All four original fixes are represented once, with attribution and upstream
  links, without unrelated renderer changes or merge commits.
- Fresh/repeated bundled builds apply patches reliably; patch conflicts fail.
- Focused tests demonstrate the defects and pass with the patches.
- Validation reports identify the platform, revisions, corpus coverage, failures,
  and exact commands; no incomplete run is described as a full-corpus pass.
- Rendering evidence includes both pypdfium2 metrics, cited-page panels, and
  enforced tests. No page's error increase exceeds the existing 9.0 MAE / 0.09
  changed-pixel-ratio tolerances (interpretation recorded explicitly).
- Any ground-truth impact is listed by document/page and reason; the HF dataset
  revision is not changed to an unmerged dataset PR.

## Known evidence gaps from #428

The original author explicitly states that #400 and #401 were found using
synthetic inputs and have no real-document reproducers yet. Existing reported
A/B corpus results are claims to reproduce, not local validation of this branch.
The original reports did not provide a dataset fixture for the Honda manual
or a download URL for the Nagra report. Both originals have now been located
and retained in .plans/blend2d-evidence/pdfs at the user's explicit request;
regression dataset additions are deferred until after this PR. Do not fabricate
real-world evidence or mark the replacement ready while the missing real-page
improvements remain unresolved. Existing corpus documents take priority over
adding dataset documents.

## Execution log

- Starting HEAD: 15c3f6a; branch: feat/consolidate-rendering-prs.
- Pre-existing tracked modification: app/render.cpp.
- Plan written before implementation; work authorized by the user.
- Fast-forwarded the existing branch to current main e90fee2 (10 commits;
  no merge commit). app/render.cpp and unrelated files remain intact.
- Imported all 10 files from the reviewed #401 stack and quoted CMake command
  arguments for paths containing spaces. Added a capture/report evidence runner.
- Verified all four patches on pristine Blend2D 6dbc2ce, repeated application
  in order, and rejection of an invalid patch. All checks passed.
- Initial tests against the previously installed binary: affine-image child
  died with SIGBUS (-10); the other four cases passed on macOS ARM64. That
  binary's build revision is unknown, so controlled baseline rebuild follows.
- Synchronized source base requires corpus revision bc46a476692b0b035112d2e47a64843fdfb6337b;
  previous local marker was 6d3458e2a6a729defd700a7b91e57b6cb2ce0290.
  Dataset synchronization is in progress; pins and ground truth are unchanged.
- Downloaded original public documents into /private/tmp only:
  Honda 31ZH7600.pdf SHA-256 cf9ddf8f0890ed28082d9c5dfbea383635755dbbc56d56c0c6c68d15e64c6141;
  Nagra NAB 22-29 SHA-256 63529beba9c6b430ddd7e2e94153232f2c638a4fcc5eea8305ba84f2bb778ad8.
  Nagra source: https://www.drbg.ch/erkunden/vertiefte-recherche-in-der-pdf-bibliothek/rbg-beva/stufe-4-beva/241-nab-22-29-verpackungsanlagen-bei-der-zwilag-umweltvertraeglichkeitsbericht-uvb-1-stufe-voruntersuchung-und-pflichtenheft-fuer-die-hauptuntersuchung/file
- Important scope finding: main commit 30ae51f (#407) already flattens stroked
  cubics before passing them to Blend2D. Consequently the original three
  stroker PDF fixtures pass on main. Preserve that renderer workaround in this
  library-only PR; add opt-in CMake/CTest direct native regressions, enabled in
  CI, to exercise the actual patched stroker. Do not claim those PDF tests
  demonstrate failures on current main.
- Controlled baseline built from e90fee2 with pristine pinned Blend2D, reusing
  installed external dependencies after an unnecessary full QPDF clone was
  interrupted. Same installed dependencies are used for the patched build.
  Baseline package saved at /private/tmp/blend2d-baseline-e90fee2.
- Baseline Honda page 7 at scale 3 exits with SIGSEGV (-11); patched page
  renders and matches pypdfium2 within MAE 5.01737105 / changed ratio 0.07087159.
  Added an enforced test with a hash-verified source PDF. Initially supplied
  through an external path; the user's follow-up now retains the manual in
  the evidence folder and makes it the default fixture for checkout/CI runs.
- Nagra page 29 at scale 2 renders on current main due to #407; before/after
  metrics are identical: MAE 0.59818068 / changed ratio 0.01330445. This is not
  new evidence of a hang on current main.
- Scanned all original 447 local PDFs via PDFium path segments for stroked flat
  cubics with a linear derivative and an interior cusp: no candidates found.
  This search does not prove the absence of all possible affected geometry.
- Pinned corpus synchronized successfully: 448 PDFs, 3,370 pages. Full before
  and after captures are running with six child processes and 60s/page limits.
- Patched Python unit suite: 316 passed, 2 skipped (32.19s).
- Source distribution checked from a clean tracked-source snapshot, excluding
  unrelated local data: all four patches, helper, and focused tests included.
- Evidence runner smoke-tested for capture/metrics/panels and incomplete-run
  rejection. Identical PDFs under different corpus filenames are kept as
  separate entries. Ruff and targeted MyPy checks passed.
- Both full captures completed: all 3,370 pages render in each build; zero
  crashes/timeouts/skips. Every captured PNG is byte-identical before/after.
- Native CTest: all five cases passed. The direct pristine-stroker check fails
  both flat-cubic cases, which pass with the patched library. The stall and
  spike native cases pass on this ARM64 compiler and are not claimed as local
  before failures. CI runs the direct tests on Linux as well.
- Renderer regressions: 22 passed, including stored ground truth (257.78s).
  No ground-truth updates or HF pin changes are part of this PR.
- Full repository Ruff formatting/lint, MyPy (84 files), and uv lock validation
  passed. Final clean sdist includes native tests and the evidence runner;
  MANIFEST.in now explicitly includes that script.
- Original four commits reapplied on current main as 215e834, 19d3980,
  0c472d1, f6a4c28, preserving Jeff Witt's authorship and sign-offs.
- Reviewable evidence is in .plans/blend2d-evidence/README.md, including the
  Honda and Nagra three-panel comparisons. The full reference comparison is
  complete: 3,370 paired pypdfium2 metric records, zero errors or tolerance
  regressions. The full per-page table is corpus-metrics.md in that folder.
- User follow-up: retain both complete source PDFs in
  .plans/blend2d-evidence/pdfs for a dataset addition after this PR. Copied the
  originals without modification and verified both SHA-256 hashes. Added a
  provenance index with the cited PDF page numbers/scales. The Honda regression
  now uses the retained manual by default; comparison images remain available
  for later use in the PR description. No dataset files or pins are changed.
- Fork migration: created local branch docling/0.21.2-patches from upstream
  6dbc2ce in .references/blend2d. Applied upstream #268/#270/#271/#273 to the
  working tree only, including upstream tests; both repository indexes remain
  untouched. No commits, tags, or pushes were made during this migration.
- Removed the four downstream CMake patches and unused helper. CMake selects
  PeterStaar-IBM/blend2d, initially by the planned docling-0.21.2-patches.1 tag.
  The user must publish the release and resolve its final SHA using the guide.
- All runtime source files and the rebuilt Python extension are byte-identical
  to the previously validated patch-based build. The existing full-corpus
  evidence therefore remains valid. The standalone upstream Blend2D suite,
  five CTest cases, and Python tests (317 passed, 2 skipped) pass with the fork.
- 2026-10-31: fork branch and upstream Blend2D PR #275 publish commit
  08d76290aded4885aafacd89c59303283a72a118. Updated docling-parse's GIT_TAG to
  this full SHA. Verified the normal CMake remote-fetch configuration with the
  local source override cleared; fetched source has the expected fork origin,
  exact revision, and no modifications. Against PR #433's base, the only file
  changed under cmake/ is extlib_blend2d.cmake. Git staging and publication of
  the docling-parse changes remain user actions.
- Normal remote-fetched build validation completed: five native CTest cases
  pass; Python unit/Honda tests report 317 passed, 2 skipped (13.49s). The rebuilt
  extension retains SHA-256 c91bf876535546f5e8985b24b8bf63bc101a0ae5a347e1d3879a3a1a0fd1a843,
  identical to the previously validated 3,370-page corpus build.
- The user published both complete source PDFs on dataset main. Updated the
  pin to 8e4594df0bd41aafc7867c448792927a3971d72e and selected only PDF page 7
  of 31ZH7600.pdf and PDF page 29 of NAB-22-29.pdf for the ordinary corpus tests.
- Consolidated image coverage into the existing generic cited-page test, with
  per-case scales (3 and 2) and tolerances. All cases render in a child process
  with a timeout. Removed the document-specific test/environment/hash/fallback
  and the one-off scripts/compare_blend2d_patches.py plus its packaging entry.
  Historical measured artefacts remain; permanent tests need only the dataset.
- New-dataset validation: all four generic cited-page comparisons pass. The
  generic affine-fill case still detects SIGSEGV against the saved unpatched
  baseline in a child process. Three shared parser/render regression functions
  pass with their input corpus restricted to the two newly added documents.
  An earlier full sequential corpus run was interrupted before completion;
  no full-corpus result is claimed for the new dataset revision.
- Full Ruff checks and MyPy (82 source files) pass. A clean sdist retains the
  generic cited-page test and excludes both retired Python files. Neither Git
  staging nor commits were performed.
