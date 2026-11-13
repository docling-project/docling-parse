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


@pytest.mark.parametrize("height", [1, 2])
@pytest.mark.parametrize("filter_chain", ["single", "array", "hex-flate"])
def test_inline_rgb_png_predictors_restore_samples(height: int, filter_chain: str):
    # These tiny predictor-encoded RGB strips make up the gradient bullets in
    # 7413869934284023443-1.pdf. Ignoring /DP leaves deltas as pixel samples.
    rows = [
        bytes([0, 0, 255, 32, 48, 255, 96, 112, 255, 192, 224, 255]),
        bytes([8, 16, 240, 40, 64, 240, 104, 128, 240, 200, 240, 240]),
    ][:height]
    # PNG Sub on the first row, Up on the second; /Predictor 15 uses the
    # leading per-row filter byte to choose the reconstruction operation.
    predicted = bytes([1]) + bytes(
        (value - (rows[0][i - 3] if i >= 3 else 0)) % 256
        for i, value in enumerate(rows[0])
    )
    if height == 2:
        predicted += bytes([2]) + bytes(
            (value - rows[0][i]) % 256 for i, value in enumerate(rows[1])
        )
    payload = zlib.compress(predicted)
    parms = "<< /Predictor 15 /Columns 4 /Colors 3 /BitsPerComponent 8 >>"
    if filter_chain == "single":
        filters = f"/F /Fl /DP {parms}"
    elif filter_chain == "array":
        filters = f"/F [/Fl] /DP [{parms}]"
    else:
        filters = f"/F [/AHx /Fl] /DP [null {parms}]"
        payload = payload.hex().encode("ascii") + b">"
    content = (
        (
            f"q 100 0 0 100 50 50 cm BI /W 4 /H {height} /BPC 8 /CS /RGB {filters} ID\n"
        ).encode("ascii")
        + payload
        + b"\nEI Q\n"
    )
    result = render_page(simple_page_pdf(content.decode("latin-1")))
    artifacts = result._export_bitmap_artifacts()
    assert len(artifacts) == 1
    assert bytes(artifacts[0]["raw_data"]) == b"".join(rows)
