"""The pictures sub-package: the menu screens' pictures, as the game has them.

Written from the game's files and notes describing how the game reads
them, checked against the game's own reading.

Purpose:
    List every picture of the menu folders of X-Wing vs. TIE Fighter and
    Balance of Power, read each the way the game reads its bitmaps (the
    icon reader's ``read_bmp``), fingerprint its pixels and colors, print
    the list in the answer sheets' format, and export PNG pictures and
    JSON for a page.

Flow:
    ``game.picture_names`` builds the list; ``game.pictures_view`` resolves
    and reads it for a view; ``render`` prints; ``to_json`` describes the
    export; ``cli`` is the ``pictures`` command.

Invariants:
    - Standard library only; no game data in this sub-package's code.

Call:
    ``from jedimaster.pictures import pictures_view, render_pictures``
"""

from __future__ import annotations

import logging

from .game import colors_hash
from .game import fnv1a64
from .game import load_picture
from .game import name_order
from .game import Picture
from .game import picture_names
from .game import pictures_view
from .game import PicturesView
from .game import pixels_hash
from .game import read_picture
from .render import picture_line
from .render import render_pictures
from .to_json import pictures_schema
from .to_json import pictures_to_json
from .to_json import png_names

logger = logging.getLogger(__name__)

__all__ = [
    "Picture",
    "PicturesView",
    "colors_hash",
    "fnv1a64",
    "load_picture",
    "name_order",
    "picture_line",
    "picture_names",
    "pictures_schema",
    "pictures_to_json",
    "pictures_view",
    "pixels_hash",
    "png_names",
    "read_picture",
    "render_pictures",
]
