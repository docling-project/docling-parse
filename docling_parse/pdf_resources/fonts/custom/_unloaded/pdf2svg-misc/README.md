# Upstream pdf2svg font mappings

This directory contains candidate font-specific Unicode mappings copied from
ContentMine/pdf2svg commit
`30f0bf8a0c39bfa65e30c2eb3ed4f96b9fdd744d`:

<https://github.com/ContentMine/pdf2svg/tree/30f0bf8a0c39bfa65e30c2eb3ed4f96b9fdd744d/src/main/resources/org/xmlcml/pdf2svg/codepoints/misc>

These XML files are intentionally **not loaded at runtime**. Some mappings are
sparse, duplicate mappings already supplied by the bundled AFM files, rely on
other upstream code-point sets, or are explicitly marked as uncertain. Promote
a mapping into an active, font-scoped resource only after verifying it against
a representative PDF and adding regression coverage.

Each XML file retains its upstream Apache License 2.0 notice.
