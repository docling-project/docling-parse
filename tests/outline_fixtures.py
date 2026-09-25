"""Hand-written PDFs that exercise outline destination resolution.

The repository ships no PDF-authoring dependency, and `tests/data` is a pinned
Hugging Face snapshot rather than repository content, so these fixtures are
assembled here from raw objects and handed to the parser as bytes. Every construct
comes from ISO 32000-1: destinations from 12.3.2, the outline hierarchy from 12.3.3.

The builders return `bytes`; `tests.test_regression_parse` wraps them in a
`BytesIO`, which `DoclingPdfParser.load()` accepts directly.
"""

from __future__ import annotations

from typing import List

PAGE_WIDTH = 612
PAGE_HEIGHT = 792

# Where the heading is drawn on every page, in unrotated default user space. The
# bookmarks point at this exact spot, so a decoded cell and its bookmark must land
# on top of each other whatever the page rotation.
HEADING_X = 108
HEADING_BASELINE = 690
HEADING_SIZE = 12


class PdfBuilder:
    """Collects indirect objects and serialises them with a valid xref table."""

    def __init__(self) -> None:
        self._objects: List[str | bytes | None] = [None]  # 1-based

    def reserve(self) -> int:
        """Reserve an object number to be filled in later."""
        self._objects.append(None)
        return len(self._objects) - 1

    def put(self, number: int, body: str | bytes) -> int:
        self._objects[number] = body
        return number

    def add(self, body: str | bytes) -> int:
        return self.put(self.reserve(), body)

    def add_stream(self, content: str) -> int:
        data = content.encode("ascii")
        return self.add(
            f"<< /Length {len(data)} >>\nstream\n".encode("ascii")
            + data
            + b"\nendstream"
        )

    def build(self) -> bytes:
        out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        offsets = [0] * len(self._objects)

        for number, body in enumerate(self._objects):
            if number == 0:
                continue
            if body is None:
                raise ValueError(f"object {number} was reserved but never filled in")
            offsets[number] = len(out)
            payload = body.encode("ascii") if isinstance(body, str) else body
            out += f"{number} 0 obj\n".encode("ascii") + payload + b"\nendobj\n"

        xref_offset = len(out)
        out += f"xref\n0 {len(self._objects)}\n".encode("ascii")
        out += b"0000000000 65535 f \n"
        for number in range(1, len(self._objects)):
            out += f"{offsets[number]:010d} 00000 n \n".encode("ascii")

        out += (
            f"trailer\n<< /Size {len(self._objects)} /Root 1 0 R >>\n"
            f"startxref\n{xref_offset}\n%%EOF\n"
        ).encode("ascii")

        return bytes(out)


def _heading_stream(text: str) -> str:
    return f"BT /F1 {HEADING_SIZE} Tf {HEADING_X} {HEADING_BASELINE} Td ({text}) Tj ET"


def _add_pages(
    pdf: PdfBuilder,
    *,
    titles: List[str],
    rotations: List[int] | None = None,
) -> tuple[int, List[int]]:
    """Add a page tree whose pages each carry one heading.

    Returns:
        A (pages_ref, page_refs) tuple.
    """
    pages_ref = pdf.reserve()
    font_ref = pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")

    page_refs = []
    for index, title in enumerate(titles):
        contents_ref = pdf.add_stream(_heading_stream(title))
        rotate = (rotations or [0] * len(titles))[index]
        page_refs.append(
            pdf.add(
                f"<< /Type /Page /Parent {pages_ref} 0 R "
                f"/MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] /Rotate {rotate} "
                f"/Resources << /Font << /F1 {font_ref} 0 R >> >> "
                f"/Contents {contents_ref} 0 R >>"
            )
        )

    kids = " ".join(f"{ref} 0 R" for ref in page_refs)
    pdf.put(
        pages_ref,
        f"<< /Type /Pages /Kids [{kids}] /Count {len(page_refs)} >>",
    )

    return pages_ref, page_refs


def _add_outline_items(
    pdf: PdfBuilder, outlines_ref: int, entries: List[str]
) -> List[int]:
    """Add a flat chain of outline items, each given as its already-formatted body."""
    refs = [pdf.reserve() for _ in entries]

    for index, (ref, body) in enumerate(zip(refs, entries)):
        links = f" /Parent {outlines_ref} 0 R"
        if index > 0:
            links += f" /Prev {refs[index - 1]} 0 R"
        if index + 1 < len(refs):
            links += f" /Next {refs[index + 1]} 0 R"
        pdf.put(ref, f"<< {body}{links} >>")

    return refs


def _finish_catalog(
    pdf: PdfBuilder, pages_ref: int, outlines_ref: int, extra: str = ""
) -> None:
    pdf.put(
        1,
        f"<< /Type /Catalog /Pages {pages_ref} 0 R "
        f"/Outlines {outlines_ref} 0 R{extra} >>",
    )


def build_destination_kinds() -> bytes:
    """One bookmark per explicit-destination syntax of ISO 32000-1, table 151."""
    pdf = PdfBuilder()
    pdf.reserve()  # catalog is object 1

    pages_ref, page_refs = _add_pages(pdf, titles=["Alpha", "Beta", "Gamma"])
    outlines_ref = pdf.reserve()

    page = page_refs[0]
    entries = [
        f"/Title (XYZ) /Dest [{page} 0 R /XYZ 108 702 0]",
        f"/Title (XYZ null top) /Dest [{page} 0 R /XYZ 108 null 0]",
        f"/Title (XYZ all null) /Dest [{page} 0 R /XYZ null null null]",
        f"/Title (Fit) /Dest [{page} 0 R /Fit]",
        f"/Title (FitH) /Dest [{page} 0 R /FitH 702]",
        f"/Title (FitV) /Dest [{page} 0 R /FitV 108]",
        f"/Title (FitR) /Dest [{page} 0 R /FitR 108 90 504 702]",
        f"/Title (FitB) /Dest [{page} 0 R /FitB]",
        f"/Title (FitBH) /Dest [{page} 0 R /FitBH 702]",
        f"/Title (FitBV) /Dest [{page} 0 R /FitBV 108]",
        f"/Title (no kind) /Dest [{page} 0 R]",
        "/Title (no destination)",
    ]
    item_refs = _add_outline_items(pdf, outlines_ref, entries)

    pdf.put(
        outlines_ref,
        f"<< /Type /Outlines /First {item_refs[0]} 0 R "
        f"/Last {item_refs[-1]} 0 R /Count {len(item_refs)} >>",
    )
    _finish_catalog(pdf, pages_ref, outlines_ref)

    return pdf.build()


def build_destination_references() -> bytes:
    """The ways an outline item can reference a destination (12.3.2.3, 12.6.4.2)."""
    pdf = PdfBuilder()
    pdf.reserve()  # catalog is object 1

    pages_ref, page_refs = _add_pages(pdf, titles=["Alpha", "Beta", "Gamma"])
    target = page_refs[1]
    dest_array = f"[{target} 0 R /XYZ 108 702 0]"

    # a name-tree destination, reached by a byte string
    name_tree_ref = pdf.add(f"<< /Names [(beta-string) {dest_array}] >>")
    names_ref = pdf.add(f"<< /Dests {name_tree_ref} 0 R >>")

    # a legacy catalog /Dests destination, reached by a name object, written as a
    # destination dictionary rather than a bare array
    dests_ref = pdf.add(f"<< /BetaName << /D {dest_array} >> >>")

    goto_ref = pdf.add(f"<< /S /GoTo /D {dest_array} >>")
    goto_named_ref = pdf.add("<< /S /GoTo /D (beta-string) >>")
    remote_ref = pdf.add(f"<< /S /GoToR /F (other.pdf) /D {dest_array} >>")

    outlines_ref = pdf.reserve()
    entries = [
        f"/Title (explicit) /Dest {dest_array}",
        "/Title (named string) /Dest (beta-string)",
        "/Title (named object) /Dest /BetaName",
        f"/Title (goto action) /A {goto_ref} 0 R",
        f"/Title (goto action named) /A {goto_named_ref} 0 R",
        f"/Title (remote action) /A {remote_ref} 0 R",
    ]
    item_refs = _add_outline_items(pdf, outlines_ref, entries)

    pdf.put(
        outlines_ref,
        f"<< /Type /Outlines /First {item_refs[0]} 0 R "
        f"/Last {item_refs[-1]} 0 R /Count {len(item_refs)} >>",
    )
    _finish_catalog(
        pdf,
        pages_ref,
        outlines_ref,
        extra=f" /Names {names_ref} 0 R /Dests {dests_ref} 0 R",
    )

    return pdf.build()


def build_rotated_pages() -> bytes:
    """One page per /Rotate value, each with a bookmark on its own heading."""
    pdf = PdfBuilder()
    pdf.reserve()  # catalog is object 1

    titles = ["Rotate0", "Rotate90", "Rotate180", "Rotate270"]
    pages_ref, page_refs = _add_pages(pdf, titles=titles, rotations=[0, 90, 180, 270])

    outlines_ref = pdf.reserve()
    entries = [
        f"/Title ({title}) /Dest [{ref} 0 R /XYZ {HEADING_X} "
        f"{HEADING_BASELINE + HEADING_SIZE} 0]"
        for title, ref in zip(titles, page_refs)
    ]
    item_refs = _add_outline_items(pdf, outlines_ref, entries)

    pdf.put(
        outlines_ref,
        f"<< /Type /Outlines /First {item_refs[0]} 0 R "
        f"/Last {item_refs[-1]} 0 R /Count {len(item_refs)} >>",
    )
    _finish_catalog(pdf, pages_ref, outlines_ref)

    return pdf.build()


def build_malformed_outline() -> bytes:
    """Cycles, a missing title and a destination outside the page tree."""
    pdf = PdfBuilder()
    pdf.reserve()  # catalog is object 1

    pages_ref, page_refs = _add_pages(pdf, titles=["Alpha", "Beta"])
    outlines_ref = pdf.reserve()

    # a page object that is deliberately not part of the page tree
    orphan_ref = pdf.add(
        f"<< /Type /Page /MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] >>"
    )

    first_ref = pdf.reserve()
    second_ref = pdf.reserve()
    third_ref = pdf.reserve()
    child_ref = pdf.reserve()

    # /Next points back at the head of the chain
    pdf.put(
        first_ref,
        f"<< /Title (looping next) /Parent {outlines_ref} 0 R "
        f"/Dest [{page_refs[0]} 0 R /XYZ 108 702 0] "
        f"/First {child_ref} 0 R /Last {child_ref} 0 R /Count 1 "
        f"/Next {second_ref} 0 R >>",
    )
    # a child whose /First points back at its own parent
    pdf.put(
        child_ref,
        f"<< /Title (looping child) /Parent {first_ref} 0 R "
        f"/First {first_ref} 0 R /Last {first_ref} 0 R /Count 1 >>",
    )
    # no /Title at all
    pdf.put(
        second_ref,
        f"<< /Parent {outlines_ref} 0 R /Prev {first_ref} 0 R "
        f"/Dest [{page_refs[1]} 0 R /FitH 702] /Next {third_ref} 0 R >>",
    )
    # a destination page that is not reachable from the page tree
    pdf.put(
        third_ref,
        f"<< /Title (dangling page) /Parent {outlines_ref} 0 R "
        f"/Prev {second_ref} 0 R /Dest [{orphan_ref} 0 R /XYZ 108 702 0] "
        f"/Next {first_ref} 0 R >>",
    )

    pdf.put(
        outlines_ref,
        f"<< /Type /Outlines /First {first_ref} 0 R /Last {third_ref} 0 R /Count 3 >>",
    )
    _finish_catalog(pdf, pages_ref, outlines_ref)

    return pdf.build()
