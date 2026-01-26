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

Install the package from Pypi

```sh
pip install docling-parse
```

Convert a PDF (look in the [visualize.py](docling_parse/visualize.py) for a more detailed information)

```python
from docling_core.types.doc.page import TextCellUnit
from docling_parse.pdf_parser import DoclingPdfParser, PdfDocument

parser = DoclingPdfParser()

pdf_doc: PdfDocument = parser.load(
    path_or_stream="<path-to-pdf>"
)

# PdfDocument.iterate_pages() will automatically populate pages as they are yielded.
for page_no, pred_page in pdf_doc.iterate_pages():

    # iterate over the word-cells
    for word in pred_page.iterate_cells(unit_type=TextCellUnit.WORD):
        print(word.rect, ": ", word.text)

        # create a PIL image with the char cells
    img = pred_page.render_as_image(cell_unit=TextCellUnit.CHAR)
    img.show()
```

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

*Coming soon - benchmarks will be updated for the current parser version.*

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
