"""Dataclasses for one XvT/BoP mission, one per structure of the format.

Purpose:
    Hold every value a mission file stores, raw, under the structure and field
    names of the format description (Mission_XvT.txt by Michael Gaisser), so
    the renderer and the JSON export read one model and never the bytes.

Flow:
    ``reader.read_mission`` builds a ``Mission`` from bytes; ``render`` and
    ``to_json`` read it. Nothing here parses or prints.

Invariants:
    - Values are stored raw: no scaling (points stay /250, delays stay /5,
      waypoints stay *160) and no clamping.
    - Each field's metadata describes its storage: ``int`` is the integer
      kind (``u8``, ``s8``, ``s16``), ``count`` the fixed array length,
      ``size`` a string field's byte length, ``enum`` / ``enum_list`` the
      document's list that names its value(s). ``to_json`` adds the names and
      builds the JSON Schema from this metadata alone.
    - Fields the document marks reserved or unused are kept where a stock
      mission was seen to store something there.

Call:
    ``mission.flight_groups[0].orders[0].target1_type``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from typing import Any

logger = logging.getLogger(__name__)


def _meta(**items: Any) -> dict[str, Any]:
    return {k: v for k, v in items.items() if v is not None}


def _u8(enum: str | None = None) -> Any:
    return field(default=0, metadata=_meta(int="u8", enum=enum))


def _s8() -> Any:
    return field(default=0, metadata=_meta(int="s8"))


def _s16(enum: str | None = None) -> Any:
    return field(default=0, metadata=_meta(int="s16", enum=enum))


def _u8s(count: int | None, enum: str | None = None) -> Any:
    return field(
        default_factory=list, metadata=_meta(int="u8", count=count, enum_list=enum)
    )


def _s16s(count: int | None = None) -> Any:
    return field(default_factory=list, metadata=_meta(int="s16", count=count))


def _text(size: int | None) -> Any:
    return field(default="", metadata=_meta(size=size))


def _texts(count: int, size: int | None) -> Any:
    return field(default_factory=list, metadata=_meta(count=count, size=size))


def _many(count: int | None) -> Any:
    return field(default_factory=list, metadata=_meta(count=count))


def _nonzero_bytes() -> Any:
    return field(default_factory=dict, metadata=_meta(int="u8"))


@dataclass
class FileHeader:
    """The file header (0xA4 bytes)."""

    platform_id: int = _s16("PlatformID")
    num_fgs: int = _s16()
    num_messages: int = _s16()
    time_limit_min: int = _u8()
    time_limit_sec: int = _u8()
    win_type: int = _u8()
    rnd_seed: int = _u8()
    rescue: int = _u8()
    all_way_shown: int = _u8()
    vars: list[int] = _u8s(8)
    iff_names: list[str] = _texts(4, 20)
    mission_type: int = _u8("MissionType")
    goals_unimportant: int = _u8()
    time_limit_minutes: int = _u8()
    time_limit_seconds: int = _u8()


@dataclass
class Trigger:
    """One trigger: if Amount of VariableType Variable are Condition."""

    condition: int = _u8("Condition")
    variable_type: int = _u8("VariableType")
    variable: int = _u8()
    amount: int = _u8("Amount")


@dataclass
class TriggerPair:
    """Two triggers and the AND (0) / OR (1) that joins them (0x0B bytes)."""

    trigger1: Trigger = field(default_factory=Trigger)
    trigger2: Trigger = field(default_factory=Trigger)
    unused: list[int] = _u8s(2)
    t1_or_t2: int = _u8()


@dataclass
class Role:
    """One craft role: a team character and a three-letter designation."""

    designation_team: int = _u8("DesignationTeam")
    designation: str = field(default="", metadata={"size": 3, "enum": "Designation"})


@dataclass
class Order:
    """One flight-group order (0x52 bytes).

    Bytes 0x23-0x51 follow Designation undefined by the document;
    ``reserved_nonzero`` maps the offset (within the order, ``"0x24"``) of
    each one that is not zero to its value.
    """

    order: int = _u8("Order")
    throttle: int = _u8()
    variables: list[int] = _u8s(4)
    target3_type: int = _u8("VariableType")
    target4_type: int = _u8("VariableType")
    target3: int = _u8()
    target4: int = _u8()
    target3_or_target4: int = _u8()
    unused_0b: int = _u8()
    target1_type: int = _u8("VariableType")
    target1: int = _u8()
    target2_type: int = _u8("VariableType")
    target2: int = _u8()
    target1_or_target2: int = _u8()
    unused_11: int = _u8()
    speed: int = _u8()
    designation: str = _text(16)
    reserved_nonzero: dict[str, int] = _nonzero_bytes()


@dataclass
class GoalFG:
    """One flight-group goal (0x4E bytes).

    ``reserved_0f`` is the first reserved byte; the answer sheets print a
    ``sequence`` value for each goal, zero everywhere, which this reader
    takes from here. ``reserved_nonzero`` keeps any other non-zero reserved
    byte (offset within the goal -> value).
    """

    argument: int = _u8("GoalArgument")
    condition: int = _u8("Condition")
    amount: int = _u8("Amount")
    points: int = _s8()
    enabled_for_team: list[int] = _u8s(10)
    time_limit: int = _u8()
    reserved_0f: int = _u8()
    reserved_nonzero: dict[str, int] = _nonzero_bytes()


@dataclass
class Waypoint:
    """One point of a flight group: X, Y, Z (km * 160) and Enabled.

    The format stores the four values as four parallel SHORT arrays of 22;
    point ``i`` here gathers element ``i`` of each. Points 0-3 are start
    points, 4-11 waypoints, 12 rendezvous, 13 hyperspace, 14-21 briefings.
    """

    x: int = _s16()
    y: int = _s16()
    z: int = _s16()
    enabled: int = _s16()


@dataclass
class FlightGroup:
    """One flight group (0x562 bytes).

    ``roles_text`` is the whole 20-byte Roles span read as one string (cut at
    the first NUL): stock missions also store free text there.
    """

    name: str = _text(20)
    roles: list[Role] = _many(4)
    roles_text: str = _text(20)
    cargo: str = _text(20)
    special_cargo: str = _text(20)
    special_cargo_craft: int = _u8()
    random_special_cargo: int = _u8()
    craft_type: int = _u8("CraftType")
    number_of_craft: int = _u8()
    status1: int = _u8("Status")
    warhead: int = _u8("Warhead")
    beam: int = _u8("Beam")
    iff: int = _u8()
    team: int = _u8()
    group_ai: int = _u8("GroupAI")
    markings: int = _u8("Markings")
    radio: int = _u8("Radio")
    unused_05c: int = _u8()
    formation: int = _u8("Formation")
    formation_spacing: int = _u8()
    global_group: int = _u8()
    leader_spacing: int = _u8()
    number_of_waves: int = _u8()
    waves_delay: int = _u8()
    stop_arriving_when: int = _u8("StopArrivingWhen")
    player_number: int = _u8()
    arrive_only_if_human: int = _u8()
    player_craft: int = _u8()
    yaw: int = _u8()
    pitch: int = _u8()
    roll: int = _u8()
    perma_death_enabled: int = _u8()
    perma_death_id: int = _u8()
    unused_06c: int = _u8()
    arrival_difficulty: int = _u8("ArrivalDifficulty")
    arrival1_and_2: TriggerPair = field(default_factory=TriggerPair)
    arrival3_and_4: TriggerPair = field(default_factory=TriggerPair)
    arrival12_or_arrival34: int = _u8()
    random_arrival_delay_extra_minutes: int = _u8()
    arrival_delay_minutes: int = _u8()
    arrival_delay_seconds: int = _u8()
    departure: TriggerPair = field(default_factory=TriggerPair)
    departure_delay_minutes: int = _u8()
    departure_delay_seconds: int = _u8()
    abort_trigger: int = _u8("AbortTrigger")
    random_arrival_delay_extra_seconds: int = _u8()
    io_reserved: int = _u8()
    current_mothership: int = _s16()
    arrival_mothership: int = _u8()
    arrive_via_mothership: int = _u8()
    departure_mothership: int = _u8()
    depart_via_mothership: int = _u8()
    alternate_mothership: int = _u8()
    alternate_mothership_used: int = _u8()
    captured_departure_mothership: int = _u8()
    captured_depart_via_mothership: int = _u8()
    orders: list[Order] = _many(4)
    skip_to_order4: TriggerPair = field(default_factory=TriggerPair)
    goals: list[GoalFG] = _many(8)
    unused_465: int = _u8()
    waypoints: list[Waypoint] = _many(22)
    waypoint_shown: int = _u8()
    unused_517: int = _u8()
    briefing_linked: list[int] = _u8s(8)
    prevent_craft_numbering: int = _u8()
    departure_clock_minutes: int = _u8()
    departure_clock_seconds: int = _u8()
    countermeasures: int = _u8("Countermeasures")
    craft_explosion_time: int = _u8()
    status2: int = _u8("Status")
    global_unit: int = _u8()
    briefing_start: list[int] = _u8s(8)
    handicap: int = _u8()
    optional_warheads: list[int] = _u8s(8, "Warhead")
    optional_beams: list[int] = _u8s(4, "Beam")
    optional_countermeasures: list[int] = _u8s(3, "Countermeasures")
    optional_craft_category: int = _u8("OptionalCraftCategory")
    optional_craft: list[int] = _u8s(10, "CraftType")
    number_of_optional_craft: list[int] = _u8s(10)
    number_of_optional_craft_waves: list[int] = _u8s(10)


@dataclass
class Message:
    """One in-flight message (0x74 bytes)."""

    message_index: int = _s16()
    message: str = _text(64)
    sent_to_teams: list[int] = _u8s(10)
    triggers: list[TriggerPair] = _many(2)
    editor_note: str = _text(16)
    delay: int = _u8()
    triggers12_or_triggers34: int = _u8()


@dataclass
class GoalGlobal:
    """One global goal of a team (0x2A bytes)."""

    triggers: list[TriggerPair] = _many(2)
    name: str = _text(16)
    version: int = _u8()
    triggers12_or_triggers34: int = _u8()
    delay: int = _u8()
    points: int = _s8()


@dataclass
class GlobalGoal:
    """One team's global goals: Primary, Prevent, Secondary (0x80 bytes)."""

    num_goals: int = _s16()
    goals: list[GoalGlobal] = _many(3)


@dataclass
class Team:
    """One team (0x1E7 bytes).

    ``unknown_12`` holds bytes 0x12-0x19, which the document leaves undefined
    between Name and Allegiances; some stock missions store values there.
    """

    reserved: int = _s16()
    name: str = _text(16)
    unknown_12: list[int] = _u8s(8)
    allegiances: list[int] = _u8s(10)
    end_of_mission_messages: list[str] = _texts(6, 64)
    eom_message_delays: list[int] = _u8s(3)


@dataclass
class Event:
    """One briefing event: a time in ticks, a type and its variables."""

    time: int = _s16()
    type: int = _s16("EventType")
    variables: list[int] = _s16s()


@dataclass
class Briefing:
    """One briefing: header counts, events, viewers, tags and strings.

    ``events`` stops after the End Briefing event (or where the walk could not
    continue); ``events_tail`` keeps the SHORTs left in the fixed event area
    after it, trailing zeros trimmed, so nothing stored is lost.
    ``events_complete`` says whether the walk reached End Briefing.
    """

    running_time: int = _s16()
    current_time: int = _s16()
    start_events: int = _s16()
    events_length: int = _s16()
    tile: int = _s16()
    events: list[Event] = _many(None)
    events_tail: list[int] = _s16s()
    events_complete: bool = True
    viewed_by_team: list[int] = _u8s(10)
    tags: list[str] = _texts(32, None)
    strings: list[str] = _texts(32, None)


@dataclass
class Layout:
    """Absolute file offsets where each section starts, as read."""

    flight_groups: int = 0
    messages: int = 0
    global_goals: int = 0
    teams: int = 0
    briefings: int = 0
    fg_goal_strings: int = 0
    global_goal_strings: int = 0
    descriptions: int = 0
    end: int = 0


@dataclass
class Mission:
    """A whole mission file.

    ``fg_goal_strings[fg][goal][state]`` and
    ``global_goal_strings[team][goal][trigger][state]``: state 0 Incomplete,
    1 Complete, 2 Failed. ``descriptions`` holds one string for version 12
    (MissionDescription) and three for version 14 (SuccessfulDebrief,
    FailedDebrief, MissionDescription).
    """

    header: FileHeader = field(default_factory=FileHeader)
    flight_groups: list[FlightGroup] = _many(None)
    messages: list[Message] = _many(None)
    global_goals: list[GlobalGoal] = _many(10)
    teams: list[Team] = _many(10)
    briefings: list[Briefing] = _many(8)
    fg_goal_strings: list[list[list[str]]] = field(
        default_factory=list, metadata={"dims": (None, 8, 3), "size": 64}
    )
    global_goal_strings: list[list[list[list[str]]]] = field(
        default_factory=list, metadata={"dims": (10, 3, 4, 3), "size": 64}
    )
    descriptions: list[str] = field(
        default_factory=list, metadata={"dims": ((1, 3),), "size": 4096}
    )
    layout: Layout = field(default_factory=Layout)
