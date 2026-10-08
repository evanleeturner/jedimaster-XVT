"""The icons sub-package: the briefing map's craft icons, as the game has them.

Written from the game's icon files and notes describing how the game reads
and draws them, checked against the game's own reading and drawing.

Purpose:
    Read the icon sheets of X-Wing vs. TIE Fighter and Balance of Power the
    way the game reads them (8-bit bitmaps, RLE8 decoded into one flat
    buffer), hold the game's two icon tables, apply its drawing rules for
    one flight group's icon, print all of it in the answer sheets' format,
    and export it as JSON and PNG pictures for the briefing page.

Flow:
    ``bmp.read_bmp`` -> ``BmpFile``; ``game.icons_view`` reads the image
    list and its sheets into an ``IconView``; ``game.draw_icon`` draws one
    icon; ``render`` prints; ``to_json`` and ``png`` export; ``cli`` is the
    ``icons`` command; ``tables`` holds the boxes and the craft table.

Invariants:
    - Standard library only.
    - The two tables of ``tables`` are the only game data in this code.

Call:
    ``from jedimaster.icons import icons_view, draw_icon``
"""

from __future__ import annotations

import logging

from .bmp import BmpFile
from .bmp import BmpFormatError
from .bmp import read_bmp
from .game import box_for_craft
from .game import color_565
from .game import draw_icon
from .game import IconDraw
from .game import IconPixel
from .game import icons_view
from .game import IconSheet
from .game import IconView
from .game import sheet_for_iff
from .png import png_bytes
from .png import write_png
from .render import render_bmp
from .render import render_draws
from .render import render_sheets
from .render import render_tables
from .tables import Box
from .tables import BOXES
from .tables import CRAFT_BOXES
from .to_json import icons_schema
from .to_json import icons_to_json
from .to_json import picture_name

logger = logging.getLogger(__name__)

__all__ = [
    "BOXES",
    "Box",
    "BmpFile",
    "BmpFormatError",
    "CRAFT_BOXES",
    "IconDraw",
    "IconPixel",
    "IconSheet",
    "IconView",
    "box_for_craft",
    "color_565",
    "draw_icon",
    "icons_schema",
    "icons_to_json",
    "icons_view",
    "picture_name",
    "png_bytes",
    "read_bmp",
    "render_bmp",
    "render_draws",
    "render_sheets",
    "render_tables",
    "sheet_for_iff",
    "write_png",
]
