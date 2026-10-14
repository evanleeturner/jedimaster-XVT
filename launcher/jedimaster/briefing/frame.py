"""The menu frame pictures: the 1997 setup screen, whole, one per background.

Purpose:
    Build, from an install, the full 640 by 480 screen the Mission
    Selection draws behind its panels (a background, the pad, the activity
    frame) so the page can show its launcher inside the game's metal frame.

Flow:
    ``FrameStore.get(name)`` reads the background ``frontres\\<bg>.bmp``, the
    pad and the activity frame, lays them with ``under.screen_picture``,
    widens the pixels to a PNG and keeps the bytes. The route
    ``GET /art/frame/<name>`` serves it.

Invariants:
    - ``FRAME_NAMES`` is the whole set (twelve ``frame-<bg>.png``); any other
      name, or a store with no install, answers None.
    - The picture is composed by the same rule and helper the under-map
      picture crops from: index 0 is kept in the background and skipped in
      the pad and the frame. The setup screen's overlay, chat box and client
      list are left out.
    - A file that cannot be built is None, with a WARNING, and is tried
      again on the next request. A missing pad or frame leaves it out; a
      missing background is no picture.

Call:
    ``art = FrameStore(install).get("frame-gametr.png")``
"""

from __future__ import annotations

import logging
from pathlib import Path

from ..icons import BmpFile
from ..icons.bmp import BmpFormatError
from .art import Art
from .art import FRAME_FILE
from .art import PAD_FILE
from .art import read_layer
from .names import PNG_TYPE
from .under import SCREEN_HEIGHT
from .under import screen_picture
from .under import SCREEN_WIDTH
from .under import widened_png

logger = logging.getLogger(__name__)

BACKGROUNDS = (
    "gametr",
    "gameti",
    "gamecr",
    "gameci",
    "gamebr",
    "gamebi",
    "gamebn",
    "gamecn",
    "gamemn",
    "gamecar",
    "gamecai",
    "gametrn",
)
"""The twelve background files (``frontres\\<bg>.bmp``) the page can ask for."""
FRAME_PREFIX = "frame-"
FRAME_SUFFIX = ".png"


def frame_art_name(background: str) -> str:
    """Return the art name of ``background``: ``frame-gametr.png`` and so on.

    Does not check that ``background`` is one of ``BACKGROUNDS``.
    """
    return f"{FRAME_PREFIX}{background}{FRAME_SUFFIX}"


FRAME_ART = {frame_art_name(name): name for name in BACKGROUNDS}
"""Each frame picture's art name and its background file's stem."""
FRAME_NAMES: tuple[str, ...] = tuple(FRAME_ART)
"""Every name the frame route serves, twelve in all."""


def frame_picture(install: Path, background: str) -> bytes | None:
    """Return the PNG of the whole setup screen on background ``background``.

    Reads the background, the pad and the activity frame; None (with a
    WARNING from the reader) when the background cannot be read. A pad or
    frame that cannot be read is left out. Does not keep anything.
    """
    ground: BmpFile | None = read_layer(install, f"frontres\\{background}.bmp")
    if ground is None:
        logger.debug("frame picture %s: no background", background)
        return None
    pad = read_layer(install, PAD_FILE)
    frame = read_layer(install, FRAME_FILE)
    return widened_png(screen_picture(ground, pad, frame), SCREEN_WIDTH, SCREEN_HEIGHT)


class FrameStore:
    """The frame pictures of one install, each built once, on first request."""

    def __init__(self, install: Path | None) -> None:
        """Hold the install (or None: nothing is served)."""
        self.install = install
        self._kept: dict[str, Art] = {}

    def get(self, name: str) -> Art | None:
        """Return the picture called ``name``, or None.

        None for a name outside ``FRAME_NAMES``, with no install, or when
        it cannot be built (WARNING). A built picture is kept: the next
        request returns the same object.
        """
        if name not in FRAME_ART or self.install is None:
            return None
        if name in self._kept:
            return self._kept[name]
        try:
            png = frame_picture(self.install, FRAME_ART[name])
        except (BmpFormatError, OSError, ValueError) as exc:
            logger.warning("cannot build %s: %s", name, exc)
            return None
        if png is None:
            return None
        art = Art(png, PNG_TYPE)
        self._kept[name] = art
        logger.info("built %s: %d bytes", name, len(png))
        return art
