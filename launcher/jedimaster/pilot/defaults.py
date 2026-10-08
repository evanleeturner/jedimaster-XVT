"""A new pilot's choices: what the game puts in a record before a lone .plt.

Purpose:
    Gather, from an install, the values the game writes into the full
    record when it loads a ``.plt`` with no ``.pl2``: the rating and its
    name, the text after the pilot's name in the network game's names,
    and the first training mission of the Rebel, Imperial and network
    training lists.

Flow:
    ``install_defaults`` resolves ``fronttxt.txt`` and the three training
    lists (Balance of Power first unless asked not to, any letter case),
    reads them with the package's text and list readers, and returns a
    ``Defaults``. ``Defaults`` can also be built by hand.

Invariants:
    - Menu entries are taken as the text reader gives them: ``No text.``
      for an entry past the file's count.
    - A list's first mission is the id of the first entry of the game's
      view of that list; a list with no entry gives 0.
    - Nothing here touches a pilot's files.

Call:
    ``defaults = install_defaults(install, balance_of_power=True)``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path

from ..lists import menu_game_path
from ..lists import menu_view
from ..lists import read_menu
from ..lists.files import resolve
from ..text import front_text
from ..text import read_front

logger = logging.getLogger(__name__)

NEW_RATING = 2
"""A new pilot's rating: trainee."""
RATING_NAME_ENTRY = 124
"""The menus' entry a new pilot's rating name comes from."""
GAME_NAME_ENTRY = 470
"""The menus' entry that follows the pilot's name in the network names."""
FRONT_FILE = "fronttxt.txt"
TRAINING = "training"
LIST_VIEWS = ("rebel", "imperial", "network")
"""The training lists a new pilot's first missions come from, in order."""


@dataclass(frozen=True)
class Defaults:
    """What the game writes before merging a lone ``.plt``.

    ``training`` is the first mission of the Rebel, Imperial and network
    training lists, in that order.
    """

    rating: int
    rating_name: bytes
    game_name_suffix: bytes
    training: tuple[int, int, int]


def _resolve(install: Path, game_path: str, balance_of_power: bool) -> Path:
    path = resolve(install, game_path, balance_of_power)
    if path is None or not path.is_file():
        raise FileNotFoundError(f"{install}: {game_path} not found")
    return path


def first_mission(install: Path, view: str, balance_of_power: bool) -> int:
    """Return the first mission id of one training list, as the game sees it.

    The list is the training menu of ``view`` (``rebel``, ``imperial`` or
    ``network``); the id is the first entry of its ``menu_view``, 0 when
    it keeps no entry. Raises ``FileNotFoundError`` when the list does not
    resolve and ``OSError`` or ``NotTextError`` when it cannot be read.
    Does not check that the mission exists.
    """
    game_path = menu_game_path(TRAINING, view)
    menu = read_menu(_resolve(install, game_path, balance_of_power))
    entries = menu_view(menu, TRAINING, view).entries
    if not entries:
        logger.info("%s keeps no entry: first mission 0", game_path)
        return 0
    logger.debug("%s: first mission %d", game_path, entries[0].id)
    return entries[0].id


def install_defaults(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> Defaults:
    """Return a new pilot's defaults as an install gives them.

    ``fronttxt.txt`` gives the rating name (entry 124) and the network
    names' suffix (entry 470); the three training lists give the first
    missions. Each file resolves in ``BalanceOfPower/`` first unless
    ``balance_of_power`` is False. Raises ``FileNotFoundError`` when a file
    does not resolve and ``OSError`` (or the list reader's errors) when
    one cannot be read. Does not check the texts' lengths.
    """
    install = Path(install)
    front = read_front(_resolve(install, FRONT_FILE, balance_of_power))
    defaults = Defaults(
        rating=NEW_RATING,
        rating_name=front_text(front, RATING_NAME_ENTRY),
        game_name_suffix=front_text(front, GAME_NAME_ENTRY),
        training=tuple(  # type: ignore[arg-type]
            first_mission(install, view, balance_of_power) for view in LIST_VIEWS
        ),
    )
    logger.info(
        "defaults: rating name %r, game name suffix %r, training %s",
        defaults.rating_name,
        defaults.game_name_suffix,
        defaults.training,
    )
    return defaults
