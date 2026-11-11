"""Exact sample values for single-component PDF /Decode mappings."""

from __future__ import annotations

import zlib

import pytest

from tests.pdf_builder import render_page, simple_page_pdf, stream_object


def _gray_image_pdf(samples: bytes, decode: str | None) -> bytes:
    decode_entry = f" /Decode {decode}" if decode is not None else ""
    image = stream_object(
        "/Type /XObject /Subtype /Image "
        f"/Width {len(samples)} /Height 1 "
        "/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode"
        f"{decode_entry}",
        zlib.compress(samples),
    )
    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


@pytest.mark.parametrize("sample_count", [1, 17, 255, 256, 257, 1024])
@pytest.mark.parametrize(
    "decode, expected_mapping",
    [
        pytest.param(None, bytes(range(256)), id="implicit-identity"),
        pytest.param("[0 1]", bytes(range(256)), id="identity"),
        pytest.param("[1 0]", bytes(reversed(range(256))), id="inverted"),
        pytest.param(
            "[0.25 0.75]",
            bytes(64 + value // 2 for value in range(256)),
            id="restricted-range",
        ),
        pytest.param("[0.5 0.5]", bytes([128]) * 256, id="constant-half"),
        pytest.param(
            "[-1 2]",
            bytes(max(0, min(255, 3 * value - 255)) for value in range(256)),
            id="clamped",
        ),
        pytest.param(
            "[1 0 0 1]",
            bytes(reversed(range(256))),
            id="unused-extra-pair",
        ),
    ],
)
def test_single_component_decode_samples(
    sample_count: int, decode: str | None, expected_mapping: bytes
) -> None:
    # Exercise the scalar/table boundary and every byte value in larger images.
    samples = bytes(index % 256 for index in range(sample_count))
    result = render_page(_gray_image_pdf(samples, decode))
    artifacts = result._export_bitmap_artifacts()
    assert len(artifacts) == 1
    assert bytes(artifacts[0]["raw_data"]) == bytes(
        expected_mapping[value] for value in samples
    )
