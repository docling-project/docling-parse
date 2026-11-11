#!/usr/bin/env python
"""Image sample data for the unit fixtures.

The codestreams come from a real encoder (Pillow's) rather than from a test's
own understanding of one, so a renderer that disagrees with them disagrees with
the encoder, not with this file.
"""

from __future__ import annotations

import io
from collections.abc import Sequence

from PIL import Image, TiffImagePlugin


def middle_half(extent: int) -> range:
    return range(extent // 4, 3 * extent // 4)


def g4_codes(
    columns: int, rows: int, ink_columns: Sequence[range], ink_rows: range
) -> bytes:
    """Group 4 codes for bands of ink, `columns` pixels to the row.

    Pillow writes a mode-"1" image with photometric=BlackIsZero, which makes
    its *0* pixels the codestream's white runs. A PDF reads the opposite way
    round under the default /BlackIs1 false, where a decoded 0 bit is black, so
    the image handed to Pillow is the inverse of the samples wanted back.
    """
    image = Image.new("1", (columns, rows), 0)
    for y in ink_rows:
        for band in ink_columns:
            for x in band:
                image.putpixel((x, y), 1)

    buffer = io.BytesIO()
    image.save(buffer, format="TIFF", compression="group4")

    tiff = Image.open(io.BytesIO(buffer.getvalue()))
    # Only a TIFF carries the strip tags this reads back out.
    assert isinstance(tiff, TiffImagePlugin.TiffImageFile)

    offsets, counts = tiff.tag_v2[273], tiff.tag_v2[279]
    assert len(offsets) == 1, "the fixture wants one strip, i.e. one codestream"

    return buffer.getvalue()[offsets[0] : offsets[0] + counts[0]]
