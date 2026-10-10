"""The control list at work: check one request, run its command, answer.

Purpose:
    Be the launcher's one control surface. Any front end (the page today, a
    chat bot later) hands a request's text to ``Control.handle`` and gets
    the reply and the pushes to send, whatever carries the bytes.

Flow:
    ``parse_request`` turns text into a ``ParsedRequest`` or a ``Refusal``
    (``bad_message``, ``unknown_command``, ``bad_arguments``); ``Control``
    runs the command from ``Control.commands`` and builds the reply dict.
    ``settings.set`` also builds the ``settings.changed`` push, and
    ``page.show_mission`` the ``page.show_mission`` push, for every open
    front end; the caller sends them.

Invariants:
    - Only the six commands of ``protocol.COMMANDS`` run; every other name
      is refused and logged at WARNING.
    - ``handle`` never raises for a request, however malformed.
    - A reply to a request whose id cannot be read carries id 0.

Call:
    ``outcome = Control(install, store, "0.1.0").handle(text)``
"""

from __future__ import annotations

import json
import logging
from collections.abc import Callable
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import Any

from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..install import resolve_game_path
from ..lists import menu_view
from ..lists import read_menu
from ..lists.game import menu_game_path
from ..lists.game import MISSION_TYPES
from ..lists.text import ListFormatError
from .protocol import COMMANDS
from .protocol import MAX_ID
from .protocol import MAX_MESSAGE_BYTES
from .protocol import MISSION_TYPE_NAMES
from .protocol import SCHEMA_REVISION
from .protocol import SETTINGS
from .protocol import UNKNOWN_ID
from .settings import SettingsStore

logger = logging.getLogger(__name__)

Json = dict[str, Any]


@dataclass
class ParsedRequest:
    """A request that passed every check."""

    id: int
    command: str
    args: Json


@dataclass
class Refusal:
    """A request that did not: the id to answer with, the code and the words."""

    id: int
    code: str
    message: str


@dataclass
class Outcome:
    """What a front end sends after one request: the reply, then the pushes."""

    reply: Json
    pushes: list[Json] = field(default_factory=list)


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _no_args(args: Json) -> str | None:
    return None if not args else "this command takes no arguments"


def _hello_args(args: Json) -> str | None:
    if set(args) != {"page_version"}:
        return "arguments must be exactly page_version"
    version = args["page_version"]
    if not isinstance(version, str) or not 1 <= len(version) <= 64:
        return "page_version must be text of 1 to 64 characters"
    return None


def _set_args(args: Json) -> str | None:
    if set(args) != {"name", "value"}:
        return "arguments must be exactly name and value"
    name, value = args["name"], args["value"]
    if not isinstance(name, str) or name not in SETTINGS:
        return f"no such setting; one of {list(SETTINGS)}"
    if not isinstance(value, str) or value not in SETTINGS[name]:
        return f"{name} must be one of {list(SETTINGS[name])}"
    return None


def _show_args(args: Json) -> str | None:
    if set(args) != {"mission_type", "id"}:
        return "arguments must be exactly mission_type and id"
    kind, ident = args["mission_type"], args["id"]
    if not isinstance(kind, str) or kind not in MISSION_TYPE_NAMES:
        return f"no such mission type; one of {list(MISSION_TYPE_NAMES)}"
    if not _is_int(ident) or not 0 <= ident <= MAX_ID:
        return "id must be a whole number from 0 to 2147483647"
    return None


ARG_CHECKS: dict[str, Callable[[Json], str | None]] = {
    "hello": _hello_args,
    "install.status": _no_args,
    "missions.list": _no_args,
    "settings.get": _no_args,
    "settings.set": _set_args,
    "page.show_mission": _show_args,
}
"""Each command's check of its arguments: None when fine, else the reason."""


def parse_request(text: str) -> ParsedRequest | Refusal:
    """Return the request ``text`` holds, or the refusal that answers it.

    Refuses with ``bad_message`` text that is too long, not JSON, not an
    object, or an object that is not exactly ``id``, ``command`` and
    ``args`` with an integer id from 1 to 2**31-1, a text command and an
    object ``args``; with ``unknown_command`` a command off the list; with
    ``bad_arguments`` arguments the command does not take. The refusal's id
    is the request's own when it could be read, else 0. Does not run
    anything.
    """
    if len(text.encode("utf-8")) > MAX_MESSAGE_BYTES:
        return Refusal(UNKNOWN_ID, "bad_message", "the message is too long")
    try:
        doc = json.loads(text)
    except ValueError:
        return Refusal(UNKNOWN_ID, "bad_message", "the message is not JSON")
    if not isinstance(doc, dict):
        return Refusal(UNKNOWN_ID, "bad_message", "the message is not an object")
    ident = doc.get("id")
    known = ident if _is_int(ident) and 1 <= ident <= MAX_ID else UNKNOWN_ID
    if set(doc) != {"id", "command", "args"}:
        return Refusal(known, "bad_message", "a request is exactly id, command, args")
    if known == UNKNOWN_ID:
        return Refusal(known, "bad_message", "id must be a whole number from 1")
    command, args = doc["command"], doc["args"]
    if not isinstance(command, str) or not isinstance(args, dict):
        return Refusal(known, "bad_message", "command is text and args an object")
    if command not in COMMANDS:
        return Refusal(known, "unknown_command", "no such command")
    problem = ARG_CHECKS[command](args)
    if problem is not None:
        return Refusal(known, "bad_arguments", problem)
    return ParsedRequest(known, command, args)


def _home_form(path: Path) -> str:
    home = Path.home()
    try:
        rest = path.resolve().relative_to(home.resolve())
    except ValueError:
        return str(path)
    return "~" if not rest.parts else f"~/{rest.as_posix()}"


class Control:
    """The launcher's state and the six commands that read or change it."""

    def __init__(
        self, install: Path | None, store: SettingsStore, launcher_version: str
    ) -> None:
        """Hold the install found (or None), the settings and the version."""
        self.install = install
        self.store = store
        self.launcher_version = launcher_version
        self.commands: dict[str, Callable[[Json], Json]] = {
            "hello": self._hello,
            "install.status": self._install_status,
            "missions.list": self._missions_list,
            "settings.get": self._settings_get,
            "settings.set": self._settings_set,
            "page.show_mission": self._show_mission,
        }
        self._pushes: list[Json] = []

    def status_push(self) -> Json:
        """Return the ``status`` push a front end sends when one connects."""
        return {
            "event": "status",
            "data": {
                "launcher_version": self.launcher_version,
                "install_found": self.install is not None,
            },
        }

    def handle(self, text: str) -> Outcome:
        """Run the request in ``text``; return its reply and any pushes.

        Always returns an ``Outcome``: an ``ok`` reply with the command's
        result, or an error reply for a refused request (logged at
        WARNING). Does not send anything and does not check who asked.
        """
        parsed = parse_request(text)
        if isinstance(parsed, Refusal):
            logger.warning(
                "request refused (id %d): %s: %s",
                parsed.id,
                parsed.code,
                parsed.message,
            )
            return Outcome(
                {
                    "id": parsed.id,
                    "ok": False,
                    "error": {"code": parsed.code, "message": parsed.message},
                }
            )
        self._pushes = []
        result = self.commands[parsed.command](parsed.args)
        logger.debug("request %d %s: ok", parsed.id, parsed.command)
        return Outcome({"id": parsed.id, "ok": True, "result": result}, self._pushes)

    def _hello(self, args: Json) -> Json:
        logger.debug("page version %s", args["page_version"])
        return {
            "launcher_version": self.launcher_version,
            "schema_revision": SCHEMA_REVISION,
        }

    def _install_status(self, args: Json) -> Json:
        if self.install is None:
            return {"found": False, "path": None, "balance_of_power": False}
        bop = child_in_any_case(self.install, BALANCE_OF_POWER)
        return {
            "found": True,
            "path": _home_form(self.install),
            "balance_of_power": bop is not None and bop.is_dir(),
        }

    def _missions_list(self, args: Json) -> Json:
        return {"menus": [self._menu(name) for name in MISSION_TYPES]}

    def _menu(self, mission_type: str) -> Json:
        game_path = menu_game_path(mission_type, "network")
        menu: Json = {
            "mission_type": mission_type,
            "game_path": game_path,
            "resolved": False,
            "entries": [],
        }
        found = resolve_game_path(self.install, game_path) if self.install else None
        if found is None or not found.is_file():
            logger.debug("menu %s does not resolve", game_path)
            return menu
        try:
            view = menu_view(read_menu(found), mission_type, "network")
        except (ListFormatError, OSError) as exc:
            logger.warning("cannot read the menu %s: %s", game_path, exc)
            return menu
        menu["resolved"] = True
        menu["entries"] = [
            {
                "section": e.section,
                "id": e.id,
                "available": e.available,
                "file": e.file,
                "title": e.title,
            }
            for e in view.entries
        ]
        return menu

    def _settings_get(self, args: Json) -> Json:
        return {"settings": self.store.get()}

    def _settings_set(self, args: Json) -> Json:
        self.store.set(args["name"], args["value"])
        result = {"settings": self.store.get()}
        self._pushes.append({"event": "settings.changed", "data": result})
        return result

    def _show_mission(self, args: Json) -> Json:
        menu = self._menu(args["mission_type"])
        for entry in menu["entries"]:
            if entry["id"] == args["id"]:
                self._pushes.append(
                    {
                        "event": "page.show_mission",
                        "data": {
                            "mission_type": args["mission_type"],
                            "id": args["id"],
                            "title": entry["title"],
                        },
                    }
                )
                return {"shown": True}
        logger.debug("no mission %d in the %s menu", args["id"], args["mission_type"])
        return {"shown": False}
