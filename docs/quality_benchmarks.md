# Quality Benchmarks

Rendering quality of `docling-parse` measured against **pypdfium2** on the
regression corpus: 108 pages across 75 documents, rasterised twice and compared
pixel by pixel.

PDFium is the reference because it is an independent, widely deployed
implementation of the same specification — not because it is correct by
definition. A difference is a question to investigate, not automatically a bug
in `docling-parse`. Nothing on this page is a CI gate; the numbers are a report.

## Reproducing

```console
uv run pytest tests/test_pypdfium_render.py -q -s --render-visualizations all
```

The test prints the table below and writes one three-panel PNG per page into
`tests/data/visualizations/`. The images in [`./quality_benchmarks/`](./quality_benchmarks/)
are that output, copied in.

`--render-visualizations` selects how much is written:

| value | writes a visualisation for |
| --- | --- |
| `all` | every page |
| `above-tolerance` | only pages over the reporting cut-off (the default) |
| `none` | nothing |

Two run-level histograms land in the same folder for every value except
`none` — `histogram_delta.png` and `histogram_mean_abs_error.png`. Each shows
the distribution of that metric over the whole corpus on a logarithmic page
count, with the median, the mean and (for `mean_abs_error`) the reporting
cut-off marked. They are what to look at after changing a decoder: a per-page
panel says what is wrong with one page, the histogram says whether the corpus
as a whole moved. Writing them needs `matplotlib` from the `perf` dependency
group; without it the run silently skips them and still prints the table.

Both renderers work at `scale = 2.0` (144 dpi). The test never fails on pixel
differences — it fails only when there is nothing to compare, e.g. when
`pypdfium2` is not installed.

## How to read a visualisation

Each PNG holds three panels of the same page:

| panel | content |
| --- | --- |
| left | `pypdfium2` — the reference render |
| centre | `docling-parse` — the Blend2D render |
| right | the per-pixel absolute difference of the two |

The difference panel carries **no brightness scaling**. A grey level of 40 means
the two renders are 40 apart at that pixel, and black means they agree. That
also makes any two panels directly comparable: they are produced by the same
fixed transform, so a panel from one page can be held next to a panel from
another. Amplifying — even by a constant — would break that, by making a
near-identical page look as damaged as a broken one.

The price is that an accurate page shows an almost black panel. That is the
honest depiction of two renders agreeing, and the metrics below are what
resolve differences too small to see.

![Difference panels for math_latex_formulas.pdf page 4, the closest page in the corpus](./quality_benchmarks/delta_0.0002_math_latex_formulas.pdf.page_no_4.png)

*The closest page in the corpus (`delta = 0.0002`, `mean_abs_error = 0.0295`):
the difference panel is black. The two renderers put this page on screen the
same way.*

## Metrics

Both renders are composited onto opaque white before anything is measured —
`pypdfium2` paints an opaque page while the Blend2D path can leave the
background transparent, and comparing them only makes sense on a common
background.

**`delta`** — the headline number, on a **0.0 – 1.0** scale.

> Per pixel, take the *largest* of the three channel differences and divide by
> 255; the score is the mean of that over the page.

`0.0` is a pixel-identical render and `1.0` is every pixel maximally inverted —
a bound that is actually attainable, which is what makes the number comparable
across pages of different size. A page that differs on 5 % of its area, fully,
scores `0.0500`; the same 5 % differing by half scores `0.0249`.

Two deliberate choices behind it. Alpha is excluded, since after flattening both
alpha channels are identical by construction. And the channels are combined with
`max()` rather than averaged: a glyph painted pure blue where it should be black
differs by 255 in one channel, which a mean reports as 85 and a luma weighting
as 29 — both understating a difference that is entirely obvious on screen.

`delta` is still an average over the whole page, so it is dominated by page
area. A mostly-white page with badly wrong text scores low in absolute terms.
It ranks pages against each other; it does not say "this render is 3 % wrong".

The remaining columns are the raw quantities it is derived from:

| column | meaning |
| --- | --- |
| `canvas` | render size in pixels; `pypdfium2` and `docling-parse` agreed exactly on all 108 pages |
| `mean_abs_error` | mean absolute per-channel deviation over RGBA, in 0–255 units. Alpha is constant, so this saturates at 191.25 rather than 255 |
| `changed_pixels_ratio` | fraction of pixels whose luma-weighted difference exceeds 12 |
| `max_abs_error` | largest single-channel deviation anywhere on the page |
| `changed_pixels` | absolute count behind `changed_pixels_ratio` |

## Summary

| statistic | `mean_abs_error` | `delta` |
| --- | ---: | ---: |
| best page | 0.0295 | 0.0002 |
| median | 2.0736 | 0.0118 |
| mean | 2.3243 | 0.0130 |
| 95th percentile | 4.3643 | 0.0237 |
| worst page | 28.5195 | 0.1564 |

| | pages |
| --- | ---: |
| `delta` < 0.01 | 46 / 108 (43 %) |
| `delta` < 0.02 | 94 / 108 (87 %) |
| `delta` < 0.05 | 107 / 108 (99 %) |
| `delta` < 0.10 | 107 / 108 (99 %) |

All but one page of the corpus sits below a `delta` of `0.05`, and the tail is
now a single page: `complex_invisible_fonts_01.pdf`, which is also the worst by
`mean_abs_error`. Everything below it is within a factor of two of the median,
which is the level at which the difference is anti-aliasing rather than
decoding.

## The tail

### `complex_invisible_fonts_01.pdf` page 1 — `mean_abs_error` 28.52, `delta` 0.1564

![complex_invisible_fonts_01.pdf page 1](./quality_benchmarks/delta_0.2266_complex_invisible_fonts_01.pdf.page_no_1.png)

The only page left above a `delta` of `0.05`, and over half of it differs
(`changed_pixels_ratio = 0.53`). Not yet characterised.

### `device_n_black.pdf` page 1 — `mean_abs_error` 48.37 → 0.79, `delta` 0.3618 → 0.0067

![device_n_black.pdf page 1](./quality_benchmarks/delta_0.3618_device_n_black.pdf.page_no_1.png)

The panel above shows the page as it rendered before the fixes described here:
blank. It was the worst page in the corpus, and the cause was not the one this
section used to name. The page is a full-width banner painted by a single `sh`
operator under a hexagonal clip path, and `renderer<BLEND2D>::render_shading`
skipped any shading whose clip was not an axis-aligned rectangle, because
Blend2D clips to rectangles only and an unclipped fill would have flooded the
page. Everything else on the page is white ink on that banner, so skipping the
one shading left nothing at all.

A shading fills its clip region and nothing else, which means the clip can be
the *fill geometry* instead of a clip: `render_shading` now fills the clip
outline with the gradient, and only falls back to skipping when no clip path
can be honoured at all. That brought the page to `delta` 0.1160 — the banner
appeared, in the wrong colours, because it is `/DeviceCMYK` and the CMYK
conversion was the textbook subtractive formula. Replacing that conversion
(below) took the page the rest of the way to 0.0067.

The limitation this section previously named was real, and is also fixed:
`pdf_resource<PAGE_COLORSPACE>` now evaluates the tint transform of
`/Separation` and `/DeviceN` colour spaces into their alternate space rather
than approximating a tint by darkening towards black. It only happens not to be
what made *this* page the worst one. `font_07.pdf` is where it shows: pages 3
and 4 of that document went from `delta` 0.60 and 0.48 to 0.010 and 0.008.

### DeviceCMYK, across the corpus

`/DeviceCMYK` has no colorimetric definition — 8.6.4.4 calls it
device-dependent — so every reader picks a rendering. The one that was in place
here was the textbook subtractive formula, `R = (1-C)(1-K)`, which assumes
perfect inks on perfect paper: solid cyan came out as `#00FFFF` where a press
gives `#00AEEF`, solid K as `#000000` where a press gives `#231F20`, and CMY at
100 % as pure black rather than the muddy `#414042` that comes off the sheet.
Any page with saturated ink looked like a screen gamut.

`src/parse/utils/color/device_cmyk.h` replaces it with the standard printing
model for the same question — Neugebauer with a Yule-Nielsen exponent — fitted
to a SWOP-coated press, and shared by the `k`/`K` operators, the colour-space
resource and the renderer's CMYK image path. Over a 9x9x9x9 sweep it tracks the
PDFium/Acrobat rendering within a mean of 6.1 and a maximum of 24 code values,
against 26.1 and 115 for the subtractive formula, and it is exact along the
neutral K-only axis where most CMYK ink on a page actually sits.

Twenty of the 108 pages moved; all of them improved. The largest were
`device_n_black.pdf` (`delta` 0.1160 → 0.0067), `cropbox_versus_mediabox_02.pdf`
page 1 (0.0711 → 0.0201) and `font_10.pdf` (0.0419 → 0.0168).

### `form_fields.pdf` and `fillable_form.pdf`

![form_fields.pdf page 3](./quality_benchmarks/delta_0.2192_form_fields.pdf.page_no_3.png)

The panel above is also historical. Each text field emits a
`text_widget_instruction`, which `renderer<BLEND2D>::render_widget` paints as a
translucent light-blue quadrilateral over the field's bounds — a debug
visualisation PDFium has no counterpart for — and a form-heavy page therefore
differed across every field. The overlay is now opt-in:
`render_config::display_widgets` defaults to `false`, so a default render draws
only the field content. That is what the table below now measures, and
`form_fields.pdf` page 3 falls from `delta` 0.2192 to 0.0089, `fillable_form.pdf`
from 0.1578 to 0.0169. Setting `display_widgets` to `true` (colour configurable
through `render_config::color_widgets`) brings the overlay, and those numbers,
back.

## Per-page results

Sorted by `mean_abs_error`, worst first, as the test prints them. The `delta`
value links to that page's three-panel visualisation.

The numbers are re-measured; the linked panels are not. Twenty rows moved with
the shading-clip, tint transform and DeviceCMYK fixes, and the widget overlay
becoming opt-in, so on those rows the panel shows the older render and its file
name carries the older `delta`. Re-running the benchmark replaces both.

<!-- Generated from `pytest tests/test_pypdfium_render.py --render-visualizations all`. -->

| document | page | canvas | mean_abs_error | delta | changed_pixels_ratio | max_abs_error | changed_pixels |
| --- | ---: | :---: | ---: | ---: | ---: | ---: | ---: |
| `complex_invisible_fonts_01.pdf` | 1 | 1191x1684 | 28.5195 | [0.1564](./quality_benchmarks/delta_0.2266_complex_invisible_fonts_01.pdf.page_no_1.png) | 0.4757 | 230 | 953,996 |
| `form_fields.pdf` | 5 | 1224x1584 | 6.4116 | [0.0349](./quality_benchmarks/delta_0.0349_form_fields.pdf.page_no_5.png) | 0.1425 | 255 | 276,235 |
| `cropbox_versus_mediabox_02.pdf` | 2 | 1088x1266 | 4.5794 | [0.0312](./quality_benchmarks/delta_0.0569_cropbox_versus_mediabox_02.pdf.page_no_2.png) | 0.1140 | 255 | 157,059 |
| `ligatures_01.pdf` | 2 | 1224x1584 | 4.4533 | [0.0233](./quality_benchmarks/delta_0.0233_ligatures_01.pdf.page_no_2.png) | 0.1275 | 175 | 247,143 |
| `11831013430589524848.pdf` | 1 | 1440x1080 | 4.4050 | [0.0312](./quality_benchmarks/delta_0.0312_11831013430589524848.pdf.page_no_1.png) | 0.1493 | 255 | 232,147 |
| `complex_invisible_fonts_02.pdf` | 1 | 1191x1684 | 4.3643 | [0.0237](./quality_benchmarks/delta_0.0321_complex_invisible_fonts_02.pdf.page_no_1.png) | 0.1089 | 183 | 218,482 |
| `ccitt_complex_image_scan.pdf` | 1 | 1224x1584 | 4.1427 | [0.0217](./quality_benchmarks/delta_0.0217_ccitt_complex_image_scan.pdf.page_no_1.png) | 0.0786 | 255 | 152,350 |
| `ligatures_01.pdf` | 3 | 1224x1584 | 3.9536 | [0.0207](./quality_benchmarks/delta_0.0207_ligatures_01.pdf.page_no_3.png) | 0.1139 | 201 | 220,891 |
| `indexed_device_n.pdf` | 1 | 2382x1684 | 3.9071 | [0.0269](./quality_benchmarks/delta_0.0277_indexed_device_n.pdf.page_no_1.png) | 0.1096 | 202 | 439,711 |
| `indexed_iccbased.pdf` | 1 | 1224x1512 | 3.6519 | [0.0205](./quality_benchmarks/delta_0.0205_indexed_iccbased.pdf.page_no_1.png) | 0.1234 | 155 | 228,284 |
| `stream_parameter_misinterpretation_01.pdf` | 1 | 1224x1584 | 3.6163 | [0.0191](./quality_benchmarks/delta_0.0191_stream_parameter_misinterpretation_01.pdf.page_no_1.png) | 0.0999 | 255 | 193,714 |
| `cropbox_versus_mediabox_01.pdf` | 1 | 1191x1684 | 3.5744 | [0.0190](./quality_benchmarks/delta_0.0447_cropbox_versus_mediabox_01.pdf.page_no_1.png) | 0.0933 | 224 | 187,036 |
| `ligatures_01.pdf` | 4 | 1224x1584 | 3.5698 | [0.0190](./quality_benchmarks/delta_0.0190_ligatures_01.pdf.page_no_4.png) | 0.1012 | 241 | 196,249 |
| `6480366468566741514.pdf` | 1 | 1190x1684 | 3.4959 | [0.0206](./quality_benchmarks/delta_0.0206_6480366468566741514.pdf.page_no_1.png) | 0.1038 | 218 | 208,021 |
| `font_11.pdf` | 2 | 1191x1582 | 3.4476 | [0.0191](./quality_benchmarks/delta_0.0191_font_11.pdf.page_no_2.png) | 0.0997 | 233 | 187,878 |
| `font_06.pdf` | 1 | 1190x1588 | 3.4216 | [0.0179](./quality_benchmarks/delta_0.0179_font_06.pdf.page_no_1.png) | 0.0952 | 255 | 179,826 |
| `device_gray_01.pdf` | 1 | 1224x1584 | 3.3863 | [0.0177](./quality_benchmarks/delta_0.0177_device_gray_01.pdf.page_no_1.png) | 0.0604 | 255 | 117,038 |
| `font_05.pdf` | 1 | 868x1327 | 3.3510 | [0.0175](./quality_benchmarks/delta_0.0175_font_05.pdf.page_no_1.png) | 0.0951 | 163 | 109,555 |
| `form_fields.pdf` | 4 | 1224x1584 | 3.3054 | [0.0183](./quality_benchmarks/delta_0.0398_form_fields.pdf.page_no_4.png) | 0.0726 | 255 | 140,826 |
| `font_07.pdf` | 1 | 1191x1571 | 3.2894 | [0.0181](./quality_benchmarks/delta_0.0370_font_07.pdf.page_no_1.png) | 0.0972 | 210 | 181,944 |
| `font_09.pdf` | 1 | 1190x1684 | 3.2271 | [0.0169](./quality_benchmarks/delta_0.0169_font_09.pdf.page_no_1.png) | 0.0959 | 166 | 192,190 |
| `rotated_text_07.pdf` | 1 | 1190x1684 | 3.2165 | [0.0168](./quality_benchmarks/delta_0.0168_rotated_text_07.pdf.page_no_1.png) | 0.0503 | 255 | 100,819 |
| `fillable_form.pdf` | 1 | 1224x1584 | 3.1795 | [0.0169](./quality_benchmarks/delta_0.1578_fillable_form.pdf.page_no_1.png) | 0.0440 | 255 | 85,211 |
| `complex_invisible_fonts_05.pdf` | 1 | 1191x1684 | 3.1688 | [0.0218](./quality_benchmarks/delta_0.0480_complex_invisible_fonts_05.pdf.page_no_1.png) | 0.0821 | 152 | 164,564 |
| `ligatures_01.pdf` | 1 | 1224x1584 | 3.0603 | [0.0163](./quality_benchmarks/delta_0.0358_ligatures_01.pdf.page_no_1.png) | 0.0897 | 255 | 173,951 |
| `complex_invisible_fonts_04.pdf` | 1 | 1191x1684 | 2.9696 | [0.0203](./quality_benchmarks/delta_0.0472_complex_invisible_fonts_04.pdf.page_no_1.png) | 0.0698 | 152 | 139,930 |
| `annots_01.pdf` | 1 | 1191x1684 | 2.8965 | [0.0167](./quality_benchmarks/delta_0.0167_annots_01.pdf.page_no_1.png) | 0.0717 | 255 | 143,766 |
| `2508.13113v2.pdf` | 9 | 1224x1584 | 2.7856 | [0.0151](./quality_benchmarks/delta_0.0151_2508.13113v2.pdf.page_no_9.png) | 0.0749 | 255 | 145,191 |
| `font_11.pdf` | 1 | 1191x1582 | 2.7568 | [0.0146](./quality_benchmarks/delta_0.0146_font_11.pdf.page_no_1.png) | 0.0866 | 196 | 163,259 |
| `math_latex_formulas.pdf` | 3 | 1224x1584 | 2.7421 | [0.0147](./quality_benchmarks/delta_0.0147_math_latex_formulas.pdf.page_no_3.png) | 0.0537 | 237 | 104,081 |
| `form_fields.pdf` | 1 | 1224x1584 | 2.7282 | [0.0155](./quality_benchmarks/delta_0.0443_form_fields.pdf.page_no_1.png) | 0.0545 | 255 | 105,685 |
| `4865216256588543301.pdf` | 6 | 1191x1684 | 2.7170 | [0.0142](./quality_benchmarks/delta_0.0142_4865216256588543301.pdf.page_no_6.png) | 0.0676 | 250 | 135,504 |
| `4865216256588543301.pdf` | 3 | 1191x1684 | 2.7078 | [0.0142](./quality_benchmarks/delta_0.0142_4865216256588543301.pdf.page_no_3.png) | 0.0675 | 238 | 135,472 |
| `table_of_contents_01.pdf` | 3 | 1224x1584 | 2.6805 | [0.0140](./quality_benchmarks/delta_0.0140_table_of_contents_01.pdf.page_no_3.png) | 0.0771 | 161 | 149,531 |
| `table_of_contents_01.pdf` | 2 | 1224x1584 | 2.6698 | [0.0140](./quality_benchmarks/delta_0.0140_table_of_contents_01.pdf.page_no_2.png) | 0.0800 | 167 | 155,181 |
| `table_of_contents_01.pdf` | 4 | 1224x1584 | 2.6593 | [0.0139](./quality_benchmarks/delta_0.0139_table_of_contents_01.pdf.page_no_4.png) | 0.0778 | 174 | 150,908 |
| `4865216256588543301.pdf` | 4 | 1191x1684 | 2.6585 | [0.0139](./quality_benchmarks/delta_0.0139_4865216256588543301.pdf.page_no_4.png) | 0.0666 | 235 | 133,521 |
| `math_latex_formulas.pdf` | 1 | 1224x1584 | 2.6476 | [0.0142](./quality_benchmarks/delta_0.0142_math_latex_formulas.pdf.page_no_1.png) | 0.0517 | 242 | 100,287 |
| `type3_fonts.pdf` | 1 | 1224x1584 | 2.6464 | [0.0138](./quality_benchmarks/delta_0.0138_type3_fonts.pdf.page_no_1.png) | 0.0527 | 255 | 102,184 |
| `2508.13113v2.pdf` | 2 | 1224x1584 | 2.6339 | [0.0144](./quality_benchmarks/delta_0.0144_2508.13113v2.pdf.page_no_2.png) | 0.0770 | 255 | 149,280 |
| `cropbox_versus_mediabox_02.pdf` | 1 | 1088x1266 | 2.6279 | [0.0201](./quality_benchmarks/delta_0.0711_cropbox_versus_mediabox_02.pdf.page_no_1.png) | 0.0329 | 162 | 45,279 |
| `4865216256588543301.pdf` | 5 | 1191x1684 | 2.6221 | [0.0137](./quality_benchmarks/delta_0.0137_4865216256588543301.pdf.page_no_5.png) | 0.0658 | 238 | 131,977 |
| `4865216256588543301.pdf` | 8 | 1191x1684 | 2.5880 | [0.0135](./quality_benchmarks/delta_0.0135_4865216256588543301.pdf.page_no_8.png) | 0.0659 | 235 | 132,250 |
| `stream_parameter_misinterpretation_02.pdf` | 1 | 1190x1690 | 2.5878 | [0.0136](./quality_benchmarks/delta_0.0136_stream_parameter_misinterpretation_02.pdf.page_no_1.png) | 0.0645 | 255 | 129,786 |
| `4865216256588543301.pdf` | 7 | 1191x1684 | 2.5755 | [0.0135](./quality_benchmarks/delta_0.0135_4865216256588543301.pdf.page_no_7.png) | 0.0652 | 238 | 130,821 |
| `font_10.pdf` | 1 | 1224x1584 | 2.5281 | [0.0168](./quality_benchmarks/delta_0.0419_font_10.pdf.page_no_1.png) | 0.0639 | 223 | 123,960 |
| `4865216256588543301.pdf` | 9 | 1191x1684 | 2.4934 | [0.0130](./quality_benchmarks/delta_0.0130_4865216256588543301.pdf.page_no_9.png) | 0.0633 | 237 | 126,962 |
| `test-parent-mediabox.pdf` | 1 | 1920x1080 | 2.4599 | [0.0129](./quality_benchmarks/delta_0.0129_test-parent-mediabox.pdf.page_no_1.png) | 0.0176 | 255 | 36,547 |
| `font_03.pdf` | 1 | 1224x1584 | 2.4003 | [0.0127](./quality_benchmarks/delta_0.0127_font_03.pdf.page_no_1.png) | 0.0585 | 255 | 113,353 |
| `2508.13113v2.pdf` | 17 | 1224x1584 | 2.3791 | [0.0128](./quality_benchmarks/delta_0.0128_2508.13113v2.pdf.page_no_17.png) | 0.0719 | 209 | 139,427 |
| `14770497121209673752.pdf` | 1 | 1191x1684 | 2.2001 | [0.0130](./quality_benchmarks/delta_0.0130_14770497121209673752.pdf.page_no_1.png) | 0.0554 | 255 | 111,086 |
| `annots_02.pdf` | 1 | 1191x1684 | 2.1380 | [0.0115](./quality_benchmarks/delta_0.0115_annots_02.pdf.page_no_1.png) | 0.0507 | 255 | 101,588 |
| `inflate_jpeg.pdf` | 1 | 1224x1584 | 2.1344 | [0.0112](./quality_benchmarks/delta_0.0112_inflate_jpeg.pdf.page_no_1.png) | 0.0354 | 216 | 68,589 |
| `cropbox_versus_mediabox_02.pdf` | 3 | 1088x1266 | 2.1015 | [0.0117](./quality_benchmarks/delta_0.0196_cropbox_versus_mediabox_02.pdf.page_no_3.png) | 0.0616 | 139 | 84,811 |
| `580-17-PIB_Unfall-Provinzial-IPID_Stand-12.2016.pdf` | 1 | 1191x1684 | 2.0456 | [0.0115](./quality_benchmarks/delta_0.0115_580-17-PIB_Unfall-Provinzial-IPID_Stand-12.2016.pdf.page_no_1.png) | 0.0604 | 225 | 121,057 |
| `font_02.pdf` | 1 | 1224x1584 | 2.0085 | [0.0123](./quality_benchmarks/delta_0.0123_font_02.pdf.page_no_1.png) | 0.0471 | 255 | 91,410 |
| `deep-mediabox-inheritance.pdf` | 2 | 1190x1684 | 1.9939 | [0.0104](./quality_benchmarks/delta_0.0104_deep-mediabox-inheritance.pdf.page_no_2.png) | 0.0562 | 171 | 112,566 |
| `jpeg_retry_logic.pdf` | 1 | 1440x810 | 1.9593 | [0.0132](./quality_benchmarks/delta_0.0138_jpeg_retry_logic.pdf.page_no_1.png) | 0.0322 | 255 | 37,574 |
| `rotated_page_02.pdf` | 1 | 906x1305 | 1.9296 | [0.0101](./quality_benchmarks/delta_0.0101_rotated_page_02.pdf.page_no_1.png) | 0.0578 | 178 | 68,333 |
| `580-17-PIB_Unfall-Provinzial-IPID_Stand-12.2016.pdf` | 2 | 1191x1684 | 1.8664 | [0.0103](./quality_benchmarks/delta_0.0103_580-17-PIB_Unfall-Provinzial-IPID_Stand-12.2016.pdf.page_no_2.png) | 0.0509 | 196 | 102,025 |
| `right_to_left_03.pdf` | 1 | 1191x1685 | 1.8608 | [0.0100](./quality_benchmarks/delta_0.0100_right_to_left_03.pdf.page_no_1.png) | 0.0424 | 221 | 85,061 |
| `complex_jbig2_overlays.pdf` | 1 | 1224x1584 | 1.8587 | [0.0099](./quality_benchmarks/delta_0.0099_complex_jbig2_overlays.pdf.page_no_1.png) | 0.0369 | 255 | 71,470 |
| `text_as_lines_02.pdf` | 1 | 1190x1684 | 1.8518 | [0.0099](./quality_benchmarks/delta_0.0099_text_as_lines_02.pdf.page_no_1.png) | 0.0429 | 251 | 86,023 |
| `device_cymk_01.pdf` | 1 | 1531x1191 | 1.8383 | [0.0119](./quality_benchmarks/delta_0.0216_device_cymk_01.pdf.page_no_1.png) | 0.0697 | 146 | 127,131 |
| `4865216256588543301.pdf` | 2 | 1191x1684 | 1.8088 | [0.0095](./quality_benchmarks/delta_0.0095_4865216256588543301.pdf.page_no_2.png) | 0.0590 | 242 | 118,247 |
| `rotated_text_03.pdf` | 1 | 737x843 | 1.8023 | [0.0107](./quality_benchmarks/delta_0.0107_rotated_text_03.pdf.page_no_1.png) | 0.0542 | 196 | 33,677 |
| `right_to_left_04.pdf` | 1 | 1191x1684 | 1.7545 | [0.0092](./quality_benchmarks/delta_0.0092_right_to_left_04.pdf.page_no_1.png) | 0.0422 | 252 | 84,719 |
| `text_as_lines_01.pdf` | 1 | 1190x1684 | 1.7059 | [0.0089](./quality_benchmarks/delta_0.0089_text_as_lines_01.pdf.page_no_1.png) | 0.0482 | 247 | 96,567 |
| `font_01.pdf` | 1 | 1224x1584 | 1.6493 | [0.0086](./quality_benchmarks/delta_0.0086_font_01.pdf.page_no_1.png) | 0.0450 | 203 | 87,225 |
| `form_fields.pdf` | 2 | 1224x1584 | 1.6123 | [0.0094](./quality_benchmarks/delta_0.0286_form_fields.pdf.page_no_2.png) | 0.0322 | 255 | 62,335 |
| `rotated_text_04.pdf` | 1 | 737x843 | 1.5652 | [0.0091](./quality_benchmarks/delta_0.0091_rotated_text_04.pdf.page_no_1.png) | 0.0585 | 155 | 36,345 |
| `PDF32000_2008.pdf` | 2 | 1190x1684 | 1.5193 | [0.0083](./quality_benchmarks/delta_0.0083_PDF32000_2008.pdf.page_no_2.png) | 0.0424 | 255 | 84,912 |
| `form_fields.pdf` | 3 | 1224x1584 | 1.5159 | [0.0089](./quality_benchmarks/delta_0.2192_form_fields.pdf.page_no_3.png) | 0.0283 | 255 | 54,959 |
| `rotated_text_01.pdf` | 1 | 737x843 | 1.4389 | [0.0094](./quality_benchmarks/delta_0.0094_rotated_text_01.pdf.page_no_1.png) | 0.0595 | 156 | 36,967 |
| `core14-alias-no-widths-extended.pdf` | 1 | 1224x1584 | 1.4364 | [0.0075](./quality_benchmarks/delta_0.0075_core14-alias-no-widths-extended.pdf.page_no_1.png) | 0.0237 | 255 | 45,931 |
| `rotated_text_06.pdf` | 1 | 737x843 | 1.3938 | [0.0077](./quality_benchmarks/delta_0.0077_rotated_text_06.pdf.page_no_1.png) | 0.0567 | 119 | 35,199 |
| `table_of_contents_01.pdf` | 1 | 1224x1584 | 1.3766 | [0.0072](./quality_benchmarks/delta_0.0072_table_of_contents_01.pdf.page_no_1.png) | 0.0415 | 169 | 80,501 |
| `rotated_text_05.pdf` | 1 | 737x843 | 1.2137 | [0.0064](./quality_benchmarks/delta_0.0064_rotated_text_05.pdf.page_no_1.png) | 0.0476 | 119 | 29,546 |
| `rotated_text_02.pdf` | 1 | 737x843 | 1.2128 | [0.0064](./quality_benchmarks/delta_0.0064_rotated_text_02.pdf.page_no_1.png) | 0.0482 | 119 | 29,952 |
| `right_to_left.pdf` | 1 | 1224x1584 | 1.1304 | [0.0059](./quality_benchmarks/delta_0.0059_right_to_left.pdf.page_no_1.png) | 0.0280 | 255 | 54,249 |
| `duplicate_bold_text_01.pdf` | 1 | 1191x1684 | 1.1013 | [0.0058](./quality_benchmarks/delta_0.0063_duplicate_bold_text_01.pdf.page_no_1.png) | 0.0208 | 211 | 41,678 |
| `right_to_left_02.pdf` | 1 | 1191x1684 | 1.0205 | [0.0063](./quality_benchmarks/delta_0.0063_right_to_left_02.pdf.page_no_1.png) | 0.0277 | 154 | 55,465 |
| `10400964487025769287.pdf` | 1 | 1190x1684 | 1.0034 | [0.0052](./quality_benchmarks/delta_0.0052_10400964487025769287.pdf.page_no_1.png) | 0.0257 | 233 | 51,480 |
| `text_as_lines_01.pdf` | 3 | 1190x1684 | 0.9971 | [0.0052](./quality_benchmarks/delta_0.0052_text_as_lines_01.pdf.page_no_3.png) | 0.0287 | 247 | 57,506 |
| `rotated_page_01.pdf` | 1 | 1584x1224 | 0.9489 | [0.0052](./quality_benchmarks/delta_0.0076_rotated_page_01.pdf.page_no_1.png) | 0.0261 | 206 | 50,598 |
| `broken_media_box_v01.pdf` | 1 | 1584x1224 | 0.8929 | [0.0056](./quality_benchmarks/delta_0.0056_broken_media_box_v01.pdf.page_no_1.png) | 0.0313 | 198 | 60,649 |
| `4865216256588543301.pdf` | 1 | 1191x1684 | 0.8354 | [0.0044](./quality_benchmarks/delta_0.0044_4865216256588543301.pdf.page_no_1.png) | 0.0214 | 200 | 42,955 |
| `text_as_lines_01.pdf` | 2 | 1190x1684 | 0.8082 | [0.0042](./quality_benchmarks/delta_0.0042_text_as_lines_01.pdf.page_no_2.png) | 0.0250 | 255 | 50,011 |
| `device_n_black.pdf` | 1 | 1224x1584 | 0.7875 | [0.0067](./quality_benchmarks/delta_0.3618_device_n_black.pdf.page_no_1.png) | 0.0011 | 132 | 2,146 |
| `math_latex_formulas.pdf` | 2 | 1224x1584 | 0.7507 | [0.0039](./quality_benchmarks/delta_0.0039_math_latex_formulas.pdf.page_no_2.png) | 0.0181 | 211 | 35,055 |
| `dln-v1.pdf` | 1 | 1152x1152 | 0.7228 | [0.0042](./quality_benchmarks/delta_0.0042_dln-v1.pdf.page_no_1.png) | 0.0135 | 255 | 17,974 |
| `4865216256588543301.pdf` | 10 | 1191x1684 | 0.6692 | [0.0035](./quality_benchmarks/delta_0.0035_4865216256588543301.pdf.page_no_10.png) | 0.0158 | 168 | 31,742 |
| `font_04.pdf` | 1 | 1191x1684 | 0.5508 | [0.0029](./quality_benchmarks/delta_0.0029_font_04.pdf.page_no_1.png) | 0.0152 | 187 | 30,564 |
| `font_08.pdf` | 1 | 1190x1684 | 0.4988 | [0.0026](./quality_benchmarks/delta_0.0026_font_08.pdf.page_no_1.png) | 0.0142 | 236 | 28,548 |
| `bitmap_decoding_01.pdf` | 1 | 1224x1584 | 0.3782 | [0.0020](./quality_benchmarks/delta_0.0020_bitmap_decoding_01.pdf.page_no_1.png) | 0.0089 | 255 | 17,228 |
| `ccitt_with_invisiable_text.pdf` | 1 | 1225x1608 | 0.3181 | [0.0017](./quality_benchmarks/delta_0.0017_ccitt_with_invisiable_text.pdf.page_no_1.png) | 0.0107 | 129 | 21,136 |
| `jpeg_2000_01.pdf` | 1 | 1191x1720 | 0.2732 | [0.0019](./quality_benchmarks/delta_0.0019_jpeg_2000_01.pdf.page_no_1.png) | 0.0076 | 215 | 15,654 |
| `PDF32000_2008.pdf` | 1 | 1190x1684 | 0.2705 | [0.0014](./quality_benchmarks/delta_0.0014_PDF32000_2008.pdf.page_no_1.png) | 0.0059 | 238 | 11,867 |
| `jbig2_test_01.pdf` | 1 | 1584x1224 | 0.2225 | [0.0015](./quality_benchmarks/delta_0.0015_jbig2_test_01.pdf.page_no_1.png) | 0.0058 | 179 | 11,195 |
| `rotated_image.pdf` | 1 | 1584x1224 | 0.2225 | [0.0015](./quality_benchmarks/delta_0.0015_rotated_image.pdf.page_no_1.png) | 0.0058 | 179 | 11,195 |
| `6480366468566741514.pdf` | 2 | 1190x1684 | 0.1584 | [0.0008](./quality_benchmarks/delta_0.0008_6480366468566741514.pdf.page_no_2.png) | 0.0054 | 147 | 10,916 |
| `math_latex_formulas.pdf` | 5 | 1224x1584 | 0.1464 | [0.0008](./quality_benchmarks/delta_0.0008_math_latex_formulas.pdf.page_no_5.png) | 0.0032 | 212 | 6,222 |
| `ocr_test_rotated_000.pdf` | 1 | 1191x1684 | 0.1417 | [0.0007](./quality_benchmarks/delta_0.0007_ocr_test_rotated_000.pdf.page_no_1.png) | 0.0043 | 123 | 8,663 |
| `ocr_test_rotated_090.pdf` | 1 | 1684x1191 | 0.1417 | [0.0007](./quality_benchmarks/delta_0.0007_ocr_test_rotated_090.pdf.page_no_1.png) | 0.0043 | 123 | 8,663 |
| `ocr_test_rotated_180.pdf` | 1 | 1191x1684 | 0.1417 | [0.0007](./quality_benchmarks/delta_0.0007_ocr_test_rotated_180.pdf.page_no_1.png) | 0.0043 | 123 | 8,663 |
| `ocr_test_rotated_270.pdf` | 1 | 1684x1191 | 0.1417 | [0.0007](./quality_benchmarks/delta_0.0007_ocr_test_rotated_270.pdf.page_no_1.png) | 0.0043 | 123 | 8,663 |
| `macroman_encoding_bug_demo.pdf` | 1 | 1224x1584 | 0.0721 | [0.0004](./quality_benchmarks/delta_0.0004_macroman_encoding_bug_demo.pdf.page_no_1.png) | 0.0018 | 255 | 3,404 |
| `math_latex_formulas.pdf` | 4 | 1224x1584 | 0.0295 | [0.0002](./quality_benchmarks/delta_0.0002_math_latex_formulas.pdf.page_no_4.png) | 0.0009 | 121 | 1,738 |
