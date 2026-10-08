"""The renderer prints the answer-sheet format, rule by rule.

Purpose:
    Pin each rendering rule the answer sheets show (field order, AND/OR,
    clocks, flight-group names after FG targets, CraftWhen labels, stored
    message index, enabled points only, quoting, section order and blank
    lines, the two offset lines) on synthetic missions.

Flow:
    Build a synthetic mission, render, compare whole lines.

Invariants:
    - No game data; the expected lines are written out here.

Call:
    ``pytest tests/test_render.py``
"""

from __future__ import annotations

import logging

from builder import build_mission

from jedimaster.mission import read_mission
from jedimaster.mission import render_mission

logger = logging.getLogger(__name__)

ZERO_TRIGGER = "AlwaysTrue(0) None(0)=0 amount=All(0)"
ZERO_PAIR = f"({ZERO_TRIGGER} AND[0] {ZERO_TRIGGER})"
LEGEND = (
    "Indices are zero-based. Order targets are predicates; "
    "fallback is tried if primary finds no target."
)


def rich_mission() -> str:
    """Return the rendering of a synthetic mission that exercises each rule."""
    alpha = {
        "name": 'Al"pha\\',
        "roles": b"1PRI",
        "cargo": "C",
        "special_cargo": "S",
        "craft_type": 5,
        "number_of_craft": 2,
        "number_of_waves": 1,
        "iff": 1,
        "team": 0,
        "group_ai": 3,
        "global_group": 4,
        "global_unit": 6,
        "status1": 3,
        "status2": 7,
        "player_number": 1,
        "arrive_only_if_human": 1,
        "arrival_difficulty": 2,
        "arrival1_and_2": {
            "trigger1": {
                "condition": 1,
                "variable_type": 1,
                "variable": 1,
                "amount": 4,
            },
            "trigger2": {
                "condition": 10,
                "variable_type": 15,
                "variable": 7,
                "amount": 18,
            },
            "t1_or_t2": 1,
        },
        "arrival_delay_minutes": 1,
        "arrival_delay_seconds": 5,
        "random_arrival_delay_extra_seconds": 30,
        "departure_delay_minutes": 12,
        "abort_trigger": 4,
        "arrival_mothership": 1,
        "arrive_via_mothership": 2,
        "departure_mothership": 3,
        "depart_via_mothership": 4,
        "alternate_mothership": 5,
        "alternate_mothership_used": 6,
        "captured_departure_mothership": 7,
        "captured_depart_via_mothership": 8,
        "orders": [
            {
                "order": 7,
                "throttle": 10,
                "variables": [1, 2, 3, 4],
                "speed": 9,
                "designation": "Go",
                "target1_type": 7,
                "target1": 12,
                "target2_type": 7,
                "target2": 9,
                "target3_type": 99,
                "target3": 1,
                "target4_type": 15,
                "target4": 0,
                "target3_or_target4": 1,
            },
            {"order": 4},
        ],
        "skip_to_order4": {"trigger1": {"condition": 38}, "t1_or_t2": 1},
        "goals": [
            {
                "argument": 2,
                "condition": 19,
                "amount": 17,
                "points": -3,
                "enabled_for_team": [0, 1, 0, 0, 0, 0, 0, 0, 0, 1],
                "time_limit": 24,
                "reserved_0f": 5,
            },
            {"condition": 11},
        ],
        "waypoints": [(10, -20, 30, 1), (0, 0, 0, 0), (0, 0, 0, 0), (9, 9, 9, 0)]
        + [(0, 0, 0, 0)] * 17
        + [(-1, -2, -3, 2)],
    }
    beta = {"name": "Beta"}
    data = build_mission(
        12,
        header={"mission_type": 3, "goals_unimportant": 1},
        flight_groups=[alpha, beta],
        messages=[
            {
                "message_index": 7,
                "message": 'say "hi"',
                "sent_to_teams": [1, 0, 1],
                "delay": 3,
                "triggers12_or_triggers34": 1,
            }
        ],
        global_goals=[
            {},
            {},
            {"goals": [{}, {"name": "GG", "points": -5, "delay": 9}]},
        ],
        teams=[
            {
                "name": "Imperial",
                "allegiances": [0, 1],
                "end_of_mission_messages": ["won"],
            }
        ],
        briefings=[{}, {}, {}, {"tags": ["", "Label1"], "strings": ["Text0"]}],
        fg_goal_strings={(1, 2, 0): "fgs"},
        global_goal_strings={(3, 1, 2, 1): "ggs"},
    )
    return render_mission(read_mission(data))


def test_header_and_legend():
    lines = rich_mission().split("\n")
    assert lines[0] == (
        "XvT/BoP version=12 flightGroups=2 messages=1 missionType=3 goalsUnimportant=1"
    )
    assert lines[1] == LEGEND
    assert lines[2] == ""


def test_flight_group_identity_lines():
    lines = rich_mission().split("\n")
    assert lines[3] == (
        'FG[0] @0xa4 "Al\\"pha\\\\" species=5 craft=2 waves(raw)=1 IFF=1 team=0 AI=3 '
        "globalGroup=4 globalUnit=6"
    )
    assert lines[4] == '  role="1PRI" cargo="C" specialCargo="S"'
    assert lines[5] == (
        "  status=3,7 player=1 playerCraft=0 arriveOnlyIfHuman=1 difficulty=2"
    )
    assert any(line.startswith('FG[1] @0x606 "Beta" species=0') for line in lines)


def test_arrival_departure_and_motherships():
    lines = rich_mission().split("\n")
    assert lines[6] == (
        '  arrival: (Arrived(1) FlightGroup(1)=1 "Beta" amount=AtLeastOne(4) OR[1] '
        "NeverFalse(10) NotFlightGroup(15)=7 amount=Unknown(18)) AND[0] "
        + ZERO_PAIR
        + " delay=1:05 random=0:30"
    )
    assert lines[7] == "  departure: " + ZERO_PAIR + " delay=12:00 abort=4"
    assert lines[8] == (
        "  motherships: arrival=1 method=2 departure=3 method=4 alternate=5 "
        "used=6 captured=7 method=8"
    )


def test_orders_and_targets():
    lines = rich_mission().split("\n")
    assert lines[9] == (
        '  Order[0] Attack(7) throttle=10 vars=1,2,3,4 speed=9 designation="Go"'
    )
    assert lines[10] == (
        "    primary: CraftWhen(7)=12 [Operational] AND[0] CraftWhen(7)=9"
    )
    assert (
        lines[11]
        == '    fallback: Unknown(99)=1 OR[1] NotFlightGroup(15)=0 "Al\\"pha\\\\"'
    )
    assert lines[12] == (
        '  Order[1] Unknown(4) throttle=0 vars=0,0,0,0 speed=0 designation=""'
    )
    assert lines[13] == "    primary: None(0)=0 AND[0] None(0)=0"
    assert lines[21] == (
        "  SkipToOrder[3]: (AlwaysPending(38) None(0)=0 amount=All(0) OR[1] "
        + ZERO_TRIGGER
        + ")"
    )


def test_goals_and_points():
    lines = rich_mission().split("\n")
    assert lines[22] == (
        "  Goal[0] type=2 condition=Unknown19(19) amount=33%(17) points(raw)=-3 "
        "teams: 1:1 9:1 timeLimit5s=24 sequence=5"
    )
    assert lines[23] == (
        "  Goal[1] type=0 condition=Unknown11(11) amount=All(0) points(raw)=0 "
        "teams: timeLimit5s=0 sequence=0"
    )
    assert lines[30:33] == [
        "  Point[0] x=10 y=-20 z=30 enabled=1",
        "  Point[21] x=-1 y=-2 z=-3 enabled=2",
        "",
    ]


def test_messages_print_stored_index():
    text = rich_mission()
    assert (
        'Message[7] "say \\"hi\\"" delay(raw)=3 teams: 0:1 2:1\n'
        "  " + ZERO_PAIR + " OR[1] " + ZERO_PAIR + "\n\n"
    ) in text
    assert "\nMessage[0]" not in text


def test_global_goals_teams_briefings_and_goal_strings():
    text = rich_mission()
    assert (
        'Team[2] GlobalGoal[1] "GG" points(raw)=-5 delay(raw)=9\n'
        "  " + ZERO_PAIR + " AND[0] " + ZERO_PAIR + "\n\n"
    ) in text
    assert 'Team[0] "Imperial" allies: 1:1\n  EndMessage[0] "won"\n' in text
    tail = text.split('  EndMessage[5] ""\n')[-1]
    assert tail.startswith(
        'Briefing[3] Label[1] "Label1"\nBriefing[3] Text[0] "Text0"\n\n'
    )
    assert '\nFG[1] Goal[2] state[0] "fgs"\n' in text
    assert '\nTeam[3] GlobalGoal[1] Trigger[2] state[1] "ggs"\n' in text
    assert text.endswith("\n") and not text.endswith("\n\n")


def empty_mission_text(version: int) -> str:
    """Return the expected rendering of an empty mission, written out."""
    lines = [
        f"XvT/BoP version={version} flightGroups=0 messages=0 missionType=0 "
        "goalsUnimportant=0",
        LEGEND,
        "",
    ]
    for team in range(10):
        for goal in range(3):
            lines.append(
                f'Team[{team}] GlobalGoal[{goal}] "" points(raw)=0 delay(raw)=0'
            )
            lines.append(f"  {ZERO_PAIR} AND[0] {ZERO_PAIR}")
            lines.append("")
    for team in range(10):
        if team:
            lines.append("")
        lines.append(f'Team[{team}] "" allies:')
        lines += [f'  EndMessage[{k}] ""' for k in range(6)]
    briefings = 0xA4 + 10 * 0x80 + 10 * 0x1E7
    goal_strings = briefings + 8 * (0x334 + 128)
    lines += [
        "",
        f"Goal text overrides @0x{goal_strings:x} (raw display-state slots)",
        "Consumed mission runtime data through 0x%x" % (goal_strings + 10 * 0x1500),
    ]
    return "\n".join(lines) + "\n"


def test_empty_mission_renders_whole_text_both_versions():
    for version in (12, 14):
        text = render_mission(read_mission(build_mission(version)))
        assert text == empty_mission_text(version)
