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
    front end; the caller sends them. The last mission shown is kept and
    sent to each page that connects later (``connect_pushes``). The launch and option commands live in
    ``launching``; a battle's ending reaches front ends through
    ``Control.subscribe``, with the debriefing of a flown battle, which is
    kept (``results``) for ``results.list`` and ``results.get``.

Invariants:
    - Only the commands of ``protocol.COMMANDS`` run; every other name
      is refused and logged at WARNING.
    - ``handle`` never raises for a request, however malformed.
    - A reply to a request whose id cannot be read carries id 0.

Call:
    ``outcome = Control(install, store, "0.1.0").handle(text)``
"""

from __future__ import annotations

import json
import logging
from collections.abc import Awaitable
from collections.abc import Callable
from dataclasses import dataclass
from dataclasses import field
from datetime import datetime
from datetime import UTC
from pathlib import Path
from typing import Any

from ..briefing import BriefingBuildError
from ..briefing import build_bundle
from ..briefing import mission_file
from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..install import resolve_game_path
from ..lists import menu_view
from ..lists import read_menu
from ..lists.game import menu_game_path
from ..lists.game import MISSION_TYPES
from ..lists.text import ListFormatError
from ..mission import MissionFormatError
from ..mission import read_mission
from .battles import Battles
from .battles import Flight
from .debrief import build_debrief
from .debrief import install_names
from .launching import Launching
from .launching import option_value_problem
from .protocol import COMMANDS
from .protocol import MAX_ID
from .protocol import MAX_MESSAGE_BYTES
from .protocol import MAX_PLAYER_DIGITS
from .protocol import MISSION_TYPE_NAMES
from .protocol import OPTION_NAMES
from .protocol import SCHEMA_REVISION
from .protocol import SETTINGS
from .protocol import UNKNOWN_ID
from .results import ResultStore
from .settings import SettingsStore
from .solo import side_problem

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


def _is_player(value: object) -> bool:
    return (
        isinstance(value, str)
        and value.isascii()
        and value.isdigit()
        and 1 <= len(value) <= MAX_PLAYER_DIGITS
    )


def _kind_and_side(args: Json) -> str | None:
    kind, side = args["mission_type"], args["side"]
    if not isinstance(kind, str) or kind not in MISSION_TYPE_NAMES:
        return f"no such mission type; one of {list(MISSION_TYPE_NAMES)}"
    return side_problem(kind, side)


def _menu_args(args: Json) -> str | None:
    if set(args) != {"mission_type", "side"}:
        return "arguments must be exactly mission_type and side"
    return _kind_and_side(args)


def _solo_args(args: Json) -> str | None:
    names = {"player", "mission_type", "side", "mission", "pilot"}
    if set(args) != names:
        return "arguments must be exactly player, mission_type, side, mission and pilot"
    if not _is_player(args["player"]):
        return "player must be an account id: 1 to 20 digits as text"
    problem = _kind_and_side(args)
    if problem is not None:
        return problem
    for name, limit in (("mission", 64), ("pilot", 12)):
        value = args[name]
        if not isinstance(value, str) or not 1 <= len(value) <= limit:
            return f"{name} must be text of 1 to {limit} characters"
    return None


def _cancel_args(args: Json) -> str | None:
    if set(args) != {"launch_id"}:
        return "arguments must be exactly launch_id"
    if not _is_int(args["launch_id"]) or not 1 <= args["launch_id"] <= MAX_ID:
        return "launch_id must be a whole number from 1 to 2147483647"
    return None


def _results_get_args(args: Json) -> str | None:
    if set(args) != {"result_id"}:
        return "arguments must be exactly result_id"
    if not _is_int(args["result_id"]) or not 1 <= args["result_id"] <= MAX_ID:
        return "result_id must be a whole number from 1 to 2147483647"
    return None


def _options_get_args(args: Json) -> str | None:
    if set(args) != {"player"}:
        return "arguments must be exactly player"
    if args["player"] is not None and not _is_player(args["player"]):
        return "player must be null or an account id: 1 to 20 digits as text"
    return None


def _options_set_args(args: Json) -> str | None:
    if set(args) != {"player", "name", "value"}:
        return "arguments must be exactly player, name and value"
    if not _is_player(args["player"]):
        return "player must be an account id: 1 to 20 digits as text"
    name = args["name"]
    if not isinstance(name, str) or name not in OPTION_NAMES:
        return f"no such option; one of {list(OPTION_NAMES)}"
    return option_value_problem(name, args["value"])


ARG_CHECKS: dict[str, Callable[[Json], str | None]] = {
    "hello": _hello_args,
    "install.status": _no_args,
    "missions.list": _no_args,
    "settings.get": _no_args,
    "settings.set": _set_args,
    "page.show_mission": _show_args,
    "briefing.get": _show_args,
    "launch.menu": _menu_args,
    "pilots.list": _no_args,
    "launch.solo": _solo_args,
    "launch.cancel": _cancel_args,
    "options.get": _options_get_args,
    "options.set": _options_set_args,
    "results.list": _no_args,
    "results.get": _results_get_args,
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
    """The launcher's state and the commands that read or change it."""

    def __init__(
        self,
        install: Path | None,
        store: SettingsStore,
        launcher_version: str,
        flight: Flight | None = None,
        results: ResultStore | None = None,
    ) -> None:
        """Hold the install found (or None), the settings, the version and the game program.

        Without ``flight`` no battle can start; without ``results`` the
        debriefings are kept in memory only. ``pages_open`` and
        ``host_player`` and ``host_pilot`` are set by the front end that serves
        the pages and by the bot; until then no page is open, no account is
        linked and no pilot is named.
        """
        self.install = install
        self.store = store
        self.launcher_version = launcher_version
        self.pages_open: Callable[[], int] = lambda: 0
        self.host_player: Callable[[], str | None] = lambda: None
        self.host_pilot: Callable[[], str | None] = lambda: None
        self.listeners: list[Callable[[Json], Awaitable[None]]] = []
        self.results = results if results is not None else ResultStore(None)
        self.battles = Battles(self._publish, keep=self._keep)
        self.launching = Launching(
            install,
            store,
            flight,
            self.battles,
            lambda: self.pages_open(),
            lambda: self.host_player(),
        )
        self.commands: dict[str, Callable[[Json], Json]] = {
            "hello": self._hello,
            "install.status": self._install_status,
            "missions.list": self._missions_list,
            "settings.get": self._settings_get,
            "settings.set": self._settings_set,
            "page.show_mission": self._show_mission,
            "briefing.get": self._briefing_get,
            "launch.menu": self.launching.menu,
            "pilots.list": self.launching.pilots,
            "launch.solo": self._launch_solo,
            "launch.cancel": self.launching.cancel,
            "options.get": self.launching.options_get,
            "options.set": self._options_set,
            "results.list": self._results_list,
            "results.get": self._results_get,
        }
        self._pushes: list[Json] = []
        self._shown: Json | None = None

    def subscribe(self, listener: Callable[[Json], Awaitable[None]]) -> None:
        """Send every later push that no request caused (a battle's ending) to ``listener``."""
        self.listeners.append(listener)

    async def _publish(self, push: Json) -> None:
        for listener in list(self.listeners):
            try:
                await listener(push)
            except Exception as exc:  # one failing front end must not stop the others
                logger.warning("a listener failed on %s: %s", push["event"], exc)

    def connect_pushes(self) -> list[Json]:
        """Return the pushes a page gets after ``status`` when it connects.

        The ``page.show_mission`` push of the last mission shown, if any, so
        a page opened after a pick in the bot shows the pick; then the
        ``launch.started`` push of the battle that is flying, if any, so a
        page opened late still shows it and can cancel it.
        """
        pushes = [self._shown] if self._shown is not None else []
        running = self.battles.running_push()
        return pushes + [running] if running is not None else pushes

    def _keep(self, battle: Json, result: Json) -> Json:
        """Build, keep and return the debriefing of a flown battle.

        Raises ``OSError`` when it cannot be kept, ``KeyError`` for a field
        missing from ``battle`` or ``result``.
        """
        names = install_names(self.install, battle["mission_type"], battle["mission"])
        ended_at = datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
        debrief = build_debrief(self.results.next_id(), ended_at, battle, result, names)
        self.results.add(debrief)
        return debrief

    def _results_list(self, args: Json) -> Json:
        return {"results": self.results.summaries()}

    def _results_get(self, args: Json) -> Json:
        return {"debrief": self.results.get(args["result_id"])}

    def _launch_solo(self, args: Json) -> Json:
        result, pushes = self.launching.solo(args)
        self._pushes.extend(pushes)
        return result

    def _options_set(self, args: Json) -> Json:
        result, pushes = self.launching.options_set(args)
        self._pushes.extend(pushes)
        return result

    def status_push(self) -> Json:
        """Return the ``status`` push a front end sends when one connects.

        It names the linked account's pilot (``host_pilot``), null when there
        is none.
        """
        return {
            "event": "status",
            "data": {
                "launcher_version": self.launcher_version,
                "install_found": self.install is not None,
                "pilot": self.host_pilot(),
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
                self._shown = {
                    "event": "page.show_mission",
                    "data": {
                        "mission_type": args["mission_type"],
                        "id": args["id"],
                        "title": entry["title"],
                    },
                }
                self._pushes.append(self._shown)
                return {"shown": True}
        logger.debug("no mission %d in the %s menu", args["id"], args["mission_type"])
        return {"shown": False}

    def _briefing_get(self, args: Json) -> Json:
        missing: Json = {"found": False, "bundle": None}
        if self.install is None:
            return missing
        path = mission_file(self.install, args["mission_type"], args["id"])
        if path is None:
            return missing
        try:
            bundle = build_bundle(
                self.install, read_mission(path), args["mission_type"]
            )
        except (
            MissionFormatError,
            BriefingBuildError,
            ListFormatError,
            OSError,
        ) as exc:
            logger.warning("cannot build the briefing of %s: %s", path, exc)
            return missing
        logger.debug("briefing of %s built", path)
        return {"found": True, "bundle": bundle}
