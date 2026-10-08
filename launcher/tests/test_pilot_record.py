"""The pilot layout and reader: packed structs, values by type, sizes refused.

Purpose:
    Prove the layout is packed as the game's records are (members end to
    end, arrays end to end, the last index fastest) and prints back as its
    sheet; the reader gives each member by name with its type's value
    (signed, unsigned, bytes, text keeping the bytes after its end mark),
    nests arrays, finds members by path and refuses a file of the wrong
    size or kind.

Flow:
    Build records with ``pilotdata`` from the layout at run time, read
    them, inspect.

Invariants:
    - No game data: no pilot file, no size or offset written here.

Call:
    ``pytest tests/test_pilot_record.py``
"""

from __future__ import annotations

import logging
import re
from math import prod

import pytest
from pilotdata import offset
from pilotdata import record

from jedimaster.pilot import BASE_RECORD
from jedimaster.pilot import FULL_RECORD
from jedimaster.pilot import locate
from jedimaster.pilot import PilotFormatError
from jedimaster.pilot import read_pilot_file
from jedimaster.pilot import read_record
from jedimaster.pilot import render_layout
from jedimaster.pilot import struct
from jedimaster.pilot import STRUCTS
from jedimaster.pilot.layout import count
from jedimaster.pilot.layout import element_size
from jedimaster.pilot.layout import is_struct
from jedimaster.pilot.layout import NUMBER_SIZES
from jedimaster.pilot.record import kind_of
from jedimaster.pilot.record import nest
from jedimaster.pilot.record import Text

logger = logging.getLogger(__name__)

MEMBER_LINE = re.compile(
    r"^member (\w+) offset=(\d+) type=(\w+) dims=(-|(?:\[\d+\])+) size=(\d+)$"
)


def test_layout_is_packed():
    for item in STRUCTS:
        at = 0
        for m in item.members:
            assert m.offset == at, (item.name, m.name)
            assert m.type in NUMBER_SIZES or is_struct(m.type), m.type
            assert (
                m.size == element_size(m) * count(m) == element_size(m) * prod(m.dims)
            )
            at += m.size
        assert at == item.size, item.name
    for name in (FULL_RECORD, BASE_RECORD):
        assert struct(name).name == name


def test_layout_sheet_prints_the_layout_back():
    lines = render_layout().splitlines()
    assert lines[0] == "kind layout"
    parsed, current = [], None
    for line in lines[1:]:
        head = re.match(r"^struct (\w+) size=(\d+)$", line)
        if head:
            current = (head[1], int(head[2]), [])
            parsed.append(current)
            continue
        m = MEMBER_LINE.match(line)
        assert m and current is not None, line
        dims = tuple(int(d) for d in re.findall(r"\d+", m[4]))
        current[2].append((m[1], int(m[2]), m[3], dims, int(m[5])))
    assert parsed == [(s.name, s.size, [tuple(m) for m in s.members]) for s in STRUCTS]
    assert render_layout().endswith("\n") and not render_layout().endswith("\n\n")


def test_signed_unsigned_and_one_value():
    data = record(
        values={("rating",): -5, ("num_human_players_last_mission",): 0xFFFFFFFF}
    )
    rec = read_record(bytes(data), FULL_RECORD)
    assert rec["rating"] == -5
    assert rec["num_human_players_last_mission"] == 0xFFFFFFFF
    assert list(rec)[: len(struct(FULL_RECORD).members)] == [
        m.name for m in struct(FULL_RECORD).members
    ]


def test_arrays_nest_last_index_fastest():
    path = ("main_stats", "kills_per_craft_per_mt", 1, 2)
    data = record(values={path: 77, ("main_stats", "total_kills_per_mt", 2): -1})
    stats = read_record(bytes(data), FULL_RECORD)["main_stats"]
    table = stats["kills_per_craft_per_mt"]
    member = next(m for m in struct("pilot_stats").members if m.name == path[1])
    assert len(table) == member.dims[0] and len(table[0]) == member.dims[1]
    assert table[1][2] == 77
    assert sum(v for row in table for v in row) == 77
    assert stats["total_kills_per_mt"][2] == -1
    assert nest([1, 2, 3, 4, 5, 6], (2, 3)) == [[1, 2, 3], [4, 5, 6]]
    assert nest([9], ()) == 9


def test_text_keeps_every_byte_and_its_text():
    data = record(values={("name",): b'A"b\0junk', ("rating_name",): b"x" * 40})
    rec = read_record(bytes(data), FULL_RECORD)
    name = rec["name"]
    assert isinstance(name, Text)
    size = offset(FULL_RECORD, ("name",))[1].size
    assert len(name.raw) == size and name.raw.startswith(b'A"b\0junk')
    assert name.text == b'A"b' and name.ended
    full = rec["rating_name"]
    assert full.text == full.raw == b"x" * len(full.raw) and not full.ended


def test_bytes_and_struct_arrays():
    data = record(
        values={
            ("xvt_record_payload",): b"\x01\xff",
            ("network_players", 3, "rating"): 9,
            ("faction_statistics", 2, "stats", "total_score_per_mt", 1): 4,
        }
    )
    rec = read_record(bytes(data), FULL_RECORD)
    payload = rec["xvt_record_payload"]
    assert isinstance(payload, bytes) and payload[:3] == b"\x01\xff\x00"
    assert rec["network_players"][3]["rating"] == 9
    assert rec["network_players"][2]["rating"] == 0
    assert rec["faction_statistics"][2]["stats"]["total_score_per_mt"] == [0, 4, 0]


def test_wrong_size_refused(tmp_path):
    size = struct(BASE_RECORD).size
    for data in (bytes(size - 1), bytes(size + 1)):
        with pytest.raises(PilotFormatError, match=f"{size} bytes"):
            read_record(data, BASE_RECORD)
    short = tmp_path / "Short0.PLT"
    short.write_bytes(bytes(size - 4))
    with pytest.raises(PilotFormatError, match=f"Short0.PLT: .* {size} bytes"):
        read_pilot_file(short)
    other = tmp_path / "pilot.txt"
    other.write_bytes(bytes(size))
    with pytest.raises(PilotFormatError, match="not a .pl2 or .plt"):
        read_pilot_file(other)


def test_files_read_as_their_kind(tmp_path):
    full = tmp_path / "Ace0.Pl2"
    full.write_bytes(bytes(record(values={("rating",): 3})))
    base = tmp_path / "Ace0.plt"
    base.write_bytes(bytes(record(BASE_RECORD, {("rating",): 4})))
    assert kind_of(full) == FULL_RECORD and kind_of(base) == BASE_RECORD
    assert kind_of(tmp_path / "x.pl3") is None
    name, rec = read_pilot_file(full)
    assert name == FULL_RECORD and rec["rating"] == 3
    name, rec = read_pilot_file(base)
    assert name == BASE_RECORD and rec["rating"] == 4
    assert "legacy_rating_state" in rec


@pytest.mark.parametrize(
    "path",
    [
        ("rating",),
        ("main_stats", "kills_per_craft_per_mt", 2, 5),
        ("main_stats", "kills_per_craft_per_mt", 1),
        ("faction_statistics", 3, "sp_campaign_missions", 7, "best_score"),
        ("teams", 9, "losses"),
    ],
)
def test_locate_finds_members(path):
    at, member = locate(FULL_RECORD, path)
    assert (at, member) == offset(FULL_RECORD, path)


def test_locate_refuses_unknown_paths():
    with pytest.raises(KeyError):
        locate(FULL_RECORD, ("no_such_member",))
    with pytest.raises(IndexError):
        locate(FULL_RECORD, ("teams", offset(FULL_RECORD, ("teams",))[1].dims[0]))
