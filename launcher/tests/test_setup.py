"""Checks the setup document's schema, its fingerprint and the engine values.

The documents are built here; no game data is read.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from jedimaster import setup

SCHEMA = json.loads(
    (
        Path(__file__).resolve().parent.parent / "schema" / "setup.schema.json"
    ).read_text()
)
VALIDATOR = Draft202012Validator(SCHEMA)


def sample() -> dict:
    """Return a valid two-player setup document."""
    return {
        "schema_version": 1,
        "revision": 7,
        "build": "0.1.0+abc1234",
        "host": "p1",
        "mission": {"path": "Melee/8B02W08.TIE", "game_type": "melee"},
        "settings": {
            "difficulty": "medium",
            "collisions": True,
            "craft_jumping": False,
            "random_setup": False,
            "battle_length": 3,
            "in_progress_join": False,
            "craft_selection": "on",
            "locate_players": True,
            "craft_waves": "default",
            "mission_time_limit": "default",
            "last_team_time_limit": "none",
            "random_seed": 4242,
            "internet_play": True,
            "ai_opponents": True,
            "server_update_rate": 8,
            "combat_balance": "neutral",
            "continue_sequence": "restart",
        },
        "password_required": False,
        "engine_options": {"rules": "1997"},
        "roster": [
            {
                "id": "p1",
                "name": "Rook",
                "ready": True,
                "ping_ms": 0,
                "flight_group": 0,
            },
            {
                "id": "p2",
                "name": "Wedge",
                "ready": False,
                "ping_ms": 41,
                "flight_group": 1,
            },
        ],
        "teams": [["p1"], ["p2"]],
        "launch": {"frozen": False},
    }


def test_schema_is_a_valid_schema() -> None:
    Draft202012Validator.check_schema(SCHEMA)


def test_sample_is_valid() -> None:
    assert list(VALIDATOR.iter_errors(sample())) == []


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("settings", "mission_time_limit"), 21),
        (("settings", "mission_time_limit"), 0),
        (("settings", "last_team_time_limit"), 11),
        (("settings", "last_team_time_limit"), "default"),
        (("settings", "server_update_rate"), 5),
        (("settings", "battle_length"), 1),
        (("settings", "difficulty"), "insane"),
        (("settings", "random_seed"), 4294967296),
        (("engine_options", "rules"), "1998"),
        (("mission", "path"), "Melee\\8B02W08.TIE"),
        (("schema_version",), 2),
    ],
)
def test_out_of_range_values_are_refused(path: tuple, value: object) -> None:
    document = sample()
    target = document
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(VALIDATOR.iter_errors(document)), f"{path} = {value!r} was accepted"


def test_unknown_setting_and_ninth_player_are_refused() -> None:
    document = sample()
    document["settings"]["warp_speed"] = True
    assert list(VALIDATOR.iter_errors(document))
    document = sample()
    document["roster"] = [
        {"id": f"p{n}", "name": f"Pilot {n}", "ready": False} for n in range(9)
    ]
    assert list(VALIDATOR.iter_errors(document))


def test_fingerprint_ignores_order_revision_ready_ping_and_launch() -> None:
    base = setup.setup_fingerprint(sample())
    changed = sample()
    changed["revision"] = 99
    changed["roster"][1]["ready"] = True
    changed["roster"][0]["ping_ms"] = 300
    changed["launch"] = {"frozen": True, "fingerprint": "0" * 64}
    reordered = json.loads(json.dumps(changed, sort_keys=False))
    reordered = dict(reversed(list(reordered.items())))
    assert setup.setup_fingerprint(changed) == base
    assert setup.setup_fingerprint(reordered) == base
    assert len(base) == 64 and int(base, 16) >= 0


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("settings", "collisions"), False),
        (("settings", "random_seed"), 4243),
        (("engine_options", "rules"), "fixed"),
        (("mission", "path"), "Melee/8B02W09.TIE"),
        (("build",), "0.1.1"),
        (("roster", 1, "flight_group"), 2),
        (("teams",), [["p1", "p2"]]),
    ],
)
def test_fingerprint_changes_with_the_battle(path: tuple, value: object) -> None:
    document = sample()
    target = document
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert setup.setup_fingerprint(document) != setup.setup_fingerprint(sample())


def test_fingerprint_does_not_change_the_document() -> None:
    document = sample()
    before = copy.deepcopy(document)
    setup.setup_fingerprint(document)
    assert document == before


def test_engine_values() -> None:
    values = setup.engine_settings(sample())
    assert values == {
        "collisions": 1,
        "craft_jumping": 0,
        "random_setup": 0,
        "in_progress_join": 0,
        "locate_players": 1,
        "internet_play": 1,
        "ai_opponents": 1,
        "difficulty": 1,
        "battle_length": 1,
        "craft_selection": 1,
        "craft_waves": 1,
        "combat_balance": 2,
        "continue_sequence": 0,
        "mission_time_limit": 255,
        "last_team_time_limit": 0,
        "random_seed": 4242,
        "server_update_rate": 8,
    }


@pytest.mark.parametrize(
    ("name", "value", "engine"),
    [
        ("mission_time_limit", "none", 0),
        ("mission_time_limit", 20, 20),
        ("last_team_time_limit", 10, 10),
        ("difficulty", "easy_cheat", 3),
        ("battle_length", 4, 2),
        ("craft_selection", "host_only", 2),
        ("craft_waves", "unlimited", 2),
        ("combat_balance", "favor_rebel", 3),
        ("continue_sequence", "continue", 1),
    ],
)
def test_engine_value_edges(name: str, value: object, engine: int) -> None:
    document = sample()
    document["settings"][name] = value
    assert setup.engine_settings(document)[name] == engine


def test_engine_values_refuse_an_unknown_name() -> None:
    document = sample()
    document["settings"]["craft_waves"] = "lots"
    with pytest.raises(ValueError, match="craft_waves"):
        setup.engine_settings(document)
