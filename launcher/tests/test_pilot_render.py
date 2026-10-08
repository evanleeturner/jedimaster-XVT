"""The pilot sheets: each type's line, struct paths, left-out elements, headers.

Purpose:
    Prove each kind of member prints in the sheets' form: ``i32`` signed,
    ``u32`` unsigned, arrays in a row with the last index fastest, ``u8``
    as lowercase hex, ``char`` quoted up to its first byte 0 with every
    byte after ``hex=``; struct members print in place under dotted and
    indexed paths; an all-zero element of a struct array is left out while
    every other member prints, zeros included; the raw and load sheets
    open with their header lines.

Flow:
    Build records with ``pilotdata``, read them, render, find lines.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_pilot_render.py``
"""

from __future__ import annotations

import logging
import re

import pytest
from pilotdata import record

from jedimaster.pilot import BASE_RECORD
from jedimaster.pilot import FULL_RECORD
from jedimaster.pilot import PilotLoad
from jedimaster.pilot import read_record
from jedimaster.pilot import record_lines
from jedimaster.pilot import render_load
from jedimaster.pilot import render_raw
from jedimaster.pilot import struct
from jedimaster.pilot.record import Text
from jedimaster.pilot.render import flatten
from jedimaster.pilot.render import is_zero

logger = logging.getLogger(__name__)

TEXT = b'Q"\\\n\r\t\x01\x7f\xe9z\0after'
"""Every escape of the sheets' quoting, then the end mark and more bytes."""


@pytest.fixture(scope="module")
def lines() -> dict[str, str]:
    """Return a rendered full record's lines by path."""
    data = record(
        values={
            ("name",): TEXT,
            ("total_score",): -7,
            ("num_human_players_last_mission",): 0xFFFFFFFE,
            ("xvt_record_payload",): b"\xab\x01",
            ("main_stats", "kills_per_craft_per_mt", 0, 1): 5,
            ("main_stats", "kills_per_craft_per_mt", 1, 0): 6,
            ("teams", 2, "losses"): 3,
            ("faction_statistics", 1, "stats", "total_kills_per_mt", 2): 8,
            ("faction_statistics", 3, "sp_campaign_missions", 4, "best_score"): (
                0xFFFFFFFF
            ),
            ("battle_sequence_state", "cumulative_score"): 0,
        }
    )
    rendered = record_lines(FULL_RECORD, read_record(bytes(data), FULL_RECORD))
    return dict(line.split(" = ", 1) for line in rendered)


def test_numbers_signed_and_unsigned(lines):
    assert lines["total_score"] == "-7"
    assert lines["num_human_players_last_mission"] == str(0xFFFFFFFE)
    path = "faction_statistics[3].sp_campaign_missions[4].best_score"
    assert lines[path] == str(0xFFFFFFFF)


def test_arrays_print_in_a_row_last_index_fastest(lines):
    values = lines["main_stats.kills_per_craft_per_mt"].split(" ")
    member = next(
        m for m in struct("pilot_stats").members if m.name == "kills_per_craft_per_mt"
    )
    assert len(values) == member.dims[0] * member.dims[1]
    assert values[1] == "5" and values[member.dims[1]] == "6"
    assert lines["faction_statistics[1].stats.total_kills_per_mt"] == "0 0 8"


def test_bytes_print_as_hex(lines):
    payload = lines["xvt_record_payload"]
    size = next(
        m.size for m in struct(FULL_RECORD).members if m.name == "xvt_record_payload"
    )
    assert payload == "ab01" + "00" * (size - 2)


def test_text_quoted_to_its_end_then_every_byte(lines):
    size = next(m.size for m in struct(FULL_RECORD).members if m.name == "name")
    raw = (TEXT + bytes(size))[:size]
    assert lines["name"] == '"Q\\"\\\\\\n\\r\\t\\x01\\x7f\\xe9z" hex=' + raw.hex()


def test_zero_struct_elements_left_out_others_print(lines):
    assert lines["teams[2].losses"] == "3"
    assert lines["teams[2].mission_score"] == "0"
    assert not any(
        path.startswith(("teams[0]", "teams[1]", "teams[3]")) for path in lines
    )
    assert not any(path.startswith("network_players") for path in lines)
    assert "faction_statistics[0].total_score" not in lines
    assert lines["faction_statistics[1].total_score"] == "0"
    assert "faction_statistics[3].sp_campaign_missions[3].best_score" not in lines
    assert lines["battle_sequence_state.cumulative_score"] == "0"
    assert lines["main_stats.total_score_per_mt"] == "0 0 0"


def test_members_in_layout_order(lines):
    tops = [m.name for m in struct(FULL_RECORD).members]
    seen: list[str] = []
    for path in lines:
        top = re.split(r"[.\[]", path)[0]
        if top not in seen:
            seen.append(top)
    assert seen[0] == tops[0]
    assert seen == [top for top in tops if top in seen]


def test_is_zero_and_flatten():
    assert is_zero({"a": 0, "b": [0, [0]], "c": b"\0\0", "d": Text(b"\0")})
    assert not is_zero({"a": [0, 1]})
    assert not is_zero(Text(b"\0x"))
    assert not is_zero(b"\0\1")
    assert not is_zero(None)
    assert flatten([[1, 2], [3]]) == [1, 2, 3] and flatten(4) == [4]


def test_raw_and_load_headers():
    base = read_record(bytes(record(BASE_RECORD)), BASE_RECORD)
    raw = render_raw(BASE_RECORD, base, 12).splitlines()
    assert raw[:2] == ["kind raw", f"bytes 12 of {struct(BASE_RECORD).size}"]
    assert raw[2].startswith("name = ")
    load = PilotLoad("P0.plt", True, False, 1, bytes(record()))
    sheet = render_load(load)
    assert sheet.splitlines()[:4] == [
        "kind load",
        "files pl2=1 plt=0",
        "result 1",
        'name = "" hex=' + "00" * struct("pilot_data").members[0].size,
    ]
    assert sheet.endswith("\n") and not sheet.endswith("\n\n")
