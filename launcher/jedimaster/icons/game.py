"""The game's view of the briefing map's icon sheets, and its drawing rules.

Purpose:
    Load the icon sheets of an install the way the game does (the image list
    ``frontres\\mapicons.lst``, each bitmap it registers, Balance of Power
    first or not), keep each sheet's pixels and 16-bit colors, and draw one
    flight group's icon by the game's rules: the sheet for its IFF, the box
    for its craft type, the box centred on the point ``P``, index 0 skipped.

Flow:
    ``icons_view`` resolves and reads the list with the lists package
    (``read_images``, ``install_bitmaps``, ``images_view``), then reads each
    registered image the game keeps uncompressed with ``read_bmp`` into an
    ``IconSheet``. ``sheet_for_iff``, ``box_for_craft``, ``icon_origin`` and
    ``draw_icon`` are the drawing rules.

Invariants:
    - Only the list, the BMP headers of every bitmap it names, and the whole
      file of each uncompressed image are read; a compressed image (the
      map's background) keeps its table entry and no pixels.
    - ``IFF_SHEETS`` and ``SHEET_COLORS`` are the game's drawing rules,
      written here, not data read from a file; the boxes come from
      ``tables``.
    - A craft type without a table entry (106 and up) has no icon: no box,
      no draw, a WARNING; nothing is guessed.

Call:
    ``view = icons_view(install); draw_icon(view, iff=1, craft=5)``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path
from typing import NamedTuple

from ..lists import images_view
from ..lists import install_bitmaps
from ..lists import read_images
from ..lists.files import resolve
from ..lists.files import sheet_file
from ..lists.game import GameImages
from .bmp import BmpFile
from .bmp import BmpFormatError
from .bmp import read_bmp
from .tables import Box
from .tables import BOXES
from .tables import CRAFT_BOXES

logger = logging.getLogger(__name__)

LIST_GAME_PATH = "frontres\\mapicons.lst"
"""The image list that registers the icon sheets."""

IFF_SHEETS: dict[int, str] = {
    0: "mapicon0",
    1: "mapicon1",
    2: "mapicon2",
    3: "mapicon3",
    4: "mapicon1",
    5: "mapicon4",
}
"""The sheet each IFF draws with; any other IFF draws with ``OTHER_SHEET``."""

OTHER_SHEET = "mapicon0"

SHEET_COLORS: dict[str, str] = {
    "mapicon0": "green",
    "mapicon1": "red",
    "mapicon2": "blue",
    "mapicon3": "yellow",
    "mapicon4": "purple",
    "greyicon": "grey",
}
"""The color each sheet's icons are drawn in, by image name."""

TRANSPARENT = 0
"""The palette index an icon never draws."""


@dataclass
class IconSheet:
    """One registered image read whole: where it came from, pixels, colors."""

    name: str
    game_path: str
    path: Path
    file: str
    bitmap: BmpFile
    colors: list[int]


@dataclass
class IconView:
    """An install's icon sheets as the game loads them, for one view."""

    install: Path
    balance_of_power: bool
    list_path: Path
    list_file: str
    images: GameImages
    sheets: dict[str, IconSheet]


class IconPixel(NamedTuple):
    """One pixel an icon writes: offset from ``P``, palette index, 565 color."""

    dx: int
    dy: int
    index: int
    color: int


@dataclass
class IconDraw:
    """One flight group's icon: its sheet, box, and the pixels it writes."""

    iff: int
    craft: int
    sheet: str
    box: int
    x: int
    y: int
    pixels: list[IconPixel]


def color_565(red: int, green: int, blue: int) -> int:
    """Return an 8-bit color as the game's 16-bit 565 value.

    ``(red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)`` for every input;
    does not check that the channels lie in 0 to 255.
    """
    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)


def sheet_for_iff(iff: int) -> str:
    """Return the image name of the sheet an IFF draws with.

    ``IFF_SHEETS[iff]`` for 0 to 5, else ``OTHER_SHEET`` (any other value,
    out-of-byte values included). Does not check that the sheet is loaded.
    """
    return IFF_SHEETS.get(iff, OTHER_SHEET)


def box_for_craft(craft: int) -> int | None:
    """Return the box number of a craft type, or None when it has none.

    Returns ``CRAFT_BOXES[craft]`` for 0 to 105; for any other value logs a
    WARNING and returns None (the game reads past its table there). Does
    not check the box against a sheet.
    """
    if 0 <= craft < len(CRAFT_BOXES):
        return CRAFT_BOXES[craft]
    logger.warning("craft type %d has no icon: the game's table ends at 105", craft)
    return None


def icon_origin(box: Box) -> tuple[int, int]:
    """Return the box's top-left corner relative to ``P``.

    ``(-(width >> 1), -(height >> 1))``: the box is centred on ``P``,
    halves rounded down. Does not check that the box is well formed.
    """
    return -(box.width >> 1), -(box.height >> 1)


def _read_sheet(
    install: Path, image_name: str, game_path: str, balance_of_power: bool
) -> IconSheet | None:
    """Read one registered image whole; None (with a WARNING) when it fails."""
    path = resolve(install, game_path, balance_of_power)
    if path is None:
        logger.warning("sheet %s: %s no longer resolves", image_name, game_path)
        return None
    try:
        bitmap = read_bmp(path)
    except (BmpFormatError, OSError) as exc:
        logger.warning("sheet %s: cannot read %s: %s", image_name, path, exc)
        return None
    colors = [color_565(*entry) for entry in bitmap.palette]
    label = sheet_file(install, game_path, path)
    logger.debug("sheet %s: %s -> %s", image_name, game_path, path)
    return IconSheet(image_name, game_path, path, label, bitmap, colors)


def icons_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> IconView:
    """Return an install's icon sheets as the game loads them.

    Resolves ``LIST_GAME_PATH`` and each bitmap in ``BalanceOfPower/`` first
    (unless ``balance_of_power`` is False), any letter case; reads the list,
    derives the game's image table, and reads every uncompressed image of
    it whole. ``sheets`` holds those images by name in table order; one
    that cannot be read is left out with a WARNING. Raises
    ``FileNotFoundError`` when the list does not resolve, and the list
    reader's ``ListFormatError`` for an unreadable list. Does not check that
    the sheets are 320 by 200 or that the five icon sheets are present.
    """
    install = Path(install)
    list_path = resolve(install, LIST_GAME_PATH, balance_of_power)
    if list_path is None or not list_path.is_file():
        raise FileNotFoundError(f"{install}: {LIST_GAME_PATH} not found")
    images = read_images(list_path)
    table = images_view(images, install_bitmaps(install, images, balance_of_power))
    sheets: dict[str, IconSheet] = {}
    for image in table.images:
        if image.compressed:
            logger.debug("image %s is compressed: no pixels kept", image.name)
            continue
        sheet = _read_sheet(install, image.name, image.bitmap, balance_of_power)
        if sheet is not None:
            sheets[image.name] = sheet
    logger.info(
        "icon sheets: %d of %d images read, balance of power %s",
        len(sheets),
        len(table.images),
        balance_of_power,
    )
    label = sheet_file(install, LIST_GAME_PATH, list_path)
    return IconView(install, balance_of_power, list_path, label, table, sheets)


def draw_icon(view: IconView, iff: int, craft: int) -> IconDraw | None:
    """Return the pixels one flight group's icon writes, relative to ``P``.

    Takes the sheet for ``iff`` and the box for ``craft``; returns an
    ``IconDraw`` whose ``pixels`` are the box's pixels whose index is not 0,
    in row order, each in the sheet's 565 color (empty for a box of only
    index 0). Returns None, with a WARNING, when the craft type has no box,
    the sheet is not loaded, or the box does not lie inside the sheet. Does
    not work out ``P`` or clip to a screen.
    """
    number = box_for_craft(craft)
    if number is None:
        return None
    name = sheet_for_iff(iff)
    sheet = view.sheets.get(name)
    if sheet is None:
        logger.warning("iff %d: sheet %s is not loaded; no icon", iff, name)
        return None
    box = BOXES[number]
    bitmap = sheet.bitmap
    inside = 0 <= box.left <= box.right < bitmap.width
    if not (inside and 0 <= box.top <= box.bottom < bitmap.height):
        logger.warning("box %d lies outside sheet %s; no icon", number, name)
        return None
    x, y = icon_origin(box)
    pixels = []
    for row in range(box.height):
        start = (box.top + row) * bitmap.width + box.left
        for column, index in enumerate(bitmap.pixels[start : start + box.width]):
            if index != TRANSPARENT:
                color = sheet.colors[index]
                pixels.append(IconPixel(x + column, y + row, index, color))
    logger.debug("iff %d craft %d: box %d, %d pixels", iff, craft, number, len(pixels))
    return IconDraw(iff, craft, name, number, x, y, pixels)
