"""Write synthetic mission bytes from the document's structure tables.

Purpose:
    Give the tests missions without game data. Every structure is packed at
    the absolute offsets the format description lists (not by reading order),
    so the builder checks the reader's offsets instead of mirroring them.

Flow:
    ``LAYOUTS`` holds one table per structure: (field, offset, kind). ``pack``
    writes a dict of values into a zeroed buffer of the structure's size.
    ``build_mission`` lays the sections out in file order and returns bytes.
    ``distinct_values`` fills every field of a layout with a value that
    differs from its neighbours, so a reader reading the wrong offset or
    width gets a different number.

Invariants:
    - Kinds: ``u8``, ``s8``, ``s16``, ``str:N`` (N bytes, NUL padded),
      ``u8[N]``, ``s16[N]``, ``<Layout>[N]`` (N nested structures),
      ``<Layout>`` (one), ``raw:N`` (bytes as given).
    - Missing values pack as zero / empty.
    - Uses only the standard library and nothing from ``jedimaster``.

Call:
    ``data = build_mission(version=12, flight_groups=[{"name": "Alpha"}])``
"""

from __future__ import annotations

import logging
import re
import struct
from typing import Any

logger = logging.getLogger(__name__)

TRIGGER = (4, [("condition", 0, "u8"), ("variable_type", 1, "u8"),
               ("variable", 2, "u8"), ("amount", 3, "u8")])  # fmt: skip

LAYOUTS: dict[str, tuple[int, list[tuple[str, int, str]]]] = {
    "Trigger": TRIGGER,
    "TriggerPair": (
        0x0B,
        [
            ("trigger1", 0x00, "Trigger"),
            ("trigger2", 0x04, "Trigger"),
            ("unused", 0x08, "u8[2]"),
            ("t1_or_t2", 0x0A, "u8"),
        ],
    ),
    "FileHeader": (
        0xA4,
        [
            ("platform_id", 0x00, "s16"),
            ("num_fgs", 0x02, "s16"),
            ("num_messages", 0x04, "s16"),
            ("time_limit_min", 0x06, "u8"),
            ("time_limit_sec", 0x07, "u8"),
            ("win_type", 0x08, "u8"),
            ("rnd_seed", 0x09, "u8"),
            ("rescue", 0x0A, "u8"),
            ("all_way_shown", 0x0B, "u8"),
            ("vars", 0x0C, "u8[8]"),
            ("iff_names", 0x14, "str:20[4]"),
            ("mission_type", 0x64, "u8"),
            ("goals_unimportant", 0x65, "u8"),
            ("time_limit_minutes", 0x66, "u8"),
            ("time_limit_seconds", 0x67, "u8"),
        ],
    ),
    "Order": (
        0x52,
        [
            ("order", 0x00, "u8"),
            ("throttle", 0x01, "u8"),
            ("variables", 0x02, "u8[4]"),
            ("target3_type", 0x06, "u8"),
            ("target4_type", 0x07, "u8"),
            ("target3", 0x08, "u8"),
            ("target4", 0x09, "u8"),
            ("target3_or_target4", 0x0A, "u8"),
            ("unused_0b", 0x0B, "u8"),
            ("target1_type", 0x0C, "u8"),
            ("target1", 0x0D, "u8"),
            ("target2_type", 0x0E, "u8"),
            ("target2", 0x0F, "u8"),
            ("target1_or_target2", 0x10, "u8"),
            ("unused_11", 0x11, "u8"),
            ("speed", 0x12, "u8"),
            ("designation", 0x13, "str:16"),
        ],
    ),
    "GoalFG": (
        0x4E,
        [
            ("argument", 0x00, "u8"),
            ("condition", 0x01, "u8"),
            ("amount", 0x02, "u8"),
            ("points", 0x03, "s8"),
            ("enabled_for_team", 0x04, "u8[10]"),
            ("time_limit", 0x0E, "u8"),
            ("reserved_0f", 0x0F, "u8"),
        ],
    ),
    "FlightGroup": (
        0x562,
        [
            ("name", 0x000, "str:20"),
            ("roles", 0x014, "raw:20"),
            ("cargo", 0x028, "str:20"),
            ("special_cargo", 0x03C, "str:20"),
            ("special_cargo_craft", 0x050, "u8"),
            ("random_special_cargo", 0x051, "u8"),
            ("craft_type", 0x052, "u8"),
            ("number_of_craft", 0x053, "u8"),
            ("status1", 0x054, "u8"),
            ("warhead", 0x055, "u8"),
            ("beam", 0x056, "u8"),
            ("iff", 0x057, "u8"),
            ("team", 0x058, "u8"),
            ("group_ai", 0x059, "u8"),
            ("markings", 0x05A, "u8"),
            ("radio", 0x05B, "u8"),
            ("unused_05c", 0x05C, "u8"),
            ("formation", 0x05D, "u8"),
            ("formation_spacing", 0x05E, "u8"),
            ("global_group", 0x05F, "u8"),
            ("leader_spacing", 0x060, "u8"),
            ("number_of_waves", 0x061, "u8"),
            ("waves_delay", 0x062, "u8"),
            ("stop_arriving_when", 0x063, "u8"),
            ("player_number", 0x064, "u8"),
            ("arrive_only_if_human", 0x065, "u8"),
            ("player_craft", 0x066, "u8"),
            ("yaw", 0x067, "u8"),
            ("pitch", 0x068, "u8"),
            ("roll", 0x069, "u8"),
            ("perma_death_enabled", 0x06A, "u8"),
            ("perma_death_id", 0x06B, "u8"),
            ("unused_06c", 0x06C, "u8"),
            ("arrival_difficulty", 0x06D, "u8"),
            ("arrival1_and_2", 0x06E, "TriggerPair"),
            ("arrival3_and_4", 0x079, "TriggerPair"),
            ("arrival12_or_arrival34", 0x084, "u8"),
            ("random_arrival_delay_extra_minutes", 0x085, "u8"),
            ("arrival_delay_minutes", 0x086, "u8"),
            ("arrival_delay_seconds", 0x087, "u8"),
            ("departure", 0x088, "TriggerPair"),
            ("departure_delay_minutes", 0x093, "u8"),
            ("departure_delay_seconds", 0x094, "u8"),
            ("abort_trigger", 0x095, "u8"),
            ("random_arrival_delay_extra_seconds", 0x096, "u8"),
            ("io_reserved", 0x097, "u8"),
            ("current_mothership", 0x098, "s16"),
            ("arrival_mothership", 0x09A, "u8"),
            ("arrive_via_mothership", 0x09B, "u8"),
            ("departure_mothership", 0x09C, "u8"),
            ("depart_via_mothership", 0x09D, "u8"),
            ("alternate_mothership", 0x09E, "u8"),
            ("alternate_mothership_used", 0x09F, "u8"),
            ("captured_departure_mothership", 0x0A0, "u8"),
            ("captured_depart_via_mothership", 0x0A1, "u8"),
            ("orders", 0x0A2, "Order[4]"),
            ("skip_to_order4", 0x1EA, "TriggerPair"),
            ("goals", 0x1F5, "GoalFG[8]"),
            ("unused_465", 0x465, "u8"),
            ("waypoint_x", 0x466, "s16[22]"),
            ("waypoint_y", 0x492, "s16[22]"),
            ("waypoint_z", 0x4BE, "s16[22]"),
            ("waypoint_enabled", 0x4EA, "s16[22]"),
            ("waypoint_shown", 0x516, "u8"),
            ("unused_517", 0x517, "u8"),
            ("briefing_linked", 0x518, "u8[8]"),
            ("prevent_craft_numbering", 0x520, "u8"),
            ("departure_clock_minutes", 0x521, "u8"),
            ("departure_clock_seconds", 0x522, "u8"),
            ("countermeasures", 0x523, "u8"),
            ("craft_explosion_time", 0x524, "u8"),
            ("status2", 0x525, "u8"),
            ("global_unit", 0x526, "u8"),
            ("briefing_start", 0x527, "u8[8]"),
            ("handicap", 0x52F, "u8"),
            ("optional_warheads", 0x530, "u8[8]"),
            ("optional_beams", 0x538, "u8[4]"),
            ("optional_countermeasures", 0x53E, "u8[3]"),
            ("optional_craft_category", 0x542, "u8"),
            ("optional_craft", 0x543, "u8[10]"),
            ("number_of_optional_craft", 0x54D, "u8[10]"),
            ("number_of_optional_craft_waves", 0x557, "u8[10]"),
        ],
    ),
    "Message": (
        0x74,
        [
            ("message_index", 0x00, "s16"),
            ("message", 0x02, "str:64"),
            ("sent_to_teams", 0x42, "u8[10]"),
            ("triggers", 0x4C, "TriggerPair[2]"),
            ("editor_note", 0x62, "str:16"),
            ("delay", 0x72, "u8"),
            ("triggers12_or_triggers34", 0x73, "u8"),
        ],
    ),
    "GoalGlobal": (
        0x2A,
        [
            ("triggers", 0x00, "TriggerPair[2]"),
            ("name", 0x16, "str:16"),
            ("version", 0x26, "u8"),
            ("triggers12_or_triggers34", 0x27, "u8"),
            ("delay", 0x28, "u8"),
            ("points", 0x29, "s8"),
        ],
    ),
    "GlobalGoal": (
        0x80,
        [("num_goals", 0x00, "s16"), ("goals", 0x02, "GoalGlobal[3]")],
    ),
    "Team": (
        0x1E7,
        [
            ("reserved", 0x000, "s16"),
            ("name", 0x002, "str:16"),
            ("unknown_12", 0x012, "u8[8]"),
            ("allegiances", 0x01A, "u8[10]"),
            ("end_of_mission_messages", 0x024, "str:64[6]"),
            ("eom_message_delays", 0x1A4, "u8[3]"),
        ],
    ),
    "BriefingFixed": (
        0x334,
        [
            ("running_time", 0x000, "s16"),
            ("current_time", 0x002, "s16"),
            ("start_events", 0x004, "s16"),
            ("events_length", 0x006, "s16"),
            ("tile", 0x008, "s16"),
            ("event_shorts", 0x00A, "s16[400]"),
            ("viewed_by_team", 0x32A, "u8[10]"),
        ],
    ),
}

_ARRAY = re.compile(r"^(.*)\[(\d+)\]$")


def _pack_one(buf: bytearray, offset: int, kind: str, value: Any) -> None:
    if kind == "u8":
        struct.pack_into("<B", buf, offset, value)
    elif kind == "s8":
        struct.pack_into("<b", buf, offset, value)
    elif kind == "s16":
        struct.pack_into("<h", buf, offset, value)
    elif kind.startswith("str:"):
        size = int(kind[4:])
        raw = value.encode("iso-8859-1")
        if len(raw) > size:
            raise ValueError(f"string too long for {kind}: {value!r}")
        buf[offset : offset + len(raw)] = raw
    elif kind.startswith("raw:"):
        size = int(kind[4:])
        if len(value) > size:
            raise ValueError(f"raw value too long for {kind}")
        buf[offset : offset + len(value)] = value
    elif kind in LAYOUTS:
        packed = pack(kind, value)
        buf[offset : offset + len(packed)] = packed
    else:
        raise ValueError(f"unknown kind {kind!r}")


def _element_size(kind: str) -> int:
    if kind in ("u8", "s8"):
        return 1
    if kind == "s16":
        return 2
    if kind.startswith("str:"):
        return int(kind[4:])
    return LAYOUTS[kind][0]


def pack(layout: str, values: dict[str, Any] | None) -> bytes:
    """Return the bytes of one structure with ``values`` at their offsets."""
    size, fields = LAYOUTS[layout]
    values = values or {}
    unknown = set(values) - {name for name, _, _ in fields}
    if unknown:
        raise KeyError(f"{layout} has no fields {sorted(unknown)}")
    buf = bytearray(size)
    for name, offset, kind in fields:
        if name not in values:
            continue
        value = values[name]
        match = _ARRAY.match(kind)
        if match:
            element, count = match.group(1), int(match.group(2))
            if len(value) > count:
                raise ValueError(f"{layout}.{name} takes {count} items")
            step = _element_size(element)
            for i, item in enumerate(value):
                _pack_one(buf, offset + i * step, element, item)
        else:
            _pack_one(buf, offset, kind, value)
    return bytes(buf)


def flight_group_values(values: dict[str, Any]) -> dict[str, Any]:
    """Return FlightGroup pack values with ``waypoints`` split into 4 arrays.

    ``waypoints`` is a list of up to 22 ``(x, y, z, enabled)`` tuples.
    """
    values = dict(values)
    points = values.pop("waypoints", None)
    if points is not None:
        for axis, key in enumerate(("x", "y", "z", "enabled")):
            values["waypoint_" + key] = [p[axis] for p in points]
    return values


def briefing_bytes(values: dict[str, Any] | None) -> bytes:
    """Return one briefing: the fixed part, then 32 tags and 32 strings.

    ``events`` is a list of ``(time, type, *variables)`` tuples flattened
    into the event area; ``events_length`` defaults to their SHORT count.
    ``tags``/``strings`` are lists of up to 32 strings (missing are empty).
    """
    values = dict(values or {})
    events = values.pop("events", [(9999, 0x22)])
    shorts = [s for event in events for s in event]
    shorts += values.pop("extra_shorts", [])
    values.setdefault("events_length", sum(len(e) for e in events))
    values["event_shorts"] = shorts
    tags = values.pop("tags", [])
    strings = values.pop("strings", [])
    out = bytearray(pack("BriefingFixed", values))
    for group in (tags, strings):
        group = list(group) + [""] * (32 - len(group))
        for text in group:
            raw = text.encode("iso-8859-1")
            out += struct.pack("<h", len(raw)) + raw
    return bytes(out)


def build_mission(
    version: int = 12,
    header: dict[str, Any] | None = None,
    flight_groups: list[dict[str, Any]] | None = None,
    messages: list[dict[str, Any]] | None = None,
    global_goals: list[dict[str, Any]] | None = None,
    teams: list[dict[str, Any]] | None = None,
    briefings: list[dict[str, Any]] | None = None,
    fg_goal_strings: dict[tuple[int, int, int], str] | None = None,
    global_goal_strings: dict[tuple[int, int, int, int], str] | None = None,
    descriptions: list[str] | None = None,
) -> bytes:
    """Return a whole mission file's bytes, sections in the document's order.

    NumFGs and NumMessages default to the list lengths; global goals default
    to NumGoals 3; goal strings are keyed by index tuples
    (fg, goal, state) and (team, goal, trigger, state).
    """
    flight_groups = flight_groups or []
    messages = messages or []
    head = {
        "platform_id": version,
        "num_fgs": len(flight_groups),
        "num_messages": len(messages),
    }
    head.update(header or {})
    out = bytearray(pack("FileHeader", head))
    for fg in flight_groups:
        out += pack("FlightGroup", flight_group_values(fg))
    for message in messages:
        out += pack("Message", message)
    global_goals = list(global_goals or [])
    global_goals += [{}] * (10 - len(global_goals))
    for goal in global_goals:
        goal = dict(goal)
        goal.setdefault("num_goals", 3)
        out += pack("GlobalGoal", goal)
    team_list = list(teams or [])
    team_list += [{}] * (10 - len(team_list))
    for team in team_list:
        out += pack("Team", team)
    brief_list = list(briefings or [])
    brief_list += [{}] * (8 - len(brief_list))
    for brief in brief_list:
        out += briefing_bytes(brief)
    fg_strings = fg_goal_strings or {}
    for f in range(len(flight_groups)):
        for g in range(8):
            for s in range(3):
                out += _fixed(fg_strings.get((f, g, s), ""), 64)
    gg_strings = global_goal_strings or {}
    for t in range(10):
        block = bytearray()
        for g in range(3):
            for k in range(4):
                for s in range(3):
                    block += _fixed(gg_strings.get((t, g, k, s), ""), 64)
        out += block + bytes(0x1500 - len(block))
    if version == 14:
        texts = list(descriptions or []) + [""] * 3
        for text in texts[:3]:
            out += _fixed(text, 4096)
    else:
        out += _fixed((descriptions or [""])[0], 1024)
    return bytes(out)


def _fixed(text: str, size: int) -> bytes:
    raw = text.encode("iso-8859-1")
    if len(raw) > size:
        raise ValueError(f"string longer than {size} bytes")
    return raw + bytes(size - len(raw))


def distinct_values(layout: str, seed: int = 1) -> dict[str, Any]:
    """Return a value for every field of ``layout``, each unlike its neighbours.

    Integers walk a sequence (offset-dependent, never zero); strings carry
    the field name; nested layouts recurse. Raw and waypoint fields are left
    to the caller.
    """
    _, fields = LAYOUTS[layout]
    values: dict[str, Any] = {}
    for index, (name, offset, kind) in enumerate(fields):
        if kind.startswith("raw:") or name.startswith("waypoint_"):
            continue
        match = _ARRAY.match(kind)
        element, count = (match.group(1), int(match.group(2))) if match else (kind, 0)
        base = seed * 31 + index * 7 + offset

        def one(k: int, element: str = element, name: str = name, base: int = base):
            """Return element ``k`` of this field's distinct values."""
            if element == "u8":
                return (base + k * 3) % 254 + 1
            if element == "s8":
                return (base + k * 3) % 250 - 125 or 1
            if element == "s16":
                return (base * 37 + k * 101) % 60000 - 30000 or 1
            if element.startswith("str:"):
                size = int(element[4:])
                return f"{name}{seed}.{k}"[:size]
            return distinct_values(element, base + k)

        values[name] = [one(k) for k in range(count)] if match else one(0)
    return values
