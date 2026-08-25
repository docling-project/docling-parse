#!/usr/bin/env python
"""What a bitmap artifact is compared on, and why a JPEG 2000 one is compared
on less.

Every bitmap the renderer paints is recorded in the groundtruth by a hash of
its decoded samples, which says that two runs decoded the same image down to
the byte. That holds for every codec in the decode path but one: JPEG 2000's
irreversible 9/7 wavelet is a floating-point transform, and OpenJPEG runs it in
single precision, which rounds to the machine. The same codestream then decodes
to samples that differ by one level here and there -- far too little to survive
the JPEG the exporter re-encodes them into, but enough to change the hash, which
made a macOS-written groundtruth fail on Linux (`17068186561688296387-1.pdf`,
page 1).

So a bitmap that came out of a floating-point codec is compared on the coarse
shape of its samples instead, and this holds both halves of that: that the
artifact says which filters decoded it, and that the coarse comparison absorbs
one-level noise while still catching a decode that actually changed.
"""

from __future__ import annotations

import io
from pathlib import Path

import pytest
from PIL import Image

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import (
    compare_raw_profile,
    has_float_decoded_samples,
    page_bitmap_artifacts,
    page_bitmap_groundtruth,
)

DOC_NAME = "unit_bitmap_artifacts.pdf"
PAGE_NO = 1
SIZE = 64

# Only ever quoted in a failure message; these comparisons take the path so a
# failure names the file the entries came out of.
BITMAPS_PATH = Path(f"{DOC_NAME}.page_no_{PAGE_NO}.bitmaps.json")


def gradient_image() -> Image.Image:
    """An image with structure in it, so block means are not all the same."""
    image = Image.new("RGB", (SIZE, SIZE))
    image.putdata(
        [
            (x * 4 % 256, y * 4 % 256, (x + y) * 2 % 256)
            for y in range(SIZE)
            for x in range(SIZE)
        ]
    )
    return image


def jpeg_2000_payload() -> bytes:
    """The gradient as a JPEG 2000 codestream, on the irreversible wavelet."""
    buffer = io.BytesIO()
    gradient_image().save(buffer, format="JPEG2000", irreversible=True)
    return buffer.getvalue()


def jpeg_payload() -> bytes:
    buffer = io.BytesIO()
    gradient_image().save(buffer, format="JPEG", quality=95)
    return buffer.getvalue()


def image_page_pdf(payload: bytes, image_filter: str) -> bytes:
    """One image over the middle of a 200x200 page."""
    image = stream_object(
        f"/Type /XObject /Subtype /Image /Width {SIZE} /Height {SIZE} "
        f"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter {image_filter}",
        payload,
    )

    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


def painted_bitmap(payload: bytes, image_filter: str) -> dict:
    """The single bitmap artifact that page painted."""
    result = render_page(image_page_pdf(payload, image_filter))
    bitmaps, _ = page_bitmap_artifacts(result)
    assert len(bitmaps) == 1, f"expected one bitmap, got {len(bitmaps)}"
    return bitmaps[0]


def groundtruth_entry(artifact: dict) -> dict:
    """The stored description of one bitmap."""
    entry = page_bitmap_groundtruth(DOC_NAME, PAGE_NO, [artifact], 0)["bitmaps"][0]
    return entry


def with_samples(artifact: dict, raw: bytes) -> dict:
    return {**artifact, "raw_data": raw}


def one_level_noise(raw: bytes) -> bytes:
    """What the platform divergence looks like: a sample off by one."""
    samples = bytearray(raw)
    for index in range(0, len(samples), 7):
        samples[index] = min(255, samples[index] + 1)
    return bytes(samples)


def inverted(raw: bytes) -> bytes:
    """What a decode regression looks like: the samples read the other way."""
    return bytes(255 - sample for sample in raw)


def test_jpeg_2000_bitmap_is_marked_as_float_decoded():
    artifact = painted_bitmap(jpeg_2000_payload(), "/JPXDecode")

    assert artifact["filters"] == ["/JPXDecode"]
    assert has_float_decoded_samples(artifact)

    # and the coarse comparison it falls back on is actually there
    entry = groundtruth_entry(artifact)
    assert entry["raw_profile"], "a float-decoded bitmap needs a sample profile"
    assert entry["raw_profile"]["grid"] == [8, 8, 3]


def test_jpeg_bitmap_stays_byte_compared():
    """libjpeg's islow IDCT is integer, so nothing excuses a DCT image."""
    artifact = painted_bitmap(jpeg_payload(), "/DCTDecode")

    assert artifact["filters"] == ["/DCTDecode"]
    assert not has_float_decoded_samples(artifact)
    assert "raw_profile" not in groundtruth_entry(artifact)


def test_profile_absorbs_one_level_noise():
    artifact = painted_bitmap(jpeg_2000_payload(), "/JPXDecode")
    expected = groundtruth_entry(artifact)
    actual = groundtruth_entry(
        with_samples(artifact, one_level_noise(bytes(artifact["raw_data"])))
    )

    assert actual["raw_sha256"] != expected["raw_sha256"], (
        "the fixture has to change the samples, or it proves nothing"
    )
    compare_raw_profile(0, expected, actual, BITMAPS_PATH)


def test_profile_catches_a_changed_decode():
    artifact = painted_bitmap(jpeg_2000_payload(), "/JPXDecode")
    expected = groundtruth_entry(artifact)
    actual = groundtruth_entry(
        with_samples(artifact, inverted(bytes(artifact["raw_data"])))
    )

    with pytest.raises(AssertionError, match="sample profile mismatch"):
        compare_raw_profile(0, expected, actual, BITMAPS_PATH)
