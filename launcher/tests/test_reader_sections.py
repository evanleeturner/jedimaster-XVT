"""Read every section of synthetic missions back, both format versions.

Purpose:
    Prove that ``read_mission`` takes each field from the offset and width
    the document gives it, for the header, flight groups (orders, goals,
    trigger pairs, waypoints, roles), messages, global goals, teams, the 8
    briefings, the goal strings and the descriptions, in versions 12 and 14.

Flow:
    The builder packs a distinct value into every field at the document's
    absolute offsets; the test reads the bytes and compares field by field.

Invariants:
    - No game data: every mission here is synthetic.

Call:
    ``pytest tests/test_reader_sections.py``
"""

from __future__ import annotations

import dataclasses
import logging
from typing import Any

import pytest
from builder import build_mission
from builder import distinct_values

from jedimaster.mission import read_mission

logger = logging.getLogger(__name__)

VERSIONS = (12, 14)


def assert_matches(obj: Any, values: dict[str, Any], path: str = "") -> None:
    """Assert every builder value equals the model attribute of the same name."""
    for name, expected in values.items():
        actual = getattr(obj, name)
        where = f"{path}.{name}"
        if isinstance(expected, dict):
            assert dataclasses.is_dataclass(actual), where
            assert_matches(actual, expected, where)
        elif isinstance(expected, list) and expected and isinstance(expected[0], dict):
            assert len(actual) == len(expected), where
            for i, (a, e) in enumerate(zip(actual, expected, strict=True)):
                assert_matches(a, e, f"{where}[{i}]")
        else:
            assert actual == expected, where


def fg_values(seed: int) -> dict[str, Any]:
    """Return builder values for every field of one flight group."""
    values = distinct_values("FlightGroup", seed)
    values["roles"] = b"1PRI2CONhs" + bytes(10)
    values["waypoints"] = [
        (seed * 100 + i, -(seed * 100 + i), i * 3 - 30, i % 2) for i in range(22)
    ]
    return values


@pytest.mark.parametrize("version", VERSIONS)
def test_header_fields(version):
    head = distinct_values("FileHeader")
    del head["platform_id"], head["num_fgs"], head["num_messages"]
    mission = read_mission(build_mission(version, header=head))
    assert mission.header.platform_id == version
    assert_matches(mission.header, head, "header")


@pytest.mark.parametrize("version", VERSIONS)
def test_flight_group_fields(version):
    groups = [fg_values(1), fg_values(2)]
    mission = read_mission(build_mission(version, flight_groups=groups))
    assert mission.header.num_fgs == 2
    for fg, values in zip(mission.flight_groups, groups, strict=True):
        points = values.pop("waypoints")
        roles = values.pop("roles")
        assert_matches(fg, values, "fg")
        assert [(p.x, p.y, p.z, p.enabled) for p in fg.waypoints] == points
        assert fg.roles_text == "1PRI2CONhs"
        assert [(r.designation_team, r.designation) for r in fg.roles] == [
            (ord("1"), "PRI"),
            (ord("2"), "CON"),
            (ord("h"), "s"),
            (0, ""),
        ]
        assert roles[:10] == b"1PRI2CONhs"


def test_role_text_reads_past_sixteen_bytes():
    """The Roles span is 20 bytes (0x14-0x27): free text may use all of it."""
    fg = {"roles": b"ABCDEFGHIJKLMNOPQRS", "cargo": "next"}
    mission = read_mission(build_mission(12, flight_groups=[fg]))
    assert mission.flight_groups[0].roles_text == "ABCDEFGHIJKLMNOPQRS"
    assert mission.flight_groups[0].cargo == "next"


@pytest.mark.parametrize("version", VERSIONS)
def test_message_fields(version):
    messages = [distinct_values("Message", s) for s in (3, 4, 5)]
    mission = read_mission(build_mission(version, messages=messages))
    assert mission.header.num_messages == 3
    for msg, values in zip(mission.messages, messages, strict=True):
        assert_matches(msg, values, "message")


def test_message_of_full_64_characters_is_kept_whole():
    text = "x" * 64
    mission = read_mission(build_mission(12, messages=[{"message": text}]))
    assert mission.messages[0].message == text


@pytest.mark.parametrize("version", VERSIONS)
def test_global_goal_fields(version):
    goals = [distinct_values("GlobalGoal", s) for s in range(10)]
    for goal in goals:
        goal["num_goals"] = 3
    mission = read_mission(build_mission(version, global_goals=goals))
    assert len(mission.global_goals) == 10
    for gg, values in zip(mission.global_goals, goals, strict=True):
        assert_matches(gg, values, "global_goal")


@pytest.mark.parametrize("version", VERSIONS)
def test_team_fields(version):
    teams = [distinct_values("Team", s) for s in range(10)]
    mission = read_mission(build_mission(version, teams=teams))
    for team, values in zip(mission.teams, teams, strict=True):
        assert_matches(team, values, "team")


@pytest.mark.parametrize("version", VERSIONS)
def test_briefing_fields(version):
    events = [
        (0, 0x04, 1),
        (0, 0x06, -100, 250),
        (0, 0x09, 2),
        (20, 0x12, 3, 10, -20, 2),
        (40, 0x08),
        (60, 0x11),
        (80, 0x03),
        (9999, 0x22),
    ]
    briefings = []
    for b in range(8):
        briefings.append(
            {
                "running_time": 200 + b,
                "current_time": 1,
                "start_events": 10,
                "tile": b,
                "events": events,
                "viewed_by_team": [b, 0, 1, 0, 0, 0, 0, 0, 0, b + 1],
                "tags": [f"tag{b}", "", "t\xe9"],
                "strings": ["", f"text {b}"] + ["s"] * 30,
                "extra_shorts": [7, 0, 9],
            }
        )
    mission = read_mission(build_mission(version, briefings=briefings))
    assert len(mission.briefings) == 8
    for brief, values in zip(mission.briefings, briefings, strict=True):
        assert brief.running_time == values["running_time"]
        assert brief.current_time == 1
        assert brief.start_events == 10
        assert brief.events_length == 24
        assert brief.tile == values["tile"]
        assert [(e.time, e.type, *e.variables) for e in brief.events] == events
        assert brief.events_complete
        assert brief.events_tail == [7, 0, 9]
        assert brief.viewed_by_team == values["viewed_by_team"]
        assert brief.tags == values["tags"] + [""] * 29
        assert brief.strings == values["strings"]


@pytest.mark.parametrize("version", VERSIONS)
def test_goal_strings(version):
    fg_strings = {(1, 7, 2): "fg1 goal7 failed", (0, 0, 0): "fg0 goal0 incomplete"}
    gg_strings = {
        (9, 2, 3, 2): "team9 secondary t4 failed",
        (0, 0, 0, 0): "team0 primary t1 incomplete",
        (4, 1, 2, 1): "team4 prevent t3 complete",
    }
    data = build_mission(
        version,
        flight_groups=[{}, {}],
        fg_goal_strings=fg_strings,
        global_goal_strings=gg_strings,
    )
    mission = read_mission(data)
    for (f, g, s), text in fg_strings.items():
        assert mission.fg_goal_strings[f][g][s] == text
    for (t, g, k, s), text in gg_strings.items():
        assert mission.global_goal_strings[t][g][k][s] == text
    filled = sum(bool(s) for fg in mission.fg_goal_strings for goal in fg for s in goal)
    assert filled == 2
    filled = sum(
        bool(s)
        for team in mission.global_goal_strings
        for goal in team
        for trigger in goal
        for s in trigger
    )
    assert filled == 3


def test_description_version_12_is_one_string():
    mission = read_mission(build_mission(12, descriptions=["the briefing text"]))
    assert mission.descriptions == ["the briefing text"]
    assert mission.layout.end - mission.layout.descriptions == 1024


def test_descriptions_version_14_are_three_strings():
    texts = ["won", "lost", "the mission"]
    mission = read_mission(build_mission(14, descriptions=texts))
    assert mission.descriptions == texts
    assert mission.layout.end - mission.layout.descriptions == 3 * 4096


@pytest.mark.parametrize("version", VERSIONS)
def test_layout_offsets(version):
    data = build_mission(version, flight_groups=[{}] * 3, messages=[{}] * 2)
    layout = read_mission(data).layout
    assert layout.flight_groups == 0xA4
    assert layout.messages == 0xA4 + 3 * 0x562
    assert layout.global_goals == layout.messages + 2 * 0x74
    assert layout.teams == layout.global_goals + 10 * 0x80
    assert layout.briefings == layout.teams + 10 * 0x1E7
    empty_briefing = 0x334 + 64 * 2
    assert layout.fg_goal_strings == layout.briefings + 8 * empty_briefing
    assert layout.global_goal_strings == layout.fg_goal_strings + 3 * 8 * 3 * 64
    assert layout.descriptions == layout.global_goal_strings + 10 * 0x1500
    assert layout.end == len(data)


def test_bytes_after_last_section_are_ignored():
    data = build_mission(12, flight_groups=[{"name": "Alpha"}]) + b"trailing"
    mission = read_mission(data)
    assert mission.flight_groups[0].name == "Alpha"
    assert mission.layout.end == len(data) - 8


def test_reads_from_a_path(tmp_path):
    path = tmp_path / "m.tie"
    path.write_bytes(build_mission(14, flight_groups=[{"name": "Beta"}]))
    assert read_mission(path).flight_groups[0].name == "Beta"
    assert read_mission(str(path)).header.platform_id == 14
