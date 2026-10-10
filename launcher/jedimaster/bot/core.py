"""Every decision of the bot: who may ask, what each command sends, what the reply says.

Purpose:
    Turn a Discord command into requests on the launcher's fixed list, and
    the answers into a plain reply. Nothing here knows Discord: a command is
    a method with the asker's account id, a reply is plain data, so the
    decisions can be tested without Discord and without a network.

Flow:
    A method (``link``, ``status``, ``missions``, ``pick_mission``,
    ``settings``, ``pick_setting``, ``unlink``) first checks the asker
    (``Linker``), then builds each request as JSON text and hands it to
    ``Control.handle`` through ``send``, so the page's own checks apply. It
    returns a ``Reply``: the words, the menus to pick from and the pushes
    for the open pages. The caller sends the pushes and then the words
    (``delivered_text`` adds a note when no page was open).

Invariants:
    - Only the linked account is answered; any other gets ``HOST_ONLY``
      (``/link`` has its own answers) and a WARNING.
    - ``/link``, ``/settings`` and ``/unlink`` are for a direct message only.
    - Every request goes through ``send``; a name off the fixed list comes
      back as ``unknown_command`` from ``Control``, never runs.
    - No reply holds the link code or the token.
    - This module imports nothing from discord and nothing from the web
      server.

Call:
    ``core = Core(control, linker); reply = core.status(user_id)``
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from dataclasses import field
from typing import Any

from ..page.control import Control
from ..page.control import Outcome
from ..page.protocol import MAX_ID
from ..page.protocol import MISSION_TYPE_NAMES
from . import link as linking
from .link import Linker

logger = logging.getLogger(__name__)

Json = dict[str, Any]

HOST_ONLY = "This launcher answers its host only."
DM_ONLY = "Send this command in a direct message to the bot."
NO_PAGE = "No page is open, so nothing changed on screen."
REFUSED = "The launcher refused that request."
BOT_VERSION_NAME = "bot"
MENU_SIZE = 25
MENUS_PER_REPLY = 5
LABEL_LIMIT = 100

LINK_WORDS = {
    linking.LINKED: "Linked. This launcher answers this account only.",
    linking.ALREADY_LINKED: "This launcher is already linked.",
    linking.WRONG: "That code is not right. Check the one on the launcher's terminal.",
    linking.EXPIRED: (
        "That code has expired (a code lasts 10 minutes). "
        "Restart the launcher for a new one."
    ),
    linking.VOID: (
        "That code is void after too many wrong tries. "
        "Restart the launcher for a new one."
    ),
    linking.NO_CODE: "There is no code to use. Restart the launcher for a new one.",
}

SETTING_WORDS: dict[str, tuple[str, dict[str, tuple[str, str]]]] = {
    "art_scaling": (
        "Art scaling",
        {
            "whole_pixels": (
                "Whole pixels",
                "Sharp pixels; the window may show borders.",
            ),
            "engine_fit": (
                "Engine fit",
                "Fitted to the window the way the game does it.",
            ),
            "sharp_bilinear": (
                "Sharp bilinear",
                "Whole steps, then smoothed a little.",
            ),
        },
    )
}
"""Each setting's name in words, and each value's name and one plain sentence."""


@dataclass(frozen=True)
class Option:
    """One line of a menu."""

    label: str
    value: str
    description: str = ""
    default: bool = False


@dataclass(frozen=True)
class Picker:
    """One menu to pick from: ``kind`` is ``mission`` or ``setting``, ``key`` the
    mission type or the setting's name."""

    kind: str
    key: str
    placeholder: str
    options: tuple[Option, ...]


@dataclass
class Reply:
    """What to send back: the words, the menus and the pushes for open pages."""

    text: str
    pickers: tuple[Picker, ...] = ()
    pushes: list[Json] = field(default_factory=list)


def _clip(text: str) -> str:
    return text if len(text) <= LABEL_LIMIT else text[: LABEL_LIMIT - 3] + "..."


def _title_case(name: str) -> str:
    return name[:1].upper() + name[1:]


class Core:
    """The bot's decisions over one ``Control`` and one ``Linker``."""

    def __init__(self, control: Control, linker: Linker, first_id: int = 1) -> None:
        """Hold the launcher's control and the link; request ids count from ``first_id``."""
        self.control = control
        self.linker = linker
        self._next_id = first_id
        self._ids: list[int] = []

    # ---- the request path ---------------------------------------------

    def send(self, command: str, args: Json) -> Outcome:
        """Send one request through ``Control.handle``; return its outcome.

        The request is JSON text with the next id from the counter (it
        starts again at 1 after 2**31-1). A command off the fixed list or
        wrong arguments come back as an error reply; this never raises for
        them. Does not check who is asking.
        """
        ident = self._next_id
        self._next_id = 1 if ident >= MAX_ID else ident + 1
        self._ids.append(ident)
        text = json.dumps({"id": ident, "command": command, "args": args})
        return self.control.handle(text)

    def _result(self, outcome: Outcome) -> Json | None:
        if outcome.reply.get("ok") is True:
            result = outcome.reply["result"]
            return result if isinstance(result, dict) else None
        logger.warning("the launcher refused a request from the bot")
        return None

    # ---- who may ask --------------------------------------------------

    def _begin(self, command: str, user_id: int) -> None:
        self._ids = []
        logger.debug("command %s from account %d", command, user_id)

    def _end(self, command: str, user_id: int, outcome: str) -> None:
        logger.debug(
            "command %s from account %d: requests %s, %s",
            command,
            user_id,
            self._ids,
            outcome,
        )

    def _refused(self, command: str, user_id: int, reason: str, text: str) -> Reply:
        logger.warning("refused %s from account %d: %s", command, user_id, reason)
        self._end(command, user_id, "refused")
        return Reply(text)

    def _gate(
        self, command: str, user_id: int, in_dm: bool, dm_only: bool
    ) -> Reply | None:
        if self.linker.linked_user_id != user_id:
            return self._refused(command, user_id, "not the linked account", HOST_ONLY)
        if dm_only and not in_dm:
            return self._refused(command, user_id, "not a direct message", DM_ONLY)
        return None

    # ---- the commands -------------------------------------------------

    def link(self, user_id: int, code: str, in_dm: bool) -> Reply:
        """Answer ``/link``: the outcome of using ``code`` from account ``user_id``.

        Returns a reply that never holds the code. Off a direct message the
        code is not looked at (and so not spent) and the reply says to use
        a direct message.
        """
        self._begin("link", user_id)
        if not in_dm:
            return self._refused("link", user_id, "not a direct message", DM_ONLY)
        outcome = self.linker.attempt(user_id, code)
        self._end("link", user_id, outcome)
        return Reply(LINK_WORDS[outcome])

    def unlink(self, user_id: int, in_dm: bool) -> Reply:
        """Answer ``/unlink``: remove the link when ``user_id`` is the linked account.

        Returns the "Unlinked" reply, or ``HOST_ONLY`` / ``DM_ONLY`` when the
        asker is another account or not in a direct message. Does not make a
        new code.
        """
        self._begin("unlink", user_id)
        refusal = self._gate("unlink", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        self.linker.unlink()
        self._end("unlink", user_id, "unlinked")
        return Reply(
            "Unlinked. This launcher answers no account now; "
            "restart it to get a new code."
        )

    def status(self, user_id: int, in_dm: bool = False) -> Reply:
        """Answer ``/status``: the version, the install and Balance of Power.

        Returns the status lines, ``HOST_ONLY`` for another account, or
        ``REFUSED`` if the launcher refused a request. Does not read the game
        files beyond the install check.
        """
        self._begin("status", user_id)
        refusal = self._gate("status", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        hello = self._result(self.send("hello", {"page_version": BOT_VERSION_NAME}))
        install = self._result(self.send("install.status", {}))
        if hello is None or install is None:
            self._end("status", user_id, "error")
            return Reply(REFUSED)
        lines = [f"Launcher version {hello['launcher_version']}."]
        if install["found"]:
            lines.append(f"Game install: {install['path']}.")
            word = "found" if install["balance_of_power"] else "not found"
            lines.append(f"Balance of Power: {word}.")
        else:
            lines.append("No game install was found.")
        self._end("status", user_id, "ok")
        return Reply("\n".join(lines))

    def missions(self, user_id: int, mission_type: str, in_dm: bool = False) -> Reply:
        """Answer ``/missions``: one mission type's network menu as menus to pick from.

        Offers each id once (the first entry of a repeated id), 25 to a menu
        and at most 5 menus; says so when the list is longer. Returns words
        only (no menus) for another account, an unknown type, a list the
        install lacks or an empty list. Does not check that a file named in
        the list exists.
        """
        self._begin("missions", user_id)
        refusal = self._gate("missions", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        if mission_type not in MISSION_TYPE_NAMES:
            self._end("missions", user_id, "bad type")
            return Reply(f"The mission type is one of {', '.join(MISSION_TYPE_NAMES)}.")
        listed = self._result(self.send("missions.list", {}))
        if listed is None:
            self._end("missions", user_id, "error")
            return Reply(REFUSED)
        menu = next(m for m in listed["menus"] if m["mission_type"] == mission_type)
        name = _title_case(mission_type)
        if not menu["resolved"]:
            self._end("missions", user_id, "no list")
            return Reply(f"The game's list of {mission_type} missions was not found.")
        seen: set[int] = set()
        options: list[Option] = []
        for entry in menu["entries"]:
            if entry["id"] in seen:
                continue
            seen.add(entry["id"])
            place = f"{entry['section']}: " if entry["section"] else ""
            word = "" if entry["available"] else ", not available"
            options.append(
                Option(
                    _clip(f"{place}{entry['title']}"),
                    str(entry["id"]),
                    _clip(f"{entry['file']}{word}"),
                )
            )
        if not options:
            self._end("missions", user_id, "empty")
            return Reply(f"The list of {mission_type} missions is empty.")
        room = MENU_SIZE * MENUS_PER_REPLY
        chunks = [options[i : i + MENU_SIZE] for i in range(0, room, MENU_SIZE)]
        pickers = tuple(
            Picker(
                "mission",
                mission_type,
                f"{name} missions {n * MENU_SIZE + 1} to {n * MENU_SIZE + len(chunk)}",
                tuple(chunk),
            )
            for n, chunk in enumerate(chunks)
            if chunk
        )
        text = f"{name} missions: pick one to show it on the page."
        if len(options) > room:
            text += f" Showing the first {room} of {len(options)}."
        self._end("missions", user_id, f"{len(options)} listed")
        return Reply(text, pickers)

    def pick_mission(
        self, user_id: int, mission_type: str, mission_id: str, in_dm: bool = False
    ) -> Reply:
        """Answer a pick from a missions menu: tell the open pages to show it.

        Returns the "Showing" reply with the push for the pages; words alone
        when the pick is not a mission, is not in the list, or the launcher
        refused it. Does not check that any page is open (``delivered_text``
        does that once the push was sent).
        """
        self._begin("pick_mission", user_id)
        refusal = self._gate("pick_mission", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        if mission_type not in MISSION_TYPE_NAMES or not (
            mission_id.isascii() and mission_id.isdigit()
        ):
            self._end("pick_mission", user_id, "bad pick")
            return Reply("That pick is not a mission I know.")
        args = {"mission_type": mission_type, "id": int(mission_id)}
        outcome = self.send("page.show_mission", args)
        result = self._result(outcome)
        if result is None:
            self._end("pick_mission", user_id, "error")
            return Reply(REFUSED)
        if not result["shown"]:
            self._end("pick_mission", user_id, "not listed")
            return Reply(
                "That mission is not in the game's list. Ask for /missions again."
            )
        title = outcome.pushes[0]["data"]["title"]
        self._end("pick_mission", user_id, "shown")
        return Reply(f"Showing {title} on the page.", pushes=outcome.pushes)

    def settings(self, user_id: int, in_dm: bool) -> Reply:
        """Answer ``/settings``: each setting as a menu with its values in words.

        Returns the menus and the current values, or ``HOST_ONLY`` /
        ``DM_ONLY`` / ``REFUSED``. Does not change anything.
        """
        self._begin("settings", user_id)
        refusal = self._gate("settings", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        got = self._result(self.send("settings.get", {}))
        if got is None:
            self._end("settings", user_id, "error")
            return Reply(REFUSED)
        pickers: list[Picker] = []
        lines: list[str] = []
        for name, (words, values) in SETTING_WORDS.items():
            now = got["settings"][name]
            lines.append(f"{words} is now {values[now][0]}.")
            options = tuple(
                Option(label, value, sentence, default=value == now)
                for value, (label, sentence) in values.items()
            )
            pickers.append(Picker("setting", name, words, options))
        self._end("settings", user_id, "ok")
        return Reply("\n".join(lines), tuple(pickers))

    def pick_setting(
        self, user_id: int, name: str, value: str, in_dm: bool = True
    ) -> Reply:
        """Answer a pick from a settings menu: change that setting.

        Returns the "set to" reply with the push for the pages, or words
        alone when the asker is refused or the launcher refused the value.
        Does not check that any page is open.
        """
        self._begin("pick_setting", user_id)
        refusal = self._gate("pick_setting", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        outcome = self.send("settings.set", {"name": name, "value": value})
        if self._result(outcome) is None:
            self._end("pick_setting", user_id, "error")
            return Reply(REFUSED)
        words, values = SETTING_WORDS[name]
        self._end("pick_setting", user_id, "set")
        return Reply(f"{words} set to {values[value][0]}.", pushes=outcome.pushes)

    def delivered_text(self, reply: Reply, pages: int) -> str:
        """Return the reply's words, with ``NO_PAGE`` added when it had pushes
        and no page (``pages`` is 0) was open to get them."""
        if reply.pushes and pages == 0:
            return f"{reply.text} {NO_PAGE}"
        return reply.text
