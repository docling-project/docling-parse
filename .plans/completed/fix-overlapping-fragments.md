# Deduplicate overlapping PDF text after segmentation

## Implementation

1. Build provisional words and lines from normalized glyphs and retain the
   source ranges of the lines. Remove the nested glyph scan from page decode.
2. Match complete lines and words by text, writing direction, font, and
   near-coincident geometry. Keep shared words intact in distinct lines.
3. Match fragments against retained lines. Require an ordered, one-to-one
   match at the painted positions and evidence of clipping or a stroke and
   fill pair.
4. Project confirmed duplicate groups to character output. Preserve ambiguous
   matches and honor the existing duplicate-removal setting.

## Embedded overprint follow-up

1. Keep complete-line and clipped-fragment matching. Before final contraction,
   inspect surviving glyphs inside provisional runs and the text-show spans that
   produced them. Use font, glyph text, geometry, clipping, and paint mode to
   identify exact overlaps and explicit stroke/fill pairs. Retain the first
   supported copy in source order.
2. For shifted repaint, compare the glyph sequences within text-show spans and
   verify that the same translation applies across the match. Preserve distinct
   text on overlapping runs. For successive glyphs inside one source span, use
   local advance and ink overlap to distinguish a near-coincident repeat from
   ordinary repeated letters. Leave blank character counts out of the acceptance
   criteria for this iteration.
3. Use spatial indexes with bounded probes. Verify all 19 word/line regressions
   and all five improvements against the source or the audited fixture. Then
   check the rest of the corpus for newly broken extraction.

### Options considered

- **Tune whole-cell tolerances.** This can help complete, equal-text copies
  that narrowly miss the current threshold. A sweep of the public merge
  parameters from 0.001 to 100 across 24 changed pages produced identical word
  and line output because contraction ignores those legacy settings. The
  implementation's distance threshold cannot reach copies inside one run.
- **Index individual overlaps within runs.** Exact geometry and explicit
  stroke/fill are strong evidence in the proper source context. Use a bounded
  neighborhood for same-span overlaps and preserve ambiguous cross-run matches.
  This is the first implementation step.
- **Align shifted paint spans.** Require a consistent offset and matching
  glyph sequence to handle Japanese offset repaint. A word can span several
  drawing instructions and an instruction can combine parts of two paint
  passes. Support partial alignment only with evidence from adjacent glyphs or
  a corroborating repeated offset pattern. This complements the indexed pass.
- **Reconstruct entire paint passes.** Cursor resets and text-show provenance
  could separate copies earlier, including highly fragmented sequences. This
  is a larger segmentation change if bounded local alignment leaves cases
  unresolved.

## Performance and verification

- Use indexed positions and bound candidate checks and glyph probes in dense
  groups. Target O(n log n) time and O(n) extra space in the glyph count.
- Check page 10 of redp5110_sampled.pdf, fake bold, overlapping distinct words,
  repeated letters in tiny text, and a densely overprinted page.
- Run the full regression suite against the current ground truth. Report
  intentional extraction changes and unexpected failures for review.

## Corpus observations

- The original parser passed the sequential corpus comparison. With the patch,
  the full regression suite finished with 124 passes and three failures, all
  in extraction comparisons. The sequential and threaded comparisons each
  reported mismatches on 67 pages across 45 PDFs. Rendered image ground truth
  passed. The unit suite finished with 1,154 passes and 14 skips.
- On page 10 of `redp5110_sampled.pdf`, `ENFORCED FOR ALL ACCESS` and
  `Specifies that the row permission is to be initially disabled` each appear
  once. The page goes from 29 lines to 25 as clipped fragments disappear.
- Line text changes on 24 of 1,021 selected pages across 19 PDFs. The change
  removes some malformed fragments but exposes overprints the previous glyph
  cleanup suppressed. For example, `07f5395c8b3e7d1c_0001.pdf` paints short
  Japanese chunks four times with small offsets and different boundaries.
  Those chunks can wind up in distinct provisional lines and remain repeated.
- Synthetic input with 256, 512, 1,024, and 2,048 copies of one line took
  approximately 12, 26, 52, and 105 ms of CPU time, respectively. A second
  input with distinct, overlapping clips that cover the full line kept all
  copies and took approximately 127, 318, 666, and 1,369 ms.

### Verified improvements

| PDF | Page | Source evidence |
| --- | ---: | --- |
| `1265837786161076401-1.pdf` | 1 | Separate `CB-L` and `CB-R` draws have leading glyphs 0.36 pt apart. Preserve both labels. |
| `5462902444445925132-4.pdf` | 2 | The score draws `des-` and `se_en-` separately. Their neighboring `s` glyphs differ by 0.335 pt. Preserve both. |
| `newspaper-00.pdf` | 1 | Separate draws place standalone `n` and `m` copies exactly over retained glyphs. Remove the stray fragments. |
| `redp5110_sampled.pdf` | 10 | The PDF paints clipped copies of `ENFORCED FOR ALL ACCESS` and the sentence ending in `initially disabled`. Extract each once, with its initial and `di` intact. |
| `redp5110_sampled.pdf` | 11 | The visible label reads `Proc1:`. The patch replaces `P 1` and `roc :` with that label. |

### Word or line regressions

| PDF | Page | Source evidence and impact |
| --- | ---: | --- |
| `07f5395c8b3e7d1c_0001.pdf` | 1 | Japanese chunks are painted four times at approximately 0.24 pt offsets, with differing joins. The heading fragments and lines rise from 296 to 340. |
| `153a16136ed8f5ac_0011.pdf` | 1 | 49 overlapping Arabic glyphs leak into the text. One extra `ي` is 0.096 pt from its neighbor while surrounding advances are about 0.86 pt. |
| `15424333971388669153-2.pdf` | 1 | 214 extra shifted glyphs survive, including repeats of `建物全景`. Lines rise from 54 to 118. |
| `15424333971388669153-2.pdf` | 2 | 400 extra shifted glyphs survive in the heading and body. Lines rise from 191 to 331. |
| `15424333971388669153-2.pdf` | 3 | 407 extra shifted glyphs survive, including bullets and heading fragments. Lines rise from 165 to 275. |
| `15424333971388669153-2.pdf` | 4 | 317 extra shifted glyphs survive, including the section number and heading. Lines rise from 372 to 466. |
| `270fc4c23ca056b4_0025.pdf` | 1 | 19 overlapping Arabic elongation glyphs appear in extracted words. |
| `3584964174907633090-1.pdf` | 1 | `Front` and `er` each gain a soft hyphen from copies offset by about 0.358 pt. The independent extraction has one at each location. |
| `3934995182849940852-6.pdf` | 5 | 36 coincident digit copies survive, mostly chart zeros. Lines rise from 236 to 250. |
| `490193be45e28959_0004.pdf` | 1 | Five overlapping `د` copies double letters in words. |
| `71fcd26c-8105-4690-8958-cf10be9b9787-2.pdf` | 1 | Four coincident stroke/fill copies split the rendered date `03/07/2023` into two corrupt strings. |
| `8793130488931799579-2.pdf` | 1 | A coincident notehead emitted twice splits the extracted group. |
| `8793130488931799579-2.pdf` | 2 | Three coincident noteheads add three word cells and two line cells. |
| `c8702e75db7ff148_0001.pdf` | 1 | 45 coincident stroke/fill glyphs survive, doubling Chinese caption text. |
| `c9f1771acf9f7e29_0001.pdf` | 1 | 22 coincident stroke/fill glyphs survive, doubling parts of a date and captions. |
| `e7a3a534-9609-4dc9-9446-2d3a13c0b2ff-5.pdf` | 1 | A coincident comma becomes `，，` in a Chinese caption. |
| `ead2c552b489aac1_0004.pdf` | 1 | Seven overlapping Arabic elongation glyphs enter extracted words. |
| `right_to_left_02.pdf` | 1 | 25 extra overlapping glyphs survive, including additional `و` within words. |
| `right_to_left_04.pdf` | 1 | Seven extra nonblank glyphs survive, including three coincident `ة` copies. Word segmentation changes. |

The 19 pages include eight with exact copies and 12 with shifted copies. One
page has both. The shifted regressions overlap the distances of the two genuine
label improvements, so a single distance cutoff cannot distinguish them.

### Blank-only differences

The following 43 pages have identical word and line text. Counts are per page.
P is distinct padding within one source span. C is a coincident blank copy.
O is nearby blank placement in separate source spans. U is an unresolved
source span for a verified blank. Seventeen pages include coincident copies.
The blank counts are outside the follow-up's word and line acceptance criteria.

| PDF | Page and additional blanks |
| --- | --- |
| `024805ca-0473-491f-8e49-6444ecac3d4b-5.pdf` | 1: 2 C |
| `10458169359009391401-5.pdf` | 3: 28 P, 4: 40 P, 5: 40 P, 6: 60 P, 7: 40 P |
| `11502661831409318491_002.pdf` | 3: 4 P |
| `11516920924344231708_004.pdf` | 7: 2 C + 1 P, 8: 1 P |
| `11586287754644715199-16.pdf` | 6, 11, 13, 28, 30: 1 O each at the end of `Französisch` |
| `11816493302388412838-4.pdf` | 2: 2 P |
| `11926187848345237766-2.pdf` | 2: 5 C + 2 P |
| `12066842041297507170_028.pdf` | 30: 1 P, 33: 1 P |
| `15044306777075682274-3.pdf` | 2: 1 C, 3: 1 C + 1 P |
| `16293520921922171261-1.pdf` | 3: 1 O |
| `17791c05056ff856_0022.pdf` | 1: 1 C |
| `17898535961965146676-4.pdf` | 3: 8 O, 4: 9 C + 1 P |
| `2508.13113v2.pdf` | 2: 1 C |
| `335fea0ba454ba15_0004.pdf` | 1: 23 P |
| `3848385316711437784-4.pdf` | 1: 2 O + 1 P, 2: 2 C + 7 O, 3: 5 O, 4: 1 O |
| `4160141760688927471-1.pdf` | 1: 5 C |
| `4576237375423671461-2.pdf` | 2: 1 C |
| `6789603da459eee1_0017.pdf` | 1: 33 P + 2 U |
| `baf6f228d11953b9_0052.pdf` | 1: 93 C |
| `da04f7ddb6f59004_0025.pdf` | 1: 27 P |
| `ec62f8dc0265e965_0018.pdf` | 1: 29 P |
| `f41b4838d5d45f01_0004.pdf` | 1: 232 C |
| `right_to_left.pdf` | 1: 8 P |
| `right_to_left_01.pdf` | 1: 8 P |
| `skipped_1page.pdf` | 1: 1 C, 3: 2 C |
| `skipped_2pages.pdf` | 1: 1 C, 4: 2 C |

### Embedded pass results

- The bounded pass verifies coincident and stroke/fill draws in source context,
  matches translated text-show spans, and recognizes corroborated four-offset
  repaint. It uses local glyph advance at overlapping positions within a
  word. Different source quote glyphs remain distinct when they normalize to
  the same character. Coincident singleton chart labels with distinct MCIDs
  are emitted once in words and lines, with both tagged characters retained.
- Eighteen of the 19 prior regressions now pass the repository's word and line
  checks for text, order, geometry, and metadata. On
  `07f5395c8b3e7d1c_0001.pdf` page 1, the new output reconstructs the six
  chart headings visible in the PDF and replaces 22 fragment lines with those
  six headings. All five previously verified improvements still match their
  audited output.
- Across 1,021 corpus pages, 1,015 match the pinned word and line text. The six
  differences are the five prior improvements and the restored Japanese
  headings. Text on the other 997 pages was identical to the pre-follow-up
  parser. Unit and targeted contraction tests reported 1,189 passes and 14 skips.
- In a single text-show operation with 1,024, 2,048, and 4,096 coincident
  glyphs, median parsing CPU time was 1.77, 3.52, and 6.47 ms respectively.
