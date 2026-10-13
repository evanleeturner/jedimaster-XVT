"""What the bot says back: words, menus, buttons and pushes, as plain data.

Purpose:
    Hold the shapes ``core`` answers with and the small helpers that cut a
    long list into the menus Discord allows, so ``core`` and the door share
    one definition and neither needs the other.

Flow:
    A ``Reply`` holds the words, the ``Picker`` menus, the ``Button`` s, the
    pushes for open pages and further replies to send as separate messages;
    or, instead of words, a ``Window`` to open, one text field to type in.
    ``menus`` cuts a list of ``Option`` s into pickers.

Invariants:
    - Nothing here knows Discord.
    - A picker holds 1 to 25 options and a reply at most 5 pickers, as one
      message may hold 5 rows.

Call:
    ``Reply("Showing it.", pickers=menus("mission", "melee", "Melee missions", options))``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from typing import Any

logger = logging.getLogger(__name__)

Json = dict[str, Any]

MENU_SIZE = 25
MENUS_PER_REPLY = 5
LABEL_LIMIT = 100


@dataclass(frozen=True)
class Option:
    """One line of a menu."""

    label: str
    value: str
    description: str = ""
    default: bool = False


@dataclass(frozen=True)
class Picker:
    """One menu to pick from.

    ``kind`` says which pick it is (``mission``, ``setting``, ``option``,
    ``missions_kind``, ``solo_kind``, ``solo_side``, ``solo_mission``,
    ``solo_pilot``) and ``key`` carries what was picked before it (the
    mission type, the setting's name, or the earlier steps joined by ``|``).
    """

    kind: str
    key: str
    placeholder: str
    options: tuple[Option, ...]


@dataclass(frozen=True)
class Button:
    """One button: its label, the kind of press and the picks it carries."""

    label: str
    kind: str
    key: str


@dataclass(frozen=True)
class Window:
    """A window with one text field: what it is for, its words, its start value."""

    kind: str
    title: str
    label: str
    value: str
    max_length: int


@dataclass
class Reply:
    """What to send back.

    ``text`` the words; ``pickers`` and ``buttons`` the controls under them;
    ``pushes`` for open pages; ``then`` further replies, each its own message;
    ``edit`` true when the reply replaces the message that was clicked;
    ``window`` a window to open instead of answering in words.
    """

    text: str
    pickers: tuple[Picker, ...] = ()
    pushes: list[Json] = field(default_factory=list)
    buttons: tuple[Button, ...] = ()
    then: tuple[Reply, ...] = ()
    edit: bool = False
    window: Window | None = None


def clip(text: str) -> str:
    """Return ``text`` cut to Discord's 100 characters, ending in ``...`` when cut."""
    return text if len(text) <= LABEL_LIMIT else text[: LABEL_LIMIT - 3] + "..."


def title_case(name: str) -> str:
    """Return ``name`` with its first letter capital."""
    return name[:1].upper() + name[1:]


def menus(
    kind: str, key: str, title: str, options: list[Option]
) -> tuple[tuple[Picker, ...], int]:
    """Cut ``options`` into pickers of 25, at most 5; return (pickers, how many were left out).

    Each placeholder reads ``<title> 1 to 25``. Returns no pickers for no
    options.
    """
    room = MENU_SIZE * MENUS_PER_REPLY
    pickers = tuple(
        Picker(
            kind,
            key,
            f"{title} {start + 1} to {start + len(chunk)}",
            tuple(chunk),
        )
        for start in range(0, min(len(options), room), MENU_SIZE)
        for chunk in [options[start : start + MENU_SIZE]]
    )
    return pickers, max(0, len(options) - room)
