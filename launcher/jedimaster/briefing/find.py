"""Find the mission file a menu entry names, the way the page's show command does.

Purpose:
    Turn ``(mission type, id)`` into the mission file on disk: the type's
    network menu, the entry with that id, that entry's file in the type's
    folder, Balance of Power's folder first.

Flow:
    ``mission_file`` resolves the menu (``lists``), keeps its entries
    (``menu_view``), picks the entry by id and resolves its file.

Invariants:
    - Reads one menu and resolves one path; never reads the mission.
    - Every failure is a None with a log line, never an exception.

Call:
    ``path = mission_file(install, "training", 1)``
"""

from __future__ import annotations

import logging
from pathlib import Path

from ..install import resolve_game_path
from ..lists import ListFormatError
from ..lists import menu_game_path
from ..lists import menu_view
from ..lists import read_menu
from ..lists.game import sequence_game_path

logger = logging.getLogger(__name__)


def mission_file(install: Path, mission_type: str, ident: int) -> Path | None:
    """Return the mission file of entry ``ident`` in ``mission_type``'s network menu.

    Returns None (with a DEBUG or WARNING line) when the menu does not
    resolve or cannot be read, no entry has that id (the first entry with
    the id wins), the entry names no file, or the file does not exist.
    Raises ``ValueError`` for an unknown mission type. Does not check that
    the file is a mission, or that the entry is available.
    """
    menu_path = resolve_game_path(install, menu_game_path(mission_type, "network"))
    if menu_path is None or not menu_path.is_file():
        logger.debug("no menu for %s", mission_type)
        return None
    try:
        view = menu_view(read_menu(menu_path), mission_type, "network")
    except (ListFormatError, OSError) as exc:
        logger.warning("cannot read the menu %s: %s", menu_path, exc)
        return None
    for entry in view.entries:
        if entry.id != ident:
            continue
        if not entry.file:
            logger.debug("entry %d of %s names no file", ident, mission_type)
            return None
        try:
            game_path = sequence_game_path(mission_type, entry.file)
            found = resolve_game_path(install, game_path)
        except ValueError as exc:
            logger.warning("entry %d: %s", ident, exc)
            return None
        if found is None or not found.is_file():
            logger.debug("entry %d: file %s is missing", ident, entry.file)
            return None
        return found
    logger.debug("no entry %d in the %s menu", ident, mission_type)
    return None
