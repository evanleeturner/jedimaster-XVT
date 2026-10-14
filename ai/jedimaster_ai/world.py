"""What the engine tells the AI program about craft, and what it answers.

Purpose:
    The meaning of the messages between the engine and the AI program, apart
    from their bytes (``wire`` turns these into bytes and back). The field
    names are the engine's, from ``engine/src/xvt_runtime/ai/ai_wire.h``.

Invariants:
    - Every number is an integer in the engine's units: angles are 65,536 to
      a full turn, throttle is a fraction of 65,536.
    - A craft answer carries each action kind at most once: ``Actions`` has
      one field per kind, and a field left None sends nothing.
    - A value that the engine would refuse fails here, where it is made,
      with ValueError: an answer the program writes always passes the
      engine's reader.
"""

from __future__ import annotations

import dataclasses
import enum

NOTEBOOK_BYTES = 64
U16_MAX = 0xFFFF
# The object index that names no object, as a target.
NO_OBJECT = U16_MAX
CANNON_GROUPS = 2
WARHEAD_LAUNCHERS = 2
# Weapon slots a craft has, and so turrets one answer may aim.
WEAPON_SLOTS = 16
# Contacts a question shows at most: the active region's craft slots.
CONTACTS_MAX = 32
# The fastest speed an arrival may set: the 1997 AI's speed out of hyperspace.
ARRIVAL_SPEED_MAX = 3600
# A flight group's order slots, 0 to 3, and the axes of a point.
ORDER_SLOTS = 4
POINT_AXES = 3


class Activity(enum.IntEnum):
    """What a craft is doing, as the engine reads it (``enum ai_activity``)."""

    NONE = 0
    HOLDING_STATION = 1
    IN_FORMATION = 2
    WAITING_FOR_GO = 3
    WAITING = 4
    ARRIVING_FROM_HYPERSPACE = 5
    LEAVING_BY_HYPERSPACE = 6
    LEAVING_HANGAR = 7
    ENTERING_HANGAR = 8
    GOING_HOME = 9
    GOING_HOME_EVADING = 10
    BOARDING = 11
    DISABLED = 12


# The activities only the engine sets: a round's arrival out of hyperspace or
# out of a hangar. An answer may end them, never start them.
ENGINE_ACTIVITIES = frozenset(
    {Activity.ARRIVING_FROM_HYPERSPACE, Activity.LEAVING_HANGAR}
)


class Books(enum.IntFlag):
    """The bits of a craft view's ``books`` (``enum ai_wire_books``).

    The engine keeps the mission's books by the 1997 rules and the program
    decides what to do about them:

    - ORDERS_DONE: every order its group has in slots 0 to 2 is done
      (state 2), or its leader's are
    - ABORTED: its group's abort trigger held, or a player sent it home
    - DEPARTURE_DUE: its group's departure came and the delay has passed
    - SKIP_DUE: its group's skip-to-order-4 trigger holds
    - SKIPPED: it skipped to order 4
    - MAY_JUMP: it may depart: active, with a hyperdrive, its group leaving
      by hyperspace, or by a mothership that is gone with none of its
      groups still to arrive
    - AT_HANGAR: it may dock: active, within 512 world units of its
      mothership's inside hangar point (1,024 for a starship)
    - BY_MOTHERSHIP: its group leaves by mothership, not by hyperspace
    - HOME_POINT: its home point is on, so ``home_point`` is given
    - TARGETS_LEFT: its current order still has a target, in the battle or
      in a group still to arrive, by the 1997 count
    """

    ORDERS_DONE = 0x001
    ABORTED = 0x002
    DEPARTURE_DUE = 0x004
    SKIP_DUE = 0x008
    SKIPPED = 0x010
    MAY_JUMP = 0x020
    AT_HANGAR = 0x040
    BY_MOTHERSHIP = 0x080
    HOME_POINT = 0x100
    TARGETS_LEFT = 0x200


@dataclasses.dataclass(frozen=True)
class CraftView:
    """One craft due to think, as a question describes it.

    ``order_code`` is the mission order the craft works on (``orders.
    OrderCode``); the engine may send a code no order has. Bit n of
    ``order_target_mask`` is set when the question's contact n matches the
    targets of that order; bit n of ``turret_slot_mask`` when weapon slot n
    is a turret. The four pairs hold each cannon group's shot object type,
    and each warhead launcher's warhead type, warheads left and ticks before
    it may fire. Bit n of ``enemy_mask`` is set when contact n is hostile to
    the craft by the mission's alliances: never its own team or an allied
    one. ``leader`` is the object index of the craft its flight follows,
    NO_OBJECT when it leads; the engine names it. ``order_codes`` and
    ``order_states`` are each order slot's order and completion (2 once its
    goal was met, 3 once a boarding order had no target left, else 0);
    ``order_throttle`` (a craft throttle, a fraction of 65,536),
    ``order_variable1``, ``order_variable2`` and ``order_speed`` are the
    current order's. ``full_speed`` is the speed full throttle gives the
    craft, by the 1997 set-speed rule. ``books`` holds ``Books`` bits.
    ``mothership`` is the object index of the craft it goes home into,
    NO_OBJECT for none, with that craft's ``outside_hangar`` and
    ``inside_hangar`` points; ``home_point`` is where it leaves by
    hyperspace, its group's mission point 13, or 12 for a captured craft
    when that point is on. Points are world (x, y, z), all 0 when not
    given. ``notebook`` is the craft's NOTEBOOK_BYTES of memory, as the
    program last wrote it, or all zero bytes before the program first
    writes it.
    """

    object_index: int
    object_signature: int
    object_type: int
    flight_group: int
    order_code: int
    order_slot: int
    activity: int
    team: int
    skill: int
    think_interval: int
    throttle: int
    x: int
    y: int
    z: int
    yaw: int
    pitch: int
    roll: int
    speed: int
    random_seed: int
    order_target_mask: int
    turret_slot_mask: int
    cannon_group_count: int
    warhead_launcher_count: int
    cannon_type: tuple[int, int]
    warhead_type: tuple[int, int]
    warheads_left: tuple[int, int]
    launcher_cooldown: tuple[int, int]
    enemy_mask: int
    leader: int
    order_codes: tuple[int, int, int, int]
    order_states: tuple[int, int, int, int]
    order_throttle: int
    order_variable1: int
    order_variable2: int
    order_speed: int
    full_speed: int
    books: int
    mothership: int
    outside_hangar: tuple[int, int, int]
    inside_hangar: tuple[int, int, int]
    home_point: tuple[int, int, int]
    notebook: bytes


@dataclasses.dataclass(frozen=True)
class Contact:
    """One craft in the active region, due to think or not.

    ``genus`` and ``object_kind`` are the engine's codes (fighter, starship,
    mine; active, breaking up, exploding). ``target`` is the player's target
    when a player flies the craft, else its AI target; ``target`` and
    ``last_attacker`` are NO_OBJECT for none. Shields and hull are the craft
    record's own numbers. ``activity`` is the engine's activity code
    (``Activity``). ``size`` is the longest side of the box around the craft
    type's model, in world units: the number the engine's flight code takes
    as the craft's size.
    """

    object_index: int
    object_signature: int
    object_type: int
    genus: int
    team: int
    flight_group: int
    object_kind: int
    flags: int
    x: int
    y: int
    z: int
    yaw: int
    pitch: int
    speed: int
    target: int
    last_attacker: int
    front_shield: int
    rear_shield: int
    hull_damage: int
    hull_strength: int
    activity: int
    size: int

    @property
    def player_flown(self) -> bool:
        """Whether a player flies the craft."""
        return bool(self.flags & 1)


@dataclasses.dataclass(frozen=True)
class Question:
    """The craft due to think at one reference step, in the engine's order,
    and every contact the step shows."""

    tick: int
    craft: tuple[CraftView, ...]
    contacts: tuple[Contact, ...] = ()


def _check_u16(name: str, value: int, low: int = 0) -> None:
    if not low <= value <= U16_MAX:
        raise ValueError(f"{name} {value!r} is outside {low} to {U16_MAX}")


@dataclasses.dataclass(frozen=True)
class Steer:
    """The heading, pitch and roll a craft turns to."""

    yaw: int
    pitch: int
    roll: int

    def __post_init__(self) -> None:
        _check_u16("yaw", self.yaw)
        _check_u16("pitch", self.pitch)
        _check_u16("roll", self.roll)


class LinkMode(enum.IntEnum):
    """How a cannon group fires its burst (``craft_laser_state.link_mode``)."""

    HOLD = 0
    ONE_AT_A_TIME = 1
    PAIRS = 2
    ALL = 3


@dataclasses.dataclass(frozen=True)
class CannonGroup:
    """One cannon group's burst: a link mode with 1 to 255 shots, or HOLD with 0."""

    mode: LinkMode = LinkMode.HOLD
    burst: int = 0

    def __post_init__(self) -> None:
        object.__setattr__(self, "mode", LinkMode(self.mode))
        low = 0 if self.mode == LinkMode.HOLD else 1
        high = 0 if self.mode == LinkMode.HOLD else 0xFF
        if not low <= self.burst <= high:
            raise ValueError(
                f"a burst of {self.burst!r} shots does not go with mode {self.mode.name}"
            )


HOLD_FIRE = CannonGroup()


@dataclasses.dataclass(frozen=True)
class Warhead:
    """A warhead launcher to fire, both its slots at once when paired."""

    launcher: int
    paired: bool = False

    def __post_init__(self) -> None:
        if not 0 <= self.launcher < WARHEAD_LAUNCHERS:
            raise ValueError(f"launcher {self.launcher!r} is not 0 or 1")


@dataclasses.dataclass(frozen=True)
class Turret:
    """A turret's weapon slot and the object it fires at, NO_OBJECT for none."""

    slot: int
    target: int

    def __post_init__(self) -> None:
        if not 0 <= self.slot < WEAPON_SLOTS:
            raise ValueError(f"weapon slot {self.slot!r} is outside 0 to 15")
        _check_u16("turret target", self.target)


@dataclasses.dataclass(frozen=True)
class Arrival:
    """The craft marked arriving from hyperspace, or active again, at a speed.

    While a craft is marked arriving the engine flies it straight on and
    leaves its speed alone; either way the speed is set at once.
    """

    arriving: bool
    speed: int

    def __post_init__(self) -> None:
        if not 0 <= self.speed <= ARRIVAL_SPEED_MAX:
            raise ValueError(
                f"arrival speed {self.speed!r} is outside 0 to {ARRIVAL_SPEED_MAX}"
            )


@dataclasses.dataclass(frozen=True)
class OrderMove:
    """The order slot a craft works on from now, 0 to 3, and the completion
    state the slot it worked on takes: 0 leaves it, 2 its goal was met, 3 a
    boarding order had no target left."""

    slot: int
    state: int = 0

    def __post_init__(self) -> None:
        if not 0 <= self.slot < ORDER_SLOTS:
            raise ValueError(f"order slot {self.slot!r} is outside 0 to 3")
        if self.state not in (0, 2, 3):
            raise ValueError(f"order state {self.state!r} is not 0, 2 or 3")


@dataclasses.dataclass(frozen=True)
class Actions:
    """What one craft is told to do; None sends nothing of that kind.

    ``throttle`` is a fraction of 65,536. ``think_interval`` is the ticks
    until the craft is asked about again, 1 or more. ``target`` is an object
    index, NO_OBJECT to clear the target. ``lasers`` sets both cannon groups.
    ``turrets`` aims 1 to 16 turrets, each slot once. ``arrival`` marks the
    craft arriving from hyperspace or active again. ``order`` moves the
    craft's order slot; ``depart`` starts it out of the battle by
    hyperspace and ``dock`` takes it into its mothership's hangar, never
    both. ``activity`` is never one of ENGINE_ACTIVITIES. What these name must
    be what the question showed (``wire.read_answer`` holds the rules).
    """

    steer: Steer | None = None
    throttle: int | None = None
    activity: Activity | None = None
    think_interval: int | None = None
    target: int | None = None
    lasers: tuple[CannonGroup, CannonGroup] | None = None
    warhead: Warhead | None = None
    turrets: tuple[Turret, ...] | None = None
    arrival: Arrival | None = None
    order: OrderMove | None = None
    depart: bool = False
    dock: bool = False

    def __post_init__(self) -> None:
        if self.depart and self.dock:
            raise ValueError("depart and dock are both sent")
        if self.target is not None:
            _check_u16("target", self.target)
        if self.lasers is not None and len(self.lasers) != CANNON_GROUPS:
            raise ValueError(f"lasers names {len(self.lasers)} cannon groups, not 2")
        if self.turrets is not None:
            slots = [turret.slot for turret in self.turrets]
            if not 1 <= len(slots) <= WEAPON_SLOTS or len(set(slots)) != len(slots):
                raise ValueError(f"turrets names slots {slots}: 1 to 16, each once")
        if self.throttle is not None:
            _check_u16("throttle", self.throttle)
        if self.activity is not None:
            activity = Activity(self.activity)
            if activity in ENGINE_ACTIVITIES:
                raise ValueError(f"activity {activity.name} is set by the engine alone")
            object.__setattr__(self, "activity", activity)
        if self.think_interval is not None:
            _check_u16("think_interval", self.think_interval, low=1)


NO_ACTIONS = Actions()


@dataclasses.dataclass(frozen=True)
class CraftAnswer:
    """One craft's answer: its new notebook and what it is told to do."""

    object_index: int
    notebook: bytes
    actions: Actions = NO_ACTIONS

    def __post_init__(self) -> None:
        if len(self.notebook) != NOTEBOOK_BYTES:
            raise ValueError(
                f"craft {self.object_index}'s notebook holds "
                f"{len(self.notebook)} bytes, not {NOTEBOOK_BYTES}"
            )


@dataclasses.dataclass(frozen=True)
class Answer:
    """The answer to one question: one craft answer per craft, in its order."""

    tick: int
    craft: tuple[CraftAnswer, ...]
