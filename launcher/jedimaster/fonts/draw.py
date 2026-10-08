"""Draw and measure a string in a menu font, by the game's rules.

Purpose:
    Give the pixels the game writes when it draws one glyph or a whole
    string, each in the text's color (a glyph is a shape, not a picture),
    and the width the game measures for a string.

Flow:
    ``draw_glyph`` follows a glyph's runs row by row from the pen.
    ``draw_string`` walks the string's bytes up to its end: byte 1 returns
    to the color the draw began with, bytes 2 to 6 switch to the text
    colors of ``colors.TEXT_COLORS``, every other byte draws its glyph and
    moves the pen right by its width plus the spacing; a pen at x 640 or
    more before a byte stops the string. ``measure_string`` sums widths.

Invariants:
    - A string ends at its first byte 0.
    - Drawing follows the codes and ignores the row lengths; a skipped run
      leaves its pixels as they were. A glyph of width 0 draws nothing.
    - The measure of a string is the sum, over its bytes above 6, of width
      plus spacing, less one spacing: an empty string measures minus the
      spacing. It does not stop at x 640.

Call:
    ``draw_string(font, b"Pilot", 0xFFFF).pixels``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import NamedTuple

from .colors import RESET_BYTE
from .colors import TEXT_COLORS
from .reader import Font

logger = logging.getLogger(__name__)

STOP_X = 640
"""A pen at this x or more before a byte stops the string."""
LAST_CODE = 6
"""Bytes 1 to this one are color codes; they draw nothing."""


class Pixel(NamedTuple):
    """One pixel a draw writes: where, and its 16-bit 565 color."""

    x: int
    y: int
    color: int


@dataclass
class StringDraw:
    """What drawing a string did: the pixels written and where the pen ended."""

    pixels: list[Pixel]
    pen: int
    stopped: bool


def draw_glyph(font: Font, code: int, x: int, y: int, color: int) -> list[Pixel]:
    """Return the pixels glyph ``code`` writes with its top-left at (x, y).

    Each row starts at ``x``; a drawn run writes ``color`` at each of its
    columns, a skipped run writes nothing; columns past the glyph's width
    are written when its runs reach them. Returns ``[]`` for a glyph of
    width 0. Raises ``IndexError`` for a code outside 0 to 255. Does not
    clip to any surface.
    """
    glyph = font.glyphs[code]
    if glyph.width == 0:
        return []
    pixels = []
    for row, runs in enumerate(glyph.rows):
        column = x
        for run in runs:
            if run.drawn:
                pixels += [Pixel(column + i, y + row, color) for i in range(run.count)]
            column += run.count
    return pixels


def _end(text: bytes) -> bytes:
    stop = text.find(0)
    return text if stop < 0 else text[:stop]


def draw_string(
    font: Font, text: bytes, color: int, x: int = 0, y: int = 0
) -> StringDraw:
    """Return the pixels the game writes drawing ``text`` from (x, y).

    ``color`` is the color the draw begins with. Bytes 1 to 6 change the
    color, every other byte draws its glyph and moves the pen by its width
    plus the font's spacing; the string stops at its first byte 0, or when
    the pen stands at x 640 or more before a byte (``stopped``). Does not
    clip pixels to any surface.
    """
    pen, current = x, color
    pixels: list[Pixel] = []
    stopped = False
    for byte in _end(text):
        if pen >= STOP_X:
            stopped = True
            break
        if byte == RESET_BYTE:
            current = color
        elif byte <= LAST_CODE:
            current = TEXT_COLORS[byte].color
        else:
            pixels += draw_glyph(font, byte, pen, y, current)
            pen += font.widths[byte] + font.spacing
    logger.debug("string %r: %d pixels, pen at %d", text, len(pixels), pen)
    return StringDraw(pixels, pen, stopped)


def measure_string(font: Font, text: bytes) -> int:
    """Return the width the game measures for ``text``.

    The sum, over the bytes above 6 before the first byte 0, of width plus
    spacing, less one spacing (minus the spacing for a string with no such
    byte). Does not stop at x 640.
    """
    total = sum(font.widths[b] + font.spacing for b in _end(text) if b > LAST_CODE)
    return total - font.spacing
