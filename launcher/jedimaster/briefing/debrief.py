"""The debriefing screen's pictures: 32 backgrounds, the tab and button sprites, two fonts.

Purpose:
    Build, from an install, the pictures the page draws the 1997 debriefing
    with: each whole 640 by 480 background (with the frame layers the game
    draws over it), the small sprites of its tabs, its Leave button and its
    side badge, and the two fonts its numbers are drawn in, so the page can
    show the debriefing in the game's look.

Flow:
    ``DebriefStore.get(name)`` reads the background ``frontres\\<stem>.bmp``
    and lays on it, as ``under.screen_picture`` lays the setup screen, the
    image lists' ``frame`` and ``alloff`` pictures, then shades the
    ``regoverlay`` picture over the result; a sprite is the bitmap the
    image lists name, found by ``side.sprite_bitmap``; a font is its atlas
    (``times12.png``, ``times15.png``) and ``fonts.json`` holds both fonts'
    metrics. The route ``GET /art/debrief/<name>`` serves them.

Invariants:
    - ``DEBRIEF_NAMES`` is the whole set (32 backgrounds, 44 sprites, two
      atlases and ``fonts.json``); any other name, or a store with no
      install, answers None.
    - The image lists are the five the game loads at start, each read from
      ``BalanceOfPower/`` first, then the base install; the first line that
      names a sprite and has its file wins. A list's ``//`` lines are
      ignored.
    - A background keeps index 0; every layer on it skips index 0; the
      overlay averages with the pixel under it, channel by channel.
    - A missing ``frame``, ``alloff`` or ``regoverlay`` is left out; a
      missing background is no picture.
    - A file that cannot be built is None, with a WARNING, and is tried
      again on the next request.

Call:
    ``art = DebriefStore(install).get("debcwi.png")``
"""

from __future__ import annotations

import json
import logging
from dataclasses import asdict
from pathlib import Path

from ..fonts import atlas_png
from ..fonts import font_atlas
from ..icons import BmpFile
from ..icons import color_565
from ..icons.bmp import BmpFormatError
from ..lists import ListFormatError
from .art import Art
from .art import read_game_font
from .art import read_layer
from .build import font_data
from .names import HEADING_FONT_NAME
from .names import PNG_TYPE
from .side import sprite_bitmap
from .under import SCREEN_HEIGHT
from .under import screen_picture
from .under import SCREEN_WIDTH
from .under import shade
from .under import sprite_png
from .under import TRANSPARENT
from .under import widened_png

logger = logging.getLogger(__name__)

DEBRIEF_LISTS = (
    "frontres\\top.lst",
    "frontres\\side.lst",
    "frontres\\awards.lst",
    "frontres\\promo.lst",
    "frontres\\icons.lst",
)
"""The image lists the game loads at start, in the order it loads them."""
FIFTEEN_NAME = "times15.abp"
BACKGROUND_STEMS = (
    *(f"debca{result}{side}" for result in "lw" for side in "ir"),
    *(f"debtr{result}{side}" for result in "wbl" for side in "ir"),
    *(f"debt{result}{side}" for result in "wb" for side in "ir"),
    *(f"debm{result}{side}" for result in "wbl" for side in "ir"),
    *(f"deb{seq}{result}{side}" for seq in "bc" for result in "wlt" for side in "ir"),
)
"""The 32 background files (``frontres\\<stem>.bmp``) the game's code names."""
TAB_SPRITES = tuple(
    f"{stem}{look}"
    for stem in ("debrief1", "debrief2", "debrief4", "deb4")
    for look in ("u", "d")
)
BUTTON_SPRITES = ("leaveup", "leavedown", "flyup", "flydown")
BADGE_FRAMES = 16
BADGE_SPRITES = tuple(
    f"{side}tiny{frame}" for side in ("imp", "reb") for frame in range(BADGE_FRAMES)
)
SPRITE_NAMES = (*TAB_SPRITES, *BUTTON_SPRITES, *BADGE_SPRITES)
PNG_SUFFIX = ".png"
FONT_ATLASES = {"times12.png": HEADING_FONT_NAME, "times15.png": FIFTEEN_NAME}
"""Each font atlas's art name and the game file it is made from."""
FONTS_NAME = "fonts.json"
JSON_TYPE = "application/json"
FONT_KEYS = {HEADING_FONT_NAME: "font12", FIFTEEN_NAME: "font15"}
BACKGROUND_ART = {f"{stem}{PNG_SUFFIX}": stem for stem in BACKGROUND_STEMS}
SPRITE_ART = {f"{name}{PNG_SUFFIX}": name for name in SPRITE_NAMES}
DEBRIEF_NAMES: tuple[str, ...] = (
    *BACKGROUND_ART,
    *SPRITE_ART,
    *FONT_ATLASES,
    FONTS_NAME,
)
"""Every name the debriefing route serves, 79 in all."""


def lay_over(screen: list[int], layer: BmpFile) -> None:
    """Shade ``layer`` over ``screen`` in place: index 0 clear, the rest averaged.

    Only the part inside the 640 by 480 screen counts. Does not check the
    layer's size.
    """
    colors = [color_565(*entry) for entry in layer.palette]
    columns = min(SCREEN_WIDTH, layer.width)
    for row in range(min(SCREEN_HEIGHT, layer.height)):
        start = row * layer.width
        base = row * SCREEN_WIDTH
        for column, index in enumerate(layer.pixels[start : start + columns]):
            if index != TRANSPARENT:
                screen[base + column] = shade(screen[base + column], colors[index])


def named_layer(install: Path, name: str) -> BmpFile | None:
    """Return the bitmap the lists give ``name``, or None when it is not there.

    A bitmap that cannot be read is left out with a WARNING, so one bad
    layer never costs the whole screen.
    """
    try:
        return sprite_bitmap(install, name, DEBRIEF_LISTS)
    except (BmpFormatError, OSError) as exc:
        logger.warning("cannot read the %s picture: %s", name, exc)
        return None


def background_picture(install: Path, stem: str) -> bytes | None:
    """Return the PNG of the debriefing screen on background ``stem``.

    The background, then ``frame`` and ``alloff`` (index 0 skipped), then
    ``regoverlay`` shaded over. None (with a WARNING from the reader) when
    the background cannot be read. Does not keep anything.
    """
    ground = read_layer(install, f"frontres\\{stem}.bmp")
    if ground is None:
        logger.debug("debriefing background %s: no file", stem)
        return None
    screen = screen_picture(
        ground, named_layer(install, "frame"), named_layer(install, "alloff")
    )
    overlay = named_layer(install, "regoverlay")
    if overlay is not None:
        lay_over(screen, overlay)
    return widened_png(screen, SCREEN_WIDTH, SCREEN_HEIGHT)


def fonts_json(install: Path) -> bytes | None:
    """Return fonts 12 and 15's metrics as JSON, or None when neither is readable.

    The keys are ``font12`` and ``font15``; a font that cannot be read is
    left out (WARNING from the reader). Does not keep anything.
    """
    found = {}
    for game_name, key in FONT_KEYS.items():
        font = read_game_font(install, game_name)
        if font is not None:
            found[key] = asdict(font_data(font, game_name))
    if not found:
        return None
    return json.dumps(found, sort_keys=True).encode("ascii")


class DebriefStore:
    """The debriefing pictures of one install, each built once, on first request."""

    def __init__(self, install: Path | None) -> None:
        """Hold the install (or None: nothing is served)."""
        self.install = install
        self._kept: dict[str, Art] = {}

    def get(self, name: str) -> Art | None:
        """Return the file called ``name``, or None.

        None for a name outside ``DEBRIEF_NAMES``, with no install, or when
        it cannot be built (WARNING). A built file is kept: the next request
        returns the same object.
        """
        if name not in DEBRIEF_NAMES or self.install is None:
            return None
        if name in self._kept:
            return self._kept[name]
        try:
            art = self._build(self.install, name)
        except (BmpFormatError, ListFormatError, OSError, ValueError) as exc:
            logger.warning("cannot build %s: %s", name, exc)
            return None
        if art is None:
            return None
        self._kept[name] = art
        logger.info("built %s: %d bytes", name, len(art.data))
        return art

    def _build(self, install: Path, name: str) -> Art | None:
        if name in BACKGROUND_ART:
            png = background_picture(install, BACKGROUND_ART[name])
        elif name in SPRITE_ART:
            bitmap = sprite_bitmap(install, SPRITE_ART[name], DEBRIEF_LISTS)
            png = None if bitmap is None else sprite_png(bitmap)
        elif name in FONT_ATLASES:
            font = read_game_font(install, FONT_ATLASES[name])
            png = None if font is None else atlas_png(font_atlas(font))
        else:
            data = fonts_json(install)
            return None if data is None else Art(data, JSON_TYPE)
        return None if png is None else Art(png, PNG_TYPE)
