# CJK system-font fallback: Blend2D rejects CFF2, so hosts with only VF fonts render tofu

Investigated 2026-08-24 on Fedora 43 (x86-64), against `chore/update-regression-testing`.

## Symptom

`tests/test_regression_threaded_render.py::test_rendered_pages_match_groundtruth`
fails on Linux (passes on macOS) for 14 pages across 9 documents, all violating
the image tolerances (`mean_abs_error` 10.0, `changed_pixels_ratio` 0.10):

```
  document                        page  mean_abs_error  changed_pixels_ratio
  1020010076157-4.pdf                3         26.2450                0.2528
  1020000086635-2.pdf                3         19.4283                0.1880
  1020000079773-1.pdf                3         18.7683                0.1800
  1019970077588-3.pdf                2         15.3334                0.1508
  7829021aca2be7b4_0002.pdf          1         14.9742                0.2285
  2020020019307-7.pdf                3         14.4705                0.1390
  15523818099946337472-5.pdf         5         12.2239                0.1876
  11273518440839632455_002.pdf       3          8.7036                0.1433
  4d0b77de7cfa5295_0005.pdf          1          8.4005                0.1130
  (+ further pages of the same documents)
```

On the Korean documents the Linux render is not merely different, it is
**unreadable**: every Hangul syllable draws as a `.notdef` box while macOS draws
the text.

## What the failing pages have in common

Every one references **CJK fonts that are not embedded**, with predefined CMaps:

| document | non-embedded CID fonts | CMap |
|---|---|---|
| `1020010076157-4.pdf`, `1020000086635-2.pdf`, `1020000079773-1.pdf`, `1019970077588-3.pdf` | `/Batang`, `/Gulim` | `/KSCms-UHC-H` |
| `7829021aca2be7b4_0002.pdf` | `/宋体`, `/楷体_GB2312` | `/GBK-EUC-H` |
| `11273518440839632455_002.pdf` | `/HGMarugothicMPRO` | — |
| `4d0b77de7cfa5295_0005.pdf` | `/HG…-PRO`, `/俵俽柧挬` (Shift-JIS mojibake name) | — |
| `2020020019307-7.pdf`, `15523818099946337472-5.pdf` | one CID font each | — |

There is nothing in the PDF to draw with, so the renderer *must* substitute a
system face. No other page in the corpus fails. This is render-only: parsing and
text extraction are unaffected.

## Root cause

**Blend2D at our pinned commit cannot load CFF2 fonts** (OpenType variable fonts
with CFF-flavoured outlines). Pin: `cmake/extlib_blend2d.cmake`,
`GIT_TAG 6dbc2cefbc996379e07104e34519a440b49b15d7` (`BL_VERSION 0.21.2`).

Measured over every font file installed on the dev box (331 files):

| outline format | faces | `BLFontFace::create_from_data` |
|---|---|---|
| CFF2 (variable) | 16, in 4 files | **all fail** — `BL_ERROR_FONT_CFF_INVALID_DATA` (65615 / 0x1004F) |
| CFF (1) | 170 | all load |
| `glyf` | 157 | all load |

The four rejected files are `NotoSansCJK-VF.ttc`, `NotoSansMonoCJK-VF.ttc`,
`NotoSerifCJK-VF.ttc` and `Cantarell-VF.otf`. It is not a collection-parsing
problem: extracting the KR face into a standalone `.otf` (fontTools) and loading
that directly fails with the same code.

Fedora ships Noto CJK **only** as the VF build, so on such a host *every*
CJK-capable face is invisible to the resolver. The consequence, from
`loglevel=info` on `1020010076157-4.pdf` page 3:

```
failed to inspect font face path=`…/NotoSansCJK-VF.ttc` face_index=0 face_res=65615   (×15, all 3 collections)
using CJK fallback font selected_key=`/Batang` path=`…/DroidSansFallbackFull.ttf`
no installed face covers all of `한` (script 'cjk')                                    (×8)
```

`DroidSansFallbackFull.ttf` carries CJK ideographs and kana but **no Hangul**
(verified against its cmap: U+4E00 yes, U+3042 yes, U+D55C **no**), so the Korean
runs shape to `.notdef`. The Chinese/Japanese documents do get glyphs from Droid,
but with Droid's metrics — which is the full-width punctuation displacement
visible in those diffs.

macOS has Hiragino / AppleGothic / PingFang, all TrueType or CFF(1), which load —
hence the split.

**FreeType, which we already build and link, reads these files.** Our pinned
FreeType opens `NotoSansCJK-VF.ttc` face 1 (`Noto Sans CJK KR`), maps U+D55C to
gid 58199 and rasterises it (27×29 bitmap). That is the basis of the fix below.

### Impact beyond the test

On any host whose CJK fonts are CFF2-only — current Fedora, RHEL-family, and
wherever distributions switch to VF packaging — CJK PDFs without embedded fonts
render broken for **users**, not just in the regression. The failing groundtruth
pages are the symptom, not the problem.

## How much of the divergence this accounts for

Downloading (not installing) Fedora's static `google-noto-sans-cjk-fonts` — same
upstream font, CFF(1) instead of CFF2 — and pointing
`DOCLING_PARSE_CJK_FALLBACK_FONT` at `NotoSansCJK-Regular.ttc`:

| document | page | before | after |
|---|---|---|---|
| `1020000086635-2.pdf` | 3 | 19.43 | **5.97** |
| `1020000079773-1.pdf` | 3 | 18.77 | **6.25** |
| `1019970077588-3.pdf` | 2 | 15.33 | **4.68** |
| `2020020019307-7.pdf` | 3 | 14.47 | **4.41** |
| `7829021aca2be7b4_0002.pdf` | 1 | 14.97 | **12.37** |
| `11273518440839632455_002.pdf` | 3 | 8.70 | **6.27** |
| `4d0b77de7cfa5295_0005.pdf` | 1 | 8.40 | **5.89** |
| `15523818099946337472-5.pdf` | 2 | 9.82 | **8.17** |
| `1020010076157-4.pdf` | 1 | 8.24 | **2.81** |

(`mean_abs_error`; the Korean pages render correct text.)

With the static packages actually installed on the dev box (2026-08-24) the
resolver picks its own candidate rather than the forced one, and lands on
**`NotoSansCJK-Thin.ttc`** — see finding 5 — so the measured values sit between
the two columns:

| document | page | before | installed (Thin) | forced Regular |
|---|---|---|---|---|
| `1019970077588-3.pdf` | 2 | 15.33 | 7.67 | **4.68** |
| `1020000086635-2.pdf` | 3 | 19.43 | 9.72 | **5.97** |
| `2020020019307-7.pdf` | 3 | 14.47 | 7.10 | **4.41** |
| `1020010076157-4.pdf` | 1 | 8.24 | 3.97 | **2.81** |

So roughly two thirds of the error on the Korean documents is ours. **The
remainder is environmental and no code change removes it**: macOS substitutes
Apple's faces, Linux substitutes Noto, so `changed_pixels_ratio` stays at
0.10–0.22 even with the bug fixed. Only forcing both platforms onto the *same
font file* converges them — see "The regression itself" below.

## Secondary findings

1. **Fallback selection tests existence, not loadability.**
   `find_first_existing_fallback()` (`blend2d_font_resolver.h:1800`) and
   `find_first_existing_cjk_fallback()` (`:1824`) return the first candidate that
   `fs::exists()`. If `load_font_face()` then fails, the caller returns an invalid
   face and never tries the next candidate. Reproduced by pointing
   `DOCLING_PARSE_CJK_FALLBACK_FONT` at a CFF2 file: the whole page degraded to
   bbox outlines — worse than the tofu.
2. **The CJK candidate list is built from Blend2D's index.**
   `cjk_fallback_candidates()` (`:1045`) ranks entries of `face_metadata_`, which
   only holds faces Blend2D could inspect, so a rejected file cannot even be
   *considered*. `arabic_fallback_candidates()` and `scan_for_fallback_fonts()`
   scan the font directories by filename stem instead and do not have this
   blind spot.
3. **The cause is invisible at default log level.** Rejected files are logged at
   INFO (`failed to inspect font face`); the WARNING the user sees is `no
   installed face covers all of …`, which points at the host's font set rather
   than at our loader.
4. **Neither `DOCLING_PARSE_CJK_FALLBACK_FONT` nor `DOCLING_PARSE_FALLBACK_FONT`
   is documented** anywhere outside the C++ source.
5. **Candidate ranking ignores weight.** `cjk_fallback_candidates()` matches the
   filename stem (`notosanscjk`) and keeps whichever file the directory iteration
   reached first. With the static Noto packages installed that is
   `NotoSansCJK-Thin.ttc`, so body text is drawn in Thin — measurably worse than
   Regular against the groundtruth (`1019970077588-3.pdf` p2: 7.67 vs 4.68
   `mean_abs_error`). The Latin path has the same shape of bug, mitigated only by
   its stems being explicit (`liberationsans-regular`, `notosans-regular`).

## Plan

### Step 0 — mitigation (no code)

Install a CFF(1) CJK font on Linux dev boxes: on Fedora
`google-noto-sans-cjk-fonts` / `google-noto-serif-cjk-fonts` (the packages
*without* `-vf-`). Applied on the dev box 2026-08-24.

Check any host with `fc-list :lang=ko file` and the probe in "Reproduction".

### Step 1 — loadability + diagnostics (small, do first)

| Change | Where |
|---|---|
| Walk fallback candidates until one *loads*, not until one exists | `blend2d_font_resolver.h:1800`, `:1824` |
| Build the CJK candidate list by directory scan (as Arabic/Latin already do), so files Blend2D rejects are still ranked | `cjk_fallback_candidates()`, `:1045` |
| WARN once per rejected file, naming the path, the `BLResult`, and the remedy (install the non-VF package / set `DOCLING_PARSE_CJK_FALLBACK_FONT`) | `index_font_file()`, `:1589` |
| Prefer the regular weight among stem-matched candidates (nearest `face.weight()` to 400, normal style), instead of whichever file the scan hit first | `cjk_fallback_candidates()`, `:1045` |
| Document both env vars | `README.md` / `tests/README.md` |

~30–60 lines. Turns a silent broken render into an actionable message, and stops
one unloadable candidate from taking the page down with it.

### Step 2 — route Blend2D-rejected system faces through FreeType (Design A)

The drawing half already exists: `render_text_freetype()`
(`blend2d_renderer.h:2240`) plus `freetype_embedded_font_cache::build_text_path()`
(`freetype_embedded_font_cache.h:154`) load a face, map characters to glyph
indices, decompose outlines into a `BLPath` (cached per glyph in font units), and
fill/stroke it under the same text transform, clip state and layer handling as the
Blend2D path. It serves embedded Type 1 / bare CFF programs today. The only reason
it cannot serve a system face is that it is fed from an `embedded_font_blob` in
memory rather than a file path.

Scope it to the **fallback** faces — the script fallback and the CJK/Latin
fallback candidates — not to name-resolved faces:

| # | File | Work | ~LOC |
|---|---|---|---|
| 1 | `src/render/freetype_embedded_font_cache.h` | Add `build_text_path_from_file(path, face_index, utf8_text, size, BLPath&, double* advance)`: same `face_entry` + glyph cache, key `"file:<path>#<index>"`, `FT_New_Face` instead of `FT_New_Memory_Face`, glyph mapping restricted to the Unicode cmap (`decode_utf8` + `FT_Get_Char_Index`, both present). Rename the class to `freetype_font_cache` (~6 call sites) since it is no longer embedded-only. | ~80 |
| 2 | `src/render/blend2d_font_resolver.h` | Return the chosen file, not just the face: `struct resolved_face { BLFontFace face; std::string path; uint32_t face_index; }`, with coverage probed through FreeType (`FT_Get_Char_Index` per codepoint) when Blend2D cannot load the file — replacing `face_coverage()`'s shaping probe for those candidates. Depends on Step 1's directory-scanned CJK list. | ~150 |
| 3 | `src/render/blend2d_renderer.h` | At the two fallback sites — system-face retry (`:2645`) and script fallback (`:2694`) — when the resolved face is invalid but a path came back, build the path through the FreeType cache and draw it. Split `render_text_freetype()` into "get the path" + `draw_text_path(instr, geom, text_path)` so both entry points share the second half (a move, not a rewrite). | ~60 |

**≈300 lines across 3 files, no signature changes outside the render layer.**

The new route only fires where the current code holds an *invalid* face — i.e.
exactly where the page today gets tofu or bbox outlines. Pages that render
correctly cannot change, which makes validation cheap: a corpus run must move only
the CJK pages.

### What the FreeType route costs

- **No shaping.** FreeType maps cmap and advances; Blend2D does GSUB/kerning.
  Irrelevant for CJK (1:1 cmap, and the PDF positions each cell itself); Latin
  faces load in Blend2D anyway, so the route does not fire for them.
- **Thread safety.** FreeType is not thread-safe and the cache serialises on one
  mutex. It is per-renderer today; system faces are shared across pages, so this
  should become process-wide like `blend2d_font_resolver::default_resolver()`.
  With 4 pages rendering concurrently the mutex is a contention point until the
  per-glyph path cache is warm — measure on the CJK pages before and after.
  — *Done: `freetype_font_cache::default_cache()` holds the system faces
  process-wide; embedded blobs stay per-renderer. Measured no contention cost:
  `test_rendered_pages_match_groundtruth` over the whole corpus with 4 threads
  ran 77.8 s before and 74.4 s after.*
- **Memory.** `FT_New_Face` on a 30 MB CJK collection is fine for a handful of
  faces; the cache needs a bound. — *Done: `max_file_faces = 24`. Past the
  bound new files are refused rather than evicted — every caller already
  handles "FreeType cannot draw this", whereas eviction would dangle the
  `FT_Face` pointers held in `faces_`.*

### Not now — Design B (dual-backend resolver)

Index CFF2 files through FreeType (family/full/PostScript names, weight/style from
`FT_Face` + `TT_OS2`) so name matching and every probe can select them, and carry a
face-handle abstraction through `local_font_cache_`, `render_text` and the per-cell
font cache. ~600–900 lines through the resolver's core, and it changes behaviour on
pages that work today. Worth it only if a PDF names a system face that exists
solely as CFF2 (e.g. "Cantarell" on Fedora) — the fallback path does not cover
that case.

## The regression itself

Even with Step 2 done, these pages compare a macOS-substituted face against a
Linux-substituted one, and cannot meet the current tolerances. Options:

1. **Pin the fallback font for the test run** — ship one CJK face in the test
   dataset and set `DOCLING_PARSE_CJK_FALLBACK_FONT` in the harness on both
   platforms. Blend2D's rasteriser is ours, so the two renders should then agree
   closely, and these pages become a real regression signal instead of a font
   census. Note the env var only steers the *fallback* path: a host that
   name-resolves `/Batang` to a local face would still diverge, which does not
   happen on CI or on either dev box.
2. Flag the affected pages as font-substituted and compare them at a looser
   tolerance.
3. Leave as is and accept that the render regression is only meaningful on the OS
   that wrote the groundtruth.

(1) is preferred; it is the only option that keeps the pages testing rendering.

**Chosen 2026-08-25: (2), with (1) done as far as it goes without a groundtruth
rewrite.** (1) is still right, and CI now pins the file — but the groundtruth
was written on macOS against Apple's faces, so pinning alone does not converge
the two platforms; the groundtruth has to be regenerated on macOS with the same
pinned file for (1) to pay off. That is a dataset-tag bump and a macOS run, and
it is left open deliberately. (2) is what makes the suite meaningful in the
meantime, and it is scoped so it does not cost the rest of the corpus anything
— see "The regression itself" under "What landed".

## CI

> **Corrected 2026-08-25 — the paragraph below was wrong, and it was the wrong
> half of the problem.** `ubuntu-24.04` does *not* ship Noto CJK in any form.
> The runner image manifest documents exactly one font package,
> `fonts-noto-color-emoji`; there is no CJK face on it, static or variable, so
> CI was not "not seeing this yet" — it was seeing a worse version of it, with
> every CJK glyph a `.notdef` box. That is what failed
> [run 32723222493](https://github.com/docling-project/docling-parse/actions/runs/32723222493).
> Fixed by installing the fonts; see "CI" under "What landed" below.

~~`.github/workflows/checks.yml` runs `uv run pytest -v tests` on `ubuntu-24.04`,
which still ships the static Noto CJK OTC, so CI does not see this today. An image
bump to VF-only Noto would break Korean rendering there silently.~~ Worth asserting
the host has a loadable CJK face as part of the test session rather than
discovering it as an image-diff. — done, as a step in the workflow rather than
in the test session: `fc-list :lang=ko` plus an existence check on the pinned
file, so a runner-image change fails as itself.

## Reproduction

Blend2D load matrix over the installed fonts (build against
`build/_deps/blend2d-build/libblend2d.a`):

```cpp
BLFontData data; data.create_from_file(path);
for (uint32_t i = 0; i < data.face_count(); ++i) {
  BLFontFace face; printf("%s %u -> %u\n", path, i, face.create_from_data(data, i));
}
```

FreeType counter-check (`externals/lib/libfreetype.a`):

```c
FT_New_Face(lib, "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc", 1, &face);
FT_Get_Char_Index(face, 0xD55C);   /* -> 58199, loads and renders */
```

Which outline table a file carries:

```python
from fontTools.ttLib import TTFont, TTCollection   # 'CFF2' vs 'CFF ' vs 'glyf'
```

Per-page font evidence: `loglevel="info"` on the threaded parser, then grep for
`failed to inspect font face`, `using CJK fallback font`, `no installed face
covers all of`.

### After the fix (2026-08-25)

Which file the resolver settled on, and whether Blend2D or FreeType is drawing
it — both are on the `using CJK fallback font` line now:

```
loglevel="info" … | grep -E "using CJK fallback font|Blend2D cannot read this font"
#  using CJK fallback font selected_key=`/Batang` path=`…/NotoSansCJK-Regular.ttc` blend2d=true
#  Blend2D cannot read this font file … path=`…/NotoSansCJK-VF.ttc` face_res=65615 (CFF2/variable …)
```

Exercise the FreeType route on a host that has the static build, by forcing the
variable one:

```sh
DOCLING_PARSE_CJK_FALLBACK_FONT=/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc \
  python -m pytest tests/test_regression_threaded_render.py -k groundtruth
```

The Korean pages must still render text (`mean_abs_error` around 12–13 against
the macOS groundtruth, versus around 26 for the original tofu). A run that
comes back at 20+ means the route did not fire.

## Status

| Item | Status |
|---|---|
| Install CFF(1) CJK fonts on the Linux dev box | ✅ done 2026-08-24 — Korean renders, worst page 26.2 → 9.7 `mean_abs_error` |
| Step 1 — loadability walk, directory-scanned CJK candidates, WARN, document env vars | ✅ done 2026-08-25 |
| Step 2 — FreeType route for rejected fallback faces (Design A) | ✅ done 2026-08-25 |
| Install the fonts on CI | ✅ done 2026-08-25 — `.github/workflows/checks.yml` |
| Pin the test-run CJK fallback font so the corpus is platform-independent | partial — pinned on CI; the macOS groundtruth still uses Apple's faces |
| Design B (dual-backend resolver) | not planned |

### What landed, 2026-08-25

**Step 1** (`src/render/blend2d_font_resolver.h`)

- `find_first_existing_fallback()` / `find_first_existing_cjk_fallback()` are
  one `find_first_loadable_fallback()`, which walks candidates until one
  *opens* (Blend2D, or FreeType) instead of until one exists. Finding 1 is
  gone: pointing `DOCLING_PARSE_CJK_FALLBACK_FONT` at a CFF2 file no longer
  takes the page down to bbox outlines.
- `cjk_fallback_candidates()` scans the font directories the way the Arabic and
  Latin lists already did, so a file Blend2D rejected is still ranked
  (finding 2).
- Candidates are ranked by weight: nearest to 400, upright before slanted, and
  a Blend2D-loadable file always ahead of a FreeType-only one. On the dev box
  that moves the pick from `NotoSansCJK-Thin.ttc` to `-Regular.ttc`
  (finding 5). Weight comes from the index where there is one, and from the
  filename stem otherwise (`stem_weight()`).
- A file Blend2D refuses is now one WARNING naming the path, the `BLResult` and
  the remedy — once per file, not once per face (finding 3).
- Both env vars, and `DOCLING_PARSE_ARABIC_FALLBACK_FONT`, are documented in
  `README.md` ("System fonts") and `tests/README.md` (finding 4).
- Faces are chosen at the lowest indexed face index rather than whichever the
  hash map reached first, so a `.ttc` resolves to the same region face on
  every run.

**Step 2** — the resolver returns `resolved_face {face, path, face_index}`;
`face` invalid with `path` set means "FreeType only". `freetype_embedded_font_cache`
is `freetype_font_cache` (`src/render/freetype_font_cache.h`) with
`build_text_path_from_file()`, `file_face_coverage()` and `can_open_file()`,
and a process-wide `default_cache()` for system files, bounded at 24 open
faces. `render_text_freetype()` split into path-building plus
`draw_text_outline_path()`, which the new file route reuses. Three sites in
`render_text` take it: the name-resolved face, the embedded-font system retry,
and the script fallback — each only where the code held an *invalid* face,
i.e. exactly where the page got tofu or bbox outlines.

Measured, `mean_abs_error` against the macOS groundtruth on the dev box:

| document | page | before (Thin) | after (Regular) |
|---|---|---|---|
| `1020010076157-4.pdf` | 3 | 12.76 | **8.74** |
| `1020000086635-2.pdf` | 3 | 9.72 | **5.97** |
| `1020000079773-1.pdf` | 3 | 9.38 | **6.25** |
| `1019970077588-3.pdf` | 2 | 7.67 | **4.68** |
| `2020020019307-7.pdf` | 3 | 7.10 | **4.41** |
| `7829021aca2be7b4_0002.pdf` | 1 | 11.30 | 12.37 |
| `15523818099946337472-5.pdf` | 5 | 9.35 | 10.11 |

The last two get worse, and that is the substitution showing through rather
than a regression: macOS draws those Chinese/Japanese pages with a *serif*
face (Songti, Hiragino Mincho) and Thin happened to sit closer to it than
Regular does. A regular weight is the right substitution for a regular-weight
request; the residue is environmental, as predicted above.

#### Negative result: sans vs serif CJK fallback

Worth recording so nobody re-runs it. Forcing the whole corpus onto
`NotoSerifCJK-Regular.ttc` instead of `NotoSansCJK-Regular.ttc`
(`mean_abs_error`, dev box):

| document | page | script | Sans | Serif |
|---|---|---|---|---|
| `1020010076157-4.pdf` | 3 | ko | **8.74** | 14.18 |
| `1019970077588-3.pdf` | 2 | ko | **4.68** | 8.41 |
| `2020020019307-7.pdf` | 3 | ko | **4.41** | 7.84 |
| `7829021aca2be7b4_0002.pdf` | 1 | zh | 12.37 | **10.39** |
| `15523818099946337472-5.pdf` | 5 | ja | 10.11 | **8.41** |
| `11273518440839632455_002.pdf` | 3 | ja | 6.27 | **5.52** |
| `4d0b77de7cfa5295_0005.pdf` | 1 | ja | 5.89 | 5.91 |

The sign flips cleanly on script, not on the PDF's request: macOS substitutes
a *sans* face for Korean (AppleGothic / Apple SD Gothic Neo) and a *serif* one
for Chinese and Japanese (Songti, Hiragino Mincho) — even though `/Batang` is
itself a serif family. So a serif/sans classifier keyed on the requested family
would make the Korean pages worse while looking more correct, and one keyed on
the script would be tuning our renderer to another OS's substitution policy.
Neither was added. This is the clearest evidence that the remaining error is
the font census the summary above calls it, and that only option (1) — one
pinned file on both platforms, with the groundtruth regenerated against it —
removes it.

#### What was verified, and what was not

Verified on the dev box (Fedora 43, x86-64):

- Korean renders correctly through the new route with **only** the CFF2 file
  available: `DOCLING_PARSE_CJK_FALLBACK_FONT=…/NotoSansCJK-VF.ttc` draws the
  page (12.68 `mean_abs_error`, legible Hangul) where it previously drew bbox
  outlines. Slightly lighter than the static build — the VF default instance.
- The resolver now logs `using CJK fallback font path=…/NotoSansCJK-Regular.ttc`
  for `/Batang` and `/Gulim`, and one WARNING per rejected file (4 on this box:
  the three Noto CJK VF collections and `Cantarell-VF.otf`).
- Full suite green: 226 passed, 1 skipped. `pre-commit run --all-files` clean.

**Not verified: anything about the CI host.** The job log is not readable
without admin rights on the repo (`GET /actions/jobs/{id}/logs` → 403), so the
diagnosis that CI has no CJK face comes from the runner-image manifest, not
from the failure output. The Ubuntu package names and the
`/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc` path are likewise
asserted in the workflow rather than tested — which is why the install step
checks them explicitly and prints `dpkg -L fonts-noto-cjk` when the path is
wrong. **First CI run on this branch is the real test of that step.**

**The regression itself** — option 2, not option 1. The nine substituting
documents are `FONT_SUBSTITUTED_DOCUMENTS` in
`tests/test_regression_threaded_render.py` and compare at
`SUBSTITUTED_FONT_IMAGE_TOLERANCE` (20.0 / 0.30); every other page keeps
`RENDERER_IMAGE_TOLERANCE` (10.0 / 0.15), whose worst case across the rest of
the corpus is 8.13 / 0.1231. Option 1 needs the groundtruth rewritten on macOS
against a pinned font file and is still open.

**CI** — `checks.yml` installs `fonts-dejavu-core fonts-liberation2
fonts-freefont-ttf fonts-urw-base35 fonts-noto-core fonts-noto-cjk`, asserts
`NotoSansCJK-Regular.ttc` and `fc-list :lang=ko` are non-empty (so a runner
image change fails as itself, not as an image diff), and pins
`DOCLING_PARSE_CJK_FALLBACK_FONT` for the pytest step. The note above that
"ubuntu-24.04 still ships the static Noto CJK OTC" was wrong: the image
documents only `fonts-noto-color-emoji`, so CI had no CJK face at all.

#### Still open

1. **Option (1) proper** — regenerate the renderer groundtruth on macOS with
   `DOCLING_PARSE_CJK_FALLBACK_FONT` pinned to the same file CI uses, then drop
   `FONT_SUBSTITUTED_DOCUMENTS` back to the tight tolerance. Needs a macOS run
   and a dataset-tag bump; it is the only thing that turns these nine documents
   back into a rendering signal. Note the env var steers only the *fallback*
   path, so a macOS host that name-resolves `/Batang` to a local face would
   still diverge — check the `info` log for `using CJK fallback font` on that
   run before trusting the numbers.
2. **Design B** (dual-backend resolver) — still not planned. Step 2 did give
   the *drawing* half of it for free: a name-resolved face that only FreeType
   can open is now drawn rather than boxed, so "Cantarell on Fedora" works
   today. What Design B would add on top is *name matching and probing* against
   CFF2 files, which nothing in the corpus needs.
3. The FreeType route does no shaping. Harmless for CJK, and Latin faces load
   in Blend2D — but if a script that needs GSUB (Arabic, Devanagari) ever
   resolves to a CFF2-only file, it will render as disconnected base letters
   rather than as boxes. No host we know of is in that state; worth a thought
   if the Arabic fallback list ever picks up a variable font.
