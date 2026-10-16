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
GOOD_RESULT_V2 = {
    "schema_version": 2,
    "status": "flown",
    "exit_code": 0,
    "outcome": "completed",
    "score": 120,
    "kills": 4,
    "losses": 1,
    "goals_met_seconds": 305,
    "why": "",
}
"""A result from a game build from before the Sith setting."""
SITH_NONE = {
    "setting": "none",
    "flight_group": -1,
    "flight_name": "",
    "craft_type": -1,
    "apprentices_flown": 0,
    "apprentices_lost": 0,
    "lord_flown": 0,
    "lord_lost": 0,
    "why": "",
}
SITH_LORD = {
    "setting": "lord",
    "flight_group": 2,
    "flight_name": "Beta",
    "craft_type": 6,
    "apprentices_flown": 2,
    "apprentices_lost": 1,
    "lord_flown": 1,
    "lord_lost": 1,
    "why": "",
}
GOOD_RESULT_V3 = {**GOOD_RESULT_V2, "schema_version": 3, "sith": SITH_NONE}
"""A result of version 3: the same, with the closed ``sith`` block."""
PLAYER = {
    "entry": 0,
    "slot": 0,
    "name": "Kestrel",
    "team": 0,
    "flight_group": 0,
    "rating": 14,
    "score": 101,
    "kills": 102,
    "shared": 103,
    "assists": 104,
    "losses": 105,
    "shots": 106,
    "hits": 107,
    "warheads": 108,
    "warhead_hits": 109,
    "has_left": False,
}
"""A player of a version 4 result; the numbers are made up."""
NO_PLACE = {
    **PLAYER,
    "entry": 5,
    "slot": -1,
    "name": "Nightw1ng_42",
    "team": -1,
    "shots": 0,
    "hits": 0,
    "warheads": 0,
    "warhead_hits": 0,
    "has_left": True,
}
"""A player no place in the flight held, with the longest name."""
TEAM = {
    "team": 0,
    "completed": True,
    "score": 2601,
    "kills": 31,
    "shared": 7,
    "losses": 3,
    "seconds": 645,
}
DETAIL = {
    "shots": 811,
    "hits": 812,
    "warheads": 813,
    "warhead_hits": 814,
    "losses": 815,
    "losses_to_collisions": 816,
    "losses_to_starships": 817,
    "losses_to_mines": 818,
    "friendlies_killed": 819,
    "special_inspected": 820,
    "by_craft": [{"craft_type": 99, "kills": 21, "shared": 0, "assists": 0}],
    "on_pilot_ranks": [{"rank": 24, "kills": 31, "shared": 0, "assists": 0}],
    "on_ai_ranks": [{"rank": 5, "kills": 41, "shared": 0, "assists": 0}],
    "killed_by_pilot_ranks": [{"rank": 0, "times": 51}],
    "killed_by_ai_ranks": [{"rank": 5, "times": 52}],
    "on_players": [{"entry": 7, "kills": 61, "shared": 0}],
    "from_players": [{"entry": 0, "kills": 63, "shared": 0}],
    "on_flight_groups": [{"flight_group": 47, "kills": 0, "shared": 72}],
    "from_flight_groups": [{"flight_group": 0, "kills": 73, "shared": 0}],
}
"""The local pilot's tables, each list holding its last or first row."""
GOOD_RESULT = {
    **GOOD_RESULT_V3,
    "schema_version": 4,
    "local_entry": 5,
    "players": [PLAYER, NO_PLACE],
    "teams": [TEAM, {**TEAM, "team": 7, "completed": False}],
    "detail": DETAIL,
}
"""A result of version 4: the same, with every player and team and the pilot's tables."""


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
    changed(GOOD_DESCRIPTION, ai_program={"python": "python3", "path": "ai/x"}),
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
    changed(GOOD_DESCRIPTION, ai_program="python3"),
    changed(GOOD_DESCRIPTION, ai_program={"python": "python3"}),
    changed(GOOD_DESCRIPTION, ai_program={"python": "python3", "path": ""}),
    changed(GOOD_DESCRIPTION, ai_program={"python": "p", "path": "a" * 512}),
    changed(GOOD_DESCRIPTION, ai_program={"python": "p", "path": "a", "args": "b"}),
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
    changed(GOOD_RESULT, schema_version=1),
    changed(GOOD_RESULT, status="done"),
    changed(GOOD_RESULT, exit_code=1),
    changed(GOOD_RESULT, exit_code=6),
    changed(GOOD_RESULT, outcome="won"),
    changed(GOOD_RESULT, kills=-1),
    changed(GOOD_RESULT, losses=-1),
    changed(GOOD_RESULT, goals_met_seconds=-1),
    changed(GOOD_RESULT, score=1.5),
    changed(GOOD_RESULT, kills="4"),
    changed(GOOD_RESULT, losses=1.5),
    changed(GOOD_RESULT, goals_met_seconds=True),
    changed(GOOD_RESULT, why=None),
    changed(GOOD_RESULT, why=7),
    changed(GOOD_RESULT, extra=1),
    changed(GOOD_RESULT, schema_version=2),
    {k: v for k, v in GOOD_RESULT.items() if k != "sith"},
    changed(GOOD_RESULT, sith={**SITH_NONE, "setting": "emperor"}),
    changed(GOOD_RESULT, sith={**SITH_NONE, "extra": 1}),
    changed(GOOD_RESULT, sith={k: v for k, v in SITH_NONE.items() if k != "why"}),
    changed(GOOD_RESULT, sith={**SITH_LORD, "apprentices_lost": -1}),
    changed(GOOD_RESULT, sith={**SITH_LORD, "flight_group": -2}),
    changed(GOOD_RESULT, sith={**SITH_LORD, "flight_name": 7}),
    changed(GOOD_RESULT, schema_version=3),
    GOOD_RESULT_V3,
    changed(GOOD_RESULT, local_entry=8),
    changed(GOOD_RESULT, local_entry=-2),
    changed(GOOD_RESULT, players=None),
    changed(GOOD_RESULT, players=PLAYER),
    changed(GOOD_RESULT, players=[PLAYER] * 9),
    changed(GOOD_RESULT, players=[{**PLAYER, "entry": 8}]),
    changed(GOOD_RESULT, players=[{**PLAYER, "slot": -2}]),
    changed(GOOD_RESULT, players=[{**PLAYER, "name": "Nightw1ng_423"}]),
    changed(GOOD_RESULT, players=[{**PLAYER, "has_left": 0}]),
    changed(GOOD_RESULT, players=[{**PLAYER, "kills": 1.5}]),
    changed(GOOD_RESULT, players=[{**PLAYER, "extra": 1}]),
    changed(GOOD_RESULT, players=[{k: v for k, v in PLAYER.items() if k != "hits"}]),
    changed(GOOD_RESULT, teams=[TEAM] * 9),
    changed(GOOD_RESULT, teams=[{**TEAM, "team": 8}]),
    changed(GOOD_RESULT, teams=[{**TEAM, "completed": 1}]),
    changed(GOOD_RESULT, detail=None),
    changed(GOOD_RESULT, detail={**DETAIL, "extra": 1}),
    changed(GOOD_RESULT, detail={k: v for k, v in DETAIL.items() if k != "by_craft"}),
    changed(
        GOOD_RESULT,
        detail={
            **DETAIL,
            "by_craft": [{"craft_type": 100, "kills": 1, "shared": 0, "assists": 0}],
        },
    ),
    changed(
        GOOD_RESULT,
        detail={
            **DETAIL,
            "on_ai_ranks": [{"rank": 6, "kills": 1, "shared": 0, "assists": 0}],
        },
    ),
    changed(GOOD_RESULT, detail={**DETAIL, "killed_by_ai_ranks": [{"rank": 0}]}),
    changed(
        GOOD_RESULT,
        detail={**DETAIL, "on_players": [{"entry": 0, "kills": 1, "shared": 0}] * 9},
    ),
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


def test_every_result_version_reads_and_each_refuses_the_others_shapes():
    assert valid(GOOD_RESULT_V2, result_schema(2))
    assert valid(changed(GOOD_RESULT_V3, sith=SITH_LORD), result_schema(3))
    assert valid(changed(GOOD_RESULT, sith=SITH_LORD), result_schema(4))
    assert valid(GOOD_RESULT, result_schema())
    assert not valid(GOOD_RESULT_V2, result_schema(3))
    assert not valid(GOOD_RESULT_V3, result_schema(2))
    assert not valid(GOOD_RESULT_V3, result_schema(4))
    assert not valid(GOOD_RESULT, result_schema(3))
    with pytest.raises(ValueError, match="no result schema"):
        result_schema(5)


def test_a_battle_not_flown_has_empty_lists():
    doc = changed(
        GOOD_RESULT,
        status="refused",
        exit_code=3,
        why="The pilot is missing.",
        local_entry=-1,
        players=[],
        teams=[],
        detail={
            **{name: 0 for name in DETAIL if not isinstance(DETAIL[name], list)},
            **{name: [] for name in DETAIL if isinstance(DETAIL[name], list)},
        },
    )
    assert list(reference("result.schema.json").iter_errors(doc)) == []
    assert valid(doc, result_schema())


def test_a_full_battle_of_eight_players_and_teams_fits():
    players = [{**PLAYER, "entry": e, "slot": 7 - e} for e in range(8)]
    teams = [{**TEAM, "team": n} for n in range(8)]
    doc = changed(GOOD_RESULT, local_entry=7, players=players, teams=teams)
    assert list(reference("result.schema.json").iter_errors(doc)) == []
    assert valid(doc, result_schema())


def test_a_flown_result_with_a_sith_flight_fits_the_reference_validator():
    doc = changed(GOOD_RESULT, sith=SITH_LORD)
    assert list(reference("result.schema.json").iter_errors(doc)) == []


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


def test_the_checker_reads_lists_item_by_item():
    numbers = {"type": "array", "maxItems": 2, "items": {"type": "integer"}}
    assert errors([1, 2], numbers) == []
    assert errors([], numbers) == []
    assert errors([1, 2, 3], numbers) == ["the document must hold at most 2 items"]
    assert errors([1, "2"], numbers) == ["the document[1] must be a integer"]
    assert errors({"0": 1}, numbers) == ["the document must be a array"]
    assert errors([1, 2], {"type": "object"}) == ["the document must be a object"]
    assert errors([[1, 2, 3]], {"type": "array", "items": numbers}) == [
        "the document[0] must hold at most 2 items"
    ]


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
