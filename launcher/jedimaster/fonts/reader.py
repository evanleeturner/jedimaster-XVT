"""Read a menu font (``times*.abp``) the way the game reads it.

Purpose:
    Keep the font's 1,547-byte header as written (the glyph data's size,
    256 offsets, heights and widths, the point size, in-use, spacing and
    the last byte) and decode every glyph's rows into runs of drawn and
    skipped pixels by the game's codes.

Flow:
    ``read_font`` checks the size, unpacks the header, then ``_glyph``
    walks each glyph's ``height`` rows from its offset in the glyph data:
    a 4-byte row length, then codes up to ``0x80``. The codes decide where
    the next row starts; the row length is only compared with them.

Invariants:
    - All numbers little-endian. The font's height is height entry 0.
    - Codes: ``0x80`` ends the row; ``0x81`` to ``0xff`` draw
      ``code - 0x80`` pixels, then that many bytes follow; ``0x40`` to
      ``0x7f`` skip ``code - 0x40`` pixels; ``0x00`` to ``0x3f`` draw
      ``code`` pixels, then one byte follows. Following bytes are not
      pixels: drawing ignores them.
    - A row length other than the bytes its row took (length included) is
      kept and reported with a WARNING, as is a file whose glyph data is
      not the size its header gives; refused (``FontFormatError``) only
      what cannot be read: a file shorter than its header, a glyph whose
      codes run past the bytes there are.

Call:
    ``font = read_font(path); font.glyphs[65].rows``
"""

from __future__ import annotations

import logging
import os
import struct
from dataclasses import dataclass
from typing import NamedTuple

from ..text.lines import load

logger = logging.getLogger(__name__)

HEADER = struct.Struct("<I256I256B256BIBBB")
HEADER_SIZE = HEADER.size
"""1,547 bytes: size, offsets, heights, widths, points, in use, spacing, spare."""
GLYPHS = 256
ROW_LENGTH = struct.Struct("<I")
ROW_END = 0x80
DRAW_RUN = 0x80
SKIP_RUN = 0x40


class FontFormatError(ValueError):
    """A font file that cannot be read at all."""


class Run(NamedTuple):
    """Pixels in a row: ``drawn`` (True) or skipped, and how many."""

    drawn: bool
    count: int


@dataclass
class Glyph:
    """One glyph: its header entries, its rows of runs, its row lengths."""

    code: int
    offset: int
    width: int
    height: int
    rows: list[list[Run]]
    row_lengths: list[int]
    row_bytes: list[int]

    @property
    def drawn(self) -> int:
        """Return the pixels its codes draw, over all rows.

        Counts drawn runs only; does not look at the width or the row lengths.
        """
        return sum(run.count for row in self.rows for run in row if run.drawn)


@dataclass
class Font:
    """A font as written, its glyphs decoded."""

    data_size: int
    offsets: tuple[int, ...]
    heights: tuple[int, ...]
    widths: tuple[int, ...]
    points: int
    in_use: int
    spacing: int
    spare: int
    glyphs: list[Glyph]

    @property
    def height(self) -> int:
        """Return the font's height: height entry 0.

        Does not compare it with the other glyphs' heights.
        """
        return self.heights[0]


def _row(data: bytes, start: int, label: str, code: int, y: int) -> tuple[list, int]:
    """Return one row's runs and where the next row starts."""
    at = start + ROW_LENGTH.size
    runs = []
    while True:
        if at >= len(data):
            raise FontFormatError(f"{label}: glyph {code} row {y} runs past the data")
        value = data[at]
        at += 1
        if value == ROW_END:
            return runs, at
        if value > DRAW_RUN:
            runs.append(Run(True, value - DRAW_RUN))
            at += value - DRAW_RUN
        elif value >= SKIP_RUN:
            runs.append(Run(False, value - SKIP_RUN))
        else:
            runs.append(Run(True, value))
            at += 1


def _glyph(data: bytes, fields: tuple, code: int, label: str) -> Glyph:
    """Decode glyph ``code`` from the glyph data ``data``."""
    offset, height, width = fields[1 + code], fields[257 + code], fields[513 + code]
    rows, lengths, sizes = [], [], []
    at = offset
    for y in range(height):
        if at + ROW_LENGTH.size > len(data):
            raise FontFormatError(f"{label}: glyph {code} row {y} starts past the data")
        (length,) = ROW_LENGTH.unpack_from(data, at)
        runs, after = _row(data, at, label, code, y)
        rows.append(runs)
        lengths.append(length)
        sizes.append(after - at)
        if length != after - at:
            logger.warning(
                "%s: glyph %d row %d: row length %d, its codes take %d bytes",
                label,
                code,
                y,
                length,
                after - at,
            )
        at = after
    logger.debug("glyph %d: %dx%d at %d, rows %s", code, width, height, offset, rows)
    return Glyph(code, offset, width, height, rows, lengths, sizes)


def read_font(source: str | os.PathLike[str] | bytes | bytearray) -> Font:
    """Return a font's header fields and every glyph's rows of runs.

    ``source`` is a path or the file's bytes. Always returns all 256 glyphs
    for a readable file. Logs a WARNING when the glyph data is not the size
    the header gives and for each row length that disagrees with its codes.
    Raises ``FontFormatError`` for a file shorter than its header or a
    glyph whose rows run past the glyph data, and ``OSError`` when the
    path cannot be read. Does not check the offsets for overlaps or the
    glyphs against their widths.
    """
    raw, label = load(source)
    if len(raw) < HEADER_SIZE:
        raise FontFormatError(f"{label}: {len(raw)} bytes, too short for the header")
    fields = HEADER.unpack_from(raw, 0)
    size = fields[0]
    data = raw[HEADER_SIZE : HEADER_SIZE + size]
    if len(raw) - HEADER_SIZE != size:
        logger.warning(
            "%s: the header gives %d bytes of glyph data, the file holds %d",
            label,
            size,
            len(raw) - HEADER_SIZE,
        )
    glyphs = [_glyph(data, fields, code, label) for code in range(GLYPHS)]
    points, in_use, spacing, spare = fields[769:]
    font = Font(
        size,
        fields[1:257],
        fields[257:513],
        fields[513:769],
        points,
        in_use,
        spacing,
        spare,
        glyphs,
    )
    logger.info(
        "font %s: %d points, height %d, spacing %d, %d bytes of glyph data",
        label,
        points,
        font.height,
        spacing,
        size,
    )
    return font
