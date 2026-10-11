"""The launch description's and result's schemas: files, examples, refusals.

Purpose:
    Prove the published schema files equal what the model builds, that made-up
    descriptions and results fit them, that each way to break them is refused,
    and that the package's own checker (the launcher uses no third-party
    library) says what the reference validator says.

Flow:
    Load the two schema files; check them with ``jsonschema``; run the same
    documents through ``jedimaster.launch.check.errors``.

Invariants:
    - No game data; every document is made up.

Call:
    ``pytest tests/test_launch_schema.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest

from jedimaster.launch.check import errors
from jedimaster.launch.check import valid
from jedimaster.launch.model import build_description
from jedimaster.launch.model import GAME_TYPES
from jedimaster.launch.schema import launch_schema
from jedimaster.launch.schema import result_schema

logger = logging.getLogger(__name__)

SCHEMA_DIR = Path(__file__).resolve().parents[1] / "schema"

GOOD_DESCRIPTION = {
    "schema_version": 1,
    "mode": "single_player",
    "game_type": "training",
    "side": "rebel",
    "mission": "train/1ta02bz.tie",
    "pilot": "Ace",
}
GOOD_RESULT = {
    "schema_version": 1,
    "status": "flown",
    "exit_code": 0,
    "outcome": "completed",
    "score": 120,
    "kills": 4,
    "losses": 1,
    "flight_seconds": 305,
    "why": "",
}


def reference(schema_name: str) -> jsonschema.Draft202012Validator:
    """Return the reference validator of a published schema file."""
    schema = json.loads((SCHEMA_DIR / schema_name).read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def changed(base: dict, **members) -> dict:
    """Return a copy of base with members set; a member of None is removed."""
    doc = copy.deepcopy(base)
    for name, value in members.items():
        if value is None:
            doc.pop(name, None)
        else:
            doc[name] = value
    return doc


def test_launch_schema_file_matches_model():
    text = json.dumps(launch_schema(), indent=2) + "\n"
    assert (SCHEMA_DIR / "launch.schema.json").read_text(encoding="utf-8") == text, (
        "schema/launch.schema.json is stale: run python tools/gen_schema.py"
    )


def test_result_schema_file_matches_model():
    text = json.dumps(result_schema(), indent=2) + "\n"
    assert (SCHEMA_DIR / "result.schema.json").read_text(encoding="utf-8") == text, (
        "schema/result.schema.json is stale: run python tools/gen_schema.py"
    )


GOOD_DESCRIPTIONS = [
    GOOD_DESCRIPTION,
    changed(GOOD_DESCRIPTION, result="/logs/result.json", seed=0),
    changed(GOOD_DESCRIPTION, seed=4294967295),
    changed(GOOD_DESCRIPTION, settings={}),
    changed(
        GOOD_DESCRIPTION,
        settings={
            "difficulty": "easy_cheat",
            "collisions": False,
            "craft_jumping": True,
            "random_setup": True,
            "battle_length": 4,
            "mission_time_limit": 20,
            "continue_sequence": "restart",
        },
    ),
    changed(GOOD_DESCRIPTION, settings={"mission_time_limit": "none"}),
    changed(GOOD_DESCRIPTION, settings={"mission_time_limit": "default"}),
]

BAD_DESCRIPTIONS = [
    changed(GOOD_DESCRIPTION, schema_version=2),
    changed(GOOD_DESCRIPTION, mode="multiplayer"),
    changed(GOOD_DESCRIPTION, mode=None),
    changed(GOOD_DESCRIPTION, game_type="duel"),
    changed(GOOD_DESCRIPTION, side="hutt"),
    changed(GOOD_DESCRIPTION, side=None),
    changed(GOOD_DESCRIPTION, mission="train"),
    changed(GOOD_DESCRIPTION, mission="train/a b.tie"),
    changed(GOOD_DESCRIPTION, mission="train/../a.tie"),
    changed(GOOD_DESCRIPTION, mission="train/" + "a" * 64),
    changed(GOOD_DESCRIPTION, pilot=""),
    changed(GOOD_DESCRIPTION, pilot="A" * 13),
    changed(GOOD_DESCRIPTION, pilot=None),
    changed(GOOD_DESCRIPTION, result=""),
    changed(GOOD_DESCRIPTION, result="r" * 512),
    changed(GOOD_DESCRIPTION, seed=-1),
    changed(GOOD_DESCRIPTION, seed=4294967296),
    changed(GOOD_DESCRIPTION, seed="7"),
    changed(GOOD_DESCRIPTION, players=2),
    changed(GOOD_DESCRIPTION, settings=[]),
    changed(GOOD_DESCRIPTION, settings={"volume": 3}),
    changed(GOOD_DESCRIPTION, settings={"difficulty": "brutal"}),
    changed(GOOD_DESCRIPTION, settings={"collisions": "on"}),
    changed(GOOD_DESCRIPTION, settings={"battle_length": 5}),
    changed(GOOD_DESCRIPTION, settings={"mission_time_limit": 21}),
    changed(GOOD_DESCRIPTION, settings={"mission_time_limit": 0}),
    changed(GOOD_DESCRIPTION, settings={"mission_time_limit": "soon"}),
    changed(GOOD_DESCRIPTION, settings={"continue_sequence": "maybe"}),
]


def test_good_descriptions_fit_both_validators():
    for doc in GOOD_DESCRIPTIONS:
        assert list(reference("launch.schema.json").iter_errors(doc)) == [], doc
        assert errors(doc, launch_schema()) == [], doc


def test_bad_descriptions_are_refused_by_both_validators():
    for doc in BAD_DESCRIPTIONS:
        assert list(reference("launch.schema.json").iter_errors(doc)), doc
        assert errors(doc, launch_schema()), doc


BAD_RESULTS = [
    changed(GOOD_RESULT, schema_version=2),
    changed(GOOD_RESULT, status="done"),
    changed(GOOD_RESULT, exit_code=1),
    changed(GOOD_RESULT, exit_code=6),
    changed(GOOD_RESULT, outcome="won"),
    changed(GOOD_RESULT, kills=-1),
    changed(GOOD_RESULT, losses=-1),
    changed(GOOD_RESULT, flight_seconds=-1),
    changed(GOOD_RESULT, score=1.5),
    changed(GOOD_RESULT, kills="4"),
    changed(GOOD_RESULT, losses=1.5),
    changed(GOOD_RESULT, flight_seconds=True),
    changed(GOOD_RESULT, why=None),
    changed(GOOD_RESULT, why=7),
    changed(GOOD_RESULT, extra=1),
    [],
]


def test_good_results_fit_both_validators():
    for status, code in (
        ("flown", 0),
        ("refused", 3),
        ("not_reached", 4),
        ("failed", 5),
    ):
        doc = changed(
            GOOD_RESULT, status=status, exit_code=code, why="because", score=-2000
        )
        assert list(reference("result.schema.json").iter_errors(doc)) == []
        assert valid(doc, result_schema())


def test_bad_results_are_refused_by_both_validators():
    for doc in BAD_RESULTS:
        assert list(reference("result.schema.json").iter_errors(doc)), doc
        assert errors(doc, result_schema()), doc


def test_every_built_description_fits():
    for game_type in GAME_TYPES:
        side = "imperial" if game_type in ("training", "combat", "campaign") else None
        doc = build_description(game_type, side, "a_1.lst", "Ace")
        assert errors(doc, launch_schema()) == [], game_type


def test_every_object_is_closed():
    for schema in (
        launch_schema(),
        result_schema(),
        launch_schema()["properties"]["settings"],
    ):
        assert schema["additionalProperties"] is False


def test_the_checker_refuses_a_keyword_it_does_not_know():
    with pytest.raises(ValueError, match="not understood"):
        errors({}, {"type": "object", "dependentRequired": {}})


def test_the_checker_counts_a_bool_as_no_integer():
    assert errors(True, {"type": "integer"})
    assert errors(5, {"type": "integer", "minimum": 1, "maximum": 4})
    assert errors("x", {"type": "string", "minLength": 2})
    assert errors("abc", {"type": "string", "maxLength": 2})
    assert errors("ab", {"type": "string", "minLength": 2, "maxLength": 2}) == []
    assert errors(1, {"type": "integer", "minimum": 1, "maximum": 1}) == []
    assert (
        errors(
            {"a": 1},
            {
                "type": "object",
                "properties": {"a": {"type": "integer"}},
                "required": ["a"],
            },
        )
        == []
    )
    assert errors({}, {"type": "object", "required": ["a"]})
    assert errors(
        {"b": 1}, {"type": "object", "properties": {}, "additionalProperties": False}
    )
    assert errors("a", {"oneOf": [{"enum": ["a"]}, {"enum": ["a", "b"]}]})
