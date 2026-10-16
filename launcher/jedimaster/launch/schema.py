"""The JSON Schemas of the launch description and the launch result.

Purpose:
    Publish the two schemas (draft 2020-12) the engine's ``--launch`` option
    and the launcher's ``launch`` command both hold to.

Flow:
    ``launch_schema`` and ``result_schema`` build each document's schema from
    the words of ``model``.

Invariants:
    - Same words, same dict.
    - Every object is closed (``additionalProperties`` false).
    - The description's optional members are ``result``, ``seed``,
      ``ai_program`` and ``settings``, and each setting is optional; a setting left out keeps
      the game's own value.

Call:
    ``json.dumps(launch_schema(), indent=2)``
"""

from __future__ import annotations

import logging
from typing import Any

from . import model

logger = logging.getLogger(__name__)

LAUNCH_ID = "https://jedimaster.invalid/schema/launch.schema.json"
RESULT_ID = "https://jedimaster.invalid/schema/result.schema.json"
DRAFT = "https://json-schema.org/draft/2020-12/schema"


def _settings() -> dict[str, Any]:
    """Return the schema of the description's settings object."""
    properties: dict[str, Any] = {
        "difficulty": {"enum": list(model.DIFFICULTIES)},
        "battle_length": {"enum": list(model.BATTLE_LENGTHS)},
        "mission_time_limit": {
            "oneOf": [
                {"enum": list(model.TIME_LIMIT_WORDS)},
                {
                    "type": "integer",
                    "minimum": model.TIME_LIMIT_MINUTES[0],
                    "maximum": model.TIME_LIMIT_MINUTES[1],
                },
            ]
        },
        "continue_sequence": {"enum": list(model.SEQUENCE_CHOICES)},
        "sith": {
            "description": "Enemy Sith pilots; left out means none, which a game build from before the setting also flies.",
            "enum": list(model.SITH_SETTINGS),
        },
    }
    for name in model.BOOLEAN_SETTINGS:
        properties[name] = {"type": "boolean"}
    return {
        "description": "Settings of the game's solo screen; one left out keeps the game's own value.",
        "type": "object",
        "additionalProperties": False,
        "properties": dict(sorted(properties.items())),
    }


def launch_schema() -> dict[str, Any]:
    """Return the JSON Schema of the launch description.

    Always the same dict. Does not encode the rules between members: that a
    game type takes a side only when it needs one, that easy_cheat is for a
    campaign, or that the mission's folder is the game type's; the engine
    refuses those with a plain sentence.
    """
    return {
        "$schema": DRAFT,
        "$id": LAUNCH_ID,
        "title": "Launch description",
        "description": "One single-player battle for the engine's --launch option to fly.",
        "type": "object",
        "additionalProperties": False,
        "required": [
            "schema_version",
            "mode",
            "game_type",
            "side",
            "mission",
            "pilot",
        ],
        "properties": {
            "schema_version": {"const": model.SCHEMA_VERSION},
            "mode": {"const": model.MODE},
            "game_type": {"enum": list(model.GAME_TYPES)},
            "side": {
                "description": "rebel or imperial for training, combat and campaign; none for the rest.",
                "enum": list(model.SIDES),
            },
            "mission": {
                "description": "folder/file as the game's own list names it; a sequence's file for a tournament, battle or campaign.",
                "type": "string",
                "pattern": model.MISSION_PATTERN,
            },
            "pilot": {
                "description": "A pilot the game already has: 1 to 12 letters, digits, spaces, underscores, hyphens or dots, starting and ending with a letter or digit, holding none of the flight switch words.",
                "type": "string",
                "minLength": 1,
                "maxLength": 12,
            },
            "result": {
                "description": "Where the engine writes the result file; beside the description when left out.",
                "type": "string",
                "minLength": 1,
                "maxLength": 511,
            },
            "seed": {
                "description": "The random seed a sequence starts from; the engine draws one when left out.",
                "type": "integer",
                "minimum": 0,
                "maximum": model.SEED_MAXIMUM,
            },
            "ai_program": {
                "description": "The AI program the engine starts to decide what computer craft do; no computer craft decides anything when left out.",
                "type": "object",
                "additionalProperties": False,
                "required": ["python", "path"],
                "properties": {
                    "python": {
                        "description": "The Python that runs the AI program.",
                        "type": "string",
                        "minLength": 1,
                        "maxLength": 511,
                    },
                    "path": {
                        "description": "The AI program's path, as the Python is given it.",
                        "type": "string",
                        "minLength": 1,
                        "maxLength": 511,
                    },
                },
            },
            "settings": _settings(),
        },
    }


def _sith_block() -> dict[str, Any]:
    """Return the schema of the version 3 result's ``sith`` object (closed, every member present)."""
    counts = {
        name: {"type": "integer", "minimum": 0}
        for name in (
            "apprentices_flown",
            "apprentices_lost",
            "lord_flown",
            "lord_lost",
        )
    }
    members: dict[str, Any] = {
        "setting": {"enum": list(model.SITH_SETTINGS)},
        "flight_group": {
            "description": "The promoted flight group's index; -1 when none was promoted.",
            "type": "integer",
            "minimum": -1,
        },
        "flight_name": {
            "description": "The promoted group's name from the mission; empty when none.",
            "type": "string",
        },
        "craft_type": {
            "description": "The promoted group's craft type number; -1 when none.",
            "type": "integer",
            "minimum": -1,
        },
        **counts,
        "why": {
            "description": "Empty when a group was promoted or the setting is none; else why none was.",
            "type": "string",
        },
    }
    return {
        "description": "The battle's Sith setting and how the Sith flight fared; destroyed is lost, a craft that jumped away or flew on lived.",
        "type": "object",
        "additionalProperties": False,
        "required": list(members),
        "properties": members,
    }


def _closed(description: str, members: dict[str, Any]) -> dict[str, Any]:
    """Return a closed object of members, every member required."""
    return {
        "description": description,
        "type": "object",
        "additionalProperties": False,
        "required": list(members),
        "properties": members,
    }


def _list(description: str, most: int, item: dict[str, Any]) -> dict[str, Any]:
    """Return a list of at most ``most`` items of the schema ``item``."""
    return {
        "description": description,
        "type": "array",
        "maxItems": most,
        "items": item,
    }


def _index(description: str, low: int, high: int) -> dict[str, Any]:
    """Return an integer from low to high."""
    return {
        "description": description,
        "type": "integer",
        "minimum": low,
        "maximum": high,
    }


def _counts(*names: str) -> dict[str, Any]:
    """Return the game's own counts of names, written as it holds them."""
    return {name: {"type": "integer"} for name in names}


def _players() -> dict[str, Any]:
    """Return the schema of the result's players list."""
    player = _closed(
        "One player as the game's end of battle counted it.",
        {
            "entry": _index("The player's place in the pilot record.", 0, 7),
            "slot": _index(
                "The player's place in the flight; -1 when no place holds the "
                "player, and then team is -1 and the flight's counts are 0.",
                -1,
                7,
            ),
            "name": {
                "description": "The name the debriefing shows.",
                "type": "string",
                "maxLength": 12,
            },
            "team": {"type": "integer", "minimum": -1},
            "flight_group": {"type": "integer", "minimum": 0},
            "rating": {
                "description": "The rank at launch, as a number.",
                "type": "integer",
            },
            "score": {
                "description": "The player's score plus its team's bonus.",
                "type": "integer",
            },
            **_counts("kills", "shared", "assists", "losses"),
            "shots": {"description": "Laser and ion shots.", "type": "integer"},
            "hits": {"description": "Laser and ion hits.", "type": "integer"},
            **_counts("warheads", "warhead_hits"),
            "has_left": {
                "description": "True when the player left before the end.",
                "type": "boolean",
            },
        },
    )
    return _list(
        "Every filled entry of the pilot record's players, in entry order.",
        model.RESULT_PLAYERS,
        player,
    )


def _teams() -> dict[str, Any]:
    """Return the schema of the result's teams list."""
    team = _closed(
        "One team of the battle.",
        {
            "team": _index("The team's number.", 0, model.RESULT_TEAMS - 1),
            "completed": {
                "description": "True when its primary goals were met and none prevented.",
                "type": "boolean",
            },
            **_counts("score", "kills", "shared", "losses"),
            "seconds": {
                "description": "When the team met its goals; 0 when it never did.",
                "type": "integer",
            },
        },
    )
    return _list(
        "Each of teams 0 to 7 that had a player flight group arrive or met its goals.",
        model.RESULT_TEAMS,
        team,
    )


def _rows(name: str, key: str, size: int, columns: tuple[str, ...]) -> dict[str, Any]:
    """Return a list of a table's rows that hold a number other than 0."""
    row = _closed(
        f"One row of {name}.",
        {key: _index(f"The row's {key}.", 0, size - 1), **_counts(*columns)},
    )
    return _list(
        f"The rows of {name} with a number other than 0, lowest {key} first.",
        size,
        row,
    )


def _detail() -> dict[str, Any]:
    """Return the schema of the result's detail object."""
    kills = ("kills", "shared", "assists")
    members: dict[str, Any] = {
        **_counts(
            "shots",
            "hits",
            "warheads",
            "warhead_hits",
            "losses",
            "losses_to_collisions",
            "losses_to_starships",
            "losses_to_mines",
            "friendlies_killed",
            "special_inspected",
        ),
        "by_craft": _rows("kills by craft type", "craft_type", 100, kills),
        "on_pilot_ranks": _rows("kills of human pilots by rank", "rank", 25, kills),
        "on_ai_ranks": _rows("kills of computer pilots by rank", "rank", 6, kills),
        "killed_by_pilot_ranks": _rows(
            "deaths by the human killer's rank", "rank", 25, ("times",)
        ),
        "killed_by_ai_ranks": _rows(
            "deaths by the computer killer's rank", "rank", 6, ("times",)
        ),
        "on_players": _rows(
            "the pilot's kills of each player", "entry", 8, ("kills", "shared")
        ),
        "from_players": _rows(
            "each player's kills of the pilot", "entry", 8, ("kills", "shared")
        ),
        "on_flight_groups": _rows(
            "the pilot's kills of each flight group",
            "flight_group",
            48,
            ("kills", "shared"),
        ),
        "from_flight_groups": _rows(
            "each flight group's kills of the pilot",
            "flight_group",
            48,
            ("kills", "shared"),
        ),
    }
    return _closed(
        "The local pilot's own record of the battle, the numbers its debriefing "
        "shows; each machine keeps only its own pilot's.",
        members,
    )


def result_schema(version: int = model.RESULT_SCHEMA_VERSION) -> dict[str, Any]:
    """Return the JSON Schema of the launch result of ``version`` (2, 3 or 4).

    Always the same dict for a version. Version 3 adds the closed ``sith``
    object; version 2 is what a game build from before the Sith setting
    writes. Version 4 adds ``local_entry``, ``players``, ``teams`` and
    ``detail``, the game's own counts as it holds them, so their numbers carry
    no bounds but their indexes do. The score may be below zero; version 3's
    counts and time may not. Does not encode that a status and its exit code
    go together, that a battle not flown has zeros for its numbers and empty
    lists, or that local_entry names a listed player. Raises ValueError for
    any other version.
    """
    if version not in model.RESULT_SCHEMA_VERSIONS:
        raise ValueError(f"no result schema of version {version}")
    numbers = {
        name: {"type": "integer", "minimum": 0}
        for name in ("kills", "losses", "goals_met_seconds")
    }
    numbers["goals_met_seconds"]["description"] = (
        "Seconds into the mission when the pilot's team met its goals; 0 when it never did."
    )
    numbers["score"] = {
        "description": "The pilot's score; below zero when the pilot leaves a melee early.",
        "type": "integer",
    }
    return {
        "$schema": DRAFT,
        "$id": RESULT_ID,
        "title": "Launch result",
        "description": "How a --launch battle ended: what the battle wrote into the pilot record, or why it did not run.",
        "type": "object",
        "additionalProperties": False,
        "required": [
            "schema_version",
            "status",
            "exit_code",
            "outcome",
            "score",
            "kills",
            "losses",
            "goals_met_seconds",
            "why",
            *(["sith"] if version >= 3 else []),
            *(["local_entry", "players", "teams", "detail"] if version >= 4 else []),
        ],
        "properties": {
            "schema_version": {"const": version},
            "status": {"enum": list(model.STATUSES)},
            "exit_code": {
                "description": "The engine's exit code: 0 flown, 3 refused, 4 not reached, 5 failed.",
                "enum": sorted(model.EXIT_CODES.values()),
            },
            "outcome": {"enum": list(model.OUTCOMES)},
            **numbers,
            "why": {
                "description": "A plain sentence when the battle was not flown; empty otherwise.",
                "type": "string",
            },
            **({"sith": _sith_block()} if version >= 3 else {}),
            **(_battle_members() if version >= 4 else {}),
        },
    }


def _battle_members() -> dict[str, Any]:
    """Return version 4's members: who flew and how each fared."""
    return {
        "local_entry": _index(
            "The local pilot's place in the pilot record; -1 when it has none.",
            -1,
            model.RESULT_PLAYERS - 1,
        ),
        "players": _players(),
        "teams": _teams(),
        "detail": _detail(),
    }
