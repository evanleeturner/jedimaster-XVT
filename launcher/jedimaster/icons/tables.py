"""The game's two icon tables: where each icon lies, and each craft's icon.

Purpose:
    Hold, as data, the 70 boxes of the icon sheets and the box of each of
    the 106 craft types. Neither table is in any file of the game: they are
    the game's own tables, as printed by the engine's icon_dump tool, copied
    here from that printout. They are carried here as facts about the
    game's art. With the strings.txt tables of ``jedimaster.text.tables``,
    the text colors of ``jedimaster.fonts.colors`` and the pilot layout of
    ``jedimaster.pilot.layout``, they are the only game data in this
    package's code.

Flow:
    ``BOXES[n]`` is box ``n``; ``CRAFT_BOXES[t]`` is the box number of craft
    type ``t``. ``game.box_for_craft`` reads them; nothing here computes.

Invariants:
    - A box's four edges are inclusive: its width is ``right - left + 1``
      and its height ``bottom - top + 1``. The same box serves the five
      colored sheets and the grey one.
    - The boxes are copied, never measured from the pictures: some icons
      touch their neighbors and some boxes are looser than the art.
    - Craft types 106 and up have no entry: the game reads past its table
      there, and this package does not guess.

Call:
    ``box = BOXES[CRAFT_BOXES[craft_type]]; box.width, box.height``
"""

from __future__ import annotations

import logging
from typing import NamedTuple

logger = logging.getLogger(__name__)


class Box(NamedTuple):
    """One icon's place in a sheet, in sheet pixels, all four edges inclusive."""

    left: int
    top: int
    right: int
    bottom: int

    @property
    def width(self) -> int:
        """Return ``right - left + 1``; does not check that it is positive."""
        return self.right - self.left + 1

    @property
    def height(self) -> int:
        """Return ``bottom - top + 1``; does not check that it is positive."""
        return self.bottom - self.top + 1


# fmt: off
BOXES: tuple[Box, ...] = (
    Box(  6,   9,  13,  20),  # 0
    Box( 25,   8,  32,  20),  # 1
    Box( 44,  10,  50,  19),  # 2
    Box( 61,  10,  71,  19),  # 3
    Box( 82,  10,  89,  18),  # 4
    Box(101,  10, 108,  18),  # 5
    Box(118,  10, 128,  19),  # 6
    Box(139,   9, 145,  19),  # 7
    Box(157,  10, 166,  19),  # 8
    Box(175,   9, 185,  18),  # 9
    Box(196,   8, 202,  20),  # 10
    Box(215,   9, 221,  19),  # 11
    Box(234,  10, 241,  19),  # 12
    Box(251,  10, 261,  18),  # 13
    Box(270,  10, 280,  18),  # 14
    Box(289,   9, 299,  19),  # 15
    Box(  6,  29,  12,  42),  # 16
    Box( 25,  30,  31,  42),  # 17
    Box( 45,  31,  50,  41),  # 18
    Box( 63,  30,  70,  42),  # 19
    Box( 82,  30,  89,  42),  # 20
    Box(103,  34, 106,  39),  # 21
    Box(121,  33, 125,  40),  # 22
    Box(140,  31, 145,  41),  # 23
    Box(158,  32, 166,  40),  # 24
    Box(179,  32, 182,  40),  # 25
    Box(195,  32, 203,  40),  # 26
    Box(216,  33, 221,  40),  # 27
    Box(233,  29, 242,  43),  # 28
    Box(252,  30, 260,  41),  # 29
    Box(271,  30, 279,  42),  # 30
    Box(290,  29, 298,  44),  # 31
    Box(  5,  53,  14,  63),  # 32
    Box( 24,  53,  32,  64),  # 33
    Box( 44,  51,  50,  65),  # 34
    Box( 63,  51,  69,  65),  # 35
    Box( 83,  50,  87,  66),  # 36
    Box(100,  50, 108,  66),  # 37
    Box(120,  49, 126,  66),  # 38
    Box(140,  51, 145,  66),  # 39
    Box(158,  50, 164,  66),  # 40
    Box(177,  50, 183,  66),  # 41
    Box(196,  50, 203,  66),  # 42
    Box(214,  49, 222,  68),  # 43
    Box(234,  50, 240,  66),  # 44
    Box(253,  51, 259,  65),  # 45
    Box(271,  50, 279,  66),  # 46
    Box(289,  48, 299,  68),  # 47
    Box(  5,  77,  14,  83),  # 48
    Box( 26,  77,  31,  83),  # 49
    Box( 43,  78,  51,  83),  # 50
    Box( 63,  76,  69,  84),  # 51
    Box( 83,  76,  88,  84),  # 52
    Box( 98,  74, 111,  87),  # 53
    Box(115,  73, 131,  87),  # 54
    Box(138,  99, 146, 104),  # 55
    Box(157,  98, 165, 105),  # 56
    Box(176,  98, 184, 105),  # 57
    Box(197, 101, 202, 103),  # 58
    Box(216, 100, 221, 105),  # 59
    Box(234,  97, 240, 106),  # 60
    Box(253,  77, 259,  83),  # 61
    Box(272,  77, 278,  83),  # 62
    Box(288,  72, 301,  88),  # 63
    Box( 46,  95,  50, 108),  # 64
    Box( 24,  90,  32, 110),  # 65
    Box( 65,  97,  68, 105),  # 66
    Box( 79,  95,  93, 108),  # 67
    Box( 98,  95, 111, 108),  # 68
    Box(120,  94, 128, 108),  # 69
)
"""The 70 boxes, by number: left, top, right, bottom, all inclusive."""

CRAFT_BOXES: tuple[int, ...] = (
     0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  # 0-9
     0,  0,  9, 10, 11, 12, 13, 14, 15, 16,  # 10-19
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26,  # 20-29
    27,  0, 28, 29, 30, 31, 64, 32, 33, 33,  # 30-39
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43,  # 40-49
    44, 45, 46, 47, 65, 48, 49, 50, 51, 52,  # 50-59
    53, 53, 53, 53, 53, 53, 63, 61, 62, 54,  # 60-69
    55, 55, 55, 55, 55, 56, 57, 58, 66, 60,  # 70-79
    59, 59, 59, 60, 60, 58, 61, 61,  0,  0,  # 80-89
    67, 68, 69,  0,  0,  0,  0,  0,  0,  0,  # 90-99
    61, 61, 61, 61, 61, 61,  # 100-105
)
"""The box of each craft type 0 to 105, by craft type."""
# fmt: on
