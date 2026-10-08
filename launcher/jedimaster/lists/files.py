"""The lists of an install: where each kind lives, and the bitmaps they name.

Purpose:
    Find an install's list files by kind, name the menu file the game reads
    for each mission type and view, resolve a list's game path the way the
    game does (with or without Balance of Power), and read the width and
    height of the bitmaps an image list names.

Flow:
    ``list_files`` scans the install and ``BalanceOfPower/``: the six
    mission-type folders (menus by name, every other ``.lst`` of a
    tournament, battle or campaign folder a sequence) and the fixed lists
    of ``FIXED_LISTS``. ``menu_paths`` names each type's menu per view.
    ``resolve`` and ``sheet_file`` resolve a game path through
    ``jedimaster.install``. ``install_bitmaps`` reads BMP headers.

Invariants:
    - Only directory listings and BMP headers (the first 26 bytes) are read
      here, never a list's content.
    - A game path resolves in ``BalanceOfPower/`` first, any letter case,
      exactly as ``jedimaster.install.resolve_game_path`` does.

Call:
    ``list_files(install)["menu"]``; ``install_bitmaps(install, images)``
"""

from __future__ import annotations

import logging
import os
import struct
from pathlib import Path

from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..install import resolve_game_path
from .game import Bitmap
from .game import menu_game_path
from .game import MISSION_TYPES
from .game import VIEWS
from .model import ImageList
from .text import ListFormatError

logger = logging.getLogger(__name__)

LIST_SUFFIX = ".lst"
MENU_FILES = ("mission.lst", "rebel.lst", "imperial.lst")
FIXED_LISTS: dict[str, tuple[str, ...]] = {
    "images": (
        "frontres\\icons.lst",
        "frontres\\mapicons.lst",
        "frontres\\top.lst",
        "frontres\\side.lst",
        "frontres\\awards.lst",
        "frontres\\promo.lst",
    ),
    "ships": ("frontres\\frntspec.lst",),
    "sounds": ("sfx\\sfx.lst",),
    "cutscenes": ("movies\\cutscene.lst",),
    "awards": ("frontres\\campawds.lst",),
}
"""The game path of each fixed list, by kind."""

KINDS = ("menu", "sequence", "images", "ships", "sounds", "cutscenes", "awards")
BMP_HEADER = 26


def menu_paths() -> dict[str, dict[str, str]]:
    """Return each mission type's menu game path per view.

    ``{type: {view: game path}}`` for the six types and the three views, in
    the notes' table order. Does not look at an install.
    """
    return {t: {v: menu_game_path(t, v) for v in VIEWS} for t in MISSION_TYPES}


def kind_of(path: str | os.PathLike[str]) -> str | None:
    """Return the kind of list a file is, from its name and folder, or None.

    Menu file names in any folder are menus; the fixed lists' names are
    their kinds; any other ``.lst`` in a tournament, battle or campaign
    folder is a sequence. Returns None for anything else. Does not read the
    file.
    """
    path = Path(path)
    name = path.name.casefold()
    if name in MENU_FILES:
        return "menu"
    for kind, game_paths in FIXED_LISTS.items():
        if name in (p.rsplit("\\", 1)[-1] for p in game_paths):
            return kind
    folders = {
        MISSION_TYPES[t].folder.casefold() for t in ("tournament", "battle", "campaign")
    }
    if path.suffix.casefold() == LIST_SUFFIX and path.parent.name.casefold() in folders:
        return "sequence"
    return None


def _roots(install: Path) -> list[Path]:
    roots = [install]
    bop = child_in_any_case(install, BALANCE_OF_POWER)
    if bop is not None and bop.is_dir():
        roots.append(bop)
    return roots


def list_files(install: str | os.PathLike[str]) -> dict[str, list[Path]]:
    """Return the install's list files by kind, base install first.

    Keys are ``KINDS`` in order; each value lists the files found in the
    install and then in ``BalanceOfPower/``, each folder's files sorted by
    name. A kind with no file has an empty list. Does not read the files.
    """
    install = Path(install)
    found: dict[str, list[Path]] = {kind: [] for kind in KINDS}
    for root in _roots(install):
        for mission_type in MISSION_TYPES.values():
            folder = child_in_any_case(root, mission_type.folder)
            if folder is None or not folder.is_dir():
                continue
            for path in sorted(folder.iterdir(), key=lambda p: p.name):
                kind = kind_of(path)
                if path.is_file() and kind in ("menu", "sequence"):
                    found[kind].append(path)
        for kind, game_paths in FIXED_LISTS.items():
            for game_path in game_paths:
                path = _lookup(root, game_path)
                if path is not None and path.is_file():
                    found[kind].append(path)
    for kind, paths in found.items():
        logger.debug("%s: %d files", kind, len(paths))
    return found


def _lookup(root: Path, game_path: str) -> Path | None:
    current: Path | None = root
    for part in game_path.split("\\"):
        current = child_in_any_case(current, part) if current else None
    return current


def resolve(
    install: str | os.PathLike[str], game_path: str, balance_of_power: bool = True
) -> Path | None:
    """Return the file a game path names, or None when nothing matches.

    Resolves through ``jedimaster.install.resolve_game_path``:
    ``BalanceOfPower/`` first unless ``balance_of_power`` is False, any
    letter case. Raises ``ValueError`` for a path leaving the install. Does
    not check that the result is a list.
    """
    return resolve_game_path(install, game_path, balance_of_power)


def sheet_file(install: str | os.PathLike[str], game_path: str, found: Path) -> str:
    """Return the sheets' ``file`` label for a game path resolved to ``found``.

    The game path lowercased with forward slashes, behind ``BalanceOfPower/``
    when ``found`` lies in that folder. Does not resolve anything itself.
    """
    label = game_path.replace("\\", "/").lower()
    try:
        first = found.relative_to(Path(install)).parts[0]
    except (ValueError, IndexError):
        first = ""
    if first.casefold() == BALANCE_OF_POWER.casefold():
        return f"{BALANCE_OF_POWER}/{label}"
    return label


def read_bmp_size(path: str | os.PathLike[str]) -> tuple[int, int]:
    """Return a BMP file's width and height from its header.

    Reads only the first 26 bytes: the ``BM`` mark, then the info header's
    size, width and height (16-bit fields for the 12-byte core header,
    32-bit otherwise). A negative height (a top-down bitmap) is returned as
    its size. Raises ``ListFormatError`` when the bytes are not a BMP
    header, ``OSError`` when the file cannot be read. Does not read pixels.
    """
    with open(path, "rb") as handle:
        head = handle.read(BMP_HEADER)
    if len(head) < BMP_HEADER or head[:2] != b"BM":
        raise ListFormatError(f"{path}: not a BMP file")
    (info,) = struct.unpack_from("<I", head, 14)
    if info == 12:
        width, height = struct.unpack_from("<HH", head, 18)
    else:
        width, height = struct.unpack_from("<ii", head, 18)
    return width, abs(height)


def install_bitmaps(
    install: str | os.PathLike[str],
    images: ImageList,
    balance_of_power: bool = True,
) -> dict[str, Bitmap]:
    """Return the existing bitmaps an image list names, by word as written.

    Each bitmap word resolves as a game path; one that resolves to a file
    with a BMP header maps to its ``Bitmap`` (``compresses`` True: pixels
    are not read). A word that does not resolve, or whose file is not a
    BMP, is left out, so the game's view skips it as missing. Does not read
    pixels.
    """
    bitmaps: dict[str, Bitmap] = {}
    for group in images.groups:
        word = group.bitmap.text
        if word in bitmaps:
            continue
        try:
            path = resolve(install, word, balance_of_power)
        except ValueError:
            path = None
        if path is None or not path.is_file():
            logger.debug("bitmap missing: %s", word)
            continue
        try:
            width, height = read_bmp_size(path)
        except (ListFormatError, OSError) as exc:
            logger.warning("bitmap unreadable: %s", exc)
            continue
        bitmaps[word] = Bitmap(width, height)
        logger.debug("bitmap %s: %dx%d", word, width, height)
    logger.info("bitmaps: %d of %d groups found", len(bitmaps), len(images.groups))
    return bitmaps
