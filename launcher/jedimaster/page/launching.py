"""The launch and game-option commands of the control list.

Purpose:
    Answer ``launch.menu``, ``pilots.list``, ``launch.solo``,
    ``launch.cancel``, ``options.get`` and ``options.set``. A launch is only
    ever started here, from values checked again against the game's lists and
    pilot folder on this computer, whatever the sender claimed.

Flow:
    ``Launching`` holds the install, the settings store and the game program
    (``Flight``, or None). ``solo`` refuses, in plain words and in this
    order: no game program, a battle already flying, no page open, no install,
    a side that does not fit, a mission not on the solo list, a pilot the game
    does not have; otherwise it builds the description with the player's
    stored game options and starts the battle in the background. Each
    command returns its result and the pushes for the open pages.

Invariants:
    - A launch needs an open page, because the page is where a battle is seen
      and cancelled.
    - The mission is taken from the solo list and the pilot from the pilot
      folder, spelled as they are there; the sender's spelling is not used.
    - An easy cheat on a battle that is not a campaign is sent as easy, and
      the answer says so.
    - No path, program name or option of the engine comes from a request.

Call:
    ``result, pushes = Launching(install, store, flight, battles, pages).solo(args)``
"""

from __future__ import annotations

import logging
from collections.abc import Callable
from pathlib import Path
from typing import Any

from ..launch.model import build_description
from ..launch.options import for_description
from ..launch.options import OPTIONS
from ..launch.options import parse_value
from ..launch.options import text_of
from ..launch.pilots import list_pilots
from .battles import Battles
from .battles import Flight
from .protocol import OPTION_NAMES
from .settings import SettingsStore
from .solo import find
from .solo import solo_menu

logger = logging.getLogger(__name__)

Json = dict[str, Any]
Pushes = list[Json]

NO_ENGINE = "This launcher was started without the game program."
BUSY = "A battle is already flying. Cancel it on the page, or wait for it to end."
NO_PAGE = "Open the launcher's page first: it shows each battle with a Cancel button."
NO_INSTALL = "No game install was found."
NO_MISSION = "That mission is not in the game's list for this kind of battle."
NO_PILOT = "That pilot is not among the game's pilots."
NO_PILOT_FOLDER = "The launcher cannot read the game's pilot folder."
CANNOT_START = "The launcher could not start the battle just now."
NOT_FLYING = "No battle with that number is flying."


def options_json(stored: dict[str, Any]) -> Json:
    """Return the seven options as choice text, null for one not set."""
    return {name: text_of(name, stored.get(name)) for name in OPTION_NAMES}


class Launching:
    """The launch and option commands over one install, one store and one battle at a time."""

    def __init__(
        self,
        install: Path | None,
        store: SettingsStore,
        flight: Flight | None,
        battles: Battles,
        pages_open: Callable[[], int],
        host_player: Callable[[], str | None],
    ) -> None:
        """Hold what the commands read; ``pages_open`` and ``host_player`` are asked each time."""
        self.install = install
        self.store = store
        self.flight = flight
        self.battles = battles
        self.pages_open = pages_open
        self.host_player = host_player

    def menu(self, args: Json) -> Json:
        """Return the solo list of ``args``' kind and side (``SoloMenuResult``)."""
        return solo_menu(self.install, args["mission_type"], args["side"])

    def pilots(self, args: Json) -> Json:
        """Return the pilots the game has; ``found`` is false if the folder is unreadable."""
        folder = self.flight.pilot_folder if self.flight else None
        names = list_pilots(folder)
        return {"found": names is not None, "pilots": names or []}

    def _refused(self, reason: str) -> tuple[Json, Pushes]:
        logger.info("launch refused: %s", reason)
        return {"started": False, "launch_id": 0, "message": reason}, []

    def _problem(self, args: Json) -> str | None:
        if self.flight is None:
            return NO_ENGINE
        if self.battles.busy():
            return BUSY
        if self.pages_open() == 0:
            return NO_PAGE
        if self.install is None:
            return NO_INSTALL
        return None

    def solo(self, args: Json) -> tuple[Json, Pushes]:
        """Start one solo battle; return (``LaunchSoloResult``, the ``launch.started`` push).

        Refuses, with a reason and no push, in the order the module
        describes. Returns at once: the battle flies in the background.
        """
        problem = self._problem(args)
        if problem is not None:
            return self._refused(problem)
        assert self.flight is not None
        kind, side = args["mission_type"], args["side"]
        entry = find(solo_menu(self.install, kind, side), args["mission"])
        if entry is None:
            return self._refused(NO_MISSION)
        names = list_pilots(self.flight.pilot_folder)
        if names is None:
            return self._refused(NO_PILOT_FOLDER)
        pilot = next((n for n in names if n.lower() == args["pilot"].lower()), None)
        if pilot is None:
            return self._refused(NO_PILOT)
        settings, note = for_description(self.store.get_options(args["player"]), kind)
        description = build_description(
            kind,
            side,
            entry["file"],
            pilot,
            settings=settings,
            ai_program=self.flight.ai_program,
        )
        data = {
            "mission_type": kind,
            "side": side,
            "mission": entry["file"],
            "title": entry["title"],
            "pilot": pilot,
        }
        try:
            launch_id = self.battles.start(self.flight, description, data)
        except RuntimeError as exc:
            logger.error("cannot start the battle: %s", exc)
            return self._refused(CANNOT_START)
        push = {"event": "launch.started", "data": {"launch_id": launch_id, **data}}
        message = f"Started {entry['title']} as {pilot}. Watch it on the page."
        if note:
            message = f"{message} {note}"
        return {"started": True, "launch_id": launch_id, "message": message}, [push]

    def cancel(self, args: Json) -> Json:
        """Stop the battle ``args['launch_id']``; return ``LaunchCancelResult``."""
        if self.battles.cancel(args["launch_id"]):
            return {"cancelled": True, "message": "Cancelling the battle."}
        return {"cancelled": False, "message": NOT_FLYING}

    def options_get(self, args: Json) -> Json:
        """Return a player's game options; ``player`` null is the linked account."""
        player = args["player"] if args["player"] is not None else self.host_player()
        stored = self.store.get_options(player) if player is not None else {}
        return {"player": player, "options": options_json(stored)}

    def options_set(self, args: Json) -> tuple[Json, Pushes]:
        """Set one game option; return (``OptionsResult``, the ``options.changed`` push).

        A null value leaves the option as the game has it. The arguments were
        already checked against the table of options.
        """
        value = (
            None if args["value"] is None else parse_value(args["name"], args["value"])
        )
        self.store.set_option(args["player"], args["name"], value)
        result = {
            "player": args["player"],
            "options": options_json(self.store.get_options(args["player"])),
        }
        return result, [{"event": "options.changed", "data": result}]


def option_value_problem(name: str, value: object) -> str | None:
    """Return why ``value`` is not a choice of option ``name``, or None when it is."""
    if value is None:
        return None
    texts = [c.text for c in OPTIONS[name].choices]
    if not isinstance(value, str) or value not in texts:
        return f"{name} must be null or one of {texts}"
    return None
