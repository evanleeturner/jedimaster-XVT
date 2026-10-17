"""The side button sprites: the eight buttons down the Mission Selection's edge.

Purpose:
    Build, from an install, the small pictures the setup screen draws for
    its eight side buttons (each button's up and down look and every light
    piece beside them) so the page can show them over its menu frame.

Flow:
    ``SideStore.get(name)`` looks the sprite's name up in the install's
    image lists (``frontres\\icons.lst`` and ``frontres\\side.lst``, each
    read from ``BalanceOfPower/`` first, then the base install), resolves
    the bitmap file the line names, decodes it the game's way and widens it
    with ``under.sprite_png``. The route ``GET /art/side/<name>.png`` serves
    it.

Invariants:
    - ``SIDE_NAMES`` is the whole set (48 names); any other name, or a store
      with no install, answers None.
    - A sprite is at its own size, drawn by the page at (0, 0) of the
      640 by 480 frame; index 0 is clear and every other pixel is widened
      as the frame's pixels are.
    - A list line is ``<file> <name> <flag>``; lines starting with ``//``
      and trailing spaces are ignored; the list's first line is skipped, as
      the game skips it.
    - A file that cannot be built is None, with a WARNING, and is tried
      again on the next request.

Call:
    ``art = SideStore(install).get("game1u.png")``
"""

from __future__ import annotations

import logging
from pathlib import Path

from ..icons import read_bmp
from ..icons.bmp import BmpFormatError
from ..lists import ListFormatError
from ..lists import read_images
from ..lists.files import resolve
from .art import Art
from .names import PNG_TYPE
from .under import sprite_png

logger = logging.getLogger(__name__)

SIDE_LISTS = ("frontres\\icons.lst", "frontres\\side.lst")
"""The image lists that name the side sprites, in the order they are asked."""
COMMENT = b"//"
TYPE_SPRITES = tuple(
    f"game{slot}{look}" for slot in (1, 2, 3, 4, 5, 8) for look in ("u", "d")
)
SIDE_SPRITES = tuple(f"reg{slot}{look}" for slot in (6, 7) for look in ("u", "d"))
ACTIVE_SPRITES = tuple(f"active{slot}" for slot in range(1, 9))
LIT_PIECES = (
    "1litb",
    "2lita",
    "2litb",
    "3lita",
    "3litb",
    "4lita",
    "4litb",
    "5lita",
    "5litb",
    "6lita",
    "7litb",
    "8lita",
)
"""The light pieces that exist; each is drawn in the on (1) or off (0) form."""
LIT_SPRITES = tuple(f"{piece}{state}" for piece in LIT_PIECES for state in (0, 1))
SIDE_SPRITE_NAMES = (
    *TYPE_SPRITES,
    *SIDE_SPRITES,
    *ACTIVE_SPRITES,
    *LIT_SPRITES,
)
SIDE_SUFFIX = ".png"
SIDE_ART = {f"{name}{SIDE_SUFFIX}": name for name in SIDE_SPRITE_NAMES}
"""Each side picture's art name and its name in the image lists."""
SIDE_NAMES: tuple[str, ...] = tuple(SIDE_ART)
"""Every name the side route serves, 48 in all."""


def list_bytes(path: Path) -> bytes:
    """Return the list at ``path`` without its ``//`` comment lines.

    The first line stays (the reader skips it whatever it holds). Does not
    check the list's content.
    """
    lines = path.read_bytes().splitlines(keepends=True)
    kept = [
        line
        for at, line in enumerate(lines)
        if at == 0 or not line.lstrip().startswith(COMMENT)
    ]
    return b"".join(kept)


def sprite_files(install: Path, name: str) -> list[str]:
    """Return the bitmap words the install's lists give ``name``, in order.

    Each list of ``SIDE_LISTS`` is read as found in ``BalanceOfPower/`` and
    then as found in the base install (a file found twice is read once); the words come in that order, so the
    first is the one the game uses. A list that is missing or unreadable is
    skipped (DEBUG, or WARNING when unreadable). Does not check the files.
    """
    words: list[str] = []
    seen: set[Path] = set()
    for game_path in SIDE_LISTS:
        for balance_of_power in (True, False):
            found = resolve(install, game_path, balance_of_power)
            if found is None or not found.is_file():
                logger.debug(
                    "%s: no list (balance of power %s)", game_path, balance_of_power
                )
                continue
            if found in seen:
                logger.debug("%s: the same list again", found)
                continue
            seen.add(found)
            try:
                groups = read_images(list_bytes(found)).groups
            except (ListFormatError, OSError) as exc:
                logger.warning("cannot read %s: %s", found, exc)
                continue
            words += [g.bitmap.text for g in groups if g.name.text == name]
    logger.debug("side sprite %s: files %s", name, words)
    return words


def side_picture(install: Path, name: str) -> bytes | None:
    """Return the PNG of the side sprite ``name``, or None.

    None, with a WARNING, when no list names it or the file it names is
    missing or cannot be read. Does not keep anything.
    """
    for word in sprite_files(install, name):
        path = resolve(install, word)
        if path is None or not path.is_file():
            logger.warning("%s: %s not found", install, word)
            continue
        return sprite_png(read_bmp(path))
    logger.warning("side sprite %s: no list line with a file", name)
    return None


class SideStore:
    """The side sprites of one install, each built once, on first request."""

    def __init__(self, install: Path | None) -> None:
        """Hold the install (or None: nothing is served)."""
        self.install = install
        self._kept: dict[str, Art] = {}

    def get(self, name: str) -> Art | None:
        """Return the picture called ``name``, or None.

        None for a name outside ``SIDE_NAMES``, with no install, or when it
        cannot be built (WARNING). A built picture is kept: the next request
        returns the same object.
        """
        if name not in SIDE_ART or self.install is None:
            return None
        if name in self._kept:
            return self._kept[name]
        try:
            png = side_picture(self.install, SIDE_ART[name])
        except (BmpFormatError, ListFormatError, OSError, ValueError) as exc:
            logger.warning("cannot build %s: %s", name, exc)
            return None
        if png is None:
            return None
        art = Art(png, PNG_TYPE)
        self._kept[name] = art
        logger.info("built %s: %d bytes", name, len(png))
        return art
