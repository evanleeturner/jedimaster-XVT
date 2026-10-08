"""Find an XvT install and resolve game paths inside it the way the game does.

Purpose:
    Locate an X-Wing vs. TIE Fighter install (a given folder, or the usual
    Steam and GOG folders on Linux and Windows), turn a game path such as
    ``TRAIN\\1TA01BF.TIE`` into a file on disk, and list an install's
    missions by folder.

Flow:
    ``find_install`` tries the given folder, then ``$JEDIMASTER_XVT``, then
    ``candidate_folders()``, returning the first that looks like an install.
    ``resolve_game_path`` normalises the path, then looks it up under
    ``BalanceOfPower/`` first and the install folder second, matching each
    part in any letter case. ``list_missions`` scans the Train, Combat and
    Melee folders of the install and of ``BalanceOfPower/``.

Invariants:
    - Backslashes in a game path are separators; empty and ``.`` parts are
      dropped; a ``..`` part or a drive/absolute path is refused, so a
      resolved path never leaves the install.
    - An exact-case match wins; otherwise the first case-insensitive match in
      sorted order is taken, so the result is deterministic on a
      case-sensitive file system with near-duplicate names.
    - Only directory listings are read here, never file contents.

Call:
    ``install = find_install(); path = resolve_game_path(install, r"TRAIN\\x.tie")``
"""

from __future__ import annotations

import logging
import os
import re
from pathlib import Path

logger = logging.getLogger(__name__)

BALANCE_OF_POWER = "BalanceOfPower"
MISSION_FOLDERS = ("Train", "Combat", "Melee")
MISSION_SUFFIX = ".tie"
ENV_VAR = "JEDIMASTER_XVT"
STEAM_FOLDER = "STAR WARS X-Wing vs TIE Fighter"
GOG_FOLDERS = ("Star Wars - XvT", "Star Wars - X-Wing vs TIE Fighter")


def candidate_folders(home: Path | None = None) -> list[Path]:
    """Return the usual Steam and GOG install folders, Linux then Windows.

    Always returns the same list for the same ``home`` (default: the user's
    home folder); does not check that any of them exists.
    """
    home = home if home is not None else Path.home()
    steam_roots = [
        home / ".local/share/Steam",
        home / ".steam/steam",
        home / ".steam/root",
        home / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
        Path("C:/Program Files (x86)/Steam"),
        Path("C:/Program Files/Steam"),
    ]
    found = [root / "steamapps/common" / STEAM_FOLDER for root in steam_roots]
    gog_roots = [
        home / "GOG Games",
        home / "Games",
        Path("C:/GOG Games"),
        Path("C:/Program Files (x86)/GOG Galaxy/Games"),
        Path("C:/Program Files/GOG Galaxy/Games"),
    ]
    found += [root / name for root in gog_roots for name in GOG_FOLDERS]
    return found


def _child(folder: Path, name: str) -> Path | None:
    """Return the entry of ``folder`` named ``name`` in any letter case."""
    exact = folder / name
    if exact.exists():
        return exact
    try:
        entries = sorted(os.listdir(folder))
    except OSError:
        return None
    wanted = name.casefold()
    for entry in entries:
        if entry.casefold() == wanted:
            return folder / entry
    return None


def is_install(folder: Path) -> bool:
    """Return True when ``folder`` holds a Train folder (any letter case).

    Returns False for a missing folder or a file. Does not check for the
    game's executable or any other file.
    """
    if not folder.is_dir():
        return False
    train = _child(folder, "Train")
    return train is not None and train.is_dir()


def find_install(given: str | os.PathLike[str] | None = None) -> Path | None:
    """Return the first folder that looks like an install, or None.

    Tries ``given`` (and only it, when given), else ``$JEDIMASTER_XVT``, else
    each of ``candidate_folders()``. Returns None when nothing matches. Does
    not check the version of the game or that Balance of Power is installed.
    """
    if given is not None:
        tries = [Path(given)]
    else:
        tries = [Path(os.environ[ENV_VAR])] if os.environ.get(ENV_VAR) else []
        tries += candidate_folders()
    for folder in tries:
        if is_install(folder):
            logger.info("install found: %s", folder)
            return folder
        logger.debug("not an install: %s", folder)
    logger.info("no install found among %d folders", len(tries))
    return None


def game_path_parts(game_path: str) -> list[str]:
    """Return the parts of a game path, backslashes treated as separators.

    Returns ``[]`` for an empty path. Raises ``ValueError`` for a path with a
    ``..`` part, a drive letter or a leading separator. Does not touch the
    file system.
    """
    text = game_path.replace("\\", "/")
    if text.startswith("/") or re.match(r"^[A-Za-z]:", text):
        raise ValueError(f"game path must be relative: {game_path!r}")
    parts = [p for p in text.split("/") if p not in ("", ".")]
    if ".." in parts:
        raise ValueError(f"game path may not leave the install: {game_path!r}")
    return parts


def _lookup(base: Path, parts: list[str]) -> Path | None:
    current = base
    for part in parts:
        found = _child(current, part)
        if found is None:
            return None
        current = found
    return current


def resolve_game_path(install: str | os.PathLike[str], game_path: str) -> Path | None:
    """Return the file or folder a game path names, or None when none exists.

    Looks under ``<install>/BalanceOfPower/`` first, then under
    ``<install>``; each part matches in any letter case. Raises
    ``ValueError`` for a path that is absolute or contains ``..``. Does not
    check that the result is a mission or readable.
    """
    install = Path(install)
    parts = game_path_parts(game_path)
    if not parts:
        return None
    bases = []
    bop = _child(install, BALANCE_OF_POWER)
    if bop is not None and bop.is_dir():
        bases.append(bop)
    bases.append(install)
    for base in bases:
        found = _lookup(base, parts)
        if found is not None:
            logger.debug("resolved %r -> %s", game_path, found)
            return found
    logger.debug("unresolved game path %r", game_path)
    return None


def list_missions(install: str | os.PathLike[str]) -> dict[str, list[Path]]:
    """Return the install's missions grouped by folder.

    Keys are folder paths relative to the install with forward slashes
    (``"Train"``, ``"BalanceOfPower/TRAIN"``), in that order: the install's
    Train, Combat, Melee, then Balance of Power's. Values are the ``.tie``
    files (any letter case) of that folder, sorted by name. A missing folder
    is left out. Does not read the files or look in other folders.
    """
    install = Path(install)
    roots = [install]
    bop = _child(install, BALANCE_OF_POWER)
    if bop is not None and bop.is_dir():
        roots.append(bop)
    result: dict[str, list[Path]] = {}
    for root in roots:
        for name in MISSION_FOLDERS:
            folder = _child(root, name)
            if folder is None or not folder.is_dir():
                continue
            files = sorted(
                (p for p in folder.iterdir() if p.suffix.casefold() == MISSION_SUFFIX),
                key=lambda p: p.name,
            )
            key = folder.relative_to(install).as_posix()
            result[key] = [p for p in files if p.is_file()]
            logger.debug("%s: %d missions", key, len(result[key]))
    return result
