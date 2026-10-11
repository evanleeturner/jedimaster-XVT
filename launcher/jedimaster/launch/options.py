"""The seven game options a player can set, in words, and how they reach a description.

Purpose:
    One table of the options the engine's launch door takes (``settings`` of
    the launch description), each with its choices as text, as the value the
    description holds, and in plain words for a menu. Everything that turns
    text into an option (the bot's menus, the page, the ``--setting`` flag)
    reads this table.

Flow:
    ``OPTIONS`` is the table. ``parse_value`` turns a choice's text into the
    description's value; ``text_of`` turns it back; ``for_description``
    builds the ``settings`` of one battle from a player's stored options.

Invariants:
    - The names and values are those of ``model`` and the launch schema;
      ``difficulty`` ``easy_cheat`` is sent as ``easy`` for any battle that
      is not a campaign, as the game's own menus do.
    - A stored value matches a choice by value and by type: ``True`` is not
      the number 1.
    - An option a player has not set is absent; the game keeps its own.

Call:
    ``settings, note = for_description({"difficulty": "easy_cheat"}, "melee")``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Any

logger = logging.getLogger(__name__)

AS_GAME = "as_game"
"""The menu value that means: leave this option as the game has it."""
AS_GAME_WORDS = "As the game has it"
CHEAT_NOTE = "Easy with cheats is for campaigns only, so this battle was sent as easy."


@dataclass(frozen=True)
class Choice:
    """One value of an option: its text, the description's value, its words."""

    text: str
    value: Any
    words: str


@dataclass(frozen=True)
class OptionSpec:
    """One option: its name, its words, what it does and its choices."""

    name: str
    words: str
    help: str
    choices: tuple[Choice, ...]


def _switch() -> tuple[Choice, ...]:
    return (Choice("on", True, "On"), Choice("off", False, "Off"))


def _minutes() -> tuple[Choice, ...]:
    return (
        Choice("none", "none", "No limit"),
        Choice("default", "default", "The mission's own limit"),
        *(
            Choice(str(n), n, "1 minute" if n == 1 else f"{n} minutes")
            for n in range(1, 21)
        ),
    )


OPTIONS: dict[str, OptionSpec] = {
    spec.name: spec
    for spec in (
        OptionSpec(
            "difficulty",
            "Difficulty",
            "How hard the enemy flies.",
            (
                Choice("easy", "easy", "Easy"),
                Choice("medium", "medium", "Medium"),
                Choice("hard", "hard", "Hard"),
                Choice("easy_cheat", "easy_cheat", "Easy with cheats (campaigns)"),
            ),
        ),
        OptionSpec("collisions", "Collisions", "Whether craft collide.", _switch()),
        OptionSpec(
            "craft_jumping",
            "Craft jumping",
            "Whether a pilot may jump into another craft with the J key.",
            _switch(),
        ),
        OptionSpec(
            "random_setup",
            "Random setup",
            "Whether a battle starts from a random setup.",
            _switch(),
        ),
        OptionSpec(
            "battle_length",
            "Battle length",
            "How many wins end a battle.",
            tuple(Choice(str(n), n, f"{n} wins") for n in (2, 3, 4)),
        ),
        OptionSpec(
            "mission_time_limit",
            "Mission time limit (multiplayer only: no effect in solo play)",
            "How long a battle with other players may last; a solo battle has no limit.",
            _minutes(),
        ),
        OptionSpec(
            "continue_sequence",
            "Battle or campaign",
            "Whether a battle or campaign goes on after a mission.",
            (
                Choice("restart", "restart", "Start again"),
                Choice("continue", "continue", "Continue"),
            ),
        ),
    )
}
"""Every option by name, in the order a menu lists them."""


def _same(a: Any, b: Any) -> bool:
    return type(a) is type(b) and a == b


def parse_value(name: str, text: str) -> Any:
    """Return the description's value for choice ``text`` of option ``name``.

    Raises ``ValueError`` for an unknown option or a text that is not one of
    its choices. Does not accept ``AS_GAME``: leaving an option out is the
    caller's decision.
    """
    if name not in OPTIONS:
        raise ValueError(f"no such option {name!r}; one of {list(OPTIONS)}")
    for choice in OPTIONS[name].choices:
        if choice.text == text:
            return choice.value
    texts = [c.text for c in OPTIONS[name].choices]
    raise ValueError(f"{name} must be one of {texts}")


def text_of(name: str, value: Any) -> str | None:
    """Return the choice text of a stored ``value``, or None if it is no choice."""
    if name not in OPTIONS:
        return None
    for choice in OPTIONS[name].choices:
        if _same(choice.value, value):
            return choice.text
    return None


def words_of(name: str, value: Any) -> str:
    """Return the words for a stored value; ``AS_GAME_WORDS`` when it is unset or unknown."""
    for choice in OPTIONS[name].choices:
        if _same(choice.value, value):
            return choice.words
    return AS_GAME_WORDS


def parse_setting(argument: str) -> tuple[str, Any]:
    """Return (name, value) for a ``NAME=VALUE`` argument; raise ValueError if it is not one."""
    name, separator, text = argument.partition("=")
    if not separator:
        raise ValueError(f"{argument!r} is not NAME=VALUE")
    return name, parse_value(name, text)


def for_description(
    options: dict[str, Any], game_type: str
) -> tuple[dict[str, Any], str | None]:
    """Return (the ``settings`` of the description, a note or None) for one battle.

    Takes the options a player has set. An easy cheat on a battle that is not
    a campaign is sent as easy, and the note says so. Does not check the
    values (the store already did).
    """
    settings = dict(options)
    note = None
    if settings.get("difficulty") == "easy_cheat" and game_type != "campaign":
        settings["difficulty"] = "easy"
        note = CHEAT_NOTE
        logger.debug("easy cheat sent as easy for a %s battle", game_type)
    return settings, note
