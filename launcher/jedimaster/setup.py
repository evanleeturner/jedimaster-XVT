"""The lobby's setup document: its launch fingerprint and the engine's values.

Purpose: a game's host owns one setup document (``schema/setup.schema.json``)
and sends it whole to every player on each change. At launch every engine
must fly the same setup, so the host stamps it with a fingerprint each engine
compares; and the launch description gives the engine each setting in the
form the engine stores it.

Flow:
  1. ``fingerprint_view`` drops what differs between players or between
     revisions without changing the battle: the revision, the launch block,
     each player's ready flag and ping
  2. ``setup_fingerprint`` hashes that view as canonical JSON
  3. ``engine_settings`` maps the 17 settings to the engine's byte values

Invariants:
  - the fingerprint does not depend on key order, revision, ready flags,
    pings or the launch block, and changes with anything else
  - the engine values are those of the 1997 lobby's GAME_OPTIONS packet:
    0 and 1 for booleans; enums in their game order; a mission time limit of
    0 for none and 255 for the mission's own

Call:
  setup_fingerprint(document) -> str, 64 lowercase hex digits
  engine_settings(document) -> dict[str, int]
"""

from __future__ import annotations

import copy
import hashlib
import json
import logging

logger = logging.getLogger(__name__)

# Fields left out of the fingerprint: they change between revisions or
# between players without changing what any engine computes.
TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")
PLAYER_UNFINGERPRINTED = ("ready", "ping_ms")

# The engine's stored value for each named setting value, in game order.
DIFFICULTY_VALUES = {"easy": 0, "medium": 1, "hard": 2, "easy_cheat": 3}
BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 2}
CRAFT_SELECTION_VALUES = {"off": 0, "on": 1, "host_only": 2}
CRAFT_WAVE_VALUES = {"none": 0, "default": 1, "unlimited": 2}
COMBAT_BALANCE_VALUES = {
    "autobalance": 0,
    "favor_imperial": 1,
    "neutral": 2,
    "favor_rebel": 3,
}
CONTINUE_SEQUENCE_VALUES = {"restart": 0, "continue": 1}
MISSION_TIME_LIMIT_NONE = 0
MISSION_TIME_LIMIT_DEFAULT = 255
LAST_TEAM_TIME_LIMIT_NONE = 0

BOOLEAN_SETTINGS = (
    "collisions",
    "craft_jumping",
    "random_setup",
    "in_progress_join",
    "locate_players",
    "internet_play",
    "ai_opponents",
)


def fingerprint_view(document: dict) -> dict:
    """Return a copy of document without the fields the fingerprint ignores.

    Leaves out the revision, the launch block, and each roster entry's ready
    flag and ping. Does not validate document; a document missing those
    fields is copied as it is.
    """
    view = copy.deepcopy(document)
    for key in TOP_LEVEL_UNFINGERPRINTED:
        view.pop(key, None)
    for player in view.get("roster", []):
        for key in PLAYER_UNFINGERPRINTED:
            player.pop(key, None)
    return view


def canonical_json(value: object) -> bytes:
    """Return value as UTF-8 JSON with sorted keys and no spaces."""
    text = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return text.encode("utf-8")


def setup_fingerprint(document: dict) -> str:
    """Return the SHA-256 of document's fingerprint view, as 64 hex digits.

    Two documents that differ only in the fields ``fingerprint_view`` drops,
    or in key order, give the same fingerprint. Does not validate document.
    """
    digest = hashlib.sha256(canonical_json(fingerprint_view(document))).hexdigest()
    logger.debug("setup fingerprint %s", digest)
    return digest


def _named(table: dict, name: str, value: object) -> int:
    """Return table[value]; raise ValueError naming the setting otherwise."""
    if value not in table:
        raise ValueError(f"setting {name!r} has no engine value for {value!r}")
    return table[value]


def engine_settings(document: dict) -> dict[str, int]:
    """Return the 17 settings as the engine stores them, keyed by setting name.

    Booleans become 0 or 1; named values become their game-order numbers; a
    mission time limit of "none" becomes 0 and "default" 255; a last team
    limit of "none" becomes 0; the seed and the update rate pass through.
    Raises KeyError when a setting is missing and ValueError when a value
    has no engine form. Does not check ranges the schema checks (minutes,
    seed size); validate the document against the schema first.
    """
    settings = document["settings"]
    values = {name: int(bool(settings[name])) for name in BOOLEAN_SETTINGS}
    values["difficulty"] = _named(
        DIFFICULTY_VALUES, "difficulty", settings["difficulty"]
    )
    values["battle_length"] = _named(
        BATTLE_LENGTH_VALUES, "battle_length", settings["battle_length"]
    )
    values["craft_selection"] = _named(
        CRAFT_SELECTION_VALUES, "craft_selection", settings["craft_selection"]
    )
    values["craft_waves"] = _named(
        CRAFT_WAVE_VALUES, "craft_waves", settings["craft_waves"]
    )
    values["combat_balance"] = _named(
        COMBAT_BALANCE_VALUES, "combat_balance", settings["combat_balance"]
    )
    values["continue_sequence"] = _named(
        CONTINUE_SEQUENCE_VALUES, "continue_sequence", settings["continue_sequence"]
    )
    limit = settings["mission_time_limit"]
    if limit == "none":
        values["mission_time_limit"] = MISSION_TIME_LIMIT_NONE
    elif limit == "default":
        values["mission_time_limit"] = MISSION_TIME_LIMIT_DEFAULT
    else:
        values["mission_time_limit"] = int(limit)
    last = settings["last_team_time_limit"]
    values["last_team_time_limit"] = (
        LAST_TEAM_TIME_LIMIT_NONE if last == "none" else int(last)
    )
    values["random_seed"] = int(settings["random_seed"])
    values["server_update_rate"] = int(settings["server_update_rate"])
    logger.debug("engine settings %s", values)
    return values
