"""Briefing events: the document's per-type walk and the structural checks.

Purpose:
    Prove the event walk follows the document's argument counts, stops at
    End Briefing, keeps leftovers raw, stops safely at an unnamed type, and
    that ``check_briefing`` and ``start_events_rule`` judge what they claim.

Flow:
    Synthetic briefings built with chosen events; read; check.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_events.py``
"""

from __future__ import annotations

import logging

import pytest
from builder import build_mission

from jedimaster.mission import read_mission
from jedimaster.mission.checks import check_briefing
from jedimaster.mission.checks import start_events_rule
from jedimaster.mission.reader import EVENT_ARG_COUNTS

logger = logging.getLogger(__name__)


def brief(**values):
    """Return briefing 0 of a synthetic mission built with ``values``."""
    return read_mission(build_mission(12, briefings=[values])).briefings[0]


def test_every_documented_type_takes_its_argument_count():
    expected = {0x03: 0, 0x04: 1, 0x05: 1, 0x06: 2, 0x07: 2, 0x08: 0, 0x11: 0, 0x22: 0}
    expected.update({t: 1 for t in range(0x09, 0x11)})
    expected.update({t: 4 for t in range(0x12, 0x1A)})
    assert EVENT_ARG_COUNTS == expected
    events = [
        (t, t, *range(1, 1 + n)) for t, n in sorted(expected.items()) if t != 0x22
    ]
    events.append((9999, 0x22))
    b = brief(events=events)
    assert [(e.time, e.type, *e.variables) for e in b.events] == events
    assert check_briefing(b) == []


def test_walk_stops_at_end_and_keeps_tail():
    b = brief(events=[(0, 0x03), (9999, 0x22)], extra_shorts=[5, 0x12, 1, 2, 0, 0])
    assert len(b.events) == 2
    assert b.events_complete
    assert b.events_tail == [5, 0x12, 1, 2]


def test_unnamed_type_stops_walk_and_keeps_rest(caplog):
    with caplog.at_level(logging.WARNING):
        b = brief(events=[(0, 0x04, 1), (5, 0x1A, 7, 8), (9999, 0x22)], events_length=9)
    assert [e.type for e in b.events] == [0x04]
    assert not b.events_complete
    assert b.events_tail == [5, 0x1A, 7, 8, 9999, 0x22]
    assert "not in the document's list" in caplog.text
    assert "no-end" in check_briefing(b)


def test_check_flags_wrong_end_time():
    b = brief(events=[(0, 0x03), (500, 0x22)])
    assert check_briefing(b) == ["end-time"]


def test_check_flags_length_disagreement():
    b = brief(events=[(0, 0x05, 1), (9999, 0x22)], events_length=7)
    assert check_briefing(b) == ["length"]


def test_check_flags_missing_end():
    b = brief(events=[(0, 0x03)], events_length=2)
    assert "no-end" in check_briefing(b)


def test_check_flags_argument_count_mismatch():
    b = brief(events=[(0, 0x06, 1, 2), (9999, 0x22)])
    b.events[0].variables.append(3)
    b.events_length += 1
    assert check_briefing(b) == ["arg-count"]


@pytest.mark.parametrize(
    ("current_time", "start_events", "rule"),
    [
        (1, 5, "time-zero"),  # 2 events at time 0 = 3 + 2 SHORTs
        (30, 8, "current-time"),  # first event at time >= 30 sits at SHORT 8
        (30, 5, "time-zero-only"),
        (1, 7, "neither"),
    ],
)
def test_start_events_rule(current_time, start_events, rule):
    events = [(0, 0x04, 0), (0, 0x08), (20, 0x05, 1), (40, 0x03), (9999, 0x22)]
    b = brief(events=events, current_time=current_time, start_events=start_events)
    assert start_events_rule(b) == rule


def test_check_flags_time_order():
    b = brief(events=[(40, 0x03), (20, 0x08), (9999, 0x22)])
    assert check_briefing(b) == ["time-order"]


@pytest.mark.parametrize(
    "event",
    [(0, 0x04, 32), (0, 0x05, -1), (0, 0x12, 32, 0, 0, 0), (0, 0x0B, 2)],
)
def test_check_flags_bad_references(event):
    b = brief(events=[event, (9999, 0x22)])
    assert check_briefing(b, num_fgs=2) == ["bad-reference"]
    ok = brief(
        events=[(0, 0x04, 31), (0, 0x0B, 1), (0, 0x19, 0, 1, 2, 3), (9999, 0x22)]
    )
    assert check_briefing(ok, num_fgs=2) == []
