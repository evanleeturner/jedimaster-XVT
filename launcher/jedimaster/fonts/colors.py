"""The game's text colors: the five a string switches to with bytes 2 to 6.

Purpose:
    Hold, as data, the six 16-bit 565 values the game keeps for its color
    codes. They are in no file of the game: they are the game's own
    values, as printed by the engine's font_dump tool (its ``color_codes``
    line), copied here from that printout. With the icon tables of
    ``jedimaster.icons.tables`` and the strings.txt tables of
    ``jedimaster.text.tables``, they are the only game data in this
    package's code.

Flow:
    ``COLOR_CODES`` is the printout's line in order; ``TEXT_COLORS`` maps
    each color byte (2 to 6) to its value and name; nothing here computes.

Invariants:
    - ``COLOR_CODES[0]`` (white) is not used by drawing: byte 1 returns to
      the color a draw began with, whatever that was.
    - Byte ``b`` (2 to 6) switches to ``COLOR_CODES[b - 1]``.

Call:
    ``TEXT_COLORS[2].color`` -> ``0x07e0``
"""

from __future__ import annotations

import logging
from typing import NamedTuple

logger = logging.getLogger(__name__)

COLOR_CODES: tuple[int, ...] = (0xFFFF, 0x07E0, 0xF800, 0xFFE0, 0x319F, 0x841F)
"""The six values of the printout's ``color_codes`` line, in its order."""

RESET_BYTE = 1
"""The byte that returns a string to the color its draw began with."""


class TextColor(NamedTuple):
    """One color byte of a string: the byte, the 565 value, a plain name."""

    byte: int
    color: int
    name: str


TEXT_COLORS: dict[int, TextColor] = {
    byte: TextColor(byte, COLOR_CODES[byte - 1], name)
    for byte, name in (
        (2, "green"),
        (3, "red"),
        (4, "yellow"),
        (5, "blue"),
        (6, "pale blue"),
    )
}
"""Each color byte (2 to 6) and the color it switches to."""
