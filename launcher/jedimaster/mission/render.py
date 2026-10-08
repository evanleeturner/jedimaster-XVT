"""Render a ``Mission`` as text in the answer-sheet format.

Purpose:
    Print a mission the way the reference listings (the answer sheets) print
    it, line for line, so a rendering can be diffed against a sheet.

Flow:
    Header line and legend; each flight group (identity, arrival and
    departure, motherships, four orders with their target predicates, the
    skip-to-order-4 pair, eight goals, enabled points); each message with its
    triggers; the 30 global goals; the 10 teams; the non-empty briefing tags
    and strings; the non-empty goal-string overrides between two offset
    lines.

Invariants:
    - Values print raw (no scaling), except the two delay clocks, which print
      ``minutes:seconds`` from their two stored bytes.
    - The vocabulary (``Destroyed(2)``, ``FlightGroup(1)``...) is the answer
      sheets' own, harvested from them, not the document's list names (those
      go to the JSON export). A value no sheet shows prints as
      ``Unknown<N>(N)`` for conditions and ``Unknown(N)`` elsewhere: the
      only two spellings the sheets show for values outside their tables.
    - Strings print in double quotes with ``\\`` and ``"`` escaped.
    - The text ends with one newline.

Call:
    ``text = render_mission(read_mission(path))``
"""

from __future__ import annotations

import logging

from .model import FlightGroup
from .model import GoalFG
from .model import Mission
from .model import Order
from .model import Trigger
from .model import TriggerPair
from .reader import FLIGHT_GROUP_SIZE

logger = logging.getLogger(__name__)

LEGEND = (
    "Indices are zero-based. Order targets are predicates; "
    "fallback is tried if primary finds no target."
)

# The answer sheets' names, keyed by raw value.
CONDITION_NAMES: dict[int, str] = {
    0: "AlwaysTrue",
    1: "Arrived",
    2: "Destroyed",
    3: "Attacked",
    4: "Captured",
    5: "Inspected",
    6: "Boarded",
    7: "Docked",
    8: "Disabled",
    9: "Survived",
    10: "NeverFalse",
    12: "Departed",
    13: "PrimaryComplete",
    14: "PrimaryFailed",
    19: "Unknown19",
    20: "ReinforcementNotCalled",
    21: "ShieldsDepleted",
    22: "HullAbove50",
    23: "NoWarheads",
    25: "NotArrived",
    26: "NotAttacked",
    27: "NotDisabled",
    28: "NotCaptured",
    29: "NotInspected",
    30: "CompletedMission",
    31: "NotBoarded",
    32: "FailedMission",
    33: "NotDocked",
    34: "ShieldsBelow50",
    35: "ShieldsBelow25",
    36: "HullAbove25",
    37: "HullAbove75",
    38: "AlwaysPending",
    39: "NoCondition",
    41: "PlayerConnected",
    42: "PlayerDisconnected",
    43: "DestroyedOrDeparted",
    44: "OrderCompleted",
    45: "DestroyedOrCaptured",
    46: "CapturedByDestination",
}

VARIABLE_TYPE_NAMES: dict[int, str] = {
    0: "None",
    1: "FlightGroup",
    2: "SpeciesMinusOne",
    3: "ShipClass",
    4: "ObjectFamily",
    5: "IFF",
    7: "CraftWhen",
    8: "GlobalGroup",
    12: "Team",
    13: "PlayerNumber",
    14: "BeforeTime",
    15: "NotFlightGroup",
    16: "NotSpeciesMinusOne",
    17: "NotShipClass",
    19: "NotIFF",
    21: "NotTeam",
    22: "NotPlayerNumber",
    23: "GlobalUnit",
    24: "NotGlobalUnit",
}

AMOUNT_NAMES: dict[int, str] = {
    0: "All",
    1: "75%",
    2: "50%",
    3: "25%",
    4: "AtLeastOne",
    5: "AllButOne",
    6: "AllSpecialCargo",
    7: "AllNonSpecial",
    9: "PlayerFG",
    16: "66%",
    17: "33%",
}

ORDER_NAMES: dict[int, str] = {
    0: "Hold",
    1: "GoHome",
    2: "Formation",
    3: "FormationEvade",
    5: "Disabled",
    6: "WaitForBoard",
    7: "Attack",
    9: "Respond",
    10: "Escort",
    11: "Disable",
    12: "BoardGive",
    13: "BoardTake",
    14: "BoardExchange",
    15: "BoardCapture",
    16: "BoardDestroy",
    17: "BoardPickup",
    18: "DropOff",
    19: "Wait",
    20: "Wait",
    21: "StarshipFormation",
    22: "StarshipWaitReturn",
    23: "StarshipWaitCreate",
    24: "StarshipProtect",
    26: "StarshipAttack",
    27: "StarshipDisable",
    29: "StarshipHyperspace",
    31: "BoardContact",
    32: "BoardRepair",
    36: "SelfDestroy",
    37: "Kamikaze",
}

# CraftWhen (variable type 7) values the sheets annotate with a bracketed
# state name; other CraftWhen values print bare.
CRAFT_WHEN_TYPE = 7
CRAFT_WHEN_NAMES: dict[int, str] = {
    2: "Boarded",
    4: "Disabled",
    12: "Operational",
}

# Variable types whose variable is a flight-group index; the sheets follow
# such a target with the group's name when the index names a group.
FLIGHT_GROUP_VARIABLE_TYPES = (1, 15)


def quote(text: str) -> str:
    """Return ``text`` in double quotes with backslash and quote escaped.

    Returns ``""`` (two quote characters) for an empty string. Does not
    escape control or non-ASCII characters.
    """
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _named(table: dict[int, str], value: int, unknown: str = "Unknown") -> str:
    return f"{table.get(value, unknown)}({value})"


def condition_label(value: int) -> str:
    """Return a condition as ``Name(value)``, ``Unknown<value>(value)`` if unnamed.

    Does not check the value's range.
    """
    return _named(CONDITION_NAMES, value, f"Unknown{value}")


def _bool(value: int) -> str:
    return f"{'OR' if value else 'AND'}[{value}]"


def _clock(minutes: int, seconds: int) -> str:
    return f"{minutes}:{seconds:02d}"


def _flags(values: list[int]) -> str:
    return "".join(f" {i}:{v}" for i, v in enumerate(values) if v)


class _Renderer:
    def __init__(self, mission: Mission) -> None:
        self.m = mission
        self.lines: list[str] = []

    def out(self, line: str = "") -> None:
        self.lines.append(line)

    def target(self, var_type: int, value: int) -> str:
        text = f"{_named(VARIABLE_TYPE_NAMES, var_type)}={value}"
        groups = self.m.flight_groups
        if var_type in FLIGHT_GROUP_VARIABLE_TYPES and value < len(groups):
            text += " " + quote(groups[value].name)
        elif var_type == CRAFT_WHEN_TYPE and value in CRAFT_WHEN_NAMES:
            text += f" [{CRAFT_WHEN_NAMES[value]}]"
        return text

    def trigger(self, t: Trigger) -> str:
        condition = condition_label(t.condition)
        target = self.target(t.variable_type, t.variable)
        return f"{condition} {target} amount={_named(AMOUNT_NAMES, t.amount)}"

    def pair(self, p: TriggerPair) -> str:
        first, second = self.trigger(p.trigger1), self.trigger(p.trigger2)
        return f"({first} {_bool(p.t1_or_t2)} {second})"

    def two_pairs(self, first: TriggerPair, joiner: int, second: TriggerPair) -> str:
        return f"{self.pair(first)} {_bool(joiner)} {self.pair(second)}"

    def order(self, k: int, o: Order) -> None:
        variables = ",".join(str(v) for v in o.variables)
        self.out(
            f"  Order[{k}] {_named(ORDER_NAMES, o.order)} throttle={o.throttle} "
            f"vars={variables} speed={o.speed} designation={quote(o.designation)}"
        )
        t1 = self.target(o.target1_type, o.target1)
        t2 = self.target(o.target2_type, o.target2)
        self.out(f"    primary: {t1} {_bool(o.target1_or_target2)} {t2}")
        t3 = self.target(o.target3_type, o.target3)
        t4 = self.target(o.target4_type, o.target4)
        self.out(f"    fallback: {t3} {_bool(o.target3_or_target4)} {t4}")

    def goal(self, k: int, g: GoalFG) -> None:
        self.out(
            f"  Goal[{k}] type={g.argument} condition={condition_label(g.condition)} "
            f"amount={_named(AMOUNT_NAMES, g.amount)} points(raw)={g.points} "
            f"teams:{_flags(g.enabled_for_team)} timeLimit5s={g.time_limit} "
            f"sequence={g.reserved_0f}"
        )

    def flight_group(self, i: int, fg: FlightGroup) -> None:
        offset = self.m.layout.flight_groups + i * FLIGHT_GROUP_SIZE
        self.out(
            f"FG[{i}] @0x{offset:x} {quote(fg.name)} species={fg.craft_type} "
            f"craft={fg.number_of_craft} waves(raw)={fg.number_of_waves} "
            f"IFF={fg.iff} team={fg.team} AI={fg.group_ai} "
            f"globalGroup={fg.global_group} globalUnit={fg.global_unit}"
        )
        self.out(
            f"  role={quote(fg.roles_text)} cargo={quote(fg.cargo)} "
            f"specialCargo={quote(fg.special_cargo)}"
        )
        self.out(
            f"  status={fg.status1},{fg.status2} player={fg.player_number} "
            f"playerCraft={fg.player_craft} "
            f"arriveOnlyIfHuman={fg.arrive_only_if_human} "
            f"difficulty={fg.arrival_difficulty}"
        )
        arrival = self.two_pairs(
            fg.arrival1_and_2, fg.arrival12_or_arrival34, fg.arrival3_and_4
        )
        delay = _clock(fg.arrival_delay_minutes, fg.arrival_delay_seconds)
        extra = _clock(
            fg.random_arrival_delay_extra_minutes,
            fg.random_arrival_delay_extra_seconds,
        )
        self.out(f"  arrival: {arrival} delay={delay} random={extra}")
        delay = _clock(fg.departure_delay_minutes, fg.departure_delay_seconds)
        self.out(
            f"  departure: {self.pair(fg.departure)} delay={delay} "
            f"abort={fg.abort_trigger}"
        )
        self.out(
            f"  motherships: arrival={fg.arrival_mothership} "
            f"method={fg.arrive_via_mothership} "
            f"departure={fg.departure_mothership} "
            f"method={fg.depart_via_mothership} "
            f"alternate={fg.alternate_mothership} "
            f"used={fg.alternate_mothership_used} "
            f"captured={fg.captured_departure_mothership} "
            f"method={fg.captured_depart_via_mothership}"
        )
        for k, o in enumerate(fg.orders):
            self.order(k, o)
        self.out(f"  SkipToOrder[3]: {self.pair(fg.skip_to_order4)}")
        for k, g in enumerate(fg.goals):
            self.goal(k, g)
        for k, p in enumerate(fg.waypoints):
            if p.enabled:
                self.out(f"  Point[{k}] x={p.x} y={p.y} z={p.z} enabled={p.enabled}")
        self.out()

    def messages(self) -> None:
        for msg in self.m.messages:
            self.out(
                f"Message[{msg.message_index}] {quote(msg.message)} "
                f"delay(raw)={msg.delay} teams:{_flags(msg.sent_to_teams)}"
            )
            joiner = msg.triggers12_or_triggers34
            self.out("  " + self.two_pairs(msg.triggers[0], joiner, msg.triggers[1]))
            self.out()

    def global_goals(self) -> None:
        for t, gg in enumerate(self.m.global_goals):
            for k, g in enumerate(gg.goals):
                self.out(
                    f"Team[{t}] GlobalGoal[{k}] {quote(g.name)} "
                    f"points(raw)={g.points} delay(raw)={g.delay}"
                )
                joiner = g.triggers12_or_triggers34
                self.out("  " + self.two_pairs(g.triggers[0], joiner, g.triggers[1]))
                self.out()

    def teams(self) -> None:
        for t, team in enumerate(self.m.teams):
            if t:
                self.out()
            self.out(f"Team[{t}] {quote(team.name)} allies:{_flags(team.allegiances)}")
            for k, text in enumerate(team.end_of_mission_messages):
                self.out(f"  EndMessage[{k}] {quote(text)}")

    def briefings(self) -> None:
        for b, brief in enumerate(self.m.briefings):
            for k, text in enumerate(brief.tags):
                if text:
                    self.out(f"Briefing[{b}] Label[{k}] {quote(text)}")
            for k, text in enumerate(brief.strings):
                if text:
                    self.out(f"Briefing[{b}] Text[{k}] {quote(text)}")

    def goal_strings(self) -> None:
        layout = self.m.layout
        self.out(
            f"Goal text overrides @0x{layout.fg_goal_strings:x} "
            "(raw display-state slots)"
        )
        for f, goals in enumerate(self.m.fg_goal_strings):
            for k, states in enumerate(goals):
                for s, text in enumerate(states):
                    if text:
                        self.out(f"FG[{f}] Goal[{k}] state[{s}] {quote(text)}")
        for t, goals in enumerate(self.m.global_goal_strings):
            for k, triggers in enumerate(goals):
                for n, states in enumerate(triggers):
                    for s, text in enumerate(states):
                        if text:
                            self.out(
                                f"Team[{t}] GlobalGoal[{k}] Trigger[{n}] "
                                f"state[{s}] {quote(text)}"
                            )
        self.out(f"Consumed mission runtime data through 0x{layout.descriptions:x}")

    def render(self) -> str:
        h = self.m.header
        self.out(
            f"XvT/BoP version={h.platform_id} flightGroups={h.num_fgs} "
            f"messages={h.num_messages} missionType={h.mission_type} "
            f"goalsUnimportant={h.goals_unimportant}"
        )
        self.out(LEGEND)
        self.out()
        for i, fg in enumerate(self.m.flight_groups):
            self.flight_group(i, fg)
        self.messages()
        self.global_goals()
        self.teams()
        self.briefings()
        self.out()
        self.goal_strings()
        return "\n".join(self.lines) + "\n"


def render_mission(mission: Mission) -> str:
    """Return the answer-sheet text for ``mission``, ending in one newline.

    Always returns a string; raises only if the model is missing a section
    the reader always fills (``AttributeError``/``TypeError``). Does not
    print briefing events, descriptions, IFF names or the other fields the
    sheets leave out, and does not check values against any list.
    """
    logger.debug("rendering %s flight groups", len(mission.flight_groups))
    return _Renderer(mission).render()
