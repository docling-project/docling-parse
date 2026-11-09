# Open PR triage and merge plan

Snapshot: 2026-10-25, 08:36 UTC
Main at snapshot: `7b7b273339722541a6576631e95996a295df052c`
Repository: https://github.com/docling-project/docling-parse/pulls

## Objective

Group all 36 remaining open PRs by topic, preserve dependencies, and merge changes
that pass the existing regression dataset before introducing new groundtruth.

This is a triage based on PR descriptions, changed files, selected diffs,
comments, and CI. GitHub metadata and head-commit checks were refreshed for
this update; branches have not been built or tested locally. Earlier dataset
impact measurements below remain author-reported or inherited from the
2026-10-23 triage, not remeasured against current main.
CI and conflict status must be refreshed before merging.

## Progress since the previous snapshot

Eight PRs from the original 44 are now merged:
#353, #423, #422, #412, #408, #395, #415 and #418.
No new open PRs appeared relative to the previous list.
The old Phase 1 is complete; truncated-file detection and damaged-stream
recovery are also on main. Preserve both when resolving later load/state changes.

Main still pins dataset `6d3458e2a6a729defd700a7b91e57b6cb2ce0290`.
No check runs were returned for the latest main commit at this snapshot;
confirm the integrated baseline before the next merge.

Current conflicts: #410, #414, #413, #327, #316, #313 and #261.
Previously reported conflicts on #392, #400 and #405 are no longer reported.
GitHub marks the other open PRs `blocked`; this is not a conflict diagnosis
or proof that their checks pass.

## Topic groups

| Topic | PRs | Dataset requirements |
|---|---|---|
| Blend2D fixes | #387 → #396 → #400 → #401 | No update expected; explicit stack |
| Stroke clipping | #397 | No update expected |
| Pattern performance/rendering | #386 → #390 | #386 passes existing expectations; #390 needs updates |
| CalGray/CalRGB and soft masks | #411, #413 | Both need updates |
| Font decoding | #419, #327, #370, #322, #389, #417 | All need updates |
| Duplicate/overprinted text | #372, #367 → #369 | All need updates; #369 includes #367's changes |
| Word spacing | #409, #385, #358 | All need updates; overlapping approaches |
| TJ boundaries | #368, #379, #380 | Identical diffs; retain one; dataset impact unverified |
| RTL and punctuation | #356, #388 | Both need updates |
| Forms/annotations | #410, #392 | No update expected |
| Damaged files/load errors | #414, #168 | No update expected; preserve merged #415/#418 |
| Render caching | #405 | No update expected |
| New APIs | #337, #313, #316 | #337 reportedly needs none; others need validation |
| Documentation/CI | #353, #332 | No regeneration expected |
| Old mixed maintenance | #261 | Changes an old dataset pin; reassess before merging |

## Order in which to start merging

The sequence below is recommended. Only explicitly marked stacks are
hard dependencies. Refresh checks after each merge and rebase as needed.

### Phase 1: Remaining form and load fixes using the existing dataset

1. #392 — widget-name encoding. Regression and wheel checks now pass;
   rerun against current main. Its former conflict is no longer reported.
2. #410 — signed-form JSON export performance. Resolve its current
   conflict after #392, then rerun the complete applicable checks.
3. #414 — structured document-load errors. Resolve its current conflict
   while preserving merged #415 and #418, then rerun load/recovery tests
   and the regression suite.

#392 and #410 overlap in to_json.h; their order here is a recommendation.
#410 and #414 have green regression and build/check results from October 22,
but those results predate the latest merges and do not clear current conflicts.
Review #168 alongside #414 for remaining useful behavior; do not assume that
the recent load fixes fully supersede it.

### Phase 2: Rendering stability using the existing dataset

4. #387 → #396 → #400 → #401 — preserve the explicit Blend2D stack.
5. #397 — stroke clipping.
6. #386 — skip unnecessary pattern replay.

All six PRs have passing regression results. #387 still has a failed
Python 3.11 manylinux_aarch64 wheel job; diagnose or rerun it before merging.
#400's former conflict is no longer reported. Other recorded build/check
results in this group pass, but must be refreshed after each ancestor merges.
Do not merge #401 ahead of its ancestors merely because its CI is green.

### Phase 3: Caching and additional changes that should preserve groundtruth

7. #405 — render caching. Its former conflict is no longer reported;
   recorded regression and build/check results pass. Validate it with the
   accepted rendering changes.
8. #337 — tagged-PDF structure API, after automated regression validation.
9. #332 — Windows regression CI, after leaving draft and completing
   a successful Windows run.

#337 still has no automated regression check on its current head; the
author-reported parser pass is insufficient as a merge gate.
#332 remains draft and its Windows regression job remains cancelled.
These independent PRs can be reviewed while the rendering stack is repaired.

### Phase 4: Changes requiring dataset updates

Complete these as separately reviewable batches.

A. Font decoding:
   Suggested review order:
   #419 → #327 → #370 → #322 → #389 → #417.
   Reconcile #327's current conflict and #370's failing code checks.
   Review combined decoding precedence, including merged #395/#412/#408,
   and regenerate affected expectations. #419 and #417 still fail regression;
   #389 passes with its own dataset pin, so it still belongs in this batch.
   #322 has no automated regression result on its current head.

B. Duplicate handling and contraction:
   #372, then #367 → #369.
   Review #409 and draft #385 together: the current #409 description says
   they target disjoint contraction branches, but combined validation remains
   necessary. #409 has fresh CI running on October 25; wait for its result.
   Reconcile #358, which overlaps those approaches.
   Retain only one of #368/#379/#380 and fix its compilation failures.
   #379 and #380 also need DCO sign-off repair. Resolve the failing code
   checks on #367/#369 and #388 separately from groundtruth differences.
   Integrate #356 after settling contraction behavior.
   Apply #388 before final regeneration so punctuation expectations are final.

C. Rendering:
   #390 after #386 and reconciliation with #397.
   #411 for CalGray/CalRGB decoding; its regression check still fails.
   #413 for soft masks, after resolving its current conflict and reconciling
   shared rendering/state changes. Its green regression result uses a
   different dataset revision from #389; reconcile the two updates cumulatively.

For each batch:

- Build the accepted combined implementation.
- Review output differences before accepting new groundtruth.
- Update the dataset cumulatively, preserving earlier batches.
- Set HF_DATASET_REVISION to the accepted dataset commit.
- Run the complete regression suite before merging dependent changes.

Individual PRs must pass against their intended dataset revision.
Do not merge a series of branches with known broken intermediate revisions.

### Phase 5: Deferred API and legacy work

- #313: remains draft and conflicting. Wait for docling-core #734 and its
  released attachment types; the prerequisite is still open and conflicting.
- #316: resolve conflicts and Python 3.14 build/check failures, then
  establish regression coverage for the compatibility API.
- #261: resolve its conflict and reassess or split its current changes;
  there is no automated regression result on its current head. It updates
  build threads, CLI initialization, and a historical dataset pin.
- #168: merge only after refreshing the implementation and checks.

## Dataset impact details

| PR(s) | Reported changes |
|---|---|
| #419 | Corrected ToUnicode text on 3 pages |
| #417 | Symbol private-use decoding on 11 pages |
| #327 | annots_01.pdf decoding |
| #370 | 16 pages |
| #322 | 16 pages in the updated implementation's reported comparison |
| #389 | 2 pages |
| #372 | 5 pages |
| #367 → #369 | 11 pages; #369 reportedly adds no further corpus changes |
| #409 | Approximately 105 pages |
| #385 | 27 pages |
| #358 | 82 pages |
| #356 | 25 additional RTL pages |
| #388 | 228 pages with punctuation changes |
| #390 | 44 pages: shapes, bitmaps, instructions, and some text |
| #411 | Confirmed stored bitmap metadata mismatch |
| #413 | At least one stored page image |

Counts come from different branch comparisons and overlap.
They must not be summed into a combined regeneration count.

Important findings:

- #368, #379 and #380 have identical diffs.
- #389 and #413 already change the dataset pin; green CI does not mean
  they work against the unchanged dataset. Their current pins are respectively
  `4103ae9b54a52f6890b57d6711614defd03d77b8` and
  `10c8edbf482df3f00612455c13e791a2dd8be9e6`. Do not assume either
  revision includes the other's dataset change. Verify dataset ancestry and
  accepted contents before choosing a cumulative revision.
- #411's description says no update is needed, but its CI log contradicts
  that claim: bitmap shape metadata differs for dln_23cb...pdf, page 1.
- #390 changes metadata and content representation even where rendered
  images remain close to their previous appearance.

#411 failure evidence:
https://github.com/docling-project/docling-parse/actions/runs/38978937435/job/117285557590

## Snapshot sources

- Open PRs: https://github.com/docling-project/docling-parse/pulls
- Merged since the previous snapshot:
  https://github.com/docling-project/docling-parse/pulls?q=is%3Apr+is%3Amerged+merged%3A%3E%3D2026-10-23
- Current main and dataset pin:
  https://github.com/docling-project/docling-parse/commit/7b7b273339722541a6576631e95996a295df052c
  and https://github.com/docling-project/docling-parse/blob/7b7b273339722541a6576631e95996a295df052c/tests/constants.py
- Dependency: https://github.com/docling-project/docling-core/pull/734

CI observations above come from the latest check run per name returned for
each PR's head SHA. Some successful runs are old and do not validate the
branch combined with current main. No PR is marked unconditionally ready.

## Completion criteria

- Every open PR has a topic, disposition, and dataset classification.
- Explicit stacks merge in dependency order.
- Duplicate and competing implementations are reconciled.
- Each merged result passes the applicable checks.
- Dataset revisions preserve all previously accepted updates.
