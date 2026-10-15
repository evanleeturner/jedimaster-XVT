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


def result_schema(version: int = model.RESULT_SCHEMA_VERSION) -> dict[str, Any]:
    """Return the JSON Schema of the launch result of ``version`` (2 or 3).

    Always the same dict for a version. Version 3 adds the closed ``sith``
    object; version 2 is what a game build from before the Sith setting
    writes. The score may be below zero; the counts and the time may not.
    Does not encode that a status and its exit code go together, or that a
    battle not flown has zeros for its numbers. Raises ValueError for any
    other version.
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
            *(["sith"] if version == 3 else []),
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
            **({"sith": _sith_block()} if version == 3 else {}),
        },
    }
