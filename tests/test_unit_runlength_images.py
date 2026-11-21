#!/usr/bin/env python
"""Images stored with /RunLengthDecode (ISO 32000-1, 7.4.5).

QPDF undoes /RunLengthDecode only from its `qpdf_dl_specialized` decode level
up. At the default level it reported the stream unfilterable, so an image
XObject or inline image using it had no samples and the renderer drew its
yellow placeholder instead (#460).
"""

from __future__ import annotations

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object

WIDTH = HEIGHT = 16


def run_length_encode(data: bytes) -> bytes:
    """Runs of 2..128 equal bytes as (257 - n, byte), other bytes as literal
    blocks of 1..128 bytes as (n - 1, bytes), then the EOD marker 128."""
    out = bytearray()
    literal = bytearray()

    def flush_literal() -> None:
        while literal:
            block = literal[:128]
            out.append(len(block) - 1)
            out.extend(block)
            del literal[:128]

    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i + run] == data[i]:
            run += 1
        if run > 1:
            flush_literal()
            out.extend((257 - run, data[i]))
        else:
            literal.append(data[i])
        i += run
    flush_literal()
    out.append(128)
    return bytes(out)


def samples() -> bytes:
    """A gray raster with long runs (white margin, black block) and rows of
    distinct values, so both run and literal blocks are exercised."""
    rows = []
    for y in range(HEIGHT):
        if y in (2, 3):
            rows.append(bytes(range(y * 16, y * 16 + WIDTH)))
        elif 4 <= y < 12:
            rows.append(b"\xff" * 4 + b"\x00" * 8 + b"\xff" * 4)
        else:
            rows.append(b"\xff" * WIDTH)
    return b"".join(rows)


def encoded(filter_chain: str, data: bytes) -> tuple[str, bytes]:
    """The /Filter value (full names) and payload for `filter_chain`."""
    payload = run_length_encode(data)
    if filter_chain == "runlength":
        return "/RunLengthDecode", payload
    return "[/ASCIIHexDecode /RunLengthDecode]", payload.hex().encode("ascii") + b">"


INLINE_NAMES = {
    "/RunLengthDecode": "/RL",
    "[/ASCIIHexDecode /RunLengthDecode]": "[/AHx /RL]",
}


@pytest.mark.parametrize("filter_chain", ["runlength", "hex-runlength"])
def test_runlength_image_xobject_is_decoded(filter_chain: str):
    data = samples()
    filters, payload = encoded(filter_chain, data)
    image = stream_object(
        f"/Type /XObject /Subtype /Image /Width {WIDTH} /Height {HEIGHT} "
        f"/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter {filters}",
        payload,
    )
    pdf = simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )

    artifacts = render_page(pdf)._export_bitmap_artifacts()

    assert len(artifacts) == 1
    assert bytes(artifacts[0]["raw_data"]) == data


@pytest.mark.parametrize("filter_chain", ["runlength", "hex-runlength"])
def test_runlength_inline_image_is_decoded(filter_chain: str):
    data = samples()
    filters, payload = encoded(filter_chain, data)
    content = (
        (
            f"q 100 0 0 100 50 50 cm BI /W {WIDTH} /H {HEIGHT} /BPC 8 /CS /G "
            f"/F {INLINE_NAMES[filters]} ID\n"
        ).encode("ascii")
        + payload
        + b"\nEI Q\n"
    )

    artifacts = render_page(
        simple_page_pdf(content.decode("latin-1"))
    )._export_bitmap_artifacts()

    assert len(artifacts) == 1
    assert bytes(artifacts[0]["raw_data"]) == data
