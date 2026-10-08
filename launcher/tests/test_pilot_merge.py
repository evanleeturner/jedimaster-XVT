"""The merge of a base record into the full record, rule by rule.

Purpose:
    Prove each rule of the game's merge on two records whose every word
    differs: members of the same name copied; ``legacy_rating_state``,
    ``mission_sequence_state`` and each side's ``selection_state`` not
    copied; the three payloads end to end; the per-craft tables with
    their seven kept slots and the entries past the base record's count
    kept; each side record's parts; the run after ``stats`` landing four
    bytes early and leaving the last four bytes of ``mp_battles`` and all
    after; every other byte of the full record kept.

Flow:
    Build a patterned full record and base record with ``pilotdata``,
    merge, compare byte ranges found by the builder's own offsets.

Invariants:
    - No game data: offsets come from the layout at run time.

Call:
    ``pytest tests/test_pilot_merge.py``
"""

from __future__ import annotations

import logging
from itertools import product

import pytest
from pilotdata import offset
from pilotdata import patterned

from jedimaster.pilot import BASE_RECORD
from jedimaster.pilot import FULL_RECORD
from jedimaster.pilot import merge
from jedimaster.pilot import merge_plan
from jedimaster.pilot import struct

logger = logging.getLogger(__name__)

KEPT_SLOTS = (4, 36, 41, 43, 45, 54, 78)
"""The craft types whose per-craft entries the merge leaves alone."""
CRAFT_TABLES = (
    "kills_per_craft_per_mt",
    "kills_shared_per_craft_per_mt",
    "kills_assists_per_craft_per_mt",
)
SIDE_PARTS = (
    "total_missions_played_count",
    "melee_plaques",
    "tournament_trophies",
    "mission_evaluations",
    "battle_medallions",
    "mission_awards",
    "field_bc",
    "total_score",
)
OWN_RULES = (
    "legacy_rating_state",
    "mission_sequence_state",
    "xvt_record_combat_payload",
    "xvt_record_identity_payload",
    "xvt_record_object_payload",
    "main_stats",
    "last_mission_stats",
    "faction_statistics",
)
"""Base-record members with a rule of their own (not a plain copy by name)."""


@pytest.fixture(scope="module")
def records() -> tuple[bytes, bytes, bytes]:
    """Return (full before, base, full after the merge)."""
    full = patterned(FULL_RECORD, 2)
    base = patterned(BASE_RECORD, 1)
    merged = bytearray(full)
    merge(merged, bytes(base))
    return bytes(full), bytes(base), bytes(merged)


def four(data: bytes, name: str, path: tuple) -> bytes:
    """Return the four bytes of the number at ``path``."""
    at, _ = offset(name, path)
    return data[at : at + 4]


def whole(data: bytes, name: str, path: tuple) -> bytes:
    """Return all the bytes of the member named last in ``path``."""
    at, member = offset(name, path)
    return data[at : at + member.size]


def side_paths() -> list[int]:
    """Return the side record indexes."""
    return list(range(offset(BASE_RECORD, ("faction_statistics",))[1].dims[0]))


def test_members_of_the_same_name_copied(records):
    full, base, merged = records
    plain = [m.name for m in struct(BASE_RECORD).members if m.name not in OWN_RULES]
    assert {"name", "rating", "rating_name", "network_players", "teams"} <= set(plain)
    assert "current_faction_id" in plain
    for name in plain:
        assert whole(merged, FULL_RECORD, (name,)) == whole(base, BASE_RECORD, (name,))
        assert whole(merged, FULL_RECORD, (name,)) != whole(full, FULL_RECORD, (name,))


def _sources_overlap(start: int, end: int) -> bool:
    return any(c.source < end and start < c.source + c.length for c in merge_plan())


def test_three_not_copied_and_full_only_members_kept(records):
    full, _, merged = records
    for name in ("legacy_rating_state", "mission_sequence_state"):
        at, member = offset(BASE_RECORD, (name,))
        assert not _sources_overlap(at, at + member.size), name
    for side in side_paths():
        at, member = offset(
            BASE_RECORD, ("faction_statistics", side, "selection_state")
        )
        assert not _sources_overlap(at, at + member.size), side
    base_names = {m.name for m in struct(BASE_RECORD).members}
    for m in struct(FULL_RECORD).members:
        if m.name not in base_names and m.name != "xvt_record_payload":
            assert whole(merged, FULL_RECORD, (m.name,)) == whole(
                full, FULL_RECORD, (m.name,)
            )
    base_side = {m.name for m in struct("pilot_xvt_faction").members}
    stats_at = [m.name for m in struct("pilot_faction").members].index("stats")
    for side in side_paths():
        for m in struct("pilot_faction").members[:stats_at]:
            if m.name not in base_side:
                path = ("faction_statistics", side, m.name)
                assert whole(merged, FULL_RECORD, path) == whole(
                    full, FULL_RECORD, path
                )


def test_payloads_go_end_to_end(records):
    _, base, merged = records
    parts = b"".join(
        whole(base, BASE_RECORD, (name,))
        for name in (
            "xvt_record_combat_payload",
            "xvt_record_identity_payload",
            "xvt_record_object_payload",
        )
    )
    assert whole(merged, FULL_RECORD, ("xvt_record_payload",)) == parts


def stats_places() -> list[tuple]:
    """Return the path of every statistics record in both records."""
    places = [("main_stats",), ("last_mission_stats",)]
    return places + [("faction_statistics", s, "stats") for s in side_paths()]


def craft_cells():
    """Yield every per-craft entry: its path and the base record's craft count."""
    for place in stats_places():
        for table in CRAFT_TABLES:
            types = offset(BASE_RECORD, (*place, table))[1].dims[1]
            mission_types, kinds = offset(FULL_RECORD, (*place, table))[1].dims
            assert kinds > types
            for mt, craft in product(range(mission_types), range(kinds)):
                yield (*place, table, mt, craft), types


def test_per_craft_tables(records):
    full, base, merged = records
    checked = 0
    for path, types in craft_cells():
        got = four(merged, FULL_RECORD, path)
        craft = path[-1]
        if craft < types and craft not in KEPT_SLOTS:
            assert got == four(base, BASE_RECORD, path), path
        else:
            assert got == four(full, FULL_RECORD, path), path
        checked += 1
    _, member = offset(FULL_RECORD, ("main_stats", CRAFT_TABLES[0]))
    assert checked == len(stats_places()) * len(CRAFT_TABLES) * member.size // 4


def test_other_statistics_by_name(records):
    _, base, merged = records
    for place in stats_places():
        for m in struct("pilot_xvt_stats").members:
            if m.name in CRAFT_TABLES:
                continue
            path = (*place, m.name)
            assert whole(merged, FULL_RECORD, path) == whole(base, BASE_RECORD, path)


def test_side_record_parts(records):
    _, base, merged = records
    for side in side_paths():
        for name in SIDE_PARTS:
            path = ("faction_statistics", side, name)
            assert whole(merged, FULL_RECORD, path) == whole(base, BASE_RECORD, path)


def test_run_after_stats_lands_four_bytes_early(records):
    full, base, merged = records
    base_side = struct("pilot_xvt_faction")
    for side in side_paths():
        start, _ = offset(BASE_RECORD, ("faction_statistics", side, "sp_training_data"))
        side_at, _ = offset(BASE_RECORD, ("faction_statistics", side))
        end = side_at + base_side.size
        target, _ = offset(FULL_RECORD, ("faction_statistics", side, "field1558"))
        missions, _ = offset(
            FULL_RECORD, ("faction_statistics", side, "sp_training_missions")
        )
        assert missions - target == 4
        assert merged[target : target + end - start] == base[start:end]
        first = offset(
            FULL_RECORD,
            (
                "faction_statistics",
                side,
                "sp_training_missions",
                0,
                "number_times_flown",
            ),
        )[0]
        assert merged[first : first + 4] == base[start + 4 : start + 8]
        battles, member = offset(
            FULL_RECORD, ("faction_statistics", side, "mp_battles")
        )
        battles_end = battles + member.size
        assert target + end - start == battles_end - 4
        assert (
            merged[battles_end - 4 : battles_end] == full[battles_end - 4 : battles_end]
        )
        after, _ = offset(FULL_RECORD, ("faction_statistics", side, "sp_campaigns"))
        side_end = (
            offset(FULL_RECORD, ("faction_statistics", side))[0]
            + struct("pilot_faction").size
        )
        assert merged[after:side_end] == full[after:side_end]


def test_only_the_plan_changes_bytes(records):
    full, _, merged = records
    covered = bytearray(len(full))
    for copy in merge_plan():
        assert not any(covered[copy.target : copy.target + copy.length]), copy.what
        covered[copy.target : copy.target + copy.length] = b"\1" * copy.length
    for at in range(len(full)):
        if not covered[at]:
            assert merged[at] == full[at], at


def test_merge_refuses_wrong_sizes():
    full = bytearray(struct(FULL_RECORD).size)
    with pytest.raises(ValueError, match="whole records"):
        merge(full, bytes(struct(BASE_RECORD).size - 1))
    with pytest.raises(ValueError, match="whole records"):
        merge(full[:-1], bytes(struct(BASE_RECORD).size))
