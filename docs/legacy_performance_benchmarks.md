# Legacy Performance Benchmarks (V1 vs V2)

This document contains historical performance benchmarks comparing the V1 and V2 parser versions. The V1 parser has been removed as of version 5.0.0.

## Characteristics of different parser versions

<table>
  <tr>
    <th>Version</th>
    <th>Original</th>
    <th>Word-level</th>
    <th>Snippet-level</th>
    <th>Performance</th>
  </tr>
  <tr>
    <th>V1</th>
    <td rowspan="2"><img src="./example_visualisations/2305.14962v1.pdf_page=0.png" alt="screenshot" width="100"/></td>
    <td>Not Supported</td>
    <td><img src="./example_visualisations/2305.14962v1.pdf_page=0.v1.png" alt="v1 snippet" width="100"/></td>
    <td>~0.250 sec/page </td>
  </tr>
  <tr>
    <th>V2</th>
    <!-- The "Original" column image spans from the previous row -->
    <td><img src="./example_visualisations/2305.14962v1.pdf_page=0.v2.original.png" alt="v1 word" width="100"/></td>
    <td><img src="./example_visualisations/2305.14962v1.pdf_page=0.v2.sanitized.png" alt="v2 snippet" width="100"/></td>
    <td>~0.050 sec/page <br><br>[~5-10X faster than v1]</td>
  </tr>
</table>

## Timings of different parser versions

We ran the v1 and v2 parser on [DocLayNet](https://huggingface.co/datasets/docling-project/DocLayNet-v1.1). We found the following overall behavior

![parser-performance](./dln-v1.png)
