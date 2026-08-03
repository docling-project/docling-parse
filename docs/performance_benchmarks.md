# Performance Benchmarks

## Overview of other open-source, low-level PDF parsers

At the time of writing (date: 3 Aug 2026), we have the following packages (ranked by download numbers),

| Rank | Python package   | Primary use                                                      | GitHub stars |          PyPI downloads/month | 
| ---: | ---------------- | ---------------------------------------------------------------- | -----------: | ----------------------------: |
|    1 | **pypdf**        | Read, merge, split, crop, encrypt and modify PDFs in pure Python |       ~10.1k | **122,033,329** ([GitHub][1]) |
|    2 | **PyMuPDF**      | High-performance rendering, extraction, search and modification  |       ~10.4k | **106,838,492** ([GitHub][2]) |
|    3 | **pypdfium2**    | Fast PDFium bindings for rendering, inspection and manipulation  |          803 |  **74,384,565** ([GitHub][3]) |
|    4 | **pdfminer.six** | Detailed text extraction, layout analysis and low-level parsing  |        ~7.0k |  **69,090,692** ([GitHub][4]) |
|    5 | **pdfplumber**   | Text and table extraction with coordinates and visual debugging  |       ~10.6k |  **57,151,282** ([GitHub][5]) |
|    6 | **WeasyPrint**   | Generate PDFs from HTML and CSS                                  |        ~9.5k |  **33,945,746** ([GitHub][6]) |
|    7 | **fpdf2**        | Lightweight programmatic PDF generation                          |        ~1.5k |  **18,548,247** ([GitHub][7]) |
|    8 | **pikepdf**      | Robust PDF repair and low-level manipulation using QPDF          |        ~2.8k |   **9,393,114** ([GitHub][8]) |
|    9 | **OCRmyPDF**     | Add searchable OCR layers to scanned PDFs and produce PDF/A      |       ~34.3k |   **1,107,206** ([GitHub][9]) |
|   10 | **camelot-py**   | Extract structured tables into pandas DataFrames                 |        ~3.8k |    **771,757** ([GitHub][10]) |

[1]: https://github.com/py-pdf/pypdf "GitHub - py-pdf/pypdf: A pure-python PDF library capable of splitting, merging, cropping, and transforming the pages of PDF files · GitHub"
[2]: https://github.com/pymupdf/PyMuPDF "GitHub - pymupdf/PyMuPDF: PyMuPDF is a high performance Python library for data extraction, analysis, conversion & manipulation of PDF (and other) documents. · GitHub"
[3]: https://github.com/pypdfium2-team/pypdfium2 "GitHub - pypdfium2-team/pypdfium2: Python bindings to PDFium, reasonably cross-platform. · GitHub"
[4]: https://github.com/pdfminer/pdfminer.six "GitHub - pdfminer/pdfminer.six: Community maintained fork of pdfminer - we fathom PDF · GitHub"
[5]: https://github.com/jsvine/pdfplumber "GitHub - jsvine/pdfplumber: Plumb a PDF for detailed information about each char, rectangle, line, et cetera — and easily extract text and tables. · GitHub"
[6]: https://github.com/Kozea/WeasyPrint "GitHub - Kozea/WeasyPrint: The awesome document factory · GitHub"
[7]: https://github.com/py-pdf/fpdf2 "GitHub - py-pdf/fpdf2: Simple PDF generation for Python · GitHub"
[8]: https://github.com/pikepdf/pikepdf "GitHub - pikepdf/pikepdf: A Python library for reading and writing PDF, powered by QPDF · GitHub"
[9]: https://github.com/ocrmypdf/OCRmyPDF "GitHub - ocrmypdf/OCRmyPDF: OCRmyPDF adds an OCR text layer to scanned PDF files, allowing them to be searched · GitHub"
[10]: https://github.com/camelot-dev/camelot "GitHub - camelot-dev/camelot: A Python library to extract tabular data from PDFs · GitHub"

## Performance comparison on parsing and rendering

Here we evaluate the top-5 python packages that provide the parsing (i.e. text-extraction from PDF with location) and page-rendering.

### What is measured

Two tasks are timed:

- **`parse`** — extract the text of every page *together with its location*. Every package therefore uses its position-bearing API, not a plain-text dump.
- **`parse+render`** — the same, plus rasterising the page at `--scale` (`scale=1.0` is 72 dpi) and materialising it as a PIL image.

| Python package | `parse` | `parse+render` |
| -------------- | ------- | -------------- |
| **docling-parse** | `DoclingThreadedPdfParser` | + `RenderConfig` |
| **PyMuPDF** | `page.get_text("rawdict")` | + `page.get_pixmap()` → `pil_image()` |
| **pypdfium2** | text-page rects + `get_text_bounded()` | + `page.render()` → `to_pil()` |
| **pdfplumber** | `page.chars` | + `page.to_image()` |
| **pdfminer.six** | `extract_pages()` + `LTChar` walk | not supported |
| **pypdf** | `extract_text(visitor_text=...)` | not supported |

Two notes on fairness. PyMuPDF is timed on `get_text("rawdict")` rather than the much faster `get_text("text")`, because the latter returns a bare string with no geometry and would not be the same task. pdfplumber has no rasteriser of its own — `to_image()` delegates to pypdfium2 — so its render row is pdfminer extraction plus a PDFium raster, which is how a pdfplumber user would in practice obtain both.

pdfminer.six and pypdf have no rendering path at all, so they appear only in the `parse` task.

**Threading.** docling-parse decodes pages in parallel, and to our knowledge none of the other packages expose a thread-safe multi-page pipeline. docling-parse is therefore measured once per thread count, and every other package is measured single-threaded and reported with `threads = 1`.

### How the numbers are defined

- **total time** is a wall clock around the whole corpus, including opening documents. This is the number that reflects the benefit of threading.
- **average / median / 95 / 99 quantile time per page** describe the cost of a *single* page. They are computed over pages, not over documents, and failed pages are excluded. Because they measure one page's cost, they stay roughly flat as the thread count rises while **total time** drops.
  - For the third-party packages this is a wall-clock timer around each page's work, with document open/close excluded.
  - For docling-parse it is the page timing reported by the C++ layer, since under concurrency no wall-clock interval belongs to a single page. At `threads = 1` the two definitions agree.

A render comparison is only meaningful if every package rasterised the same canvas, so the benchmark records the pixel width and height of each rendered page and reports how far each package deviates from a reference. PDFium is the ground truth — it is the rasteriser that three of the six packages ultimately rely on — and it is run first for that reason. The numbers below were accepted with all packages agreeing to within 2 px per page.

### Results

<!-- Rows come from the per-machine reports in docs/performance_benchmarks/ -->

| System hardware | dataset | Python package | Task | threads | total time (s) | average time/page | median time/page | 95 quantile time/page | 99 quantile time/page |
|    ---: |           ---: | ---: |       ---: |    ---: |              ---: |             ---: |                  ---: |                  ---: |

The speedup table below is reported separately, because it compares *wall time between runs* rather than per-page cost. Each cell is `column_total_time / row_total_time`, so a value above `1.00x` means the row finished the corpus faster than the column.

| Python package | Task | threads | total time (s) | pages/sec | vs docling-parse (1t) | vs pypdfium2 (1t) |
|           ---: | ---: |    ---: |           ---: |      ---: |                  ---: |              ---: |

### Reproducing these numbers

Install the benchmark dependencies (this pulls in every third-party package in the table):

```sh
uv sync --group perf
```

Then run the comparison suite. It downloads the dataset from Hugging Face on first use, runs the requested tasks, prints the tables, and writes a full report:

```sh
uv run python ./perf/run_scaling.py \
    --threads 1,4,8,12 \
    --compare all \
    --mode render \
    --output-dir ./docs/performance_benchmarks/
```

You only name a directory. The report and the per-page CSV are named `<cpu>_<dataset>_<mode>` — for example `apple_m3_max_performance-dataset-bo767_render.md` — so results from different machines never overwrite each other. Numbers are only comparable within a single row of the *System hardware* column, so the tables in this file are aggregated from those per-machine reports rather than produced in one run.

Each report is self-contained: it records the exact command it was produced by, the dataset name/revision/size, the machine and the version of every benchmarked package, the decode/content/render configs the run was driven with, and all result tables. That is what makes a number here traceable without the terminal scrollback it came from.

Useful variations:

- `--compare` on its own compares only docling-parse against pypdfium2, which is the cheapest meaningful run.
- `--max-pages 5000` caps the total page count for a quick check; the cap is applied in input order and the last document is truncated, so the subset is reproducible.
- `--mode both` runs `parse` and `parse+render` in one go.
- Omitting `--compare` gives the thread-scaling tables instead; that mode writes the per-page CSV only.
- The default `--output-dir` is `./scratch`, so an exploratory run does not touch the docs.

For a stable measurement, run on an otherwise idle machine, and note that the first run of a dataset pays for a cold file cache.

The companion CSV holds one row per `(backend, task, threads, document, page)`, including the rendered `image_width` and `image_height`. It is the input both for the render size check and for the per-page distribution plots requested below, via `perf/run_eval.py`.

### Overview of Regression and Performance datasets

**docling-project/regression-dataset-for-docling-parse**

- number of documents: ??
- number of pages: ??

TBD: we need a distribution of 
- number of pages per document
- time for parse per page
- time for parse and render per page

**docling-project/performance-dataset-bo767**

- number of documents: 767
- number of pages: ~50K

TBD: we need a distribution of 
- number of pages per document
- time for parse per page
- time for parse and render per page

The document and page counts are printed by any `run_scaling.py` invocation on the dataset. The three distributions can be built from the `--pages-csv` output described above, which carries the per-page time for each backend and task alongside the document key.

