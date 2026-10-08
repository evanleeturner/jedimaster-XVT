"""Export the fonts as JSON data and glyph atlases, and describe the JSON.

Purpose:
    Give the page that will draw the game's words each font as a picture
    of all its glyphs (an atlas: each glyph's pixels opaque white,
    everything else fully transparent) and the JSON that places each glyph
    in it, with the font's height, spacing and text colors; and publish
    the JSON Schema (draft 2020-12) the export always satisfies.

Flow:
    ``font_atlas`` draws every glyph with ``draw_glyph`` into a grid of
    ``COLUMNS`` cells per row; ``atlas_png`` writes it with the shared
    ``rgba_png``; ``fonts_to_json`` describes the view; ``fonts_schema``
    builds the schema with the text export's helpers.

Invariants:
    - Glyph ``c`` sits in cell ``(c % 16, c // 16)``; every cell is as wide
      and as tall as the widest and tallest glyph (or draw, when a draw
      reaches further), at least 1 pixel.
    - An atlas pixel is (255, 255, 255, 255) where the glyph's draw writes,
      else (0, 0, 0, 0). The page tints the glyphs with the text's color.
    - The output holds only dicts, lists, strings, integers, booleans and
      null.

Call:
    ``write_atlas(path, font)``; ``fonts_to_json(fonts_view(install))``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import PurePath
from typing import Any

from ..icons.png import rgba_png
from ..text.to_json import closed_object
from ..text.to_json import ints
from ..text.to_json import nullable
from .colors import RESET_BYTE
from .colors import TEXT_COLORS
from .draw import draw_glyph
from .draw import Pixel
from .game import FontFile
from .game import FontsView
from .reader import Font

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/fonts.schema.json"
JSON_FORMAT = "jedimaster.fonts"
JSON_FORMAT_VERSION = 1
COLUMNS = 16
INK = b"\xff\xff\xff\xff"
INK_COLOR = 0xFFFF


@dataclass
class Atlas:
    """A font's glyphs in one RGBA picture: size, cells, places, pixels."""

    width: int
    height: int
    cell_width: int
    cell_height: int
    places: list[tuple[int, int]]
    rgba: bytes


def font_atlas(font: Font) -> Atlas:
    """Return every glyph drawn into a grid of 16 cells per row.

    A cell is as wide as the widest glyph (or draw) and as tall as the
    tallest, at least 1 by 1; glyph ``c`` is drawn at its cell's top-left
    corner, ``places[c]``. Does not trim empty cells.
    """
    draws: list[list[Pixel]] = [
        draw_glyph(font, code, 0, 0, INK_COLOR) for code in range(len(font.glyphs))
    ]
    reach_x = [p.x + 1 for pixels in draws for p in pixels]
    reach_y = [p.y + 1 for pixels in draws for p in pixels]
    cell_width = max([1, *font.widths, *reach_x])
    cell_height = max([1, *font.heights, *reach_y])
    rows = -(-len(draws) // COLUMNS)
    width, height = COLUMNS * cell_width, rows * cell_height
    rgba = bytearray(4 * width * height)
    places = []
    for code, pixels in enumerate(draws):
        left = (code % COLUMNS) * cell_width
        top = (code // COLUMNS) * cell_height
        places.append((left, top))
        for pixel in pixels:
            at = 4 * ((top + pixel.y) * width + left + pixel.x)
            rgba[at : at + 4] = INK
    logger.debug("atlas %dx%d, cells %dx%d", width, height, cell_width, cell_height)
    return Atlas(width, height, cell_width, cell_height, places, bytes(rgba))


def atlas_png(atlas: Atlas) -> bytes:
    """Return the atlas as a PNG file's bytes (8-bit RGBA).

    Raises ``ValueError`` for an empty atlas (never, for 1 by 1 cells).
    Does not keep the glyph places: the JSON gives them.
    """
    return rgba_png(atlas.width, atlas.height, atlas.rgba)


def write_atlas(path: str | os.PathLike[str], font: Font) -> Atlas:
    """Write a font's atlas PNG to ``path``; return the atlas.

    Raises ``OSError`` when the file cannot be written. Does not create
    the folder.
    """
    atlas = font_atlas(font)
    with open(path, "wb") as handle:
        handle.write(atlas_png(atlas))
    logger.debug("atlas %s written", path)
    return atlas


def atlas_name(name: str) -> str:
    """Return the PNG file name of a font's atlas: its game name's stem.

    ``times10.abp`` -> ``times10.png``. Does not check for collisions.
    """
    return f"{PurePath(name.lower()).stem}.png"


def text_colors() -> list[dict[str, Any]]:
    """Return the color bytes 2 to 6: byte, name and 565 value each.

    Always the same list, from ``colors.TEXT_COLORS``. Does not give 8-bit
    channels: the game keeps these colors as 565 values only.
    """
    return [
        {"byte": c.byte, "name": c.name, "color": c.color} for c in TEXT_COLORS.values()
    ]


def _font(font_file: FontFile) -> dict[str, Any]:
    data: dict[str, Any] = {
        "kind": font_file.kind,
        "name": font_file.name,
        "file": font_file.file,
        "error": None if font_file.error is None else str(font_file.error),
    }
    font = font_file.font
    if font is None:
        keys = ("points", "in_use", "spacing", "height", "picture", "atlas")
        data.update(dict.fromkeys(keys))
        data.update(text_colors=None, glyphs=None)
        return data
    atlas = font_atlas(font)
    glyphs = [
        {"code": c, "x": x, "y": y, "width": font.widths[c], "height": font.heights[c]}
        for c, (x, y) in enumerate(atlas.places)
    ]
    data.update(
        points=font.points,
        in_use=font.in_use,
        spacing=font.spacing,
        height=font.height,
        picture=atlas_name(font_file.name),
        atlas={
            "width": atlas.width,
            "height": atlas.height,
            "columns": COLUMNS,
            "cell_width": atlas.cell_width,
            "cell_height": atlas.cell_height,
        },
        text_colors=text_colors(),
        glyphs=glyphs,
    )
    return data


def fonts_to_json(view: FontsView) -> dict[str, Any]:
    """Return the view as JSON-ready data: dicts, lists and scalars.

    Always returns ``format``, ``format_version``, ``balance_of_power``,
    ``reset_byte`` and ``fonts`` (the four, in order; a font that could
    not be read keeps its ``error`` and null values). Does not write the
    atlases.
    """
    fonts = [_font(font_file) for font_file in view.fonts.values()]
    logger.debug("exported %d fonts", len(fonts))
    return {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": view.balance_of_power,
        "reset_byte": RESET_BYTE,
        "fonts": fonts,
    }


def fonts_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``fonts_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (for
    example, that a glyph lies inside its atlas).
    """
    glyph = closed_object(
        "One glyph: its code, its cell's top-left corner in the atlas, size.",
        {
            **ints("code", maximum=255),
            **ints("x", "y"),
            **ints("width", "height", maximum=255),
        },
    )
    color = closed_object(
        "A color byte of a string and the 16-bit 565 color it switches to.",
        {
            **ints("byte", minimum=2, maximum=6),
            "name": {"type": "string"},
            **ints("color", maximum=0xFFFF),
        },
    )
    atlas = closed_object(
        "The atlas picture: size, cells per row, cell size.",
        {
            **ints("width", "height", minimum=1),
            "columns": {"const": COLUMNS},
            **ints("cell_width", "cell_height", minimum=1),
        },
    )
    number = {"type": "integer", "minimum": 0}
    font = closed_object(
        "One menu font: where it came from, its fields, atlas and glyphs.",
        {
            "kind": {"type": "string"},
            "name": {"type": "string"},
            "file": {"type": "string"},
            "error": nullable({"type": "string"}),
            "points": nullable(number),
            "in_use": nullable({**number, "maximum": 255}),
            "spacing": nullable({**number, "maximum": 255}),
            "height": nullable({**number, "maximum": 255}),
            "picture": nullable({"type": "string", "pattern": r"^[a-z0-9_.-]+\.png$"}),
            "atlas": nullable({"$ref": "#/$defs/Atlas"}),
            "text_colors": nullable(
                {
                    "type": "array",
                    "items": {"$ref": "#/$defs/TextColor"},
                    "minItems": len(TEXT_COLORS),
                    "maxItems": len(TEXT_COLORS),
                }
            ),
            "glyphs": nullable(
                {
                    "type": "array",
                    "items": {"$ref": "#/$defs/Glyph"},
                    "minItems": 256,
                    "maxItems": 256,
                }
            ),
        },
    )
    top = closed_object(
        "The game's menu fonts as glyph atlases (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            "reset_byte": {"const": RESET_BYTE},
            "fonts": {"type": "array", "items": {"$ref": "#/$defs/Font"}},
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP menu fonts (jedimaster export)",
        **top,
        "$defs": {"Atlas": atlas, "Font": font, "Glyph": glyph, "TextColor": color},
    }
