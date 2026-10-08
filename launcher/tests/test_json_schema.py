"""JSON export: raw values, the document's names, and the published schema.

Purpose:
    Prove that exported synthetic missions (both versions) validate against
    ``schema/mission.schema.json``, that raw values survive with the
    document's names beside them, that the schema file matches the model,
    and that the schema rejects data it should reject.

Flow:
    Build synthetic missions, export, validate with jsonschema (draft
    2020-12), inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_json_schema.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from builder import build_mission
from builder import distinct_values

from jedimaster.mission import mission_to_json
from jedimaster.mission import read_mission
from jedimaster.mission.to_json import mission_schema

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "mission.schema.json"


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def full_mission(version: int) -> bytes:
    """Return a synthetic mission with every section filled."""
    fg = distinct_values("FlightGroup", 2)
    fg["roles"] = b"1PRIhsec"
    fg["craft_type"] = 1
    fg["optional_warheads"] = [4, 3, 0, 0, 0, 0, 0, 99]
    fg["waypoints"] = [(1, -2, 3, 1)] * 22
    messages = [distinct_values("Message", 3)]
    goals = [dict(distinct_values("GlobalGoal", t), num_goals=3) for t in range(10)]
    teams = [distinct_values("Team", t) for t in range(10)]
    briefings = [
        {"events": [(0, 0x05, 0), (9999, 0x22)], "strings": ["caf\xe9"], "tags": ["x"]}
    ]
    return build_mission(
        version,
        header={"mission_type": 2, "iff_names": ["1Red", "", "", "Blue"]},
        flight_groups=[fg, {"name": "Beta"}],
        messages=messages,
        global_goals=goals,
        teams=teams,
        briefings=briefings,
        fg_goal_strings={(0, 0, 0): "g"},
        global_goal_strings={(0, 0, 0, 0): "gg"},
        descriptions=["one", "two", "three"],
    )


@pytest.mark.parametrize("version", (12, 14))
def test_export_validates_against_schema(validator, version):
    data = mission_to_json(read_mission(full_mission(version)))
    errors = sorted(validator.iter_errors(data), key=lambda e: list(e.path))
    assert errors == []
    text = json.dumps(data, ensure_ascii=False)
    assert json.loads(text) == data
    assert len(data["descriptions"]) == (1 if version == 12 else 3)


def test_empty_mission_validates(validator):
    for version in (12, 14):
        assert (
            list(
                validator.iter_errors(
                    mission_to_json(read_mission(build_mission(version)))
                )
            )
            == []
        )


def test_raw_values_kept_with_document_names():
    data = mission_to_json(read_mission(full_mission(14)))
    assert data["format"] == "jedimaster.mission"
    assert data["header"]["platform_id"] == 14
    assert data["header"]["platform_id_name"] == "BoP"
    assert data["header"]["mission_type_name"] == "Melee"
    fg = data["flight_groups"][0]
    assert fg["craft_type"] == 1 and fg["craft_type_name"] == "X-wing"
    assert fg["optional_warheads_names"][:3] == [
        "Torpedo",
        "Concussion Missile",
        "None",
    ]
    assert fg["optional_warheads_names"][7] is None  # 99 is not in the list
    assert fg["roles"][0]["designation_team_name"] == "Team1"
    assert fg["roles"][0]["designation_name"] == "PRImary Target"
    assert fg["roles"][1]["designation_team_name"] == "Hostiles"
    assert fg["roles"][1]["designation_name"] == "SECondary Target"
    assert fg["roles_text"] == "1PRIhsec"
    assert data["briefings"][0]["events"][0] == {
        "time": 0,
        "type": 5,
        "type_name": "Caption Text",
        "variables": [0],
    }
    assert data["briefings"][0]["strings"][0] == "caf\xe9"
    assert data["layout"]["flight_groups"] == 0xA4


def test_schema_file_matches_model():
    text = json.dumps(mission_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/mission.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("header", "mission_type"), 256),
        (("header", "num_fgs"), "2"),
        (("flight_groups", 0, "goals", 0, "points"), 200),
        (("flight_groups", 0, "name"), "x" * 21),
        (("teams", 0, "allegiances"), [0] * 11),
        (("flight_groups", 0, "extra_field"), 1),
        (("briefings", 0, "events_complete"), "yes"),
    ],
)
def test_schema_rejects_bad_data(validator, path, value):
    data = copy.deepcopy(mission_to_json(read_mission(full_mission(12))))
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path
