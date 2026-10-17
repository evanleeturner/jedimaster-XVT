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
# The static object slots; the static target mask has a bit for each.
STATICS_MAX = 64
# The fastest speed an arrival may set: the 1997 AI's speed out of hyperspace.
ARRIVAL_SPEED_MAX = 3600
# A flight group's order slots, 0 to 3, and the axes of a point.
ORDER_SLOTS = 4
POINT_AXES = 3
# The homing shots aimed at a craft that its view can name.
THREATS_MAX = 4
# A flight group's waypoints, mission points 4 to 11.
WAYPOINTS = 8
# The longest maneuver timer an answer may set, in ticks: the engine's int.
MANEUVER_TIMER_MAX = 0x7FFFFFFF
# The self destruct delay's units, each 1,180 ticks: 1 to 55, as 56 units
# would not fit the engine's 16-bit lifetime timer.
SELF_DESTRUCT_UNITS_MAX = 55
# Ticks in one unit of the self destruct delay.
SELF_DESTRUCT_UNIT_TICKS = 1180
# The signatures of docked craft a craft view holds.
BOARDED_MAX = 10
# The mission order codes that board a craft: Give, Take, Exchange, Capture,
# Destroy and Pick Up cargo (12 to 17), SS Board (31) and Repair (32).
BOARDING_ORDER_CODES = frozenset({12, 13, 14, 15, 16, 17, 31, 32})


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
    LOOKING_TO_BOARD = 13
    LOOKING_TO_CAPTURE = 14
    LOOKING_TO_PICK_UP = 15
    DEPLOYING_CRAFT = 16
    SELF_DESTRUCTING = 17
    KAMIKAZE_ATTACK = 18
    FLYING_TO_RENDEZVOUS = 19
    WAITING_FOR_BOARDING_CRAFT = 20


# The activities only the engine sets: a round's arrival out of hyperspace or
# out of a hangar. An answer may end them, never start them.
ENGINE_ACTIVITIES = frozenset(
    {Activity.ARRIVING_FROM_HYPERSPACE, Activity.LEAVING_HANGAR}
)


class Countermeasure(enum.IntEnum):
    """A craft's countermeasure (``enum ai_wire_countermeasure``).

    The engine's own countermeasure type; a view may carry a later type,
    such as a cluster mine, which the program cannot launch.
    """

    NONE = 0
    CHAFF = 1
    FLARE = 2


class Lock(enum.IntEnum):
    """A craft's warhead lock (``enum ai_wire_lock_state``), as a player's:
    none, counting while the target stays in aim, or declared once the count
    reaches the ticks the craft needs."""

    NONE = 0
    COUNTING = 1
    DECLARED = 2


@dataclasses.dataclass(frozen=True)
class Threat:
    """A homing shot aimed at a craft: its object index, its object type,
    where it is, and whether a countermeasure already homes on it."""

    object_index: int
    object_type: int
    x: int
    y: int
    z: int
    decoyed: bool


class Commands(enum.IntFlag):
    """The bits of a craft view's ``commands`` (``enum ai_wire_commands``):
    a player's wingman commands that stand.

    - WAIT: a wait command (Shift+W) holds the craft until a go command
      (Shift+G), an evade command or an attack or cover order
    - EVADE: an evade command (Shift+E): the craft drops its target and
      says so with the ``evaded`` action
    """

    WAIT = 0x01
    EVADE = 0x02


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
    - PLACE: it follows a leader, so ``place`` is given
    - ALL_RETURNED: every flight group that goes home into its group has
      arrived, has no waves left and has no craft left in the battle, the
      1997 test that ends SS Await Return
    - STATION: it escorts a craft, so ``escorted`` and ``station`` are given
    - START_POINT: its group has arrived, so ``start_point`` is given
    - BOARD_POINT: its current order boards a craft and its target is a
      craft with a model, so ``board_point`` is given
    - AT_BOARD_POINT: BOARD_POINT, it is within 2,048 units of the point,
      and its target is not a player's craft that moves
    - ALL_LAUNCHED: every flight group that arrives from its group as its
      mothership has arrived with no waves left, the 1997 test that ends
      SS Launch
    - CAPTURED: the craft has been captured: its capture mark, the flight
      group that holds it, is not 0
    - DROP_POINT: its current order is Drop Off, variable2 names a group of
      the mission whose delivered count is below its total, and it has no
      leader; ``drop_point`` is given (a group whose total is past 255 counts
      as nothing to drop)
    - AT_DROP_POINT: DROP_POINT, within 2,048 of the point (2,048 itself is
      out), and the delivered group has no arrived outcome
    - ALL_DROPPED: a Drop Off with nothing left to drop: the delivered count
      has reached the group's total, or, with no drop running, variable2 names
      no group of the mission, the group has already arrived, or its total is
      past 255
    - HURT: the craft's hull damage has reached its craft record's
      system damage hull threshold, a number its model sets
    - RENDEZVOUS_POINT: its current order is Rendezvous, so
      ``rendezvous_point`` is given

    ALL_RETURNED comes with wire version 13, STATION and START_POINT with
    14, BOARD_POINT, AT_BOARD_POINT and ALL_LAUNCHED with 16, CAPTURED with
    17, DROP_POINT, AT_DROP_POINT and ALL_DROPPED with 18, HURT with 19,
    RENDEZVOUS_POINT with 20; the engine sends none of them before.
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
    PLACE = 0x400
    ALL_RETURNED = 0x800
    STATION = 0x1000
    START_POINT = 0x2000
    BOARD_POINT = 0x4000
    AT_BOARD_POINT = 0x8000
    ALL_LAUNCHED = 0x10000
    CAPTURED = 0x20000
    DROP_POINT = 0x40000
    AT_DROP_POINT = 0x80000
    ALL_DROPPED = 0x100000
    HURT = 0x200000
    RENDEZVOUS_POINT = 0x400000


class ContactFlags(enum.IntFlag):
    """The bits of a contact's ``flags`` (``enum ai_wire_contact_flag``).

    - PLAYER: a player flies the craft
    - DISABLED: none of its subsystems work, its ``working_subsystems`` 0
    - ATTACKED: a craft of some team, any of the ten, has hit it
    - STATIC: a static object, not a craft: a buoy, mine or other fixed
      object, listed in ``Question.statics``
    - PARKED: its current order holds it still: Hold Steady or one of its
      copies, or no order (codes 0, 28, 30, 33, 34, 35, 38, 39)
    - AWAITING: its current order is Disabled or Await Boarding (5, 6), so
      it waits to be boarded

    DISABLED and ATTACKED come with wire version 13, STATIC with 14, and
    PARKED and AWAITING with 16; the engine sends none of them before.
    """

    PLAYER = 0x1
    DISABLED = 0x2
    ATTACKED = 0x4
    STATIC = 0x8
    PARKED = 0x10
    AWAITING = 0x20


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
    given. ``countermeasure`` is the craft's countermeasure type, with
    ``countermeasures_left``, ``countermeasure_working`` (1 while its
    countermeasure system works), ``chaff_seconds`` (simulated seconds of
    chaff left) and ``countermeasure_cooldown`` (ticks before another
    launches). The ``threat_`` fields hold up to THREATS_MAX homing shots
    aimed at the craft, in the engine's slot order, the first
    ``threat_count`` filled and the rest 0; ``threats`` reads them. Bit n
    of ``waypoint_mask`` is set when its group's waypoint n, mission point
    4 + n, is on, and the ``waypoint_`` fields hold each waypoint's world
    position, 0 for one that is off; ``waypoint_on`` and ``waypoint`` read
    them. ``commands`` holds ``Commands`` bits; ``command_target`` is the
    object a player's attack or cover order gave the craft and
    ``avoid_target`` the one an ignore order told it to leave alone, each
    NO_OBJECT for none. ``place`` is the world position of the craft's
    place in its flight's formation, beside its leader by the 1997 rule,
    given with the PLACE book; (0, 0, 0) when not. ``lock_ticks`` is the
    craft's warhead lock, which the engine counts by a player's rule against
    its target, ``lock_needed`` the ticks it must reach to be declared (354
    on a Missile Boat, else 708) and ``lock_state`` a ``Lock`` value.
    ``skill_level`` is its AI rank, 0 to 7 (6 and 7 the Sith ranks), where
    ``skill`` gives levels 4 and 5 one value.
    ``notebook`` is the craft's NOTEBOOK_BYTES of memory, as the
    program last wrote it, or all zero bytes before the program first
    writes it. ``maneuver_timer`` is the ticks left in the craft's
    maneuver, which the engine lowers to 0 and the HUD shows as its order
    time; the ``maneuver_timer`` action sets it. ``escorted`` is the object
    index of the craft it escorts, NO_OBJECT for none, and ``station`` the
    world position of its station beside that craft, (0, 0, 0) when not
    given; both come with the STATION book and read NO_OBJECT and (0, 0, 0)
    until the engine sends them. ``start_point`` is where its group
    arrived: the group's start point, or the variant the engine picked at
    arrival, a world position as ``waypoint`` gives one; it comes with the
    START_POINT book and reads (0, 0, 0) until the engine sends it.
    ``targets_now`` holds bits 0 to 2: bit n is set when the order in slot n
    has a target in the battle now, by the books' count of targets left
    without its later half; it ignores range and capacity. Bit n of
    ``static_target_mask`` is set when the question's static n matches the
    targets of the craft's order. ``maneuver_timer`` comes with wire version
    13 and reads 0 before it; ``targets_now`` and ``static_target_mask`` come
    with 14 and read 0.

    ``board_point`` is where the craft waits to dock with its target, 1997's
    approach point above the target's docking hatch, a world position; it
    comes with the BOARD_POINT book and reads (0, 0, 0) without it. Item n
    of ``order_progress`` counts what the order in slot n has done: for a
    boarding order, the craft's boardings, for a Drop Off the places
    delivered (at most 255). ``boarded`` holds the signatures
    of the craft it has boarded, oldest first, at most 10. All three come
    with wire version 16 and read (0, 0, 0), (0, 0, 0, 0) and () before it.

    ``board_phase`` is where the engine's dock of the craft stands: 0 none,
    1 align, 2 transfer, 3 separate. The engine docks a craft from the board
    action to the separation's end, steering it and setting its throttle,
    and refuses the maneuver timer meanwhile. ``lifetime_timer`` is the ticks
    left on the craft's object's lifetime timer, counted down, the craft
    destroyed at 0; 0 when none runs. A self destruct is asked for only
    while it reads 0, as the engine keeps a running timer. Bit n of
    ``order_first_pair_mask`` is set when the question's contact n matches
    the first pair of the targets of the craft's order alone (targets 1 and
    2), by the rule of ``order_target_mask``, of which every bit of it is
    also set: a search tries these first and the rest after, as the second
    pair, the order's fallback, can swap the boarders of a mirrored order.
    These three come with wire version 17 and read 0, 0 and 0 before it.

    ``drop_point`` is a Drop Off carrier's approach point, slot 0 of the
    delivered group with 932 added in Z, given with the books' DROP_POINT bit
    and (0, 0, 0) without it. ``drop_phase`` is where the engine's drop
    stands: 0 none, 1 push to the place, 2 wait after a delivery. The engine
    runs the drop from the drop off action to its end, steering the craft,
    setting its throttle and refusing the maneuver timer and a target
    meanwhile (``engine_flies``); ``order_progress`` then counts the places
    delivered. Both come with wire version 18 and read (0, 0, 0) and 0
    before it.

    ``rendezvous_point`` is a Rendezvous craft's point: its group's mission
    point 12 as the mission file holds it, on or off (1997 flew there either
    way), given with the RENDEZVOUS_POINT book and (0, 0, 0)
    without it. On Rendezvous ``order_progress`` counts the boardings done
    to the craft, as on Await Boarding, without switching it off or
    restoring it. On Attack Escorts the order's targets, in
    ``order_target_mask``, ``order_first_pair_mask``, ``targets_now`` and
    TARGETS_LEFT, are the craft that escort a group the order names: a craft,
    a player's too, whose current order is Escort and whose
    flight escorts that group, leader and wingmen; TARGETS_LEFT
    also counts a named group still to arrive, 1997's later test. All of
    this comes with wire version 20; ``rendezvous_point`` reads (0, 0, 0)
    before it.
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
    countermeasure: int
    countermeasures_left: int
    countermeasure_working: int
    threat_count: int
    chaff_seconds: int
    countermeasure_cooldown: int
    threat_object: tuple[int, int, int, int]
    threat_type: tuple[int, int, int, int]
    threat_decoyed: tuple[int, int, int, int]
    threat_x: tuple[int, int, int, int]
    threat_y: tuple[int, int, int, int]
    threat_z: tuple[int, int, int, int]
    waypoint_mask: int
    waypoint_x: tuple[int, ...]
    waypoint_y: tuple[int, ...]
    waypoint_z: tuple[int, ...]
    commands: int
    command_target: int
    avoid_target: int
    place: tuple[int, int, int]
    lock_ticks: int
    lock_needed: int
    lock_state: int
    skill_level: int
    notebook: bytes
    maneuver_timer: int = 0
    escorted: int = NO_OBJECT
    station: tuple[int, int, int] = (0, 0, 0)
    start_point: tuple[int, int, int] = (0, 0, 0)
    targets_now: int = 0
    static_target_mask: int = 0
    board_point: tuple[int, int, int] = (0, 0, 0)
    order_progress: tuple[int, int, int, int] = (0, 0, 0, 0)
    boarded: tuple[int, ...] = ()
    board_phase: int = 0
    lifetime_timer: int = 0
    order_first_pair_mask: int = 0
    drop_point: tuple[int, int, int] = (0, 0, 0)
    drop_phase: int = 0
    rendezvous_point: tuple[int, int, int] = (0, 0, 0)

    @property
    def threats(self) -> tuple[Threat, ...]:
        """The homing shots aimed at the craft, in the engine's slot order."""
        return tuple(
            Threat(
                self.threat_object[n],
                self.threat_type[n],
                self.threat_x[n],
                self.threat_y[n],
                self.threat_z[n],
                bool(self.threat_decoyed[n]),
            )
            for n in range(min(self.threat_count, THREATS_MAX))
        )

    def waypoint_on(self, number: int) -> bool:
        """Whether waypoint number, mission point 4 + number, is on."""
        return bool(self.waypoint_mask >> number & 1)

    def waypoint(self, number: int) -> tuple[int, int, int]:
        """Return waypoint number's world position, (0, 0, 0) when it is
        off."""
        return (
            self.waypoint_x[number],
            self.waypoint_y[number],
            self.waypoint_z[number],
        )

    @property
    def countermeasure_ready(self) -> bool:
        """Whether the engine takes a countermeasure action from the craft:
        chaff with no chaff seconds left, or a flare with its cooldown run
        out, either working with one left."""
        if not self.countermeasure_working or not self.countermeasures_left:
            return False
        if self.countermeasure == Countermeasure.CHAFF:
            return self.chaff_seconds == 0
        return (
            self.countermeasure == Countermeasure.FLARE
            and self.countermeasure_cooldown == 0
        )


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
    as the craft's size. ``warheads_coming`` counts the homing warheads in flight
    at the craft, at most 255, and ``warheads_coming_damage`` sums their damage.
    ``flags`` holds ``ContactFlags`` bits. ``hit_by`` is the object whose
    shot last hit the craft, NO_OBJECT for none; it comes with wire version
    13 and reads NO_OBJECT before it. ``boarder`` is the craft that carries
    it, else the lowest-numbered craft on a boarding order whose target it
    is, else NO_OBJECT; it comes with wire version 16 and reads NO_OBJECT
    before it.

    A static, flagged STATIC, has no craft record: its ``team`` is its
    flight group's, DISABLED means an ion hit took its working systems, and
    its ``object_kind``, ``speed``, shields, hull, ``activity`` and
    warheads read 0, its ``target``, ``last_attacker`` and ``hit_by``
    NO_OBJECT. Its ``object_index`` is its slot in the object table.

    ``box_min`` and ``box_max`` are the smallest and largest corners of the
    box around the craft type's model, as (x, y, z) in the model's own
    axes (x to the side, y backward, z up), and ``roll`` its roll angle,
    65,536 to a full turn: the engine sends them from wire version 15, and
    they read (0, 0, 0) and 0 before it.

    ``weapon_inhibit_timer`` is the ticks left on the craft record's weapon
    fire inhibit timer, 0 when none runs and for a static.
    ``pulses_coming`` counts the homing magnetic and ion pulses among
    ``warheads_coming``, at most 255. Both come with wire version 19
    and read 0 before it.
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
    warheads_coming: int
    warheads_coming_damage: int
    hit_by: int = NO_OBJECT
    box_min: tuple[int, int, int] = (0, 0, 0)
    box_max: tuple[int, int, int] = (0, 0, 0)
    roll: int = 0
    boarder: int = NO_OBJECT
    weapon_inhibit_timer: int = 0
    pulses_coming: int = 0

    @property
    def player_flown(self) -> bool:
        """Whether a player flies the craft."""
        return bool(self.flags & ContactFlags.PLAYER)

    @property
    def disabled(self) -> bool:
        """Whether none of the craft's subsystems work."""
        return bool(self.flags & ContactFlags.DISABLED)

    @property
    def attacked(self) -> bool:
        """Whether a craft of some team has hit the craft."""
        return bool(self.flags & ContactFlags.ATTACKED)


@dataclasses.dataclass(frozen=True)
class Question:
    """The craft due to think at one reference step, in the engine's order,
    every contact the step shows, and the static objects that count as
    targets, at most STATICS_MAX, in slot order.

    ``mission_version`` is the version of the mission file that is flown:
    12 for the retail X-wing vs. TIE Fighter missions, 14 for most of
    Balance of Power's. It comes with wire version 19 and reads 0
    before it."""

    tick: int
    craft: tuple[CraftView, ...]
    contacts: tuple[Contact, ...] = ()
    statics: tuple[Contact, ...] = ()
    mission_version: int = 0


def engine_flies(view: CraftView) -> bool:
    """Whether the engine flies the craft: a dock or a drop runs.

    The engine alone steers it, sets its throttle and pushes it until the dock
    or drop ends; steer and throttle are read and not applied, and the
    maneuver timer and a target action are refused. A refused action loses the
    whole answer, notebook included, so a target sent mid-dock or mid-drop
    loses that think. The program sends no order while a craft docks (it gives
    a docking craft to its board behavior alone).
    """
    return view.board_phase != 0 or view.drop_phase != 0


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
    """A turret's weapon slot and the object it fires at, NO_OBJECT for none.

    ``ion`` has the turret fire ion shots, which disable a craft, instead of
    its usual ones; it comes with wire version 13.
    """

    slot: int
    target: int
    ion: bool = False

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
    both. ``countermeasure`` launches the craft's chaff or a flare.
    ``evaded`` says the craft took up its evade command.
    ``maneuver_timer`` sets the craft's maneuver timer, 0 to
    MANEUVER_TIMER_MAX ticks; it comes with wire version 13, and before it
    ``wire`` refuses to write it.
    ``self_destruct`` starts the craft's self destruct: its delay in units
    of 1,180 ticks, 1 to SELF_DESTRUCT_UNITS_MAX; the engine sets the
    craft's lifetime timer to it when none runs and keeps one that does. It
    comes with wire version 16.
    ``board`` is the object index of the craft the engine docks it with: the
    craft's target, while its view shows it at its board point on a boarding
    order and not docking, and the object is a contact (the answer's own
    target action, when it has one, names it); NO_OBJECT stops a dock in its
    align or transfer phase. Never with ``depart`` or ``dock``. It comes with
    wire version 17.
    ``drop_off`` is 1 to start the engine's drop of the group its Drop Off
    order names, while the view's books show AT_DROP_POINT and no drop or
    dock runs, or 0 to stop a running drop, keeping the places delivered.
    Never with ``depart``, ``dock`` or ``board``. It comes with wire version
    18.
    ``activity`` is never one of ENGINE_ACTIVITIES. What these name must
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
    countermeasure: bool = False
    evaded: bool = False
    maneuver_timer: int | None = None
    self_destruct: int | None = None
    board: int | None = None
    drop_off: int | None = None

    def __post_init__(self) -> None:
        if self.depart and self.dock:
            raise ValueError("depart and dock are both sent")
        if self.board is not None:
            _check_u16("board", self.board)
            if self.depart or self.dock:
                raise ValueError("board is sent with a depart or a dock")
        if self.drop_off is not None:
            if self.drop_off not in (0, 1):
                raise ValueError(f"drop_off is {self.drop_off}, not 0 or 1")
            if self.depart or self.dock or self.board is not None:
                raise ValueError("drop_off is sent with a depart, a dock or a board")
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
        if self.maneuver_timer is not None and not (
            0 <= self.maneuver_timer <= MANEUVER_TIMER_MAX
        ):
            raise ValueError(
                f"maneuver_timer {self.maneuver_timer!r} is outside 0 to "
                f"{MANEUVER_TIMER_MAX}"
            )
        if self.self_destruct is not None and not (
            1 <= self.self_destruct <= SELF_DESTRUCT_UNITS_MAX
        ):
            raise ValueError(
                f"self_destruct {self.self_destruct!r} is outside 1 to "
                f"{SELF_DESTRUCT_UNITS_MAX}"
            )


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
