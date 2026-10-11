"""The solo lists: which missions a pilot of one kind and side may be offered.

Purpose:
    Read the game's own list for a kind of solo battle and side from the
    install, and say which of its missions a player may be offered. The
    launch command checks a mission against the same reading, whatever the
    bot sent.

Flow:
    ``side_problem`` says whether a side fits a kind. ``solo_menu`` reads
    ``<folder>\\mission.lst`` (or ``rebel.lst`` / ``imperial.lst`` for the
    kinds that take a side) and returns the offered entries in file order,
    one per mission file. ``find`` looks one up by file name.

Invariants:
    - A mission marked ``&`` is never offered; one marked ``*`` is offered
      with ``opens_once_flown`` true, because the pilot's flown missions
      decide whether the door opens it.
    - A file name that would break the launch description's pattern is not
      offered.
    - A tournament, battle or campaign entry is its sequence file.
    - Nothing here changes a file.

Call:
    ``solo_menu(install, "training", "rebel")["entries"]``
"""

from __future__ import annotations

import logging
import re
from pathlib import Path
from typing import Any

from ..install import resolve_game_path
from ..launch.model import SIDED_TYPES
from ..lists import menu_game_path
from ..lists import read_menu
from ..lists.model import MenuEntry
from ..lists.model import MenuSection
from ..lists.text import ascii_lower
from ..lists.text import ListFormatError

logger = logging.getLogger(__name__)

FILE_PATTERN = re.compile(r"^[A-Za-z0-9_.]{1,63}$")
Json = dict[str, Any]


def side_problem(mission_type: str, side: str) -> str | None:
    """Return a plain sentence if ``side`` does not fit ``mission_type``, else None.

    A training, combat or campaign battle takes ``rebel`` or ``imperial``;
    the others take ``none``.
    """
    if mission_type in SIDED_TYPES:
        if side not in ("rebel", "imperial"):
            return f"A {mission_type} battle needs a side, rebel or imperial."
    elif side != "none":
        return f"A {mission_type} battle takes no side."
    return None


def solo_menu(install: Path | None, mission_type: str, side: str) -> Json:
    """Return the solo list of ``mission_type`` and ``side`` as a result dict.

    ``resolved`` is false, with no entries, when there is no install, the
    list is not in it or cannot be read. Raises ``ValueError`` when the side
    does not fit the kind (``side_problem``). Does not check that a mission
    file exists.
    """
    problem = side_problem(mission_type, side)
    if problem is not None:
        raise ValueError(problem)
    view = side if mission_type in SIDED_TYPES else "network"
    game_path = menu_game_path(mission_type, view)
    menu: Json = {
        "mission_type": mission_type,
        "side": side,
        "game_path": game_path,
        "resolved": False,
        "entries": [],
    }
    found = resolve_game_path(install, game_path) if install else None
    if found is None or not found.is_file():
        logger.debug("solo list %s does not resolve", game_path)
        return menu
    try:
        items = read_menu(found).items
    except (ListFormatError, OSError) as exc:
        logger.warning("cannot read the solo list %s: %s", game_path, exc)
        return menu
    section = ""
    seen: set[str] = set()
    for item in items:
        if isinstance(item, MenuSection):
            section = item.name
        elif isinstance(item, MenuEntry) and item.complete:
            entry = _entry(item, section)
            if entry is None or entry["file"] in seen:
                continue
            seen.add(entry["file"])
            menu["entries"].append(entry)
    menu["resolved"] = True
    logger.debug("solo list %s: %d offered", game_path, len(menu["entries"]))
    return menu


def _entry(item: MenuEntry, section: str) -> Json | None:
    name = ascii_lower(item.file_name or "")
    if item.marker == "&":
        logger.debug("%r is marked & and never offered", name)
        return None
    if not FILE_PATTERN.match(name):
        logger.debug("%r cannot be launched and is not offered", name)
        return None
    assert item.title_line is not None
    return {
        "section": section,
        "file": name,
        "title": item.title_line.text,
        "opens_once_flown": item.marker == "*",
    }


def find(menu: Json, file: str) -> Json | None:
    """Return the entry of ``menu`` whose file is ``file`` (any letter case), or None."""
    for entry in menu["entries"]:
        if entry["file"] == file.lower():
            return entry
    return None
