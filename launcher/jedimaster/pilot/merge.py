"""Merge a base record (.plt) into the full record (.pl2) the way the game does.

Purpose:
    Build, from the layout, the list of byte copies the game makes when it
    merges the base game's record into the full record, and apply it to a
    full record's bytes.

Flow:
    ``merge_plan`` walks the base record's members: a member of the same
    name in the full record is copied whole; the three payloads go end to
    end into ``xvt_record_payload``; ``legacy_rating_state`` and
    ``mission_sequence_state`` are not copied; the two statistics records
    and each side record follow their own rules (``_stats``, ``_side``).
    ``merge`` applies the plan.

Invariants:
    - Per-craft tables: for each mission type the base record's entries
      go over the full record's same entries, except ``KEPT_CRAFT_SLOTS``
      and every entry from the base record's count (88) on, which keep the
      full record's values.
    - A side record: its members up to ``stats`` by name (but not
      ``selection_state``), ``stats`` by the statistics rules, then one
      run: the base side's bytes from its first member after ``stats`` to
      its end go to the full side from its first member after ``stats``
      (``field1558``), so every result lands four bytes before its own
      place; the full side's last four bytes of ``mp_battles`` and
      everything after keep their values.
    - Full-record members the base record does not have keep their values.
    - The plan is the same every time; it is built once.

Call:
    ``merge(full_bytes, base_bytes)``
"""

from __future__ import annotations

import logging
from functools import cache
from typing import NamedTuple

from .layout import BASE_RECORD
from .layout import FULL_RECORD
from .layout import Member
from .layout import Struct
from .layout import struct

logger = logging.getLogger(__name__)

NOT_COPIED = ("legacy_rating_state", "mission_sequence_state")
"""Base-record members the merge leaves out."""
SIDE_NOT_COPIED = ("selection_state",)
"""Side-record members the merge leaves out."""
PAYLOAD = "xvt_record_payload"
PAYLOAD_PARTS = (
    "xvt_record_combat_payload",
    "xvt_record_identity_payload",
    "xvt_record_object_payload",
)
"""The base record's payloads, in the order they fill ``PAYLOAD``."""
STATS = ("main_stats", "last_mission_stats")
SIDES = "faction_statistics"
SIDE_STATS = "stats"
CRAFT_TABLES = (
    "kills_per_craft_per_mt",
    "kills_shared_per_craft_per_mt",
    "kills_assists_per_craft_per_mt",
)
"""The per-craft tables of a statistics record: [mission type][craft type]."""
KEPT_CRAFT_SLOTS = (4, 36, 41, 43, 45, 54, 78)
"""Craft types whose entries keep the full record's values in a merge."""
NUMBER = 4
"""The bytes of one per-craft entry."""


class Copy(NamedTuple):
    """One copy: ``length`` bytes from base offset ``source`` to full ``target``."""

    source: int
    target: int
    length: int
    what: str


def _member(layout: Struct, name: str) -> Member | None:
    return next((m for m in layout.members if m.name == name), None)


def _same(base: Member, full: Member | None, where: str) -> Member:
    if full is None or full.size != base.size:
        raise ValueError(f"{where}{base.name}: no full-record member of its size")
    return full


def _stats(base_name: str, full_name: str, src: int, dst: int, where: str):
    base_layout, full_layout = struct(base_name), struct(full_name)
    for member in base_layout.members:
        target = _member(full_layout, member.name)
        path = where + member.name
        if member.name not in CRAFT_TABLES:
            target = _same(member, target, where)
            yield Copy(src + member.offset, dst + target.offset, member.size, path)
            continue
        assert target is not None, f"{path} missing from {full_name}"
        types, kinds = member.dims[1], target.dims[1]
        for mission_type in range(member.dims[0]):
            start = None
            for craft in range(types + 1):
                copied = craft < types and craft not in KEPT_CRAFT_SLOTS
                if copied and start is None:
                    start = craft
                if not copied and start is not None:
                    yield Copy(
                        src + member.offset + (mission_type * types + start) * NUMBER,
                        dst + target.offset + (mission_type * kinds + start) * NUMBER,
                        (craft - start) * NUMBER,
                        f"{path}[{mission_type}][{start}:{craft}]",
                    )
                    start = None


def _side(base_name: str, full_name: str, src: int, dst: int, where: str):
    base_layout, full_layout = struct(base_name), struct(full_name)
    names = [m.name for m in base_layout.members]
    after = names.index(SIDE_STATS) + 1
    for member in base_layout.members[:after]:
        if member.name in SIDE_NOT_COPIED:
            continue
        target = _member(full_layout, member.name)
        if member.name == SIDE_STATS:
            assert target is not None, f"{where}{SIDE_STATS} missing from {full_name}"
            yield from _stats(
                member.type,
                target.type,
                src + member.offset,
                dst + target.offset,
                f"{where}{SIDE_STATS}.",
            )
            continue
        target = _same(member, target, where)
        yield Copy(
            src + member.offset, dst + target.offset, member.size, where + member.name
        )
    run_from = base_layout.members[after].offset
    full_names = [m.name for m in full_layout.members]
    run_to = full_layout.members[full_names.index(SIDE_STATS) + 1].offset
    length = base_layout.size - run_from
    yield Copy(src + run_from, dst + run_to, length, f"{where}<run>")


@cache
def merge_plan() -> tuple[Copy, ...]:
    """Return every byte copy of a merge, in the base record's member order.

    Built from the layout and the rules of this module; the same tuple
    every time. Raises ``ValueError`` when a base member copied by name
    has no full-record member of its size. Does not check that copies do
    not overlap.
    """
    base, full = struct(BASE_RECORD), struct(FULL_RECORD)
    plan: list[Copy] = []
    payload = _member(full, PAYLOAD)
    assert payload is not None, f"{FULL_RECORD} has no {PAYLOAD}"
    filled = 0
    for member in base.members:
        target = _member(full, member.name)
        if member.name in NOT_COPIED:
            continue
        if member.name in PAYLOAD_PARTS:
            plan.append(
                Copy(member.offset, payload.offset + filled, member.size, member.name)
            )
            filled += member.size
        elif member.name in STATS:
            assert target is not None, f"{member.name} missing from {FULL_RECORD}"
            plan += _stats(
                member.type,
                target.type,
                member.offset,
                target.offset,
                member.name + ".",
            )
        elif member.name == SIDES:
            assert target is not None, f"{SIDES} missing from {FULL_RECORD}"
            base_side, full_side = struct(member.type), struct(target.type)
            for side in range(member.dims[0]):
                plan += _side(
                    member.type,
                    target.type,
                    member.offset + side * base_side.size,
                    target.offset + side * full_side.size,
                    f"{SIDES}[{side}].",
                )
        else:
            target = _same(member, target, "")
            plan.append(Copy(member.offset, target.offset, member.size, member.name))
    logger.debug("merge plan: %d copies", len(plan))
    return tuple(plan)


def merge(full: bytearray, base: bytes) -> None:
    """Merge the base record ``base`` into the full record ``full`` in place.

    Applies ``merge_plan`` in order; bytes the plan does not name keep
    their values. Returns None. Raises ``ValueError`` when either is not
    its record's size. Does not check any value.
    """
    if len(full) != struct(FULL_RECORD).size or len(base) != struct(BASE_RECORD).size:
        raise ValueError(f"merge needs whole records, not {len(full)} and {len(base)}")
    for copy in merge_plan():
        full[copy.target : copy.target + copy.length] = base[
            copy.source : copy.source + copy.length
        ]
        logger.debug(
            "%s: %d bytes from %d to %d",
            copy.what,
            copy.length,
            copy.source,
            copy.target,
        )
    logger.debug("merged %d copies", len(merge_plan()))
