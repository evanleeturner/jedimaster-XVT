"""Build synthetic 8-bit bitmaps, RLE8 codes and icon installs for the tests.

Purpose:
    Give the icon tests bitmaps without game data: a file header, a 40-byte
    information header, a 256-entry palette and pixel data, every field
    settable so a test can write exactly the case it reads back; and a fake
    install holding an icon list and its sheets.

Flow:
    ``run``, ``eol``, ``end``, ``move`` and ``stretch`` write RLE8 codes;
    ``plain`` writes uncompressed rows; ``bmp`` wraps pixel data in the
    headers and a palette; ``sheet`` builds a whole uncompressed sheet from
    a dict of pixels; ``icon_install`` writes a fake install; ``warned``
    reads back the WARNING lines a test logged.

Invariants:
    - No game data: palettes are a made-up ramp, pixels are the tests' own.
    - Uses only the standard library and nothing from ``jedimaster``.
    - The palette is written blue, green, red, spare, as the format stores it.

Call:
    ``data = bmp(4, 2, run(4, 7) + eol() + run(4, 9) + end())``
"""

from __future__ import annotations

import logging
import struct
from pathlib import Path

logger = logging.getLogger(__name__)

PALETTE_BYTES = 1024
DATA_START = 54 + PALETTE_BYTES


def ramp(index: int) -> tuple[int, int, int]:
    """Return a made-up (red, green, blue) for a palette index, all distinct."""
    return (index, 255 - index, (index * 7 + 3) % 256)


def run(n: int, value: int) -> bytes:
    """Return an RLE8 run: ``value`` written ``n`` times."""
    return bytes((n, value))


def eol() -> bytes:
    """Return the end-of-line code."""
    return b"\0\0"


def end() -> bytes:
    """Return the end-of-bitmap code."""
    return b"\0\1"


def move(dx: int, dy: int) -> bytes:
    """Return a move code with its two bytes."""
    return bytes((0, 2, dx, dy))


def stretch(*values: int, pad: int = 0xEE) -> bytes:
    """Return an absolute stretch of 3 or more values, odd lengths padded."""
    padding = bytes((pad,)) if len(values) % 2 else b""
    return bytes((0, len(values), *values)) + padding


def plain(rows: list[bytes], pad: int = 0xEE) -> bytes:
    """Return uncompressed pixel data: ``rows`` top first, stored bottom first.

    Each row is padded with ``pad`` bytes to a multiple of 4.
    """
    out = b""
    for row in reversed(rows):
        out += row + bytes((pad,)) * (-len(row) % 4)
    return out


def bmp(
    width: int,
    height: int,
    data: bytes,
    *,
    compression: int = 1,
    palette: list[tuple[int, int, int]] | None = None,
    colors_used: int = 256,
    offset: int = DATA_START,
    image_size: int | None = None,
    planes: int = 1,
    bits: int = 8,
    mark: bytes = b"BM",
    reserved: tuple[int, int] = (0, 0),
) -> bytes:
    """Return a whole .bmp file: headers, 256-entry palette, ``data``.

    ``image_size`` defaults to ``len(data)``; ``offset`` is the file
    header's pixel offset, written as given whatever the data's place.
    """
    entries = palette if palette is not None else [ramp(i) for i in range(256)]
    table = b"".join(bytes((b, g, r, 0x5A)) for r, g, b in entries)
    size = len(data) if image_size is None else image_size
    info = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height,
        planes,
        bits,
        compression,
        size,
        2835,
        2834,
        colors_used,
        colors_used,
    )
    total = 14 + len(info) + len(table) + len(data)
    return mark + struct.pack("<IHHI", total, *reserved, offset) + info + table + data


def sheet(
    width: int,
    height: int,
    pixels: dict[tuple[int, int], int],
    palette: list[tuple[int, int, int]] | None = None,
) -> bytes:
    """Return an uncompressed sheet whose (x, y) pixels are set, others 0."""
    rows = [bytearray(width) for _ in range(height)]
    for (x, y), value in pixels.items():
        rows[y][x] = value
    return bmp(
        width, height, plain([bytes(r) for r in rows]), compression=0, palette=palette
    )


def warned(caplog) -> str:
    """Return the messages of the WARNING (and worse) records ``caplog`` holds."""
    return "\n".join(
        r.getMessage() for r in caplog.records if r.levelno >= logging.WARNING
    )


def crlf_list(*lines: str) -> bytes:
    """Return an image list: lines joined by CR LF, then the 0x1A mark."""
    return ("\r\n".join(lines) + "\r\n").encode("latin-1") + b"\x1a"


def icon_install(root: Path, files: dict[str, bytes]) -> Path:
    """Write ``files`` (install-relative path -> bytes) under ``root``."""
    for rel, data in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (root / "Train").mkdir(exist_ok=True)
    return root
