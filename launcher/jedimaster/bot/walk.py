"""The two walks of the bot: Single player, and the game options.

Purpose:
    Decide what the bot says at each step of starting a solo battle (kind,
    side, mission, pilot, Launch) and of setting the game options (one menu
    each), as plain replies. ``Core`` takes these methods in, so a slash
    command and a right-click item call the same method.

Flow:
    ``solo`` offers the kinds; ``pick_solo`` takes one pick and offers the
    next step, each reply replacing the message that was clicked, with what
    was picked so far carried in the next menu's key (``kind|side|mission``);
    ``launch_solo`` checks nothing itself and asks the launcher to fly the
    battle through ``launch.solo``, which checks every value again.
    ``options`` shows the seven options as menus (five to a message, the rest
    in a second message) and ``pick_option`` sets one.

Invariants:
    - Menus are built only from the launcher's own answers
      (``launch.menu``, ``pilots.list``, ``options.get``); no path and no
      program name ever comes from or goes to Discord.
    - A mission marked ``&`` never appears; one marked ``*`` says in its
      description that it opens once flown.
    - A launcher started without the game program is named as the reason at
      the pilot step, before the pilot folder is.
    - Every step goes through ``_gate``: only the linked account is answered.
    - The first choice of every option menu is "As the game has it", and the
      current choice is marked.

Call:
    ``reply = core.pick_solo(user_id, "kind", "", "training")``
"""

from __future__ import annotations

import logging
from typing import Any

from ..launch.options import AS_GAME
from ..launch.options import AS_GAME_WORDS
from ..launch.options import OPTIONS
from ..launch.options import words_of
from ..page.control import Outcome
from ..page.launching import NO_ENGINE
from ..page.launching import NO_PAGE
from ..page.protocol import MISSION_TYPE_NAMES
from ..page.protocol import OPTION_NAMES
from ..page.solo import side_problem
from .reply import Button
from .reply import clip
from .reply import menus
from .reply import Option
from .reply import Picker
from .reply import Reply
from .reply import title_case

logger = logging.getLogger(__name__)

Json = dict[str, Any]

REFUSED = "The launcher refused that request."
BAD_PICK = "That pick is not one I know. Start again with Single player."
NO_PILOTS = "The game has no pilots yet. Make one in the game first."
NO_PILOT_FOLDER = "The launcher cannot read the game's pilot folder."
SIDES = ("rebel", "imperial")
OPTIONS_PER_MESSAGE = 5


def kind_options() -> tuple[Option, ...]:
    """Return the six mission types as menu lines."""
    return tuple(Option(title_case(n), n) for n in MISSION_TYPE_NAMES)


def _words(kind: str, side: str) -> str:
    return title_case(kind) if side == "none" else f"{title_case(kind)}, {side}"


class Walk:
    """The solo and options methods ``Core`` takes in; they use ``Core``'s own helpers."""

    def solo(self, user_id: int, in_dm: bool = False) -> Reply:
        """Answer ``/solo`` and the Single player item: offer the six kinds of battle.

        Returns the kinds as one menu, or ``HOST_ONLY`` for another account.
        The menu ``opens_page``, so the page the Launch needs opens while the
        host picks. Starts nothing.
        """
        self._begin("solo", user_id)
        refusal = self._gate("solo", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        self._end("solo", user_id, "kinds")
        return Reply(
            "Single player: pick a kind of battle.",
            (Picker("solo_kind", "", "Kind of battle", kind_options()),),
            opens_page=True,
        )

    def pick_solo(
        self, user_id: int, step: str, key: str, value: str, in_dm: bool = False
    ) -> Reply:
        """Answer a pick in the Single player walk and offer the next step.

        ``step`` is ``kind``, ``side``, ``mission`` or ``pilot``; ``key`` what
        was picked before (``""``, ``kind``, ``kind|side``, ``kind|side|file``);
        ``value`` this pick. The reply replaces the clicked message. Words
        alone (not edited in) for another account or a pick that is not on
        the lists.
        """
        self._begin("pick_solo", user_id)
        refusal = self._gate("pick_solo", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        walk = {
            "kind": self._after_kind,
            "side": self._after_side,
            "mission": self._after_mission,
            "pilot": self._after_pilot,
        }.get(step)
        parts = key.split("|") if key else []
        reply = walk(parts, value) if walk is not None else None
        if reply is None:
            self._end("pick_solo", user_id, "bad pick")
            return Reply(BAD_PICK)
        reply.edit = True
        self._end("pick_solo", user_id, f"{step} -> {len(reply.pickers)} menus")
        return reply

    def _after_kind(self, parts: list[str], value: str) -> Reply | None:
        if parts or value not in MISSION_TYPE_NAMES:
            return None
        if side_problem(value, "none") is None:
            return self._missions_step(value, "none")
        options = tuple(Option(title_case(s), s) for s in SIDES)
        return Reply(
            f"Single player, {title_case(value)}: pick a side.",
            (Picker("solo_side", value, "Side", options),),
        )

    def _after_side(self, parts: list[str], value: str) -> Reply | None:
        if len(parts) != 1 or parts[0] not in MISSION_TYPE_NAMES:
            return None
        if side_problem(parts[0], value) is not None:
            return None
        return self._missions_step(parts[0], value)

    def _missions_step(self, kind: str, side: str) -> Reply | None:
        listed = self._result(
            self.send("launch.menu", {"mission_type": kind, "side": side})
        )
        if listed is None:
            return Reply(REFUSED)
        words = _words(kind, side)
        if not listed["resolved"]:
            return Reply(f"The game's list of {words} missions was not found.")
        options = [
            Option(
                clip(f"{e['section']}: {e['title']}" if e["section"] else e["title"]),
                e["file"],
                clip(
                    e["file"] + (", opens once flown" if e["opens_once_flown"] else "")
                ),
            )
            for e in listed["entries"]
        ]
        if not options:
            return Reply(f"The list of {words} missions is empty.")
        pickers, left_out = menus(
            "solo_mission", f"{kind}|{side}", f"{title_case(kind)} missions", options
        )
        text = f"Single player, {words}: pick a mission."
        if left_out:
            text += f" Showing the first {len(options) - left_out} of {len(options)}."
        return Reply(text, pickers)

    def _after_mission(self, parts: list[str], value: str) -> Reply | None:
        if len(parts) != 2 or parts[0] not in MISSION_TYPE_NAMES:
            return None
        kind, side = parts
        if side_problem(kind, side) is not None:
            return None
        listed = self._result(
            self.send("launch.menu", {"mission_type": kind, "side": side})
        )
        entry = next(
            (e for e in (listed or {}).get("entries", []) if e["file"] == value), None
        )
        if entry is None:
            return None
        got = self._result(self.send("pilots.list", {}))
        if got is None:
            return Reply(REFUSED)
        if not got["engine_given"]:
            return Reply(NO_ENGINE)
        if not got["found"]:
            return Reply(NO_PILOT_FOLDER)
        if not got["pilots"]:
            return Reply(NO_PILOTS)
        pickers, left_out = menus(
            "solo_pilot",
            f"{kind}|{side}|{value}",
            "Pilots",
            [Option(clip(name), name) for name in got["pilots"]],
        )
        text = f"Single player, {_words(kind, side)}, {entry['title']}: pick a pilot."
        if left_out:
            text += f" Showing the first {len(got['pilots']) - left_out}."
        return Reply(text, pickers)

    def _after_pilot(self, parts: list[str], value: str) -> Reply | None:
        if len(parts) != 3 or parts[0] not in MISSION_TYPE_NAMES or not value:
            return None
        kind, side, mission = parts
        if side_problem(kind, side) is not None:
            return None
        return Reply(
            f"Ready: {_words(kind, side)}, {mission}, flown by {value}. "
            "Press Launch to start.",
            buttons=(
                Button("Launch", "solo_launch", f"{kind}|{side}|{mission}|{value}"),
            ),
        )

    def launch_solo(self, user_id: int, key: str, in_dm: bool = False) -> Reply:
        """Answer the Launch button: ask the launcher to fly ``key`` (``kind|side|file|pilot``).

        The launcher checks every value again against the lists and the
        pilot folder and answers in words; its push for the open pages goes
        with the reply, which replaces the Launch message only when the battle
        started (a refusal leaves the button to try again; one because no page
        is open ``opens_page``). Returns words alone for another account or a
        key that is not four parts.
        """
        self._begin("launch_solo", user_id)
        refusal = self._gate("launch_solo", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        parts = key.split("|")
        if len(parts) != 4 or parts[0] not in MISSION_TYPE_NAMES:
            self._end("launch_solo", user_id, "bad key")
            return Reply(BAD_PICK)
        kind, side, mission, pilot = parts
        args = {
            "player": str(user_id),
            "mission_type": kind,
            "side": side,
            "mission": mission,
            "pilot": pilot,
        }
        outcome: Outcome = self.send("launch.solo", args)
        result = self._result(outcome)
        if result is None:
            self._end("launch_solo", user_id, "error")
            return Reply(REFUSED)
        self._end("launch_solo", user_id, f"started={result['started']}")
        return Reply(
            result["message"],
            pushes=outcome.pushes,
            edit=bool(result["started"]),
            opens_page=result["message"] == NO_PAGE,
        )

    def options(self, user_id: int, in_dm: bool = False) -> Reply:
        """Answer ``/options`` and the Game options item: one menu for each option.

        Each menu starts with "As the game has it" and marks the current
        choice; five menus go in the first message and the rest in a second.
        Returns the menus and the current values in words, ``HOST_ONLY`` for
        another account, or ``REFUSED``. Changes nothing.
        """
        self._begin("options", user_id)
        refusal = self._gate("options", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        got = self._result(self.send("options.get", {"player": str(user_id)}))
        if got is None:
            self._end("options", user_id, "error")
            return Reply(REFUSED)
        current = got["options"]
        pickers: list[Picker] = []
        lines: list[str] = []
        for name in OPTION_NAMES:
            spec, now = OPTIONS[name], current[name]
            choices = [Option(AS_GAME_WORDS, AS_GAME, spec.help, now is None)]
            choices += [
                Option(c.words, c.text, default=c.text == now) for c in spec.choices
            ]
            shown = AS_GAME_WORDS if now is None else words_of(name, _native(name, now))
            lines.append(f"{spec.words}: {shown}.")
            pickers.append(Picker("option", name, spec.words, tuple(choices)))
        first, rest = pickers[:OPTIONS_PER_MESSAGE], pickers[OPTIONS_PER_MESSAGE:]
        self._end("options", user_id, "ok")
        return Reply(
            "Game options (a choice is kept for you):\n" + "\n".join(lines),
            tuple(first),
            then=(Reply("More game options:", tuple(rest)),),
        )

    def pick_option(
        self, user_id: int, name: str, value: str, in_dm: bool = False
    ) -> Reply:
        """Answer a pick from an options menu: set option ``name`` to ``value``.

        ``AS_GAME`` leaves the option out so the game keeps its own. Returns
        the "set to" reply with the push for the open pages, or words alone
        for another account, an unknown option or a refused value.
        """
        self._begin("pick_option", user_id)
        refusal = self._gate("pick_option", user_id, in_dm, dm_only=False)
        if refusal is not None:
            return refusal
        if name not in OPTIONS:
            self._end("pick_option", user_id, "bad option")
            return Reply(BAD_PICK)
        sent = None if value == AS_GAME else value
        outcome = self.send(
            "options.set", {"player": str(user_id), "name": name, "value": sent}
        )
        if self._result(outcome) is None:
            self._end("pick_option", user_id, "error")
            return Reply(REFUSED)
        spec = OPTIONS[name]
        if sent is None:
            text = f"{spec.words} is left as the game has it."
        else:
            words = next(c.words for c in spec.choices if c.text == sent)
            text = f"{spec.words} set to {words}."
        self._end("pick_option", user_id, "set")
        return Reply(text, pushes=outcome.pushes, opens_page=True)


def _native(name: str, text: str) -> Any:
    return next(c.value for c in OPTIONS[name].choices if c.text == text)
