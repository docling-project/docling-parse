# Page-aware PDF outlines in docling-parse

> Plan for extending docling-parse so that the docling backends can resolve PDF bookmarks
> to a **page** (and a **position on that page**) without falling back to pypdfium2.
>
> Upstream context: [docling#4169](https://github.com/docling-project/docling/pull/4169)
> — *fix(pdf): resolve bookmarks using page-aware PDF outlines*, resolving docling#4162.
>
> Three PRs, in order (§7):
> **A** `docling-core` — the shared destination model (§11) — **merged and released** in
>   [v2.95.0](https://github.com/docling-project/docling-core/releases/tag/v2.95.0)
>   ([#747](https://github.com/docling-project/docling-core/pull/747), commit `4631492`)
> **B** `docling-parse` — extraction and API (§4, §5) — **implemented**, depending on the
>   released `docling-core>=2.95.0`
> **C** `docling` — rework of #4169 on top of them (§6) — not started

---

## 1. What docling#4169 does, and why it is a workaround

docling's heading-hierarchy stage uses the PDF outline (bookmarks) as the most authoritative
heading signal. `_PdfOutlineItem` carries:

| field     | meaning                                                     |
| --------- | ----------------------------------------------------------- |
| `title`   | bookmark title                                               |
| `level`   | 0-based depth                                                |
| `page_no` | **1-based target page**                                      |
| `y_top`   | **vertical position of the target, top-left origin**          |

Two extractors feed it:

* `extract_outline_from_pdfium()` — fills all four fields (PDFium resolves the destination).
* `extract_outline_from_docling_parse()` — fills only `title` and `level`; `page_no` and
  `y_top` are `None`, because docling-parse's `get_table_of_contents()` returns titles and
  hierarchy only.

Without `page_no`, every bookmark is fuzzy-matched against *every* heading in the document.
That is both **wrong** (a bookmark matches a same-titled heading on an unrelated page —
docling#4162) and **slow** (the PR reports 1140 s → 11 s on a large document once matching is
narrowed per page).

PR #4169 fixes this by making **both docling-parse backends open a second, throwaway PDFium
document** purely to read the outline, keeping the native docling-parse ToC as a fallback.
For `ThreadedDoclingParseDocumentBackend` this is on top of an *already existing* workaround
that loads a whole extra lazy `DoclingPdfParser` document just to read the ToC.

That is the wart we remove: **docling-parse already has the PDF open in qpdf and already knows
every page's geometry. It should be the one answering this question.**

### 1.1 Consequences of fixing it here

* Both docling-parse backends drop the PDFium detour and the transient-document dance
  (`extract_outline_from_pdfium_path_or_stream`, the `BytesIO` seek/restore, the
  `PdfiumError`/`RuntimeError` handling, the second `DoclingPdfParser` load).
* `docling-parse` becomes usable for bookmark resolution *without* pypdfium2 at all, which is
  the stated goal of the `extract_outline_from_docling_parse` code path.
* The threaded backend stops paying a full second document open per conversion.
* docling-parse's own `PdfTocEntry.page` field — declared public since it was introduced and
  **always `None`**, because the C++ side never emitted it — is replaced by a destination that
  actually carries data.
* The two duplicate Python views of the same ToC JSON (§2.2) collapse into one shared model in
  `docling-core`, so `get_table_of_contents()` and `get_annotations().table_of_contents` stop
  being different types of the same thing.

---

## 2. Gap analysis in docling-parse (as of `8cff78a`)

### 2.1 C++

`src/parse/qpdf/annots.h`, `extract_toc_entry_in_json()`:

```cpp
    // Extract title
    if(node.hasKey("/A"))
      {
        //toc_entry["link"] = to_json(node.getKey("/A"), {}, 0, 8);
      }

    // Extract destination
    if(node.hasKey("/Dest"))
      {
        //auto dest = node.getKey("/Dest");
        //toc_entry["destination"] = to_json(dest, {}, 0, 8);
      }
```

Destination handling is commented out entirely. The emitted JSON per node is
`{"title", "level", "children"}` — nothing else.

Secondary weaknesses in the same routine:

1. **Loop detection uses `first.unparse()` as the key.** That serialises the whole object to a
   string on every node — expensive — and is not an identity test: two structurally identical
   but distinct outline items collide and the second is silently dropped.
2. **The `visited` set is shared across recursion levels but only guards `/First`/`/Next`
   chains**, not `/Parent` cycles.
3. `extract_toc_in_json()` walks `/Outlines` by hand and re-implements what qpdf's
   `QPDFOutlineDocumentHelper` already does correctly, including named-destination resolution.

### 2.2 Python — and the duplicate ToC model

There are **two** Python views of the *same* C++ JSON blob (`json_annots["table_of_contents"]`):

| view | model | shape | built by |
| ---- | ----- | ----- | -------- |
| `PdfDocument.get_table_of_contents()` | `docling_core…PdfTableOfContents` | `{text, orig, marker, children}`, wrapped in a synthetic `<root>` node | `_to_table_of_contents()` |
| `PdfDocument.get_annotations().table_of_contents` | `docling_parse…PdfTocEntry` | `{title, level, page, children}`, a flat list of roots | `_to_pdf_toc_entry()` |

Neither carries a destination:

* `PdfTocEntry.page: int | None` is **never populated** — the C++ side emits no such key. Dead
  weight, not a usable extension point.
* `PdfTableOfContents` **structurally cannot carry a page**, and it lives in `docling-core`.

So the honest answer is not "pick one and bolt a field on", it is: **there should be one ToC
model, it should live in docling-core next to every other PDF page model, and it should carry
the destination.** That is what §5 and §11 do.

Separately, `DoclingThreadedPdfParser` exposes no outline/annotation accessor at all, even
though `docling_threaded_base` holds a full `pdf_decoder<DOCUMENT>` per document key.

---

## 3. Design principles for this change

1. **Resolve destinations with qpdf, not by hand.** `QPDFOutlineDocumentHelper` /
   `QPDFOutlineObjectHelper` already implement ISO 32000-1 §12.3.3 traversal and §12.3.2.3
   named-destination resolution (both the catalog `/Dests` dictionary and the `/Names /Dests`
   name tree), plus `/A` actions with `/S /GoTo`. Verified against the vendored qpdf 12.2.0
   source in `externals/`.
2. **Ground the destination semantics in ISO 32000-1 §12.3.2.2**, not in "whatever PDFium
   returns". Deliberate deviations get named in comments.
3. **Reuse the page-geometry code that decodes pages.** The outline's coordinates must land in
   *exactly* the frame that `PdfTextCell` coordinates land in, or matching a bookmark against a
   heading's provenance is meaningless. That means reusing
   `page_item<PAGE_DIMENSION>::execute()` and `::rotate()`, not re-deriving `/MediaBox` +
   `/Rotate` maths.
4. **Structure-only, no page decoding.** Outline extraction stays as cheap as it is today:
   dictionary reads only, no content streams, no fonts.
5. **One representation.** docling-parse has *two* Python views of one ToC JSON blob today
   (§2.2). This change leaves **one**, and puts it where every other PDF page model already
   lives: `docling_core…PdfTableOfContents`, enriched with the destination (§11). No new
   parallel model in either repo.
6. **Additive JSON.** The C++ → Python JSON contract gains keys; nothing existing changes shape.

---

## 4. C++ design

### 4.1 New file: `src/parse/qpdf/outline.h`

A single class replaces `extract_toc_in_json()` / `extract_toc_entry_in_json()` in
`annots.h` (which are deleted, not commented out).

```cpp
namespace pdflib
{
  // Extracts the document outline (ISO 32000-1, 12.3.3 "Document Outline") together with the
  // resolved destination of each item (12.3.2 "Destinations").
  //
  // Destination coordinates are reported in the same frame as decoded page content: the page
  // is normalised by its /Rotate angle exactly as pdf_decoder<PAGE>::rotate_contents() does,
  // and the result is expressed with a bottom-left origin.
  class pdf_outline
  {
  public:

    pdf_outline(QPDF& qpdf_document,
                const std::vector<QPDFObjectHandle>& qpdf_pages);
    ~pdf_outline();

    nlohmann::json get();

  private:

    struct page_frame
    {
      int                   page_number;  // 1-based
      int                   angle;
      std::pair<double, double> delta;
      std::array<double, 4> bbox;         // rotated page rectangle, bottom-left origin
    };

    nlohmann::json to_json(QPDFOutlineObjectHelper& item, int level);

    nlohmann::json resolve_destination(QPDFOutlineObjectHelper& item);
    std::optional<int> resolve_destination_page(QPDFObjectHandle dest);

    const page_frame& get_page_frame(int page_number);

    std::string title_of(QPDFOutlineObjectHelper& item);

  private:

    QPDF& qpdf_document;
    const std::vector<QPDFObjectHandle>& qpdf_pages;

    std::map<QPDFObjGen, int> page_number_of;   // page object -> 1-based page number
    std::map<int, page_frame> page_frames;      // lazily filled, only for targeted pages
  };
}
```

Implementations live outside the class body, matching the house style.

### 4.2 Destination resolution — ISO 32000-1, Table 151 (§12.3.2.2)

`QPDFOutlineObjectHelper::getDest()` returns the destination **array** for direct, named, and
`/GoTo`-action destinations, and a null object otherwise (notably for `/GoToR` and `/GoToE`
remote destinations, which have no page in *this* document — correctly reported as "no
destination").

The array is `[page, /Kind, args…]`. The kinds and the argument we care about:

| syntax                                    | `kind`   | `x`      | `y`     |
| ----------------------------------------- | -------- | -------- | ------- |
| `[p /XYZ left top zoom]`                  | `XYZ`    | `left`   | `top`   |
| `[p /Fit]`                                | `FIT`    | —        | —       |
| `[p /FitH top]`                           | `FIT_H`  | —        | `top`   |
| `[p /FitV left]`                          | `FIT_V`  | `left`   | —       |
| `[p /FitR left bottom right top]`         | `FIT_R`  | `left`   | `top`   |
| `[p /FitB]`                               | `FIT_B`  | —        | —       |
| `[p /FitBH top]`                          | `FIT_BH` | —        | `top`   |
| `[p /FitBV left]`                         | `FIT_BV` | `left`   | —       |

Rules taken straight from the spec:

* Any coordinate **may be `null`**, meaning "leave the current value unchanged"
  (§12.3.2.2). A `null` is reported as absent, never as `0`.
* `[p /XYZ …]` with a `null` `top` is common in real files; such an item still has a usable
  `page`, so `page` and the point are independent optionals.
* `dest[0]` is a **page object** for a destination inside this document. A few producers write
  a **page number** there instead (legal only for remote destinations, §12.3.2.2). We accept an
  integer as a 0-based page index — a named, deliberate deviation, logged at `WARNING`.
* An unknown/missing kind name yields a destination with a page and no point, rather than
  dropping the item.

`pdf_destination_kind` and its `to_string()` go into `src/parse/enums.h` alongside the other
parse enums.

### 4.3 Coordinate frame — the part that must be exactly right

Destination coordinates are in the target page's **default user space** (unrotated, origin at
the `/MediaBox` lower-left). Decoded page content is *not*: `pdf_decoder<PAGE>::rotate_contents()`
normalises the page by its `/Rotate` angle and moves every cell with it.

So the destination point is put through the *same* transform, using the *same* code:

```cpp
  page_item<PAGE_DIMENSION> dimension;
  dimension.execute(qpdf_pages.at(page_number - 1));   // reads /Rotate, /MediaBox, /CropBox,
                                                       // including page-tree inheritance
  int angle = dimension.get_angle();
  std::pair<double, double> delta = {0.0, 0.0};

  if((angle % 360) != 0)
    {
      delta = dimension.rotate(angle);                 // identical to rotate_contents()
    }

  utils::values::rotate_inplace(angle, x, y);          // identical to page_cell::rotate()
  utils::values::translate_inplace(delta, x, y);
```

`page_item<PAGE_DIMENSION>` and `utils::values` are therefore the single source of truth for
this maths; the outline extractor adds none of its own.

The `page_frame` cache means a page targeted by 300 bookmarks is measured once.

> Note: this is *more* correct than the PDFium path docling uses today, which subtracts an
> unrotated destination `y` from a **rotated** page height. On a `/Rotate 90` page that is
> simply wrong. Our fixtures will cover it (§8).

**Include ordering.** `src/parse.h` includes `parse/qpdf/annots.h` (line 57) *before*
`parse/page_item.h` / `parse/page_items/page_dimension.h` (lines 66–67), so the outline
extractor cannot live in `annots.h`. `parse/qpdf/outline.h` is included immediately after the
page-items block, with a one-line comment stating the dependency.

Correspondingly, `extract_document_annotations_in_json()` **stops** producing
`"table_of_contents"`; `pdf_decoder<DOCUMENT>::ensure_annots_loaded()` composes it:

```cpp
    json_annots = extract_document_annotations_in_json(qpdf_document, qpdf_root);
    json_annots["table_of_contents"] = pdf_outline(qpdf_document, qpdf_pages).get();
```

Both stay inside the existing `KEY_EXTRACT_DOC_ANNOTATIONS` timing block and behind the same
lazy `annots_loaded` guard, so nothing new is paid unless a caller asks.

### 4.4 JSON contract (additive)

Per outline node:

```jsonc
{
  "title": "Model Architecture",
  "level": 1,
  "destination": {                 // absent when the item has no resolvable destination
    "page_no": 7,                  // 1-based
    "kind": "XYZ",                 // ISO 32000-1, table 151; "UNKNOWN" when unnamed
    "coord_origin": "BOTTOMLEFT",
    "point": [72.0, 690.4],        // absent when the destination gives no vertical position
    "page_size": {                 // target page, same frame as "point"
      "width": 595.276,
      "height": 841.89
    }
  },
  "children": [ … ]
}
```

The key names are exactly the `PdfDestination` field names (§11.1), so the sub-dictionary is
handed to pydantic as it comes and the field list is not restated on the Python side.

`page_size` is carried on the destination deliberately: it lets a consumer flip to a top-left
origin **without decoding the target page**, which is the whole point of keeping this path
cheap. See decision **D1**.

**When `point` is present.** A destination may leave any coordinate `null`, meaning "retain the
current value" (12.3.2.2), and four of the eight syntaxes carry no vertical position at all.
Since the point exists to locate a heading *vertically*, it is emitted only when the destination
actually specifies a top — so `/Fit`, `/FitB`, `/FitV`, `/FitBV` and an `/XYZ` with a null `top`
produce a destination with a page and no point. The horizontal coordinate is filled from the
page's left edge when the destination omits it, because the rotation in §4.3 mixes the two axes
and needs both; on an unrotated page it cannot influence the reported vertical position.

`"title"`, `"level"` and `"children"` keep today's exact meaning and placement.

---

## 5. Python design

The models move to **docling-core** (§11 is the companion PR). This section describes the
resulting docling-parse surface.

### 5.1 One ToC model, carrying its destination

`docling_core.types.doc.page` gains `PdfDestinationKind` and `PdfDestination`, and
`PdfTableOfContents` gains a `destination` field and an `iterate()` walk. In docling-parse that
collapses the duplication in §2.2:

```python
# docling_parse/pdf_parser.py
from docling_core.types.doc.page import PdfTableOfContents   # already imported


class PdfAnnotations(BaseModel):
    form: dict[str, Any] | None = None
    language: str | None = None
    meta_xml: str | None = None
    table_of_contents: PdfTableOfContents | None = None      # was list[PdfTocEntry] | None
```

* **`PdfTocEntry` is deleted.** With `PdfTableOfContents.destination` in place it is a byte-for-byte
  duplicate of a docling-core model, built from the same JSON by a second converter. It is not
  re-exported from `docling_parse/__init__.py`; its only references are `pdf_parser.py` itself
  and one assertion block in `tests/test_regression_parse.py`.
* `_to_pdf_toc_entry()` is deleted; `_to_table_of_contents()` becomes the single converter and
  learns to fill `destination`.
* `PdfAnnotations.table_of_contents` and `PdfDocument.get_table_of_contents()` now return **the
  same object** — the synthetic `<root>` node, matching `ParsedPdfDocument.table_of_contents`.
  `get_table_of_contents()` becomes a one-line delegation to `get_annotations()`, so the two can
  no longer drift, and the existing `self._toc` cache collapses into the existing
  `self._annotations` cache.

There is deliberately **no `level` field** on the model: level is depth, and a stored copy can
only ever disagree with the tree. `PdfTableOfContents.iterate()` yields it (§11.2). The C++ JSON
keeps emitting `"level"` — it is the pre-existing contract of the raw
`parser.get_table_of_contents(key)` binding, and it is useful when eyeballing the JSON — but no
typed model mirrors it.

### 5.2 Threaded parser

`docling_threaded_base` already stores a `pdf_decoder<DOCUMENT>` per key, so this is a
pass-through, mirroring `number_of_pages`:

* `src/pybind/docling_threaded_base.h` → `nlohmann::json get_annotations(std::string key) const;`
* `app/pybind_parse.cpp` → `.def("get_annotations", …)` on `_threaded_pdf_parser`
* `docling_parse/pdf_parser.py` → `DoclingThreadedPdfParser.get_annotations(doc_key) -> PdfAnnotations | None`

The `PdfAnnotations` construction currently inlined in `PdfDocument.get_annotations()` moves to
a module-level `_to_annotations(dict) -> PdfAnnotations`, used by both, so the threaded and
non-threaded paths cannot drift.

### 5.3 No new `PdfDocument` accessor

`get_annotations().table_of_contents` and `get_table_of_contents()` already return the enriched
tree, and `PdfTableOfContents.iterate()` covers the only thing consumers were missing — a
recursion-safe pre-order walk. A third name for one concept would be a regression, not a feature.

## 6. What this lets docling delete

Once both PRs are released, docling#4169 reduces to:

```python
def extract_outline_from_docling_parse(dp_doc) -> list[_PdfOutlineItem]:
    toc = dp_doc.get_table_of_contents()
    if toc is None:
        return []

    items: list[_PdfOutlineItem] = []
    for level, entry in toc.iterate():
        title = (entry.text or entry.orig or "").strip()
        if not title:
            continue
        dest = entry.destination
        top_left = dest.to_top_left_origin() if dest else None
        items.append(_PdfOutlineItem(
            title=title,
            level=level,
            page_no=dest.page_no if dest else None,
            y_top=top_left.point.y if top_left and top_left.point else None,
        ))
    return items
```

and the threaded backend calls `self.parser.get_annotations(self.doc_key)` instead of loading a
second document. `extract_outline_from_pdfium_path_or_stream()` and both PDFium preference
branches in `docling_parse_backend.py` disappear, as does docling's hand-rolled explicit-stack
tree walk and the comment explaining why it exists. `extract_outline_from_pdfium()` stays for
the pypdfium2 backend, which is its proper home.

The page-indexing change in `heading_hierarchy_model.py` (the actual bug fix) is orthogonal and
stays exactly as PR #4169 has it.

## 7. Work breakdown

### PR A — `docling-core` (companion, lands first)

| # | Change | Files |
| - | ------ | ----- |
| A1 | `PdfDestinationKind`, `PdfDestination` (+ `to_top_left_origin()` / `to_bottom_left_origin()`) | `docling_core/types/doc/page.py` |
| A2 | `PdfTableOfContents.destination` + `PdfTableOfContents.iterate()` | `docling_core/types/doc/page.py` |
| A3 | Re-export the two new names | `docling_core/types/doc/__init__.py` |
| A4 | Tests | `tests/test_page.py` |

### PR B — `docling-parse` (this repo, depends on PR A) — **implemented**

| # | Change | Files |
| - | ------ | ----- |
| 1 | `pdf_destination_kind` + `to_string()` | `src/parse/enums.h` |
| 2 | `pdf_outline` class: traversal, destination resolution, page-frame cache, JSON emission | `src/parse/qpdf/outline.h` *(new)* |
| 3 | Delete `extract_toc_in_json` / `extract_toc_entry_in_json`; drop `"table_of_contents"` from `extract_document_annotations_in_json` | `src/parse/qpdf/annots.h` |
| 4 | Include `parse/qpdf/outline.h` after the page-items block, with the ordering comment | `src/parse.h` |
| 5 | Compose the outline in `ensure_annots_loaded()` | `src/parse/pdf_decoders/document.h` |
| 6 | `get_annotations(key)` on the threaded base + bindings on **both** `_threaded_pdf_parser` and `_threaded_pdf_renderer` (a `DoclingThreadedPdfParser` with a `render_config` is renderer-backed) | `src/pybind/docling_threaded_base.h`, `app/pybind_parse.cpp` |
| 7 | Delete `PdfTocEntry` + `_to_pdf_toc_entry()`; retype `PdfAnnotations.table_of_contents`; `_to_annotations()`; `get_table_of_contents()` delegates; `DoclingThreadedPdfParser.get_annotations()` | `docling_parse/pdf_parser.py` |
| 8 | Raise the `docling-core` floor from `>=2.85.0` to `>=2.95.0` (the release carrying PR A); the temporary `[tool.uv.sources]` git pin is gone | `pyproject.toml`, `uv.lock` |
| 9 | In-memory fixture builders + tests | `tests/outline_fixtures.py` *(new)*, `tests/test_regression_parse.py` |

Steps 1–5 are one commit (C++ extraction), 6–8 a second (API surface), 9 a third.

Both translation units that reach the new code pass `c++ -fsyntax-only` with `-Wall` clean:
`app/parse.cpp` (covers `parse.h`, `outline.h`, `document.h`) and `app/pybind_parse.cpp`
(covers `docling_threaded_base.h` and the new bindings).

### PR C — `docling` (upstream, depends on PR B)

Rework of docling#4169 per §6.

## 8. Tests

Extending `tests/test_regression_parse.py::test_table_of_contents` and adding cases:

1. **`table_of_contents_01.pdf`** — every entry now has `destination.page_no`; `Introduction`
   is on page 1; nested entries resolve to pages `>=` their parent's; `y` is inside
   `[0, page_size.height]`.
2. **Named destinations** — a fixture using `/Names /Dests` (name tree) and one using the
   legacy catalog `/Dests` dictionary; both must resolve to the same pages as an equivalent
   file with explicit destinations.
3. **`/A` `/GoTo` action destinations** — resolve identically to `/Dest`.
4. **`/GoToR` remote action** — `destination is None`, entry still present with title + level.
5. **Destination kinds** — one bookmark per kind in Table 151; assert `kind`, and that
   `point` is `None` exactly for `FIT`, `FIT_B` and for `XYZ` with a `null` `top`.
6. **Rotated pages** — a ToC pointing into `/Rotate 90/180/270` pages. Assert the destination
   point lands within the bounding box of the heading cell decoded from that page. This is the
   test that pins §4.3 and that the current PDFium path would fail.
7. **Cyclic / malformed outlines** — `/First` loop, `/Next` loop, missing `/Title`,
   `/Dest` pointing at a page not in the page tree. Must terminate and degrade, not throw.
8. **Threaded parity** — `DoclingThreadedPdfParser.get_annotations(key).table_of_contents`
   equals `DoclingPdfParser` + `PdfDocument.get_annotations().table_of_contents`.
9. **Model consolidation** — `get_table_of_contents()` and
   `get_annotations().table_of_contents` return the *same* object; the existing
   `PdfTocEntry` assertion block in `test_table_of_contents` is rewritten against
   `PdfTableOfContents` + `iterate()`.

The repo ships no PDF-authoring dependency, **and `tests/data/**` is gitignored** — it is a
pinned Hugging Face snapshot (`docling-project/regression-dataset-for-docling-parse`), not
repository content, so a new fixture file there would need a dataset publish and an
`HF_DATASET_REVISION` bump before CI could see it.

So the fixtures are not files. `tests/outline_fixtures.py` assembles them from raw PDF objects
and returns `bytes`, which the tests wrap in a `BytesIO` and hand to
`DoclingPdfParser.load()`. Nothing to publish, nothing to keep in sync, and the PDF being
tested is readable right next to the assertion. All four were validated with `qpdf --check`
while being written:

| builder | covers |
| ------- | ------ |
| `build_destination_kinds()` | all eight syntaxes of table 151, `/XYZ` with a null `top`, all-null `/XYZ`, a bare `[page]` dest, an item with no destination at all |
| `build_destination_references()` | explicit array, name-tree byte string, catalog `/Dests` name written as a destination *dictionary*, `/A /GoTo` with an array and with a name, `/A /GoToR` |
| `build_rotated_pages()` | `/Rotate` 0 / 90 / 180 / 270, each with a heading the bookmark must land on |
| `build_malformed_outline()` | a `/Next` cycle, a `/First` cycle back to the parent, a missing `/Title`, a destination page outside the page tree |

`table_of_contents_01.pdf` turned out to be the ideal case-1 fixture: every bookmark is an
`/A << /S /GoTo /D (section.N) >>` resolved through the `/Names /Dests` name tree to
`[<page> /XYZ 108 490.534 null]` — precisely the path that produced nothing before.

`docs/PDF32000_2008.pdf` §12.3.2–12.3.3 is the reference for every assertion above.

---

## 9. Risks and deliberate limits

* **qpdf skips direct (non-indirect) outline items.** `QPDFOutlineObjectHelper`'s constructor
  walks `/First`/`/Next` only while `cur.isIndirect()`. Today's hand-rolled walk accepts direct
  dictionaries. Such files are rare and malformed (§12.3.3 requires indirect references), but
  this is a behaviour change — it will be called out in the PR body and covered by fixture 7.
* **Depth limits.** Today: hard-coded `level >= 16`. qpdf: 50. Adopting qpdf's traversal raises
  the cap to 50, which is a strict improvement; nothing downstream assumes 16.
* **Cost.** Extraction stays dictionary-only. The added work is one `page_item<PAGE_DIMENSION>::execute()`
  per *targeted* page (cached) plus a `QPDFObjGen → page number` map built once from the
  already-materialised `qpdf_pages`. Negligible next to page decoding, and still fully lazy.
* **Encrypted documents.** Handled by qpdf at load; nothing outline-specific.
* **`PdfTocEntry` removal and the `PdfAnnotations.table_of_contents` retype are public-model
  changes** (D2). `PdfTocEntry` is not re-exported from `docling_parse/__init__.py` and its
  `page` field never carried a value, so the failure mode for any caller is a loud `ImportError`
  / `AttributeError`, not a silent wrong answer. `table_of_contents` changes from
  `list[PdfTocEntry] | None` to `PdfTableOfContents | None` (the `<root>` node) — a caller
  iterating it directly now iterates `.children`. Both called out in the PR body and release notes.
* **Cross-repo ordering.** PR B does not build against a `docling-core` without PR A. The
  `docling-core` floor bump (step 8) is what enforces it; until PR A is released, PR B can be
  developed against a local `_references/docling-core` checkout but must not be merged.
* This changes no rendering or parsing output — no groundtruth regeneration.

---

## 10. Design decisions (settled)

**D1 — Coordinate origin of the emitted destination: bottom-left, with `page_size`.**
The destination point is reported in `CoordOrigin.BOTTOMLEFT`, docling-parse's uniform
convention (the only origin `pdf_parser.py` uses today), in the same rotated page frame as the
target page's cells. `PdfDestination.page_size` carries the target page's rectangle in that
same frame, so a consumer flips to a top-left origin with `page_size.height - point.y` **without
decoding the target page** — which is what keeps this path structure-only and cheap.

*Rejected:* emitting `y_top` in top-left origin directly. It would be a one-line drop-in for
docling's `_PdfOutlineItem`, but it would put a second coordinate convention inside
docling-parse for the sake of exactly one consumer.

*Rejected:* bottom-left with no `page_size`. Leaner model, but it pushes the consumer into
loading the target page just to learn its height, which defeats the point.

**D2 — `PdfTocEntry` is removed entirely, not just its `page` field.**
Once `PdfTableOfContents` carries a destination (D4), `PdfTocEntry` is a duplicate model built
from the same JSON by a second converter — exactly the redundancy this change exists to remove.
Its `page` field has *always* been `None`, so no caller can depend on a value; a caller that
merely names the class or the field fails loudly at import or attribute access rather than
silently reading a stale duplicate. It is not re-exported from `docling_parse/__init__.py`.
Listed as a model change in the release notes.

**D3 — No new `PdfDocument` outline accessor.**
After D4 both `get_annotations().table_of_contents` and `get_table_of_contents()` return the
same enriched tree, and `PdfTableOfContents.iterate()` covers the only thing consumers were
missing — a recursion-safe pre-order walk. A third name for one concept would be a regression.

**D4 — The destination model lives in `docling-core`, on `PdfTableOfContents`.**
*(Reverses the earlier "leave docling-core alone" position, on the user's call to open a
companion PR.)* Every other PDF page/document model docling-parse emits — `PdfTextCell`,
`PdfPageGeometry`, `PdfHyperlink`, `BoundingRectangle`, `Coord2D`, `PdfMetaData`,
`PdfTableOfContents` itself — already lives in `docling-core`. Defining a *second* ToC model
inside docling-parse to carry the destination would entrench the duplication described in §2.2
instead of removing it, and would force docling to import a docling-parse-private type to read a
bookmark. Enriching the existing shared model gives one producer, one JSON contract, one typed
model, one tree. See §11.

---

## 11. Companion PR in `docling-core` — **implemented**

Branch `feat/extending-table-of-contents` in `_references/docling-core` (off `main` @ `a59c38c`,
version `2.94.1`). Three files touched, +236 / -3.
All changes are in `docling_core/types/doc/page.py`, in the `PdfMetaData` /
`PdfTableOfContents` / `ParsedPdfDocument` block at the end of the file.

`PdfTableOfContents` today is referenced only by `ParsedPdfDocument.table_of_contents` and the
`docling_core.types.doc` re-export; it has **no tests** and appears in **no** JSON schema
snapshot (`docs/schemas/` covers `DoclingDocument` only). So this is a low-blast-radius,
purely additive change.

### 11.1 New models

```python
class PdfDestinationKind(str, Enum):
    """Explicit-destination syntax (ISO 32000-1, 12.3.2.2, Table 151)."""

    XYZ = "XYZ"
    FIT = "FIT"
    FIT_H = "FIT_H"
    FIT_V = "FIT_V"
    FIT_R = "FIT_R"
    FIT_B = "FIT_B"
    FIT_BH = "FIT_BH"
    FIT_BV = "FIT_BV"
    UNKNOWN = "UNKNOWN"

    def __str__(self) -> str:
        """Return string representation of the enum value."""
        return str(self.value)


class PdfDestination(BaseModel):
    """Model representing a resolved PDF destination (ISO 32000-1, 12.3.2).

    ``point`` is in the target page's own coordinate frame, i.e. the frame that page's cells
    are reported in: the page normalised by its ``/Rotate`` angle. It is ``None`` when the
    destination kind carries no position (``FIT``, ``FIT_B``) or when the PDF left the
    coordinate ``null`` -- the spec allows ``null`` for "retain the current value".

    ``page_size`` is the target page's rectangle in that same frame, so a consumer can change
    the coordinate origin without loading the page.
    """

    page_no: PageNumber
    kind: PdfDestinationKind = PdfDestinationKind.UNKNOWN

    point: Coord2D | None = None
    coord_origin: CoordOrigin = CoordOrigin.BOTTOMLEFT

    page_size: Size

    def to_top_left_origin(self) -> "PdfDestination":
        """Return this destination with a top-left coordinate origin."""

    def to_bottom_left_origin(self) -> "PdfDestination":
        """Return this destination with a bottom-left coordinate origin."""
```

Both converters are no-ops when the origin already matches and flip
`y -> page_size.height - y` otherwise, mirroring `BoundingBox.to_top_left_origin()` /
`to_bottom_left_origin()` in `docling_core/types/doc/base.py`. Because `page_size` is on the
model they take **no** `page_height` argument — that is the whole reason it is carried.

`Size` must be added to the existing `from docling_core.types.doc.base import (…)` block in
`page.py`; `Coord2D`, `CoordOrigin` and `PageNumber` are already in scope there.

### 11.2 `PdfTableOfContents`

```python
class PdfTableOfContents(BaseModel):
    """Model representing a PDF table of contents entry with hierarchical structure."""

    text: str
    orig: str = ""

    marker: str = ""

    destination: PdfDestination | None = None      # NEW

    children: list["PdfTableOfContents"] = []

    def iterate(self) -> Iterator[tuple[int, "PdfTableOfContents"]]:
        """Yield ``(level, entry)`` for every descendant, depth-first in document order.

        ``self`` is not yielded and its direct children are at level ``0``, matching the
        convention that a document's outline hangs under a single synthetic root node.
        """
```

* `destination` is optional and defaults to `None`, so every existing construction site,
  serialized payload and `load_from_json()` file keeps validating unchanged.
* `iterate()` uses an **explicit stack**, not recursion. Deeply nested outlines are real
  (technical manuals, legal filings) and malformed PDFs can nest further still; docling
  currently hand-rolls exactly this walk with a comment saying why. Putting it here means
  neither docling nor docling-parse writes it again.
* **No `level` field.** Level is depth; a stored copy can only ever disagree with the tree.
  `iterate()` is where level comes from.
* `export_to_dict()` uses `exclude_none=True`, so entries without a destination serialize
  exactly as they do today.

`ParsedPdfDocument` needs no change — it already holds a `PdfTableOfContents`.

### 11.3 Exports

Add `PdfDestination` and `PdfDestinationKind` to the `docling_core.types.doc.page` import block
and the `__all__` list in `docling_core/types/doc/__init__.py`, keeping the existing
alphabetical order (they sort just before `PdfHyperlink`).

### 11.4 Tests (`tests/test_page.py`)

`PdfTableOfContents` has no test coverage at all today, so this PR adds the first:

1. **Backwards compatibility** — a `PdfTableOfContents` built and round-tripped
   (`export_to_dict()` / `model_validate`) without a destination is identical to the
   pre-change output; `exclude_none=True` keeps `destination` out of the payload.
2. **`iterate()` order and levels** — a three-deep tree yields pre-order document order with
   levels `0,1,2,…`; the root itself is not yielded; an empty tree yields nothing.
3. **`iterate()` on a deep chain** — 5000 nested single-child nodes complete without
   `RecursionError`. This is the test that pins the explicit stack.
4. **Origin conversion** — `to_top_left_origin()` flips `y` to `page_size.height - y`, sets
   `coord_origin`, leaves `x`, `kind`, `page_no` and `page_size` untouched, and is a no-op when
   already top-left; `to_top_left_origin().to_bottom_left_origin()` round-trips; a destination
   with `point=None` converts to `point=None` rather than raising.
5. **Validation** — `page_no` rejects `0` and negatives (it is `PageNumber`, `ge=1`);
   an unknown `kind` string fails validation, while `PdfDestinationKind.UNKNOWN` is accepted.

### 11.5 Release coupling

`docling-core` is versioned from tags (`version_source = "tag_only"`, currently `2.94.1`), so
PR A must be **released** before PR B's `pyproject.toml` floor bump (step 8) can point at it.
Until then PR B is developed against the local `_references/docling-core` checkout.

Everything in PR A is additive: new enum, new model, one new optional field, one new method,
two new exports. No existing field changes type, name or default, so no `docling-core` consumer
needs to do anything.

---

## 12. Build & validation commands (for the user to run)

```bash
# --- PR A: docling-core -------------------------------------------------
# merged and released as v2.95.0; nothing left to run here

# --- PR B: docling-parse ------------------------------------------------
# re-resolve after the docling-core floor bump, then rebuild in-place
uv lock
uv sync --reinstall-package docling-parse

# targeted tests
uv run pytest tests/test_regression_parse.py -k "table_of_contents or outline" -q

# full regression
uv run pytest tests/ -q
```

(Adjust to whatever your usual build invocation is — noted here only so the plan is
self-contained; I will not run builds or tests myself.)
