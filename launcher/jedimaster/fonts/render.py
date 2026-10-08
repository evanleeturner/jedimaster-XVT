"""Print a font's reading and drawing in the answer sheets' form.

Purpose:
    ``render_font`` prints a font as its sheet does: the header lines, the
    reader's fields, the color codes, every glyph drawn alone on a blank
    surface, then each sample string drawn and measured, so a rendering
    diffs empty against its sheet given the same sample strings.

Flow:
    Header (``kind font``, ``name``, ``file``), ``result``, the ``points``
    line, ``color_codes``; per glyph 0 to 255 its line and its rows of
    ``#`` (written) and ``.``; per sample its line and its rows of 565
    colors and ``----``.

Invariants:
    - Glyphs and strings are drawn at (0, 0) with base color ``0xffff``,
      as the sheets draw them.
    - A glyph prints over its width and height, or as far as any write
      reached, whichever is more; a string over columns 0 to ``right - 1``
      and rows 0 to ``bottom - 1``.
    - ``clip`` prints 0: these draws clip nothing.
    - The sample strings come from the caller, never from this package.

Call:
    ``render_font(font, "times10.abp", "times10.abp", [b"Pilot"])``
"""

from __future__ import annotations

import logging
from collections.abc import Iterable

from ..text.sheet import header
from ..text.sheet import join
from ..text.sheet import quote
from .colors import COLOR_CODES
from .draw import draw_glyph
from .draw import draw_string
from .draw import measure_string
from .draw import Pixel
from .reader import Font

logger = logging.getLogger(__name__)

BASE_COLOR = 0xFFFF
"""The color the sheets' draws begin with."""
RESULT = 1
"""What the game's font loader returns for a font it loaded."""
CLIP = 0
UNWRITTEN = "----"


def _extent(pixels: list[Pixel]) -> tuple[int, int]:
    """Return one past the rightmost and the lowest written pixel (0, 0 for none)."""
    if not pixels:
        return 0, 0
    return max(p.x for p in pixels) + 1, max(p.y for p in pixels) + 1


def glyph_lines(font: Font, code: int) -> list[str]:
    """Return one glyph's sheet lines: its line, then ``#``/``.`` rows.

    The glyph is drawn alone at (0, 0); rows and columns run over its
    height and width or as far as a write reached, whichever is more.
    Does not check the glyph against the font's height.
    """
    glyph = font.glyphs[code]
    pixels = draw_glyph(font, code, 0, 0, BASE_COLOR)
    right, bottom = _extent(pixels)
    width, height = max(glyph.width, right), max(glyph.height, bottom)
    grid = [["."] * width for _ in range(height)]
    for pixel in pixels:
        grid[pixel.y][pixel.x] = "#"
    lines = [
        f"glyph {code} width={glyph.width} height={glyph.height} "
        f"offset={glyph.offset} clip={CLIP} written={len(pixels)}"
    ]
    lines += [f"row {y} " + "".join(cells) for y, cells in enumerate(grid)]
    return lines


def sample_lines(font: Font, text: bytes) -> list[str]:
    """Return one sample string's sheet lines: its line, then color rows.

    The string is drawn from (0, 0) with base color ``0xffff``; each cell
    prints the last color written there as four hex digits, or ``----``.
    Does not clip to the 640 by 64 surface.
    """
    drawn = draw_string(font, text, BASE_COLOR)
    right, bottom = _extent(drawn.pixels)
    grid = [[UNWRITTEN] * right for _ in range(bottom)]
    for pixel in drawn.pixels:
        grid[pixel.y][pixel.x] = f"{pixel.color:04x}"
    lines = [
        f"text {quote(text)} measure={measure_string(font, text)} clip={CLIP} "
        f"written={len(drawn.pixels)} right={right} bottom={bottom}"
    ]
    lines += [f"row {y} " + " ".join(cells) for y, cells in enumerate(grid)]
    return lines


def render_font(font: Font, name: str, file: str, samples: Iterable[bytes] = ()) -> str:
    """Return a font's sheet: header, fields, colors, glyphs, samples.

    ``name`` is the game name and ``file`` the file it resolved to;
    ``samples`` are the strings to draw after the glyphs, in order (none by
    default). Does not check the font.
    """
    lines = header("font", name, file)
    lines.append(f"result {RESULT}")
    lines.append(
        f"points {font.points} in_use {font.in_use} spacing {font.spacing} "
        f"field_60a {font.spare} height {font.height}"
    )
    lines.append("color_codes " + " ".join(f"{c:04x}" for c in COLOR_CODES))
    for code in range(len(font.glyphs)):
        lines += glyph_lines(font, code)
    for text in samples:
        lines += sample_lines(font, text)
    return join(lines)
