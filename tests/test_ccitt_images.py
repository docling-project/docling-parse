#!/usr/bin/env python
"""CCITT fax images: the filter chain around them and their /DecodeParms.

Three things have to be right before a /CCITTFaxDecode image can be drawn.

A stream's /Filter is a chain applied in order (ISO 32000-1, 7.4) with the
image codec last, so `/Filter [/ASCII85Decode /CCITTFaxDecode]` means the CCITT
codes were wrapped in ASCII85 afterwards. QPDF will not decode CCITT and
declares the *whole* chain unfilterable, which left the parser holding the raw
stream -- ASCII85 text -- and handing that to the CCITT decoder, which duly
decoded noise for eighteen rows and gave up. The page came out blank.

The codes describe rows of /Columns pixels (Table 11). /Columns defaults to
1728 and is not required to equal /Width; decoding at the image's width instead
runs off the end of the first row that differs and loses synchronisation.

/EncodedByteAlign pads every row out to a byte boundary. Reading straight on
takes that padding for code bits, so the second row is already wrong.
"""

from __future__ import annotations

import io
import zlib
from base64 import a85encode
from collections.abc import Sequence

import pytest
from PIL import Image, TiffImagePlugin

from tests.pdf_builder import render_page, simple_page_pdf, stream_object
from tests.rendering_regression import assert_color_near, center_color, region_image

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

# Every fixture below draws its image over the middle 100x100 points of a
# 200x200 page and puts ink over the middle half of the image in both
# directions, whatever its pixel dimensions, so the ink always lands on the
# middle 50x50 points of the page. Boxes are (left, top, right, bottom) in
# points from the top-left, as the renderer's crop wants them.
INK = (90.0, 90.0, 110.0, 110.0)
MARGIN = (55.0, 55.0, 70.0, 70.0)
# Still inside the image, below the ink: this is where a row that decoded out
# of step with the codes shows up, as ink running on to the image's bottom.
BELOW_INK = (90.0, 130.0, 110.0, 145.0)


def middle_half(extent: int) -> range:
    return range(extent // 4, 3 * extent // 4)


# ---------------------------------------------------------------------------
# Fixtures encoded by Pillow, so the codes under test come from a real fax
# encoder rather than from this file's understanding of one.
# ---------------------------------------------------------------------------


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


# ---------------------------------------------------------------------------
# A hand-written codestream, for the one parameter no TIFF encoder will
# produce: TIFF has no /EncodedByteAlign, so the padded rows have to be built
# here. The same builder emits the unpadded stream, and the test renders both,
# which is what keeps these bits honest.
# ---------------------------------------------------------------------------

# T.4 terminating codes (Tables 2 and 3), for the two run lengths used below.
MH_CODES = {("white", 4): "1011", ("black", 8): "000101"}


class BitWriter:
    def __init__(self) -> None:
        self.bits = ""

    def write(self, bits: str) -> None:
        self.bits += bits

    def align(self) -> None:
        """Pad to the next byte boundary, as /EncodedByteAlign asks."""
        if len(self.bits) % 8:
            self.bits += "0" * (8 - len(self.bits) % 8)

    def to_bytes(self) -> bytes:
        padded = self.bits + "0" * (-len(self.bits) % 8)
        return bytes(int(padded[i : i + 8], 2) for i in range(0, len(padded), 8))


def g4_rectangle_by_hand(
    columns: int,
    rows: int,
    ink_columns: range,
    ink_rows: range,
    *,
    byte_align: bool,
) -> bytes:
    """Group 4 codes for the same rectangle, written mode by mode (T.6, 2.2).

    Each row is coded against the one above it, which is why only four cases
    arise for a rectangle: the run is absent, appears, continues, or ends.
    """
    start, stop = ink_columns.start, ink_columns.stop
    writer = BitWriter()

    for y in range(rows):
        if byte_align:
            writer.align()

        above = (y - 1) in ink_rows
        here = y in ink_rows

        if not above and not here:
            # Nothing changes anywhere: a1 and b1 are both the end of the row.
            writer.write("1")  # V0
        elif not above and here:
            # The reference row is blank, so its changing elements are too far
            # away to code vertically: both runs are stated outright.
            writer.write("001")  # horizontal mode
            writer.write(MH_CODES[("white", start)])
            writer.write(MH_CODES[("black", stop - start)])
            writer.write("1")  # V0 for the white run out to the row's end
        elif above and here:
            writer.write("1" * 3)  # V0 at each of a0, the run's end, the row's
        else:
            # The run ended: b2 is still inside the row, so pass mode carries
            # a0 across the reference row's run without coding a colour change.
            writer.write("0001")  # pass mode
            writer.write("1")  # V0

    return writer.to_bytes()


# ---------------------------------------------------------------------------
# Page assembly and assertions
# ---------------------------------------------------------------------------


def ccitt_page(
    payload: bytes,
    *,
    width: int,
    height: int,
    filters: str,
    decode_parms: str,
) -> bytes:
    """A page drawing one CCITT image over the middle of a 200x200 page."""
    image = stream_object(
        "/Type /XObject /Subtype /Image "
        f"/Width {width} /Height {height} "
        "/ColorSpace /DeviceGray /BitsPerComponent 1 "
        f"/Filter {filters} /DecodeParms {decode_parms}",
        payload,
    )

    return simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[image],
    )


def assert_rectangle_rendered(result, what: str) -> None:
    """The ink is a rectangle in the middle, with white page around it."""
    assert_color_near(
        center_color(region_image(result, INK)),
        BLACK,
        tolerance=30,
        what=f"ink of {what}",
    )
    assert_color_near(
        center_color(region_image(result, MARGIN)),
        WHITE,
        tolerance=30,
        what=f"margin of {what}",
    )
    assert_color_near(
        center_color(region_image(result, BELOW_INK)),
        WHITE,
        tolerance=30,
        what=f"the rows below the ink of {what}",
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "transport, wrap",
    [
        ("/ASCII85Decode", lambda data: a85encode(data) + b"~>"),
        ("/FlateDecode", zlib.compress),
    ],
)
def test_transport_filter_in_front_of_the_codec_is_undone(transport: str, wrap):
    """The codec is handed its own bytes, not the encoding wrapped around them.

    This is the page-12 regression of `deep-mediabox-inheritance.pdf`, whose
    figures are `/Filter [/ASCII85Decode /CCITTFaxDecode]`: the ASCII85 text
    reached the CCITT decoder unchanged and no figure was drawn.
    """
    width, height = 64, 32
    payload = wrap(g4_codes(width, height, [middle_half(width)], middle_half(height)))

    result = render_page(
        ccitt_page(
            payload,
            width=width,
            height=height,
            filters=f"[{transport} /CCITTFaxDecode]",
            decode_parms=f"[null << /K -1 /Columns {width} >>]",
        )
    )

    assert_rectangle_rendered(result, f"a CCITT image behind {transport}")


def test_codec_first_in_the_chain_still_decodes():
    """With nothing wrapped around it, the raw stream is already the codec's."""
    width, height = 64, 32
    payload = g4_codes(width, height, [middle_half(width)], middle_half(height))

    result = render_page(
        ccitt_page(
            payload,
            width=width,
            height=height,
            filters="/CCITTFaxDecode",
            decode_parms=f"<< /K -1 /Columns {width} >>",
        )
    )

    assert_rectangle_rendered(result, "a bare CCITT image")


def test_columns_wider_than_width_decodes_at_columns():
    """/Columns is the width the codes were written against, not /Width.

    Here the codes describe 96-pixel rows while the image is 64 pixels wide.
    The second band of ink lies past the image's width, so reading rows as 64
    pixels leaves its codes unread and starts the next row in the middle of a
    code word. The band itself is not part of the image and must not be drawn.
    """
    columns, width, height = 96, 64, 32
    beyond_the_width = range(width + 8, columns - 8)

    payload = g4_codes(
        columns,
        height,
        [middle_half(width), beyond_the_width],
        middle_half(height),
    )

    result = render_page(
        ccitt_page(
            payload,
            width=width,
            height=height,
            filters="/CCITTFaxDecode",
            decode_parms=f"<< /K -1 /Columns {columns} >>",
        )
    )

    assert_rectangle_rendered(result, "an image coded wider than its /Width")


def test_encoded_byte_align_skips_the_padding_between_rows():
    """/EncodedByteAlign true means every row starts on a byte boundary.

    The unpadded stream is rendered first from the very same builder: if these
    codes were wrong, both cases would fail, and it is the padding alone that
    separates them.
    """
    columns, height = 16, 8
    ink_columns, ink_rows = middle_half(columns), middle_half(height)

    for byte_align in (False, True):
        payload = g4_rectangle_by_hand(
            columns, height, ink_columns, ink_rows, byte_align=byte_align
        )

        result = render_page(
            ccitt_page(
                payload,
                width=columns,
                height=height,
                filters="/CCITTFaxDecode",
                decode_parms=(
                    f"<< /K -1 /Columns {columns} "
                    f"/EncodedByteAlign {str(byte_align).lower()} >>"
                ),
            )
        )

        assert_rectangle_rendered(
            result, f"a CCITT image with /EncodedByteAlign {byte_align}"
        )
