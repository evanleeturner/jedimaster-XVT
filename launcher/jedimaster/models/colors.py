"""A texture's colors the way the 3D card path shows them, and its glow.

Purpose:
    Give a texture's top level as palette indices and as colors in any
    sub-palette, 565 colors as 8-bit channels, and which palette indices
    glow (drawn unlit over a face: lit windows, lights) and in what color.

Flow:
    ``rgb8`` widens one 565 color; ``texture_rgb`` maps the top level's
    indices through a sub-palette; ``glow_of`` applies the glow rule to a
    texture's palette and returns a ``Glow``.

Invariants:
    - A 565 color: red in the top 5 bits, green in the middle 6, blue in
      the low 5; widened as ``r8 = (r5 << 3) | (r5 >> 2)``, ``g8 = (g6 <<
      2) | (g6 >> 4)``, ``b8`` like ``r8``.
    - The glow rule compares 5-bit components (green's top five bits) of
      sub-palette 0 with sub-palettes 1 to 6; a glowing index takes
      sub-palette 10's color. A palette in which no index glows, or every
      index glows, gives no glow. A palette of fewer than 11 sub-palettes
      gives no glow.
    - Base colors are sub-palette 8's (``BASE_SUB_PALETTE``), or the last
      sub-palette of an inline palette that has no sub-palette 8.

Call:
    ``glow_of(texture).glows``; ``texture_rgb(texture, BASE_SUB_PALETTE)``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass

from .model import OptTexture

logger = logging.getLogger(__name__)

BASE_SUB_PALETTE = 8
GLOW_SUB_PALETTE = 10
SHADED_SUB_PALETTES = range(1, 7)
DARK_LIMIT = 32
"""An index whose sub-palette 0 color has a squared length below this is dark."""
SHADE_LIMIT = 16
"""An index that moves further than this between sub-palettes is shaded."""
NO_INDEX = 65535


@dataclass(frozen=True)
class Glow:
    """Which palette indices glow, their colors, and the sheets' counts.

    ``colors`` is the 565 glow color of each of the 256 indices, 0 for one
    that does not glow, and ``mask`` is True for each index that glows:
    both as the rule decides per index, even when the texture as a whole
    has no glow (``glows`` False: no index glows, or every one does).
    ``count`` is how many indices do not glow (0 when the texture has no
    glow); ``first`` the first of them, ``NO_INDEX`` when every one glows.
    """

    glows: bool
    colors: tuple[int, ...]
    count: int
    first: int
    mask: tuple[bool, ...]


def split565(color: int) -> tuple[int, int, int]:
    """Return a 565 color's red (5 bits), green (6 bits) and blue (5 bits).

    Does not check that ``color`` fits in 16 bits.
    """
    return (color >> 11) & 31, (color >> 5) & 63, color & 31


def rgb8(color: int) -> tuple[int, int, int]:
    """Return a 565 color as 8-bit red, green and blue.

    ``(r5 << 3) | (r5 >> 2)``, ``(g6 << 2) | (g6 >> 4)``, ``(b5 << 3) |
    (b5 >> 2)``: 0 stays 0 and full stays 255. Does not check the range.
    """
    red, green, blue = split565(color)
    return (
        (red << 3) | (red >> 2),
        (green << 2) | (green >> 4),
        (blue << 3) | (blue >> 2),
    )


def base_sub_palette(texture: OptTexture) -> int:
    """Return the sub-palette a texture's base colors come from.

    Sub-palette 8, or the last one an inline palette of fewer than nine
    holds. Does not check that the palette holds any.
    """
    return min(BASE_SUB_PALETTE, texture.sub_palettes - 1)


def texture_rgb(texture: OptTexture, sub_palette: int) -> bytes:
    """Return the top level's texels as 8-bit RGB, 3 bytes each, rows top first.

    Each texel's palette index is looked up in ``sub_palette``. Raises
    ``IndexError`` for a sub-palette the palette does not hold. Does not
    apply the glow.
    """
    table = [bytes(rgb8(c)) for c in texture.colors(sub_palette)]
    return b"".join(table[index] for index in texture.top)


def _five(color: int) -> tuple[int, int, int]:
    return (color >> 11) & 31, (color >> 6) & 31, color & 31


def _distance(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return sum((x - y) * (x - y) for x, y in zip(a, b, strict=True))


def _index_glows(index: int, tables: list[list[int]]) -> bool:
    """Return True when one palette index glows by the three-step rule."""
    color = _five(tables[0][index])
    if _distance(color, (0, 0, 0)) < DARK_LIMIT:
        return False
    return all(
        _distance(color, _five(tables[k][index])) <= SHADE_LIMIT
        for k in SHADED_SUB_PALETTES
    )


def glow_of(texture: OptTexture) -> Glow:
    """Return which indices of a texture's palette glow, and in what colors.

    No glow (count 0) when no index or every index glows; ``first`` is the
    first index that does not glow either way (``NO_INDEX`` when none),
    and ``colors`` and ``mask`` give each index's own result. A palette of
    fewer than 11 sub-palettes has no glow, no glowing index and ``first``
    0. Does not look at the texture's name or texels.
    """
    if texture.sub_palettes <= GLOW_SUB_PALETTE:
        return Glow(False, (0,) * 256, 0, 0, (False,) * 256)
    tables = [texture.colors(k) for k in range(GLOW_SUB_PALETTE + 1)]
    lit = tuple(_index_glows(i, tables) for i in range(256))
    dark = [i for i in range(256) if not lit[i]]
    first = dark[0] if dark else NO_INDEX
    colors = tuple(tables[GLOW_SUB_PALETTE][i] if lit[i] else 0 for i in range(256))
    if not dark or len(dark) == 256:
        logger.debug(
            "palette glows %s: no glow", "everywhere" if not dark else "nowhere"
        )
        return Glow(False, colors, 0, first, lit)
    logger.debug("palette: %d indices glow, first dark %d", 256 - len(dark), first)
    return Glow(True, colors, len(dark), first, lit)
