"""Read an 8-bit .bmp file the way the game reads it: headers, palette, pixels.

Purpose:
    Keep the file as written (both headers' fields, the 256 palette entries)
    and decode its pixels exactly as the game does, including what the
    game's decoding does that the usual one does not: an RLE8 run or stretch
    that passes a row's right edge goes on into the next byte of one flat
    buffer, which on screen is the row below.

Flow:
    ``read_bmp`` checks the headers (``_check``), reads the palette at byte
    54, then decodes the pixel data from byte 1,078: rows of ``width`` bytes
    padded to a multiple of 4 (``_decode_plain``), or the ``image_size``
    bytes of RLE8 codes into one flat buffer (``_decode_rle`` with a
    ``_Writer``). It returns a
    ``BmpFile`` with what the decoding met.

Invariants:
    - The palette is always 256 entries of 4 bytes (blue, green, red, spare)
      right after the 40-byte information header, whatever the header's
      colors-used count says; the pixel data always starts at byte 1,078,
      whatever the file header's offset says. ``offset_differs`` reports a
      file header offset elsewhere.
    - Pixels are ``width * height`` palette indices, rows top to bottom,
      every byte 0 until written.
    - No write ever lands outside the buffer: a write that would fall before
      its start or at or after its end is counted (``dropped``), not made.
    - RLE8 decodes exactly the ``image_size`` bytes from byte 1,078, as the
      game does; file bytes after them are never read. Decoding stops at
      the end-of-bitmap code or at the end of those bytes (``end_missing``:
      past them the game reads beyond its own buffer, with no defined
      answer). A file holding fewer than ``image_size`` bytes there is
      decoded as far as it goes (``bytes_missing``).
    - Refused, with ``BmpFormatError``, only what cannot be read at all: no
      ``BM`` mark, a file too short for its headers and palette, planes not
      1, a depth other than 8, a compression other than 0 and 1, a negative
      height or width, or more pixels than ``MAX_PIXELS``.

Call:
    ``bmp = read_bmp(path); bmp.pixels[y * bmp.width + x]``
"""

from __future__ import annotations

import logging
import os
import struct
from dataclasses import dataclass

logger = logging.getLogger(__name__)

FILE_HEADER = struct.Struct("<2sIHHI")
INFO_HEADER = struct.Struct("<IiiHHIIiiII")
PALETTE_OFFSET = FILE_HEADER.size + 40
PALETTE_ENTRIES = 256
DATA_OFFSET = PALETTE_OFFSET + 4 * PALETTE_ENTRIES
"""Byte 1,078: where the game always starts reading the pixel data."""

PLAIN = 0
RLE8 = 1
END_OF_LINE = 0
END_OF_BITMAP = 1
MOVE = 2
MAX_PIXELS = 1 << 26
"""Refuse a header asking for more pixels than this (64 Mi): not a sheet."""


class BmpFormatError(ValueError):
    """A .bmp file that cannot be read at all."""


@dataclass
class BmpFile:
    """A .bmp file as written, its pixels as the game decodes them, and how."""

    file_size: int
    reserved1: int
    reserved2: int
    pixel_offset: int
    header_size: int
    width: int
    height: int
    planes: int
    bits: int
    compression: int
    image_size: int
    x_pixels_per_meter: int
    y_pixels_per_meter: int
    colors_used: int
    colors_important: int
    palette: list[tuple[int, int, int]]
    pixels: bytes
    spilled: int
    dropped: int
    end_missing: bool
    bytes_read: int
    bytes_missing: int
    offset_differs: bool


@dataclass
class _Decoded:
    pixels: bytearray
    spilled: int
    dropped: int
    end_missing: bool
    bytes_read: int


class _Writer:
    """The game's two numbers, write position and row start, over one buffer."""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.buffer = bytearray(width * height)
        self.row_start = (height - 1) * width
        self.position = self.row_start
        self.spilled = 0
        self.dropped = 0

    def put(self, value: int) -> None:
        """Write one byte at the write position, or count it as dropped."""
        if 0 <= self.position < len(self.buffer):
            if self.position - self.row_start >= self.width:
                self.spilled += 1
            self.buffer[self.position] = value
        else:
            self.dropped += 1
        self.position += 1

    def end_of_line(self) -> None:
        """Move the row start up one row; the write position goes to it."""
        self.row_start -= self.width
        self.position = self.row_start

    def move(self, dx: int, dy: int) -> None:
        """Keep the column as it is (even past the edge), move it by dx, dy."""
        column = self.position - self.row_start
        self.row_start -= dy * self.width
        self.position = self.row_start + column + dx


def _decode_rle(data: bytes, width: int, height: int) -> _Decoded:
    """Decode RLE8 codes the game's way; see the module's invariants."""
    writer = _Writer(width, height)
    debug = logger.isEnabledFor(logging.DEBUG)
    i = 0
    end = False
    while i + 2 <= len(data):
        n, v = data[i], data[i + 1]
        if debug:
            logger.debug("code at %d: %d %d", i, n, v)
        i += 2
        if n > 0:
            for _ in range(n):
                writer.put(v)
        elif v == END_OF_LINE:
            writer.end_of_line()
        elif v == END_OF_BITMAP:
            end = True
            break
        elif v == MOVE:
            if i + 2 > len(data):
                i = len(data)
                break
            writer.move(data[i], data[i + 1])
            i += 2
        else:
            for value in data[i : i + v]:
                writer.put(value)
            i += v + (v & 1)
    return _Decoded(
        writer.buffer, writer.spilled, writer.dropped, not end, min(i, len(data))
    )


def _decode_plain(data: bytes, width: int, height: int) -> _Decoded:
    """Copy rows of ``width`` bytes, padded to 4, bottom row first."""
    stride = (width + 3) & ~3
    buffer = bytearray(width * height)
    for row in range(height):
        chunk = data[row * stride : row * stride + width]
        top = (height - 1 - row) * width
        buffer[top : top + len(chunk)] = chunk
    wanted = stride * (height - 1) + width if height else 0
    return _Decoded(buffer, 0, 0, len(data) < wanted, min(len(data), stride * height))


def _check(data: bytes, label: str) -> tuple[tuple, tuple]:
    """Return both headers' fields, or raise ``BmpFormatError``."""
    if len(data) < DATA_OFFSET:
        raise BmpFormatError(
            f"{label}: {len(data)} bytes, too short for headers and palette"
        )
    head = FILE_HEADER.unpack_from(data, 0)
    info = INFO_HEADER.unpack_from(data, FILE_HEADER.size)
    _, width, height, planes, bits, compression = info[:6]
    if head[0] != b"BM":
        raise BmpFormatError(f"{label}: not a BMP file (no BM mark)")
    if planes != 1:
        raise BmpFormatError(f"{label}: {planes} planes, not 1")
    if bits != 8:
        raise BmpFormatError(f"{label}: {bits} bits per pixel, not 8")
    if compression not in (PLAIN, RLE8):
        raise BmpFormatError(f"{label}: compression {compression}, not 0 or 1")
    if height < 0:
        raise BmpFormatError(f"{label}: negative height {height} (top-down file)")
    if width < 0:
        raise BmpFormatError(f"{label}: negative width {width}")
    if width * height > MAX_PIXELS:
        raise BmpFormatError(f"{label}: {width}x{height} is too many pixels")
    return head, info


def _palette(data: bytes) -> list[tuple[int, int, int]]:
    """Return the 256 entries at byte 54, stored blue, green, red, spare."""
    entries = []
    for i in range(PALETTE_ENTRIES):
        blue, green, red = data[PALETTE_OFFSET + 4 * i : PALETTE_OFFSET + 4 * i + 3]
        entries.append((red, green, blue))
    return entries


def _report(bmp: BmpFile, label: str) -> None:
    """Log what the decoding met: WARNING for drops, a missing end code,
    bytes missing from the file, and a file header offset other than 1,078."""
    logger.info(
        "bmp %s: %dx%d compression %d, %d bytes read (image size %d), "
        "%d spilled, %d dropped",
        label,
        bmp.width,
        bmp.height,
        bmp.compression,
        bmp.bytes_read,
        bmp.image_size,
        bmp.spilled,
        bmp.dropped,
    )
    if bmp.dropped:
        logger.warning(
            "bmp %s: %d writes outside the buffer dropped", label, bmp.dropped
        )
    if bmp.end_missing:
        logger.warning("bmp %s: pixel data ended before its end code", label)
    if bmp.bytes_missing:
        logger.warning(
            "bmp %s: %d of the image size's %d bytes are missing from the file",
            label,
            bmp.bytes_missing,
            bmp.image_size,
        )
    if bmp.offset_differs:
        logger.warning(
            "bmp %s: file header offset %d, pixels read at %d",
            label,
            bmp.pixel_offset,
            DATA_OFFSET,
        )


def read_bmp(source: str | os.PathLike[str] | bytes | bytearray) -> BmpFile:
    """Return a .bmp file's headers, palette and pixels as the game decodes them.

    ``source`` is a path or the file's bytes. Always returns a ``BmpFile``
    for a readable file, whatever its pixel data holds: writes past a row's
    edge (``spilled``) or outside the buffer (``dropped``), a missing end
    code or short rows (``end_missing``), an RLE8 file holding fewer bytes
    than its ``image_size`` (``bytes_missing``; always 0 uncompressed), a
    file header offset other than 1,078 (``offset_differs``). An RLE8 file
    whose ``image_size`` is 0 decodes nothing. Raises ``BmpFormatError``
    for a file that cannot be read at all (see the module's invariants) and
    ``OSError`` when the path cannot be read. Does not check ``file_size``, ``header_size``,
    ``colors_used`` or the reserved fields, which the game does not use.
    """
    if isinstance(source, bytes | bytearray):
        data, label = bytes(source), "<bytes>"
    else:
        with open(source, "rb") as handle:
            data = handle.read()
        label = os.fspath(source)
    head, info = _check(data, label)
    logger.debug("bmp %s: file header %s", label, head[1:])
    logger.debug("bmp %s: info header %s", label, info)
    palette = _palette(data)
    logger.debug("bmp %s: palette %s", label, palette)
    width, height, compression = info[1], info[2], info[5]
    missing = 0
    if compression == RLE8:
        codes = data[DATA_OFFSET : DATA_OFFSET + info[6]]
        missing = info[6] - len(codes)
        decoded = _decode_rle(codes, width, height)
    else:
        decoded = _decode_plain(data[DATA_OFFSET:], width, height)
    bmp = BmpFile(
        *head[1:],
        *info,
        palette=palette,
        pixels=bytes(decoded.pixels),
        spilled=decoded.spilled,
        dropped=decoded.dropped,
        end_missing=decoded.end_missing,
        bytes_read=decoded.bytes_read,
        bytes_missing=missing,
        offset_differs=head[4] != DATA_OFFSET,
    )
    _report(bmp, label)
    return bmp
