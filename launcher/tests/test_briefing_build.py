"""The briefing bundle: which briefing each team sees, and what it holds.

Purpose:
    Prove the bundle built from made-up mission bytes and a made-up install:
    the rule for the briefing a team sees, the briefings used, the flight
    groups with their briefing points, team names, the page word, font
    metrics, the sheets and sounds found, the events read with the setup
    screen's counts, and that every bundle satisfies its schema.

Flow:
    ``briefingdata`` writes the install and the mission bytes; each test
    builds a bundle and checks one rule.

Invariants:
    - No game data; no browser.

Call:
    ``pytest tests/test_briefing_build.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import jsonschema
import pytest
from briefingdata import briefing_points
from briefingdata import flagged
from briefingdata import make_install
from briefingdata import mission_bytes
from briefingdata import PAGE_WORD
from briefingdata import point
from builder import build_mission

from jedimaster.briefing import BriefingBuildError
from jedimaster.briefing import build_bundle
from jedimaster.briefing.build import team_briefing
from jedimaster.briefing.build import walk_events
from jedimaster.briefing.schema import briefing_schema
from jedimaster.mission import read_mission

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "briefing.schema.json"


@pytest.fixture
def install(tmp_path: Path) -> Path:
    return make_install(tmp_path / "XvT")


def bundle_of(install: Path, **parts) -> dict:
    bundle = build_bundle(install, read_mission(mission_bytes(**parts)))
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.validate(bundle, schema, cls=jsonschema.Draft202012Validator)
    return bundle


def test_the_last_briefing_with_the_teams_flag_is_the_one_it_sees():
    flags = [flagged(0), flagged(0, 1), flagged(1), flagged()]
    assert team_briefing(flags, 0) == 1
    assert team_briefing(flags, 1) == 2
    assert team_briefing(flags, 2) is None


def test_teams_list_their_briefing_and_name(install):
    briefings = [{"viewed_by_team": flagged(0, 3)}, {"viewed_by_team": flagged(3)}]
    bundle = bundle_of(install, briefings=briefings, team_names=["Red", "Blue"])
    assert [t["briefing"] for t in bundle["teams"]] == [0, None, None, 1] + [None] * 6
    assert [t["name"] for t in bundle["teams"][:3]] == ["Red", "Blue", ""]
    assert [t["team"] for t in bundle["teams"]] == list(range(10))


def test_only_the_briefings_some_team_sees_are_in_the_bundle(install):
    briefings = [{}, {"viewed_by_team": flagged(2)}, {}, {"viewed_by_team": flagged(5)}]
    bundle = bundle_of(install, briefings=briefings)
    assert [b["index"] for b in bundle["briefings"]] == [1, 3]


def test_no_team_with_a_briefing_leaves_the_list_empty(install):
    assert bundle_of(install)["briefings"] == []


def test_a_briefing_carries_its_time_events_and_texts(install):
    brief = {
        "viewed_by_team": flagged(0),
        "running_time": 321,
        "events": [(0, 6, 5, -7), (4, 9, 2), (9999, 34)],
        "tags": ["label zero", "\xe9t\xe9"],
        "strings": ["", "caption one"],
    }
    entry = bundle_of(install, briefings=[brief])["briefings"][0]
    assert entry["running_time"] == 321
    assert entry["events"] == [
        {"time": 0, "type": 6, "variables": [5, -7]},
        {"time": 4, "type": 9, "variables": [2]},
        {"time": 9999, "type": 34, "variables": []},
    ]
    assert len(entry["labels"]) == len(entry["captions"]) == 32
    assert entry["labels"][:3] == ["label zero", "\xe9t\xe9", ""]
    assert entry["captions"][1] == "caption one"


def test_flight_groups_take_the_briefing_points_14_to_21(install):
    points = briefing_points(*[point(10 * k, -k, k % 2) for k in range(8)])
    group = {
        "name": "Alpha",
        "craft_type": 7,
        "iff": 3,
        "team": 2,
        "player_number": 4,
        "waypoints": points,
    }
    bundle = bundle_of(install, groups=[group, {"name": "Beta"}])
    first = bundle["groups"][0]
    assert (first["number"], first["name"], first["craft_type"]) == (0, "Alpha", 7)
    assert (first["iff"], first["team"], first["player_number"]) == (3, 2, 4)
    assert first["points"] == [
        {"x": 10 * k, "y": -k, "enabled": bool(k % 2)} for k in range(8)
    ]
    assert bundle["groups"][1]["number"] == 1 and bundle["groups"][1]["name"] == "Beta"


def test_the_page_word_is_line_640_of_the_front_text(install):
    assert bundle_of(install)["front_string"] == PAGE_WORD


def test_a_missing_front_text_gives_no_text(install):
    (install / "FRONTTXT.TXT").unlink()
    assert bundle_of(install)["front_string"] == "No text."


def test_font_10_metrics_come_from_the_font(install):
    font = bundle_of(install)["font"]
    assert font["spacing"] == 0 and font["height"] == 2
    assert len(font["widths"]) == 256 and font["widths"][65] == 3
    assert font["widths"][66] == 1 and font["widths"][67] == 2
    assert font["picture"] == "times10.png"
    assert (font["columns"], len(font["text_colors"])) == (16, 5)
    assert font["cell_width"] >= 3 and font["cell_height"] >= 2


def test_a_missing_font_is_an_error(install):
    (install / "TIMES10.ABP").unlink()
    with pytest.raises(BriefingBuildError):
        build_bundle(install, read_mission(mission_bytes()))


def test_the_icon_tables_are_the_games_tables(install):
    bundle = bundle_of(install)
    assert len(bundle["boxes"]) == 70 and len(bundle["craft_boxes"]) == 106
    assert bundle["iff_sheets"][:6] == [f"mapicon{n}" for n in (0, 1, 2, 3, 1, 4)]
    assert set(bundle["iff_sheets"][6:]) == {"mapicon0"}
    assert [s["name"] for s in bundle["sheets"]] == [f"mapicon{n}" for n in range(5)]
    assert bundle["grey_sheet"] == {"name": "greyicon", "picture": "greyicon.png"}


def test_a_missing_sheet_is_left_out(install):
    (install / "frontres" / "S5.BMP").unlink()
    (install / "frontres" / "S3.BMP").unlink()
    (install / "BalanceOfPower" / "FRONTRES" / "s5.bmp").unlink()
    bundle = bundle_of(install)
    assert "mapicon3" not in [s["name"] for s in bundle["sheets"]]
    assert bundle["grey_sheet"] is None


def test_sounds_are_those_of_the_list_that_have_a_file(install):
    bundle = bundle_of(install)
    names = [s["name"] for s in bundle["sounds"]]
    assert names == ["jewelsound", "sfxTarget1", "sfxTarget2", "sfxText"]
    assert bundle["sounds"][1]["picture"] == "sfxTarget1.wav"
    (install / "SFX" / "t2.wav").unlink()
    assert "sfxTarget2" not in [s["name"] for s in bundle_of(install)["sounds"]]


def test_a_missing_sound_list_leaves_no_sounds(install):
    (install / "SFX" / "SFX.LST").unlink()
    assert bundle_of(install)["sounds"] == []


def test_events_are_read_with_the_setup_screens_counts(install):
    shorts = [(0, 2, 5), (3, 4, 1), (7, 26), (9999, 34)]
    brief = {"viewed_by_team": flagged(0), "events": shorts}
    events = bundle_of(install, briefings=[brief])["briefings"][0]["events"]
    assert [(e["time"], e["type"], e["variables"]) for e in events] == [
        (0, 2, [5]),
        (3, 4, [1]),
        (7, 26, []),
        (9999, 34, []),
    ]


def test_the_event_walk_stops_at_a_type_it_does_not_know():
    data = build_mission(briefings=[{"events": [(1, 4, 0), (2, 40, 9), (9999, 34)]}])
    events = walk_events(read_mission(data).briefings[0])
    assert [e.type for e in events] == [4]


def test_the_event_walk_keeps_a_trailing_zero_variable_after_an_unknown_type():
    data = build_mission(briefings=[{"events": [(1, 2, 5), (3, 6, 100, 0)]}])
    events = walk_events(read_mission(data).briefings[0])
    assert [(e.type, e.variables) for e in events[:2]] == [(2, [5]), (6, [100, 0])]


def test_the_last_short_of_the_event_area_can_be_a_zero_variable():
    shorts = [(1, 2, 5)] + [(2, 1)] * 197
    brief = {"events": shorts, "extra_shorts": [3, 2, 0]}
    data = build_mission(briefings=[brief])
    events = walk_events(read_mission(data).briefings[0])
    assert len(events) == 199 and events[-1].variables == [0]


def test_the_event_walk_stops_when_an_event_would_run_past_the_area():
    shorts = [(1, 2, 5)] + [(2, 1)] * 197
    data = build_mission(briefings=[{"events": shorts, "extra_shorts": [3, 6, 1]}])
    events = walk_events(read_mission(data).briefings[0])
    assert events[-1].type == 1 and len(events) == 198


def test_the_schema_file_equals_the_model():
    text = json.dumps(briefing_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/briefing.schema.json is stale: run python tools/gen_schema.py"
    )


def test_the_schema_closes_every_object():
    defs = briefing_schema()["$defs"]
    assert all(d["additionalProperties"] is False for d in defs.values())
    assert set(defs) >= {"BriefingBundle", "BriefingData", "GroupData", "FontData"}
