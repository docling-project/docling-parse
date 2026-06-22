# Docling Parse

[![PyPI version](https://img.shields.io/pypi/v/docling-parse)](https://pypi.org/project/docling-parse/)
[![PyPI - Python Version](https://img.shields.io/pypi/pyversions/docling-parse)](https://pypi.org/project/docling-parse/)
[![uv](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/astral-sh/uv/main/assets/badge/v0.json)](https://github.com/astral-sh/uv)
[![Pybind11](https://img.shields.io/badge/build-pybind11-blue)](https://github.com/pybind/pybind11/)
[![Platforms](https://img.shields.io/badge/platform-macos%20|%20linux%20|%20windows-blue)](https://github.com/docling-project/docling-parse/)
[![License MIT](https://img.shields.io/github/license/docling-project/docling-parse)](https://opensource.org/licenses/MIT)

Simple package to extract text, paths and bitmap images with coordinates from programmatic PDFs. This package is used in the [Docling](https://github.com/docling-project/docling) PDF conversion. Below, we show a few output of the latest parser with char, word and line level output for text, in addition to the extracted paths and bitmap resources.

To do the visualizations yourself, simply run (change `word` into `char` or `line`),

```sh
uv run python ./docling_parse/visualize.py -i <path-to-pdf-file> -c word --interactive
```

<table>
  <tr>
    <th>original</th>
    <th>char</th>
    <th>word</th>
    <th>line</th>
  </tr>
  <tr>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_1.orig.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_1.char.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_1.word.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_1.line.png" alt="screenshot" width="170"/></td>
  </tr>
  <tr>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_3.orig.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_3.char.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_3.word.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_3.line.png" alt="screenshot" width="170"/></td>
  </tr>
  <tr>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_4.orig.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_4.char.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_4.word.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/ligatures_01.pdf.page_4.line.png" alt="screenshot" width="170"/></td>
  </tr>
  <tr>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_1.orig.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_1.char.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_1.word.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_1.line.png" alt="screenshot" width="170"/></td>
  </tr>
  <tr>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_4.orig.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_4.char.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_4.word.png" alt="screenshot" width="170"/></td>
    <td><img src="./docs/visualisations/table_of_contents_01.pdf.page_4.line.png" alt="screenshot" width="170"/></td>
  </tr>  
</table>

## Quick start

Install the package from PyPI:

```sh
pip install docling-parse
```

### Sequential parsing

`docling-parse` v7 split page parsing into two public configs:

- `DecodeConfig`: how to compute pages. This is fixed when a document is opened.
- `ContentConfig`: what to keep or materialize per page. This can be overridden per page.

```python
from docling_core.types.doc.page import TextCellUnit
from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingPdfParser,
)

parser = DoclingPdfParser(loglevel="fatal")

pdf_doc = parser.load(
    path_or_stream="<path-to-pdf>",
    decode_config=DecodeConfig(
        do_sanitization=True,
        keep_glyphs=False,
    ),
    content_config=ContentConfig(
        char_cells_content_level=ContentLevel.SKIP,
        word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        shapes_content_level=ContentLevel.SKIP,
        bitmaps_content_level=ContentLevel.SKIP,
    ),
)

for page_no, page in pdf_doc.iterate_pages():
    print(page_no, len(page.word_cells), len(page.textline_cells))

    for word in page.iterate_cells(unit_type=TextCellUnit.WORD):
        print(word.rect, word.text)

    image = page.render_as_image(cell_unit=TextCellUnit.WORD)
    image.show()
```

If you open cheaply and later need richer output, request it per page. When the
new `content_config` needs entities that were previously skipped, that page is
re-decoded automatically:

```python
from docling_parse.pdf_parser import ContentConfig, ContentLevel

page = pdf_doc.get_page(
    1,
    content_config=ContentConfig(
        word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
    ),
)
```

### v6 -> v7 migration

The main API break in v7 is that the old public `DecodePageConfig` selection
flags were split into two concerns:

- `DecodeConfig`: compute-time tuning only
- `ContentConfig`: what to skip, compute, or materialize per page

In practice:

- open-time `decode_config` replaces the old per-page decode tuning
- per-page content selection now lives in `content_config`
- `materialize_bitmap_bytes` became `include_bitmap_bytes`
- threaded `page_materialization_config` became `page_content_config`

Typical migration examples:

- old `DecodePageConfig.keep_char_cells=True` -> `ContentConfig(char_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE)`
- old `DecodePageConfig.create_word_cells=True` without surfacing them everywhere -> `ContentConfig(word_cells_content_level=ContentLevel.COMPUTE)`
- old `materialize_bitmap_bytes=False` -> `ContentConfig(include_bitmap_bytes=False)`

One semantic change matters: `decode_config` is now fixed when the document or
threaded batch is opened. If you want richer page output later, override
`content_config` on `get_page(...)` instead. On the sequential path this may
re-decode that page; on the threaded path you can only materialize entities the
batch already computed.

### Parallel parsing (multi-threaded)

Parse one or more PDFs in parallel with backpressure:

```python
from docling_parse.pdf_parser import (
    ContentConfig,
    ContentLevel,
    DecodeConfig,
    DoclingThreadedPdfParser,
    ThreadedPdfParserConfig,
)

parser = DoclingThreadedPdfParser(
    parser_config=ThreadedPdfParserConfig(
        loglevel="fatal",
        threads=4,
        max_concurrent_results=32,
        page_content_config=ContentConfig(
            word_cells_content_level=ContentLevel.COMPUTE,
            line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        ),
    ),
    decode_config=DecodeConfig(),
)

doc_key = parser.load("doc_a.pdf", page_numbers=[1, 3, 5])
print(doc_key, parser.page_count(doc_key), parser.scheduled_page_count(doc_key))

for result in parser.iterate_results():
    if not result.success:
        print(result.doc_key, result.page_number, result.error_message)
        continue

    # Batch decode kept word cells in C++, but did not materialize them by default.
    page = result.get_page(
        ContentConfig(
            word_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
            line_cells_content_level=ContentLevel.COMPUTE_AND_MATERIALIZE,
        )
    )
    print(
        result.doc_key,
        result.page_number,
        len(page.word_cells),
        result.timings.total_s,
    )
```

For threaded parse-and-render workloads, set
`ThreadedPdfParserConfig.render_config` and use `result.get_image()`,
`result.get_image(scale=...)`, or `result.get_image(canvas_size=...)`.

Use the CLI

```sh
$ docling-parse -h
usage: docling-parse [-h] -p PDF

Process a PDF file.

options:
  -h, --help         show this help message and exit
  -p PDF, --pdf PDF  Path to the PDF file
```

## Performance Benchmarks

Current perf tooling lives under [`perf/`](./perf/README.md):

- [`perf/run_perf.py`](./perf/run_perf.py): per-page CSV benchmarking across `docling`, `docling-threaded`, `pdfplumber`, `pypdfium2`, and `pymupdf`
- [`perf/run_scaling.py`](./perf/run_scaling.py): pages/sec and scaling sweeps for threaded parse and render workloads
- [`docs/performance_code.md`](./docs/performance_code.md): usage notes and interpretation

For historical V1 vs V2 benchmarks, see [legacy_performance_benchmarks.md](./docs/legacy_performance_benchmarks.md).

## Development

### CXX

To build the parser, simply run the following command in the root folder,

```sh
rm -rf build; cmake -B ./build; cd build; make
```

You can run the parser from your build folder:

```sh
% ./parse.exe -h
program to process PDF files or configuration files
Usage:
  PDFProcessor [OPTION...]

  -i, --input arg          Input PDF file
  -c, --config arg         Config file
      --create-config arg  Create config file
  -p, --page arg           Pages to process (default: -1 for all) (default:
                           -1)
      --password arg       Password for accessing encrypted, password-protected files
  -o, --output arg         Output file
  -l, --loglevel arg       loglevel [error;warning;success;info]
  -h, --help               Print usage
```

If you don't have an input file, a template input file will be printed on the terminal.


### Python

To build the package, simply run (make sure [uv](https://docs.astral.sh/uv/) is [installed](https://docs.astral.sh/uv/getting-started/installation)),

```sh
uv sync
```

The latter will only work after a clean `git clone`. If you are developing and updating C++ code, please use,

```sh
# uv pip install --force-reinstall --no-deps -e .
rm -rf .venv; uv venv; uv pip install --force-reinstall --no-deps -e ".[perf-tools]"
```

or 

```sh
BUILD_THREADS=12 uv pip install --force-reinstall --no-deps -e ".[perf]"
```

To test the package, run:

```sh
uv run pytest ./tests -v -s
```

## Contributing

Please read [Contributing to Docling Parse](https://github.com/docling-project/docling-parse/blob/main/CONTRIBUTING.md) for details.

## References

If you use Docling in your projects, please consider citing the following:

```bib
@techreport{Docling,
  author = {Docling Team},
  month = {8},
  title = {Docling Technical Report},
  url = {https://arxiv.org/abs/2408.09869},
  eprint = {2408.09869},
  doi = {10.48550/arXiv.2408.09869},
  version = {1.0.0},
  year = {2024}
}
```

## License

The Docling Parse codebase is under MIT license.
For individual model usage, please refer to the model licenses found in the original packages.

## LF AI & Data

Docling (and also docling-parse) is hosted as a project in the [LF AI & Data Foundation](https://lfaidata.foundation/projects/).

### IBM ❤️ Open Source AI

The project was started by the AI for knowledge team at IBM Research Zurich.
