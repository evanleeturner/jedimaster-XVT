"""The fonts sub-package: the game's menu fonts, as the game reads and draws them.

Written from the game's files and notes describing how the game reads and
draws them, checked against the game's own reading and drawing.

Purpose:
    Read the menu fonts of X-Wing vs. TIE Fighter (``times10.abp`` to
    ``times20.abp``) the way the game reads them, decode each glyph into
    runs of drawn and skipped pixels, draw and measure strings by the
    game's rules (its color bytes included), print it all in the answer
    sheets' format, and export glyph atlases and JSON for a page.

Flow:
    ``reader.read_font`` -> ``Font``; ``draw`` draws glyphs and strings and
    measures; ``colors`` holds the five text colors; ``game.fonts_view``
    resolves and reads an install's four fonts; ``render`` prints;
    ``to_json`` exports atlases and JSON; ``cli`` is the ``fonts`` command.

Invariants:
    - Standard library only.
    - The colors of ``colors`` are this sub-package's only game data.

Call:
    ``from jedimaster.fonts import read_font, draw_string, measure_string``
"""

from __future__ import annotations

import logging

from .colors import COLOR_CODES
from .colors import TEXT_COLORS
from .colors import TextColor
from .draw import draw_glyph
from .draw import draw_string
from .draw import measure_string
from .draw import Pixel
from .draw import StringDraw
from .game import FONT_FILES
from .game import FontFile
from .game import fonts_view
from .game import FontsView
from .reader import Font
from .reader import FontFormatError
from .reader import Glyph
from .reader import read_font
from .reader import Run
from .render import render_font
from .to_json import Atlas
from .to_json import atlas_png
from .to_json import font_atlas
from .to_json import fonts_schema
from .to_json import fonts_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "Atlas",
    "COLOR_CODES",
    "FONT_FILES",
    "Font",
    "FontFile",
    "FontFormatError",
    "FontsView",
    "Glyph",
    "Pixel",
    "Run",
    "StringDraw",
    "TEXT_COLORS",
    "TextColor",
    "atlas_png",
    "draw_glyph",
    "draw_string",
    "font_atlas",
    "fonts_schema",
    "fonts_to_json",
    "fonts_view",
    "measure_string",
    "read_font",
    "render_font",
]
