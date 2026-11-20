"""Turning image files into PDF pages.

JPEG files go into the PDF as they are: the file is the image's /DCTDecode
stream, so there is no loss and no re-encoding. Every other format (PNG, TIFF,
GIF, BMP, WebP, ...) is decoded with Pillow and stored losslessly; transparency
becomes a soft mask. Pillow is imported only when an image is added.
"""

from __future__ import annotations

import math
import os
from collections.abc import Iterator, Sequence
from contextlib import nullcontext
from io import BytesIO
from typing import Any, BinaryIO, NamedTuple, Union

ImageSource = Union[
    str, "os.PathLike[str]", bytes, bytearray, memoryview, BinaryIO, Any
]

# Page sizes in points (1/72 inch), portrait.
PAGE_SIZES: dict[str, tuple[float, float]] = {
    "a3": (841.89, 1190.55),
    "a4": (595.28, 841.89),
    "a5": (419.53, 595.28),
    "letter": (612.0, 792.0),
    "legal": (612.0, 1008.0),
}

# EXIF orientation (tag 0x0112) to the clockwise page rotation that shows the
# stored image upright. The mirrored orientations (2, 4, 5, 7) have no page
# rotation that undoes them; those images are decoded and turned instead.
_ORIENTATION_TO_ROTATION = {1: 0, 3: 180, 6: 90, 8: 270}


class Picture(NamedTuple):
    """One image prepared for insertion as a PDF page."""

    width: int
    height: int
    colorspace: str  # "gray", "rgb" or "cmyk"
    data: bytes  # a JPEG file, or width x height x components samples
    is_jpeg: bool
    invert: bool  # samples stored inverted (Adobe CMYK JPEG)
    alpha: bytes | None  # width x height opacity samples
    dpi: tuple[float, float] | None
    rotate: int  # clockwise page rotation that shows the image upright


def read_source(source: ImageSource) -> bytes | Any:
    """The bytes of a path, bytes object or stream; a PIL image unchanged."""
    if isinstance(source, (str, os.PathLike)):
        with open(source, "rb") as stream:
            return stream.read()
    if isinstance(source, (bytes, bytearray, memoryview)):
        return bytes(source)
    if hasattr(source, "read"):
        # the stream stays the caller's: read, never closed
        if hasattr(source, "seek") and source.seekable():
            source.seek(0)
        return bytes(source.read())
    if hasattr(source, "mode") and hasattr(source, "size"):
        return source
    raise TypeError(
        f"cannot read an image from {type(source).__name__}: expected a path, "
        "bytes, a binary stream or a PIL image"
    )


def _dpi_of(info: Any) -> tuple[float, float] | None:
    dpi = info.get("dpi")
    if not dpi:
        return None
    try:
        x, y = float(dpi[0]), float(dpi[1])
    except (TypeError, ValueError, IndexError):
        return None
    if not (x > 0 and y > 0 and math.isfinite(x) and math.isfinite(y)):
        return None
    return (x, y)


def _from_jpeg(data: bytes, image: Any) -> Picture | None:
    """The JPEG file as it is, or None when it has to be decoded instead."""
    colorspaces = {1: "gray", 3: "rgb", 4: "cmyk"}

    # PDF readers decode 8-bit DCT data only.
    if image.bits != 8 or image.layers not in colorspaces:
        return None

    orientation = image.getexif().get(0x0112, 1)

    rotation = _ORIENTATION_TO_ROTATION.get(orientation)
    if rotation is None:
        return None

    return Picture(
        width=image.width,
        height=image.height,
        colorspace=colorspaces[image.layers],
        data=data,
        is_jpeg=True,
        # Adobe applications store CMYK JPEG samples inverted.
        invert=image.layers == 4 and "adobe" in image.info,
        alpha=None,
        dpi=_dpi_of(image.info),
        rotate=rotation,
    )


def _from_pil(image: Any, dpi: tuple[float, float] | None) -> Picture:
    """Raw 8-bit samples of a decoded image, with its opacity split off."""
    mode = image.mode
    has_transparency = "transparency" in image.info

    if mode == "P" or mode == "PA":
        image = image.convert("RGBA" if has_transparency or mode == "PA" else "RGB")
    elif mode == "1":
        image = image.convert("L")
    elif mode in ("I", "I;16", "I;16B", "I;16L", "I;16N"):
        # 16-bit gray to 8 bit
        image = image.convert("I").point(lambda value: value * (1 / 256)).convert("L")
    elif mode == "F":
        image = image.convert("L")
    elif mode in ("L", "LA", "RGB", "RGBA", "CMYK"):
        pass
    else:
        # YCbCr, LAB, HSV, RGBX, ...
        image = image.convert("RGBA" if has_transparency else "RGB")

    alpha: bytes | None = None
    if image.mode in ("LA", "RGBA"):
        opacity = image.getchannel("A")
        # a fully opaque mask only costs space
        if opacity.getextrema() != (255, 255):
            alpha = opacity.tobytes()
        image = image.convert("L" if image.mode == "LA" else "RGB")

    colorspace = {"L": "gray", "RGB": "rgb", "CMYK": "cmyk"}[image.mode]

    return Picture(
        width=image.width,
        height=image.height,
        colorspace=colorspace,
        data=image.tobytes(),
        is_jpeg=False,
        invert=False,
        alpha=alpha,
        dpi=dpi,
        rotate=0,
    )


def pictures(source: ImageSource) -> Iterator[Picture]:
    """The images of `source`: one for most files, one per frame for
    multi-page TIFF and animated GIF files."""
    content = read_source(source)

    from PIL import Image, ImageOps, ImageSequence

    with (
        Image.open(BytesIO(content))
        if isinstance(content, bytes)
        else nullcontext(content) as image
    ):
        if isinstance(content, bytes) and image.format == "JPEG":
            picture = _from_jpeg(content, image)
            if picture is not None:
                yield picture
                return
        for frame in ImageSequence.Iterator(image):
            dpi = _dpi_of(frame.info) or _dpi_of(image.info)
            upright = ImageOps.exif_transpose(frame)
            yield _from_pil(upright if upright is not None else frame, dpi)


class PageLayout(NamedTuple):
    """Where an image goes, in the page's unrotated coordinates."""

    page_width: float
    page_height: float
    placement: tuple[float, float, float, float]  # x, y, width, height


def page_size_of(page_size: str | Sequence[float] | None) -> tuple[float, float] | None:
    if page_size is None:
        return None
    if isinstance(page_size, str):
        size = PAGE_SIZES.get(page_size.lower())
        if size is None:
            names = ", ".join(sorted(PAGE_SIZES))
            raise ValueError(
                f"unknown page size {page_size!r}: expected one of {names}"
            )
        return size
    width, height = (float(value) for value in page_size)
    if not (
        width > 0 and height > 0 and math.isfinite(width) and math.isfinite(height)
    ):
        raise ValueError("a page size has a finite positive width and height")
    return (width, height)


def layout(
    picture: Picture,
    page_size: tuple[float, float] | None,
    dpi: float | None,
    margin: float,
) -> PageLayout:
    """Page size and image placement.

    Without a page size the page is the image at its resolution (`dpi`, the
    image's own, or 72 so that a pixel is a point), plus the margin. With a
    page size the image is centered and scaled to fill the page inside the
    margin, keeping its proportions.
    """
    if not (margin >= 0 and math.isfinite(margin)):
        raise ValueError("the margin must be finite and nonnegative")

    if dpi is not None:
        if not (dpi > 0 and math.isfinite(dpi)):
            raise ValueError("dpi must be finite and positive")
        x_dpi = y_dpi = float(dpi)
    else:
        x_dpi, y_dpi = picture.dpi or (72.0, 72.0)

    # the image's natural size as stored, then as displayed
    stored_width = picture.width * 72.0 / x_dpi
    stored_height = picture.height * 72.0 / y_dpi

    turned = picture.rotate in (90, 270)
    shown_width, shown_height = (
        (stored_height, stored_width) if turned else (stored_width, stored_height)
    )

    if page_size is None:
        page_width = shown_width + 2 * margin
        page_height = shown_height + 2 * margin
        draw_width, draw_height = shown_width, shown_height
    else:
        page_width, page_height = page_size
        room_width = page_width - 2 * margin
        room_height = page_height - 2 * margin
        if not (room_width > 0 and room_height > 0):
            raise ValueError("the margin leaves no room on the page")
        scale = min(room_width / shown_width, room_height / shown_height)
        draw_width, draw_height = shown_width * scale, shown_height * scale

    # The page is stored unrotated and turned by /Rotate, so for a quarter
    # turn its stored width is the height it is shown with. The image is
    # centered, which is the same in both frames.
    if turned:
        page_width, page_height = page_height, page_width
        draw_width, draw_height = draw_height, draw_width

    return PageLayout(
        page_width=page_width,
        page_height=page_height,
        placement=(
            (page_width - draw_width) / 2,
            (page_height - draw_height) / 2,
            draw_width,
            draw_height,
        ),
    )
