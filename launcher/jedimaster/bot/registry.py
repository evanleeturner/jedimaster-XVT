"""The table of right-click items, and the slash command each one twins.

Purpose:
    One table, as data, of everything a player can do by right-clicking in
    Discord: the name Discord shows, the ``Core`` method it calls, where it
    may be used and its slash command. The door builds both the user command
    (right-click) and the slash command of each row from this table, so the
    two cannot drift apart.

Flow:
    ``ITEMS`` is the table. ``arguments`` turns a row, the asker and the one
    typed value (if the row takes one) into the call ``core.<method>(...)``.

Invariants:
    - Every row has a right-click name and a slash twin, and both call the
      same ``Core`` method with the same arguments.
    - The person a right-click is used on is not an input; the callback is
      given the asker only.
    - At most 15 rows (Discord allows 15 global user commands).
    - A row with ``where == "dm"`` is for a direct message only; any other
      is for a direct message or a server.
    - A row that reads files answers after Discord is told to wait
      (``defers``).

Call:
    ``for item in ITEMS: print(item.title, "->", item.slash)``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass

from ..page.protocol import MISSION_TYPE_NAMES

logger = logging.getLogger(__name__)

MAX_USER_COMMANDS = 15
DM = "dm"
ANYWHERE = "anywhere"


@dataclass(frozen=True)
class Param:
    """The one typed value a row takes.

    ``choices`` non-empty makes it a choice of those words; ``required``
    false lets the asker leave it out (the method then gets None).
    """

    name: str
    description: str
    choices: tuple[str, ...] = ()
    required: bool = True


@dataclass(frozen=True)
class Item:
    """One right-click item: its name, its ``Core`` method, its slash twin."""

    title: str
    method: str
    where: str
    slash: str
    summary: str
    param: Param | None = None
    defers: bool = False


ITEMS: tuple[Item, ...] = (
    Item("Single player", "solo", ANYWHERE, "solo", "Fly one solo battle"),
    Item(
        "Game options", "options", ANYWHERE, "options", "Show and set the game options"
    ),
    Item(
        "Launcher settings",
        "settings",
        DM,
        "settings",
        "Show and change the settings",
    ),
    Item(
        "Show mission",
        "missions",
        ANYWHERE,
        "missions",
        "List a kind of mission",
        Param("type", "The kind of mission", MISSION_TYPE_NAMES, required=False),
        defers=True,
    ),
    Item("Status", "status", ANYWHERE, "status", "Show the launcher's status"),
    Item(
        "Link",
        "link",
        DM,
        "link",
        "Link this launcher to your account",
        Param("code", "The code the launcher printed"),
    ),
    Item("Unlink", "unlink", DM, "unlink", "Unlink this launcher"),
)


def arguments(item: Item, user_id: int, in_dm: bool, value: str | None) -> tuple:
    """Return the arguments of ``core.<item.method>`` for this asker.

    The asker's id first, then the typed value when the row takes one, then
    whether the command came in a direct message. Does not call anything.
    """
    if item.param is None:
        return (user_id, in_dm)
    return (user_id, value, in_dm)
