"""Write an icon sheet as a PNG file with the standard library only.

Purpose:
    Give the briefing page a picture of each sheet: 8-bit RGBA, each pixel
    in its palette entry's own 8-bit color, palette index 0 fully
    transparent, as icons draw it.

Flow:
    ``png_bytes`` expands the palette indices to RGBA pixels and hands them
    to ``rgba_png``, which writes rows (filter 0 each), compresses them
    with ``zlib`` and wraps them in the IHDR, IDAT and IEND chunks;
    ``write_png`` writes those bytes. ``rgba_png`` is shared with the other
    pictures this package writes.

Invariants:
    - Standard library only (``zlib``, ``struct``).
    - Every pixel but index 0 is opaque (alpha 255); index 0 keeps its
      palette color with alpha 0.

Call:
    ``write_png(path, sheet.bitmap)``
"""

from __future__ import annotations

import logging
import os
import struct
import zlib

from .bmp import BmpFile
from .game import TRANSPARENT

logger = logging.getLogger(__name__)

SIGNATURE = b"\x89PNG\r\n\x1a\n"
BIT_DEPTH = 8
COLOR_RGBA = 6


def _chunk(kind: bytes, body: bytes) -> bytes:
    crc = zlib.crc32(kind + body)
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)


def rgba_png(width: int, height: int, rgba: bytes) -> bytes:
    """Return a PNG file's bytes for ``width * height`` RGBA pixels, rows top first.

    ``rgba`` holds 4 bytes per pixel. Raises ``ValueError`` for an empty
    image (PNG has none) or a pixel count other than ``width * height``.
    Does not check the alpha values.
    """
    if width <= 0 or height <= 0:
        raise ValueError(f"no PNG for a {width}x{height} bitmap")
    stride = 4 * width
    if len(rgba) != stride * height:
        raise ValueError(f"{len(rgba)} bytes are not {width}x{height} RGBA pixels")
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride : (y + 1) * stride]
    header = struct.pack(">IIBBBBB", width, height, BIT_DEPTH, COLOR_RGBA, 0, 0, 0)
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )


def png_bytes(bmp: BmpFile) -> bytes:
    """Return a bitmap's pixels as a PNG file's bytes, 8-bit RGBA.

    Each pixel is its palette entry's red, green and blue with alpha 255,
    or alpha 0 for index 0. Raises ``ValueError`` for a bitmap with no
    pixels (PNG has no empty image). Does not keep the palette as a palette.
    """
    rgba = [
        bytes((*entry, 0 if index == TRANSPARENT else 255))
        for index, entry in enumerate(bmp.palette)
    ]
    pixels = b"".join(rgba[index] for index in bmp.pixels)
    return rgba_png(bmp.width, bmp.height, pixels)


def write_png(path: str | os.PathLike[str], bmp: BmpFile) -> int:
    """Write ``png_bytes(bmp)`` to ``path``; return the bytes written.

    Raises what ``png_bytes`` raises and ``OSError`` when the file cannot be
    written. Does not create the folder.
    """
    data = png_bytes(bmp)
    with open(path, "wb") as handle:
        handle.write(data)
    logger.debug("png %s: %d bytes", path, len(data))
    return len(data)
