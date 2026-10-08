"""Write an icon sheet as a PNG file with the standard library only.

Purpose:
    Give the briefing page a picture of each sheet: 8-bit RGBA, each pixel
    in its palette entry's own 8-bit color, palette index 0 fully
    transparent, as icons draw it.

Flow:
    ``png_bytes`` expands the palette indices to RGBA rows (filter 0 each),
    compresses them with ``zlib`` and wraps them in the IHDR, IDAT and IEND
    chunks; ``write_png`` writes those bytes.

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


def png_bytes(bmp: BmpFile) -> bytes:
    """Return a bitmap's pixels as a PNG file's bytes, 8-bit RGBA.

    Each pixel is its palette entry's red, green and blue with alpha 255,
    or alpha 0 for index 0. Raises ``ValueError`` for a bitmap with no
    pixels (PNG has no empty image). Does not keep the palette as a palette.
    """
    if bmp.width <= 0 or bmp.height <= 0:
        raise ValueError(f"no PNG for a {bmp.width}x{bmp.height} bitmap")
    rgba = [
        bytes((*entry, 0 if index == TRANSPARENT else 255))
        for index, entry in enumerate(bmp.palette)
    ]
    raw = bytearray()
    for y in range(bmp.height):
        raw.append(0)
        row = bmp.pixels[y * bmp.width : (y + 1) * bmp.width]
        raw += b"".join(rgba[index] for index in row)
    header = struct.pack(
        ">IIBBBBB", bmp.width, bmp.height, BIT_DEPTH, COLOR_RGBA, 0, 0, 0
    )
    return (
        SIGNATURE
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )


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
