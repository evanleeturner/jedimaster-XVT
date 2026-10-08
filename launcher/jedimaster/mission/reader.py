"""Read an X-Wing vs. TIE Fighter / Balance of Power mission file (.tie).

Written from Mission_XvT.txt by Michael Gaisser (GNU Free Documentation
License 1.3 or later). The document's structure and field names are followed;
its text is not reproduced here.

Purpose:
    Turn the bytes of a format-version 12 (XvT) or 14 (BoP) mission into a
    ``Mission`` model, refusing anything this reader cannot read faithfully.

Flow:
    1. Header: PlatformID must be 12 or 14; NumFGs and NumMessages must be
       within ``MAX_FLIGHT_GROUPS`` / ``MAX_MESSAGES``.
    2. Fixed-size sections in file order: flight groups, messages, ten global
       goals, ten teams.
    3. Eight variable-size briefings: a 0x334-byte fixed part (header, the
       0x320-byte event area, viewers) then 32 tags and 32 strings, each a
       SHORT length and that many characters.
    4. Fixed-size string sections: flight-group goal strings, global goal
       strings (0x900 of strings and 0xC00 skipped per team), then the
       description(s): one 1024-byte string for 12, three 4096-byte strings
       for 14.

Invariants:
    - Every read is bounds-checked; a file that ends early raises
      ``ShortReadError`` naming the offset.
    - Bytes after the last section are allowed and ignored (logged at INFO).
    - Briefing events are walked with the document's per-type argument
      counts; the walk stops at End Briefing (0x22). An event type the
      document does not list stops the walk with a warning, keeping the
      remaining SHORTs raw, because its length is unknown.

Call:
    ``mission = read_mission(path_or_bytes)``
"""

from __future__ import annotations

import logging
import os
from pathlib import Path

from .binary import Cursor
from .binary import decode_fixed
from .binary import MissionFormatError
from .model import Briefing
from .model import Event
from .model import FileHeader
from .model import FlightGroup
from .model import GlobalGoal
from .model import GoalFG
from .model import GoalGlobal
from .model import Layout
from .model import Message
from .model import Mission
from .model import Order
from .model import Role
from .model import Team
from .model import Trigger
from .model import TriggerPair
from .model import Waypoint

logger = logging.getLogger(__name__)

SUPPORTED_VERSIONS = (12, 14)
HEADER_SIZE = 0xA4
FLIGHT_GROUP_SIZE = 0x562
ORDER_SIZE = 0x52
GOAL_FG_SIZE = 0x4E
MESSAGE_SIZE = 0x74
GLOBAL_GOAL_SIZE = 0x80
TEAM_SIZE = 0x1E7
BRIEFING_FIXED_SIZE = 0x334
EVENT_AREA_SHORTS = 0x320 // 2
GOAL_STRING_SIZE = 64
GLOBAL_GOAL_STRINGS_SIZE = 0x1500
TEAM_COUNT = 10
BRIEFING_COUNT = 8
BRIEFING_TAGS = 32
BRIEFING_STRINGS = 32
END_BRIEFING = 0x22

# Every field that names a flight group (trigger variables, order targets,
# motherships) is one BYTE, so a file can address at most 256 of them. The
# document gives no count for messages; the same one-byte bound is applied.
MAX_FLIGHT_GROUPS = 256
MAX_MESSAGES = 256
MAX_GLOBAL_GOALS = 3

# Variables per briefing event type, from the document's EventType list.
EVENT_ARG_COUNTS: dict[int, int] = {
    0x03: 0,
    0x04: 1,
    0x05: 1,
    0x06: 2,
    0x07: 2,
    0x08: 0,
    **{t: 1 for t in range(0x09, 0x11)},
    0x11: 0,
    **{t: 4 for t in range(0x12, 0x1A)},
    END_BRIEFING: 0,
}


class UnsupportedVersionError(MissionFormatError):
    """PlatformID is not 12 (XvT) or 14 (BoP)."""


class CountLimitError(MissionFormatError):
    """A count field is negative or past the limit the format allows."""


def _nonzero(raw: bytes, base: int) -> dict[str, int]:
    return {"0x%02x" % (base + i): value for i, value in enumerate(raw) if value}


def _trigger(cur: Cursor) -> Trigger:
    return Trigger(cur.u8(), cur.u8(), cur.u8(), cur.u8())


def _tpair(cur: Cursor) -> TriggerPair:
    t1 = _trigger(cur)
    t2 = _trigger(cur)
    return TriggerPair(t1, t2, cur.u8s(2), cur.u8())


def _header(cur: Cursor) -> FileHeader:
    h = FileHeader()
    h.platform_id = cur.s16()
    if h.platform_id not in SUPPORTED_VERSIONS:
        raise UnsupportedVersionError(
            f"unsupported PlatformID {h.platform_id} (this reader reads 12 and 14)"
        )
    h.num_fgs = cur.s16()
    h.num_messages = cur.s16()
    for label, value, limit in (
        ("NumFGs", h.num_fgs, MAX_FLIGHT_GROUPS),
        ("NumMessages", h.num_messages, MAX_MESSAGES),
    ):
        if not 0 <= value <= limit:
            raise CountLimitError(f"{label} is {value}, outside 0..{limit}")
    h.time_limit_min = cur.u8()
    h.time_limit_sec = cur.u8()
    h.win_type = cur.u8()
    h.rnd_seed = cur.u8()
    h.rescue = cur.u8()
    h.all_way_shown = cur.u8()
    h.vars = cur.u8s(8)
    h.iff_names = [cur.string(20) for _ in range(4)]
    h.mission_type = cur.u8()
    h.goals_unimportant = cur.u8()
    h.time_limit_minutes = cur.u8()
    h.time_limit_seconds = cur.u8()
    cur.skip(HEADER_SIZE - 0x68)
    logger.debug(
        "header: version=%s fgs=%s messages=%s type=%s",
        h.platform_id,
        h.num_fgs,
        h.num_messages,
        h.mission_type,
    )
    return h


def _order(cur: Cursor) -> Order:
    o = Order()
    o.order = cur.u8()
    o.throttle = cur.u8()
    o.variables = cur.u8s(4)
    o.target3_type = cur.u8()
    o.target4_type = cur.u8()
    o.target3 = cur.u8()
    o.target4 = cur.u8()
    o.target3_or_target4 = cur.u8()
    o.unused_0b = cur.u8()
    o.target1_type = cur.u8()
    o.target1 = cur.u8()
    o.target2_type = cur.u8()
    o.target2 = cur.u8()
    o.target1_or_target2 = cur.u8()
    o.unused_11 = cur.u8()
    o.speed = cur.u8()
    o.designation = cur.string(16)
    o.reserved_nonzero = _nonzero(cur.raw(ORDER_SIZE - 0x23), 0x23)
    return o


def _goal_fg(cur: Cursor) -> GoalFG:
    g = GoalFG()
    g.argument = cur.u8()
    g.condition = cur.u8()
    g.amount = cur.u8()
    g.points = cur.s8()
    g.enabled_for_team = cur.u8s(10)
    g.time_limit = cur.u8()
    g.reserved_0f = cur.u8()
    g.reserved_nonzero = _nonzero(cur.raw(GOAL_FG_SIZE - 0x10), 0x10)
    return g


def _roles(raw: bytes) -> list[Role]:
    roles = []
    for i in range(4):
        part = raw[4 * i : 4 * i + 4]
        designation = part[1:4].split(b"\0")[0].decode("iso-8859-1")
        roles.append(Role(part[0], designation))
    return roles


def _flight_group(cur: Cursor) -> FlightGroup:
    start = cur.offset
    fg = FlightGroup()
    fg.name = cur.string(20)
    # Role[4] is 16 bytes, but Cargo sits at 0x28: the field spans 20.
    role_bytes = cur.raw(20)
    fg.roles = _roles(role_bytes)
    fg.roles_text = decode_fixed(role_bytes)
    fg.cargo = cur.string(20)
    fg.special_cargo = cur.string(20)
    fg.special_cargo_craft = cur.u8()
    fg.random_special_cargo = cur.u8()
    fg.craft_type = cur.u8()
    fg.number_of_craft = cur.u8()
    fg.status1 = cur.u8()
    fg.warhead = cur.u8()
    fg.beam = cur.u8()
    fg.iff = cur.u8()
    fg.team = cur.u8()
    fg.group_ai = cur.u8()
    fg.markings = cur.u8()
    fg.radio = cur.u8()
    fg.unused_05c = cur.u8()
    fg.formation = cur.u8()
    fg.formation_spacing = cur.u8()
    fg.global_group = cur.u8()
    fg.leader_spacing = cur.u8()
    fg.number_of_waves = cur.u8()
    fg.waves_delay = cur.u8()
    fg.stop_arriving_when = cur.u8()
    fg.player_number = cur.u8()
    fg.arrive_only_if_human = cur.u8()
    fg.player_craft = cur.u8()
    fg.yaw = cur.u8()
    fg.pitch = cur.u8()
    fg.roll = cur.u8()
    fg.perma_death_enabled = cur.u8()
    fg.perma_death_id = cur.u8()
    fg.unused_06c = cur.u8()
    fg.arrival_difficulty = cur.u8()
    fg.arrival1_and_2 = _tpair(cur)
    fg.arrival3_and_4 = _tpair(cur)
    fg.arrival12_or_arrival34 = cur.u8()
    fg.random_arrival_delay_extra_minutes = cur.u8()
    fg.arrival_delay_minutes = cur.u8()
    fg.arrival_delay_seconds = cur.u8()
    fg.departure = _tpair(cur)
    fg.departure_delay_minutes = cur.u8()
    fg.departure_delay_seconds = cur.u8()
    fg.abort_trigger = cur.u8()
    fg.random_arrival_delay_extra_seconds = cur.u8()
    fg.io_reserved = cur.u8()
    fg.current_mothership = cur.s16()
    fg.arrival_mothership = cur.u8()
    fg.arrive_via_mothership = cur.u8()
    fg.departure_mothership = cur.u8()
    fg.depart_via_mothership = cur.u8()
    fg.alternate_mothership = cur.u8()
    fg.alternate_mothership_used = cur.u8()
    fg.captured_departure_mothership = cur.u8()
    fg.captured_depart_via_mothership = cur.u8()
    fg.orders = [_order(cur) for _ in range(4)]
    fg.skip_to_order4 = _tpair(cur)
    fg.goals = [_goal_fg(cur) for _ in range(8)]
    fg.unused_465 = cur.u8()
    axes = [cur.s16s(22) for _ in range(4)]
    fg.waypoints = [Waypoint(*(axis[i] for axis in axes)) for i in range(22)]
    fg.waypoint_shown = cur.u8()
    fg.unused_517 = cur.u8()
    fg.briefing_linked = cur.u8s(8)
    fg.prevent_craft_numbering = cur.u8()
    fg.departure_clock_minutes = cur.u8()
    fg.departure_clock_seconds = cur.u8()
    fg.countermeasures = cur.u8()
    fg.craft_explosion_time = cur.u8()
    fg.status2 = cur.u8()
    fg.global_unit = cur.u8()
    fg.briefing_start = cur.u8s(8)
    fg.handicap = cur.u8()
    fg.optional_warheads = cur.u8s(8)
    fg.optional_beams = cur.u8s(4)
    cur.skip(2)
    fg.optional_countermeasures = cur.u8s(3)
    cur.skip(1)
    fg.optional_craft_category = cur.u8()
    fg.optional_craft = cur.u8s(10)
    fg.number_of_optional_craft = cur.u8s(10)
    fg.number_of_optional_craft_waves = cur.u8s(10)
    cur.skip(1)
    assert cur.offset - start == FLIGHT_GROUP_SIZE, hex(cur.offset - start)
    return fg


def _message(cur: Cursor) -> Message:
    m = Message()
    m.message_index = cur.s16()
    m.message = cur.string(64)
    m.sent_to_teams = cur.u8s(10)
    m.triggers = [_tpair(cur), _tpair(cur)]
    m.editor_note = cur.string(16)
    m.delay = cur.u8()
    m.triggers12_or_triggers34 = cur.u8()
    return m


def _global_goal(cur: Cursor) -> GlobalGoal:
    gg = GlobalGoal()
    gg.num_goals = cur.s16()
    if not 0 <= gg.num_goals <= MAX_GLOBAL_GOALS:
        raise CountLimitError(
            f"global goal NumGoals is {gg.num_goals}, outside 0..{MAX_GLOBAL_GOALS}"
        )
    for _ in range(3):
        g = GoalGlobal()
        g.triggers = [_tpair(cur), _tpair(cur)]
        g.name = cur.string(16)
        g.version = cur.u8()
        g.triggers12_or_triggers34 = cur.u8()
        g.delay = cur.u8()
        g.points = cur.s8()
        gg.goals.append(g)
    return gg


def _team(cur: Cursor) -> Team:
    t = Team()
    t.reserved = cur.s16()
    t.name = cur.string(16)
    t.unknown_12 = cur.u8s(8)
    t.allegiances = cur.u8s(10)
    t.end_of_mission_messages = [cur.string(64) for _ in range(6)]
    t.eom_message_delays = cur.u8s(3)
    cur.skip(TEAM_SIZE - 0x1A7)
    return t


def _events(shorts: list[int]) -> tuple[list[Event], list[int], bool]:
    """Walk the event area; return (events, raw tail, reached End Briefing)."""
    events: list[Event] = []
    i = 0
    complete = False
    while i + 2 <= len(shorts):
        time, kind = shorts[i], shorts[i + 1]
        argc = EVENT_ARG_COUNTS.get(kind)
        if argc is None:
            logger.warning(
                "briefing event type 0x%x at SHORT %s is not in the document's "
                "list; event walk stopped",
                kind,
                i,
            )
            break
        if i + 2 + argc > len(shorts):
            logger.warning("briefing event at SHORT %s runs past the event area", i)
            break
        events.append(Event(time, kind, list(shorts[i + 2 : i + 2 + argc])))
        i += 2 + argc
        if kind == END_BRIEFING:
            complete = True
            break
    tail = list(shorts[i:])
    while tail and tail[-1] == 0:
        tail.pop()
    if not complete:
        logger.warning("briefing event list has no End Briefing event")
    return events, tail, complete


def _briefing(cur: Cursor) -> Briefing:
    b = Briefing()
    b.running_time = cur.s16()
    b.current_time = cur.s16()
    b.start_events = cur.s16()
    b.events_length = cur.s16()
    b.tile = cur.s16()
    for label, value in (
        ("EventsLength", b.events_length),
        ("StartEvents", b.start_events),
    ):
        if not 0 <= value <= EVENT_AREA_SHORTS:
            raise CountLimitError(
                f"briefing {label} is {value}, outside 0..{EVENT_AREA_SHORTS} "
                f"(the event area holds {EVENT_AREA_SHORTS} SHORTs)"
            )
    b.events, b.events_tail, b.events_complete = _events(cur.s16s(EVENT_AREA_SHORTS))
    b.viewed_by_team = cur.u8s(10)
    for target, count in ((b.tags, BRIEFING_TAGS), (b.strings, BRIEFING_STRINGS)):
        for _ in range(count):
            length = cur.s16()
            if length < 0:
                raise CountLimitError(
                    f"briefing string length {length} at offset "
                    f"0x{cur.offset - 2:x} is negative"
                )
            target.append(cur.string(length))
    return b


def read_mission(source: str | os.PathLike[str] | bytes) -> Mission:
    """Read a version 12 or 14 mission from a path or from its bytes.

    Returns a ``Mission`` holding every section. Raises
    ``UnsupportedVersionError`` for a PlatformID other than 12 or 14,
    ``CountLimitError`` for a negative or over-limit count (NumFGs,
    NumMessages, a global goal's NumGoals, a briefing's EventsLength or
    StartEvents, a negative briefing string length), ``ShortReadError`` when
    the data ends before the last section, and ``OSError`` when a path cannot
    be read. Does not check that enum values are in the document's lists,
    that references (flight-group indices, string numbers) point at something
    that exists, or that reserved bytes are zero; bytes after the last section
    are ignored.
    """
    if isinstance(source, bytes | bytearray | memoryview):
        data = bytes(source)
        label = "<bytes>"
    else:
        label = str(source)
        data = Path(source).read_bytes()
    logger.info("reading mission %s (%d bytes)", label, len(data))
    cur = Cursor(data)
    layout = Layout()
    header = _header(cur)
    mission = Mission(header=header, layout=layout)

    layout.flight_groups = cur.offset
    mission.flight_groups = [_flight_group(cur) for _ in range(header.num_fgs)]
    layout.messages = cur.offset
    mission.messages = [_message(cur) for _ in range(header.num_messages)]
    layout.global_goals = cur.offset
    mission.global_goals = [_global_goal(cur) for _ in range(TEAM_COUNT)]
    layout.teams = cur.offset
    mission.teams = [_team(cur) for _ in range(TEAM_COUNT)]
    layout.briefings = cur.offset
    mission.briefings = [_briefing(cur) for _ in range(BRIEFING_COUNT)]

    layout.fg_goal_strings = cur.offset
    mission.fg_goal_strings = [
        [[cur.string(GOAL_STRING_SIZE) for _ in range(3)] for _ in range(8)]
        for _ in range(header.num_fgs)
    ]
    layout.global_goal_strings = cur.offset
    for _ in range(TEAM_COUNT):
        start = cur.offset
        team_strings = [
            [[cur.string(GOAL_STRING_SIZE) for _ in range(3)] for _ in range(4)]
            for _ in range(3)
        ]
        cur.skip(GLOBAL_GOAL_STRINGS_SIZE - (cur.offset - start))
        mission.global_goal_strings.append(team_strings)

    layout.descriptions = cur.offset
    if header.platform_id == 12:
        mission.descriptions = [cur.string(1024)]
    else:
        mission.descriptions = [cur.string(4096) for _ in range(3)]
    layout.end = cur.offset
    if layout.end != len(data):
        logger.info(
            "mission %s: %d bytes after the last section ignored",
            label,
            len(data) - layout.end,
        )
    logger.debug("layout: %s", layout)
    return mission
