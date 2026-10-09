"""Find the movies the game asks for, and the files it opens for each.

Purpose:
    List the movie names the game plays (``Opening``, each movie of the
    cutscene list in its order, ``Flyby1a``) and resolve each name to its
    ``movies\\NAME.smk`` and ``movies\\NAME.txt`` the game's way: Balance of
    Power first, then the base game, any letter case. A name that does not
    resolve is reported, not raised.

Flow:
    ``movie_names`` reads ``movies\\cutscene.lst`` through the lists reader
    and joins the three sources; ``find_movie`` resolves one name with
    ``install.resolve_game_path`` and builds the sheets' file labels.

Invariants:
    - The label of a resolved file is the install-relative path with the
      asked name's letter case for the folder and the file, a
      ``BalanceOfPower/`` prefix when Balance of Power supplied it, and
      forward slashes.
    - A name asked twice (in any letter case) is kept once, at its first
      place and spelling.
    - Only directory listings and the cutscene list are read here.

Call:
    ``for name in movie_names(install): movie = find_movie(install, name)``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path

from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..install import resolve_game_path
from ..lists import cutscenes_view
from ..lists import ListFormatError
from ..lists import read_cutscenes

logger = logging.getLogger(__name__)

OPENING = "Opening"
NETWORK_FALLBACK = "Flyby1a"
FOLDER = "movies"
VIDEO_SUFFIX = ".smk"
SUBTITLE_SUFFIX = ".txt"
CUTSCENE_LIST = "cutscene.lst"


@dataclass(frozen=True)
class MovieFiles:
    """Where one asked-for name resolved: video and subtitle file, or None.

    ``video_label`` and ``subtitle_label`` are the sheets' ``file`` values.
    """

    name: str
    video: Path | None
    video_label: str | None
    subtitles: Path | None
    subtitle_label: str | None


def _game_path(name: str, suffix: str) -> str:
    return f"{FOLDER}\\{name}{suffix}"


def _label(install: Path, found: Path, name: str, suffix: str, bop: bool) -> str:
    """Return the install-relative label of ``found`` in the asked name's case."""
    base = child_in_any_case(install, BALANCE_OF_POWER) if bop else None
    inside = base is not None and base in found.parents
    return ("BalanceOfPower/" if inside else "") + f"{FOLDER}/{name}{suffix}"


def _resolve(
    install: Path, name: str, suffix: str, bop: bool
) -> tuple[Path | None, str | None]:
    try:
        found = resolve_game_path(install, _game_path(name, suffix), bop)
    except ValueError as exc:
        logger.warning("movie name %r cannot be a game path: %s", name, exc)
        return None, None
    if found is None or not found.is_file():
        logger.debug("%s%s does not resolve", name, suffix)
        return None, None
    return found, _label(install, found, name, suffix, bop)


def find_movie(
    install: str | os.PathLike[str], name: str, balance_of_power: bool = True
) -> MovieFiles:
    """Return where ``name``'s video and subtitle file resolve.

    Each of the two is None when it does not resolve (a name that cannot be
    a game path included). With ``balance_of_power`` False the install is
    read as if Balance of Power were absent. Never raises for a missing
    file. Does not open the files.
    """
    install = Path(install)
    video, video_label = _resolve(install, name, VIDEO_SUFFIX, balance_of_power)
    subs, sub_label = _resolve(install, name, SUBTITLE_SUFFIX, balance_of_power)
    logger.debug("movie %s: video=%s subtitles=%s", name, video_label, sub_label)
    return MovieFiles(name, video, video_label, subs, sub_label)


def cutscene_movies(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> list[str]:
    """Return the movie names of the cutscene list, in the list's order.

    Returns ``[]`` when the list does not resolve or cannot be read (a
    WARNING says so for the second). Names the game would not keep (an
    entry cut short) and empty names are left out. Does not remove repeats.
    """
    path = resolve_game_path(install, _game_path("cutscene", ".lst"), balance_of_power)
    if path is None or not path.is_file():
        logger.info("no cutscene list under %s", install)
        return []
    try:
        view = cutscenes_view(read_cutscenes(path))
    except (ListFormatError, OSError) as exc:
        logger.warning("cannot read the cutscene list %s: %s", path, exc)
        return []
    return [c.movie for c in view.cutscenes if c.movie]


def movie_names(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> list[str]:
    """Return the names the game asks for, once each, in the game's order.

    ``Opening``, then the cutscene list's movies, then ``Flyby1a``; a repeat
    (in any letter case) is dropped. Does not check that any resolves.
    """
    names: list[str] = []
    seen: set[str] = set()
    asked = [OPENING, *cutscene_movies(install, balance_of_power), NETWORK_FALLBACK]
    for name in asked:
        if name.casefold() not in seen:
            seen.add(name.casefold())
            names.append(name)
    logger.info("%d movie names asked", len(names))
    return names
