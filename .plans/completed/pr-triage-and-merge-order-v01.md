# Open PR triage and merge plan

Snapshot: 2026-10-23
Repository: https://github.com/docling-project/docling-parse/pulls

## Objective

Group all 44 open PRs by topic, preserve dependencies, and merge changes
that pass the existing regression dataset before introducing new groundtruth.

This is a triage based on PR descriptions, changed files, selected diffs,
comments, and CI. Branches have not been built or tested locally.
CI and conflict status must be refreshed before merging.

## Topic groups

| Topic | PRs | Dataset requirements |
|---|---|---|
| Dependency security | #423 | No update expected |
| Blend2D fixes | #387 → #396 → #400 → #401 | No update expected; explicit stack |
| Stroke clipping | #397 | No update expected |
| Type3 rendering | #422 | No update expected |
| Pattern performance/rendering | #386 → #390 | #386 passes existing expectations; #390 needs updates |
| CalGray/CalRGB and soft masks | #411, #413 | Both need updates |
| Narrow font/space fixes | #395, #412, #408 | No update expected |
| Font decoding | #419, #327, #370, #322, #389, #417 | All need updates |
| Duplicate/overprinted text | #372, #367 → #369 | All need updates; #369 includes #367's changes |
| Word spacing | #409, #385, #358 | All need updates; overlapping approaches |
| TJ boundaries | #368, #379, #380 | Identical diffs; retain one; dataset impact unverified |
| RTL and punctuation | #356, #388 | Both need updates |
| Forms/annotations | #410, #392 | No update expected |
| Damaged files/load errors | #414, #415, #418, #168 | No update expected |
| Render caching | #405 | No update expected |
| New APIs | #337, #313, #316 | #337 reportedly needs none; others need validation |
| Documentation/CI | #353, #332 | No regeneration expected |
| Old mixed maintenance | #261 | Changes an old dataset pin; reassess before merging |

## Order in which to start merging

The sequence below is recommended. Only explicitly marked stacks are
hard dependencies. Refresh checks after each merge and rebase as needed.

### Phase 1: Small changes using the existing dataset

1. #353 — performance documentation.
2. #423 — security dependency updates, once regression CI passes.
3. #422 — Type3 glyph image rendering.
4. #412 — implausible font metrics.
5. #408 — control-character space handling.
6. #395 — noncharacter ToUnicode fallback, once pending checks pass.

At the snapshot, #353, #422, #412 and #408 have passing regression CI.
#423 and #395 still have checks running.

### Phase 2: Load handling and form fixes

7. #410 — signed-form JSON export performance.
8. #392 — widget-name encoding, after rebasing and resolving wheel failures.
9. #414 — structured document-load errors.
10. #415 — report missing content in truncated files.
11. #418 — recover from damaged content-stream operators.
12. #405 — render caching, after resolving its conflict with main.

#410 and #392 overlap in to_json.h.
#414, #415 and #418 share document/stream handling and need combined checks.
Review #168 alongside #414; refresh its old implementation and failing checks
before deciding whether to merge it.

### Phase 3: Rendering stability using the existing dataset

13. #387 → #396 → #400 → #401 — preserve the explicit Blend2D stack.
14. #397 — stroke clipping.
15. #386 — skip unnecessary pattern replay.

Resolve #387's wheel failure and #400's conflict before merging.
Wait for outstanding checks on the other branches.
Do not merge #401 ahead of its ancestors merely because its CI is green.

### Phase 4: Additional changes that should preserve groundtruth

16. #337 — tagged-PDF structure API, after automated regression validation.
17. #332 — Windows regression CI, after leaving draft and completing
    a successful Windows run.

#337 has an author-reported parser regression pass without a dataset bump.
#332's Windows regression job was cancelled.

### Phase 5: Changes requiring dataset updates

Complete these as separately reviewable batches.

A. Font decoding:
   Suggested review order:
   #419 → #327 → #370 → #322 → #389 → #417.
   Review combined decoding precedence and regenerate affected expectations.

B. Duplicate handling and contraction:
   #372, then #367 → #369.
   Decide how #409 and draft #385 should compose.
   Reconcile #358, which overlaps those approaches.
   Retain only one of #368/#379/#380 and fix its compilation failures.
   Integrate #356 after settling contraction behavior.
   Apply #388 before final regeneration so punctuation expectations are final.

C. Rendering:
   #390 after #386 and reconciliation with #397.
   #411 for CalGray/CalRGB decoding.
   #413 for soft masks, after reconciling shared rendering/state changes.

For each batch:

- Build the accepted combined implementation.
- Review output differences before accepting new groundtruth.
- Update the dataset cumulatively, preserving earlier batches.
- Set HF_DATASET_REVISION to the accepted dataset commit.
- Run the complete regression suite before merging dependent changes.

Individual PRs must pass against their intended dataset revision.
Do not merge a series of branches with known broken intermediate revisions.

### Phase 6: Deferred API and legacy work

- #313: wait for docling-core #734 and its released attachment types;
  the prerequisite is still open and conflicting at the snapshot.
- #316: resolve conflicts and Python 3.14 build/check failures, then
  establish regression coverage for the compatibility API.
- #261: reassess or split its current changes; it updates build threads,
  CLI initialization, and a historical dataset pin.
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
  they work against the unchanged dataset.
- #411's description says no update is needed, but its CI log contradicts
  that claim: bitmap shape metadata differs for dln_23cb...pdf, page 1.
- #390 changes metadata and content representation even where rendered
  images remain close to their previous appearance.

#411 failure evidence:
https://github.com/docling-project/docling-parse/actions/runs/38978937435/job/117285557590

## Completion criteria

- Every open PR has a topic, disposition, and dataset classification.
- Explicit stacks merge in dependency order.
- Duplicate and competing implementations are reconciled.
- Each merged result passes the applicable checks.
- Dataset revisions preserve all previously accepted updates.
