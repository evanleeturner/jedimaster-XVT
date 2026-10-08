"""The lists sub-package: read the game's text lists and derive its view.

Written from the game's list files and notes describing how the game reads
them, checked against the game's own reading.

Purpose:
    Read the text lists of X-Wing vs. TIE Fighter and Balance of Power
    (mission menus, sequence files, image lists, the ship list, the sound
    list, the cutscene list and the campaign medal list) keeping each file
    as written; derive what the game keeps from each; print that in the
    answer sheets' format; export the reading as JSON.

Flow:
    ``read_<kind>`` (reader) -> model dataclass -> ``<kind>_view`` (game)
    -> ``render_<kind>`` (render); ``list_to_json`` (to_json) for export;
    ``files`` finds the lists of an install and the bitmaps they name.

Invariants:
    - Nothing here reads files except the readers given a path and the
      helpers of ``files``.
    - Standard library only.

Call:
    ``from jedimaster.lists import read_menu, menu_view, render_menu``
"""

from __future__ import annotations

import logging

from .files import install_bitmaps
from .files import kind_of
from .files import list_files
from .files import menu_paths
from .game import awards_view
from .game import Bitmap
from .game import cutscenes_view
from .game import images_view
from .game import menu_game_path
from .game import menu_view
from .game import sequence_view
from .game import ships_view
from .game import sounds_view
from .reader import read_awards
from .reader import read_cutscenes
from .reader import read_images
from .reader import read_list
from .reader import read_menu
from .reader import read_sequence
from .reader import read_ships
from .reader import read_sounds
from .render import render_awards
from .render import render_cutscenes
from .render import render_images
from .render import render_menu
from .render import render_raw
from .render import render_sequence
from .render import render_ships
from .render import render_sounds
from .text import ListFormatError
from .text import NotTextError
from .to_json import list_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "Bitmap",
    "ListFormatError",
    "NotTextError",
    "awards_view",
    "cutscenes_view",
    "images_view",
    "install_bitmaps",
    "kind_of",
    "list_files",
    "list_to_json",
    "menu_game_path",
    "menu_paths",
    "menu_view",
    "read_awards",
    "read_cutscenes",
    "read_images",
    "read_list",
    "read_menu",
    "read_sequence",
    "read_ships",
    "read_sounds",
    "render_awards",
    "render_cutscenes",
    "render_images",
    "render_menu",
    "render_raw",
    "render_sequence",
    "render_ships",
    "render_sounds",
    "sequence_view",
    "ships_view",
    "sounds_view",
]
