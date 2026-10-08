"""The reader refuses what it cannot read faithfully, with a clear exception.

Purpose:
    Prove the three refusals: a short read (the file ends inside any
    section), an unknown format version, and counts past the limits.

Flow:
    Build a valid synthetic mission, then truncate it or patch one count.

Invariants:
    - Every refusal is a ``MissionFormatError`` subclass naming the problem.

Call:
    ``pytest tests/test_refusals.py``
"""

from __future__ import annotations

import logging
import struct

import pytest
from builder import build_mission

from jedimaster.mission import CountLimitError
from jedimaster.mission import MissionFormatError
from jedimaster.mission import read_mission
from jedimaster.mission import ShortReadError
from jedimaster.mission import UnsupportedVersionError

logger = logging.getLogger(__name__)


def sample(version: int = 12) -> bytes:
    """Return a small valid synthetic mission."""
    return build_mission(
        version,
        flight_groups=[{"name": "Alpha"}],
        messages=[{"message": "hi"}],
        briefings=[{"tags": ["a"], "strings": ["b"]}],
    )


def section_cuts(version: int) -> list[int]:
    """Return truncation points inside every section of ``sample``."""
    layout = read_mission(sample(version)).layout
    return [
        0,
        1,
        0xA3,
        layout.flight_groups + 0x100,
        layout.messages + 0x10,
        layout.global_goals + 0x7F,
        layout.teams + 0x1E6,
        layout.briefings + 0x333,
        layout.briefings + 0x334 + 1,
        layout.fg_goal_strings + 5,
        layout.global_goal_strings + 0x1400,
        layout.descriptions + 3,
        layout.end - 1,
    ]


@pytest.mark.parametrize("version", (12, 14))
def test_short_read_in_every_section(version):
    data = sample(version)
    read_mission(data)  # the whole file reads
    for cut in section_cuts(version):
        with pytest.raises(ShortReadError, match="short read"):
            read_mission(data[:cut])


def test_version_14_file_cut_to_version_12_length_is_short():
    data = sample(14)
    with pytest.raises(ShortReadError):
        read_mission(data[: len(data) - 3 * 4096 + 1024])


@pytest.mark.parametrize("version", (0, 1, 11, 13, 15, 16, -12, 0x0C00))
def test_unknown_versions_refused(version):
    data = bytearray(sample(12))
    struct.pack_into("<h", data, 0, version)
    with pytest.raises(UnsupportedVersionError, match=f"PlatformID {version}"):
        read_mission(bytes(data))


def patched(offset: int, value: int, version: int = 12) -> bytes:
    """Return ``sample`` with one SHORT overwritten."""
    data = bytearray(sample(version))
    struct.pack_into("<h", data, offset, value)
    return bytes(data)


@pytest.mark.parametrize(
    ("offset", "value", "label"),
    [
        (2, -1, "NumFGs"),
        (2, 257, "NumFGs"),
        (4, -1, "NumMessages"),
        (4, 257, "NumMessages"),
    ],
)
def test_header_counts_past_limits(offset, value, label):
    with pytest.raises(CountLimitError, match=label):
        read_mission(patched(offset, value))


def test_header_counts_at_limit_are_read_as_counts():
    # 256 flight groups is allowed; the sample is then simply too short.
    with pytest.raises(ShortReadError):
        read_mission(patched(2, 256))


def test_global_goal_count_past_three_refused():
    layout = read_mission(sample()).layout
    with pytest.raises(CountLimitError, match="NumGoals is 4"):
        read_mission(patched(layout.global_goals + 0x80, 4))
    with pytest.raises(CountLimitError, match="NumGoals is -1"):
        read_mission(patched(layout.global_goals, -1))


@pytest.mark.parametrize(
    ("field_offset", "value", "label"),
    [
        (6, 401, "EventsLength"),
        (6, -2, "EventsLength"),
        (4, 401, "StartEvents"),
        (4, -1, "StartEvents"),
    ],
)
def test_briefing_counts_past_event_area_refused(field_offset, value, label):
    layout = read_mission(sample()).layout
    with pytest.raises(CountLimitError, match=label):
        read_mission(patched(layout.briefings + field_offset, value))


def test_negative_briefing_string_length_refused():
    layout = read_mission(sample()).layout
    with pytest.raises(CountLimitError, match="negative"):
        read_mission(patched(layout.briefings + 0x334, -5))


def test_all_refusals_share_one_base_class():
    for exc in (ShortReadError, UnsupportedVersionError, CountLimitError):
        assert issubclass(exc, MissionFormatError)
        assert issubclass(exc, ValueError)
