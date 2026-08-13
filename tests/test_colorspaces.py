#!/usr/bin/env python
"""Colour spaces that need more than a device formula (PDF 32000-1, 8.6).

/Separation and /DeviceN name inks and carry a tint transform -- a function
from tint values to an alternate space. Ignoring it and reading the tint as if
it were a device colour turns a spot-red logo grey. /ICCBased carries a profile
that has to be applied rather than assumed.

CMYK has two traps of its own. A JPEG written with an Adobe marker stores
inverted ink, and correcting for it twice is as wrong as not at all; and a pure
black tint is a *linear* grey, so writing it straight into an sRGB pixel comes
out far too dark.
"""

from __future__ import annotations

import io

import pytest

from tests.pdf_builder import (
    postscript_function,
    render_page,
    simple_page_pdf,
    stream_object,
)
from tests.rendering_regression import assert_color_near, center_color, region_image

SWATCH = (50.0, 50.0, 150.0, 150.0)
SWATCH_CONTENT_BOX = "50 50 100 100 re f"


def _swatch_color(pdf: bytes) -> tuple[int, int, int]:
    return center_color(region_image(render_page(pdf), SWATCH))


def test_separation_tint_transform_is_evaluated():
    """A /Separation fill maps its tint through the transform, not around it.

    Full tint of an ink whose transform ends at CMYK (0, 1, 1, 0) is red. Read
    as a device value instead, the same 1.0 would come out white or grey.
    """
    tint = (
        "<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 1 1 0] /N 1 >>"
    )
    pdf = simple_page_pdf(
        f"/CS0 cs 1 sc\n{SWATCH_CONTENT_BOX}\n",
        resources="/ColorSpace << /CS0 [/Separation /Spot /DeviceCMYK 5 0 R] >>",
        extra_objects=[tint],
    )
    assert_color_near(
        _swatch_color(pdf),
        (255, 0, 0),
        tolerance=24,
        what="full tint of a /Separation ink",
    )


def test_separation_half_tint_interpolates():
    """Half tint lands between the transform's endpoints."""
    tint = (
        "<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 1 1 0] /N 1 >>"
    )
    pdf = simple_page_pdf(
        f"/CS0 cs 0.5 sc\n{SWATCH_CONTENT_BOX}\n",
        resources="/ColorSpace << /CS0 [/Separation /Spot /DeviceCMYK 5 0 R] >>",
        extra_objects=[tint],
    )
    red, green, blue = _swatch_color(pdf)
    assert red > 200, f"half tint lost the ink's hue: {(red, green, blue)}"
    assert 60 < green < 220, (
        f"half tint did not interpolate; green channel is {green}"
    )
    assert abs(green - blue) < 24, "the two half-tinted channels drifted apart"


def test_devicen_postscript_tint_transform_is_evaluated():
    """A /DeviceN space with a Type 4 (PostScript calculator) transform.

    The function drops both inputs and returns blue, so anything other than
    blue means the calculator was not run.
    """
    pdf = simple_page_pdf(
        f"/CS0 cs 1 1 sc\n{SWATCH_CONTENT_BOX}\n",
        resources="/ColorSpace << /CS0 [/DeviceN [/A /B] /DeviceRGB 5 0 R] >>",
        extra_objects=[
            postscript_function(
                "{ pop pop 0 0 1 }", domain="[0 1 0 1]", range_="[0 1 0 1 0 1]"
            )
        ],
    )
    assert_color_near(
        _swatch_color(pdf),
        (0, 0, 255),
        tolerance=24,
        what="/DeviceN fill through a Type 4 tint transform",
    )


def test_pure_black_tint_is_srgb_encoded():
    """A mid CMYK black is a linear grey and has to be encoded for sRGB.

    `0 0 0 0.5 k` is half the ink, not half the pixel value: writing 127
    straight out renders a grey markedly darker than every other renderer
    produces. The encoded value is near 188.
    """
    pdf = simple_page_pdf(f"0 0 0 0.5 k\n{SWATCH_CONTENT_BOX}\n")
    red, green, blue = _swatch_color(pdf)

    assert abs(red - green) < 6 and abs(green - blue) < 6, (
        f"a pure-K tint came out tinted: {(red, green, blue)}"
    )
    assert red > 160, (
        f"a 50% K grey rendered as {red}; the naive product (about 127) means "
        "the sRGB encoding was skipped"
    )


def test_full_black_tint_stays_black():
    """The encoding must not lift solid black off the floor."""
    pdf = simple_page_pdf(f"0 0 0 1 k\n{SWATCH_CONTENT_BOX}\n")
    assert max(_swatch_color(pdf)) < 24, "solid K did not render black"


def test_iccbased_fill_is_colour_managed():
    """An /ICCBased fill goes through the profile.

    The profile here is sRGB, so the managed result has to stay close to the
    device interpretation; what this pins is that a profile is applied at all
    and that applying it does not corrupt the colour.
    """
    ImageCms = pytest.importorskip(
        "PIL.ImageCms", reason="colour management needs Pillow's lcms bindings"
    )
    profile_bytes = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()

    pdf = simple_page_pdf(
        f"/CS0 cs 1 0 0 sc\n{SWATCH_CONTENT_BOX}\n",
        resources="/ColorSpace << /CS0 [/ICCBased 5 0 R] >>",
        extra_objects=[stream_object("/N 3", profile_bytes)],
    )
    red, green, blue = _swatch_color(pdf)
    assert red > 200 and green < 90 and blue < 90, (
        f"an ICCBased red fill rendered as {(red, green, blue)}"
    )


def test_cmyk_jpeg_keeps_its_ink():
    """A CMYK JPEG decodes to the ink actually stored in it.

    The bytes in the file are process ink: (0, 255, 255, 0) is red. Inverting
    them on the way in -- which an Adobe marker was once taken to require --
    turns that red into cyan, which is what the corpus photographs looked like.

    Pillow writes CMYK JPEGs with the values inverted, so the source image is
    inverted here first; what lands in the file is the process ink above. The
    same file renders red in pypdfium2, which is the reference this was
    settled against.
    """
    from PIL import Image, ImageChops

    # inverted on the way in, so the file stores (0, 255, 255, 0) = red
    source = ImageChops.invert(Image.new("CMYK", (16, 16), (0, 255, 255, 0)))
    buffer = io.BytesIO()
    source.save(buffer, format="JPEG", quality=95)

    pdf = simple_page_pdf(
        "q\n100 0 0 100 50 50 cm\n/Im0 Do\nQ\n",
        resources="/XObject << /Im0 5 0 R >>",
        extra_objects=[
            stream_object(
                "/Type /XObject /Subtype /Image /Width 16 /Height 16 "
                "/ColorSpace /DeviceCMYK /BitsPerComponent 8 /Filter /DCTDecode",
                buffer.getvalue(),
            )
        ],
    )

    red, green, blue = _swatch_color(pdf)
    assert red > green + 60 and red > blue + 60, (
        f"a red CMYK JPEG rendered as {(red, green, blue)}; cyan here means the "
        "stored ink was inverted on the way in"
    )
