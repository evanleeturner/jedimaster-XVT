"""Arrival: a craft coming in out of hyperspace or out of a mothership's hangar.

Purpose:
    The engine starts a round that comes out of hyperspace after the flight's
    start with the activity ARRIVING_FROM_HYPERSPACE, and a round out of a
    hangar that no player's group owns with LEAVING_HANGAR.

    Out of hyperspace this behavior flies the craft in, as the 1997 AI did,
    then sets its activity to NONE: from its next think the behavior of its
    order flies it. The engine placed the round 8 times 65,535 units back
    from its point, facing it. The craft is marked arriving (the engine then
    flies it straight on and leaves its speed alone) at speed 3,600, which
    drops to 1,800 after 6 of the game's seconds and to 900 after 9. A
    flight's leader ends its jump after 11, on time alone. A follower ends
    its jump with its leader's: at its first think after its leader
    (``CraftView.leader``) is no longer arriving, or is not in the question;
    after 11 seconds at the latest. Then the craft is active again at speed
    250, or 0 when its order starts stopped. The steps and times are the
    1997 AI's, at 236 ticks a game second.

    Out of a hangar a craft in a flight forms up before its order: at its
    first think it ends the leaving mark with IN_FORMATION, the moment the
    engine gives its group the mission's formation, and for at most
    FORM_UP_TICKS a follower flies to its place beside its leader
    (``formation.fly``) while the leader holds the heading it launched on at
    ``formation.EASED_THROTTLE``. A follower's form-up ends at its first
    think after its leader no longer forms up or is not in the question; a
    leader's when no craft of its flight forms up any more; a craft's at once
    when an enemy is after it (``attack.pursuer``) or a homing warhead is
    coming at it, so its order defends it. The end sets the activity NONE
    and puts the craft's own interval back: from its next think its order
    flies it. A craft out of a hangar alone, or a follower without its
    place, flies its order from its first think, at its own throttle, as the
    craft of a player's group do: ``leave_hangar`` ends the leaving mark on
    the order's decision, and no speed or throttle is set.

Phases, the notebook's phase byte:

    START       not begun; the first think begins the jump
    JUMPING     out of hyperspace, slowing in steps
    FORMING_UP  out of a hangar, forming up on its flight

The scratch bytes, little-endian: u16 the think interval the craft had when
it began; forming up, then u16 the yaw and u16 the pitch it launched on; the
rest zero.

Invariants:
    - Times run from the tick the craft began, kept in the notebook.
    - It thinks every ARRIVAL_INTERVAL ticks while it jumps, and puts the
      craft's own interval back when it ends.
    - A forming craft thinks every ``formation.INTERVAL`` ticks
      (``INTERVAL_BULKY`` for a bulky craft), sooner when less time is left:
      its last think falls on FORM_UP_TICKS, never past it.
    - A craft never picks its leader: the engine names it, and names a new
      one when a leader is lost.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import notebook
from .. import tactics
from ..orders import OrderCode
from ..world import Actions
from ..world import Activity
from ..world import Arrival
from ..world import Contact
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import attack
from . import BehaviorCode
from . import Decision
from . import formation

logger = logging.getLogger(__name__)

CODE = BehaviorCode.ARRIVAL
START = 0
JUMPING = 1
FORMING_UP = 2

# The 1997 AI thought every 59 ticks while out of hyperspace.
ARRIVAL_INTERVAL = 59
GAME_SECOND = 236
# Speed out of hyperspace: 3,600 for the first 6 game seconds, 1,800 to the
# 9th, 900 to the 11th; then the craft has arrived.
JUMP_SPEED = 3600
SLOWER_SECOND = 6
SLOWER_SPEED = 1800
SLOWEST_SECOND = 9
SLOWEST_SPEED = 900
JUMP_TICKS = 11 * GAME_SECOND
ARRIVED_SPEED = 250
# The orders a craft starts stopped for, as the engine's
# mission_order_starts_stopped names them; out of hyperspace it stops too.
STOPPED_ORDERS = frozenset(
    {
        OrderCode.HOLD_STEADY,
        OrderCode.DISABLED,
        OrderCode.HOLD_STEADY_2,
        OrderCode.HOLD_STEADY_3,
        OrderCode.HOLD_STEADY_4,
        OrderCode.HOLD_STEADY_5,
        OrderCode.HOLD_STEADY_6,
        OrderCode.HOLD_STEADY_7,
        OrderCode.NULL,
    }
)
# A flight out of a hangar forms up for at most 5 game seconds; 1997's own
# out-of-hangar timer ran 10, 2,360 ticks.
FORM_UP_TICKS = 5 * GAME_SECOND
# Why a form-up ended, the formed_up line's why: its time ran out, its flight
# no longer forms up, or an enemy is after it or a homing warhead coming.
ON_TIME = 0
WITH_FLIGHT = 1
ATTACKED = 2
# The activities of a craft on its way out of a hangar.
LAUNCHING = frozenset({Activity.LEAVING_HANGAR, Activity.IN_FORMATION})

_SCRATCH = struct.Struct("<H")
_FORMING = struct.Struct("<HHH")


def is_arriving(view: CraftView) -> bool:
    """Whether the engine has the craft arriving from hyperspace, so this
    behavior flies it."""
    return view.activity == Activity.ARRIVING_FROM_HYPERSPACE


def is_leaving_hangar(view: CraftView) -> bool:
    """Whether the engine has the craft leaving a hangar: its order flies it,
    or ``form_up`` when ``forms_up`` holds."""
    return view.activity == Activity.LEAVING_HANGAR


def is_forming_up(view: CraftView) -> bool:
    """Whether the craft forms up on its flight out of a hangar."""
    return view.activity == Activity.IN_FORMATION


def forms_up(view: CraftView, question: Question) -> bool:
    """Whether ``form_up`` flies the craft at this think: it forms up, or it
    leaves a hangar in a flight that forms up with no enemy after it."""
    if is_forming_up(view):
        return True
    return (
        is_leaving_hangar(view)
        and _flight_forms(view, question)
        and not _attacked(view, question)
    )


def jump_speed(ticks: int) -> int:
    """Return the speed out of hyperspace ticks after the jump began."""
    seconds = ticks // GAME_SECOND
    if seconds < SLOWER_SECOND:
        return JUMP_SPEED
    if seconds < SLOWEST_SECOND:
        return SLOWER_SPEED
    return SLOWEST_SPEED


def leader_arriving(view: CraftView, question: Question) -> bool:
    """Whether the craft's leader is in the question and still arriving."""
    return any(
        contact.object_index == view.leader
        and contact.activity == Activity.ARRIVING_FROM_HYPERSPACE
        for contact in question.contacts
    )


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step in from hyperspace."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    if book.phase == START:
        return _begin(view, book, tick)
    (interval,) = _SCRATCH.unpack_from(book.scratch)
    ticks = tick - book.phase_tick
    led = view.leader == NO_OBJECT or leader_arriving(view, question)
    if ticks < JUMP_TICKS and led:
        arrival = Arrival(arriving=True, speed=jump_speed(ticks))
        return Decision(book.encode(), Actions(arrival=arrival))
    speed = 0 if view.order_code in STOPPED_ORDERS else ARRIVED_SPEED
    actions = Actions(
        activity=Activity.NONE,
        think_interval=interval,
        arrival=Arrival(arriving=False, speed=speed),
    )
    _log_finished(view, ticks, interval)
    return Decision(book.encode(), actions)


def leave_hangar(view: CraftView, decision: Decision) -> Decision:
    """Return the order's decision for a craft leaving a hangar that does not
    form up, with the leaving mark ended unless the order set an activity
    itself."""
    _log_finished(view, 0, view.think_interval)
    if decision.actions.activity is not None:
        return decision
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)


def form_up(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step forming up on its flight out of a hangar,
    or the end of its form-up."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    if book.phase != FORMING_UP:
        book = _begin_forming(view, book, tick)
    interval, yaw, pitch = _FORMING.unpack_from(book.scratch)
    ticks = tick - book.phase_tick
    why = _form_up_end(view, question, ticks)
    if why is not None:
        _log_formed_up(view, ticks, interval, why)
        actions = Actions(activity=Activity.NONE, think_interval=interval)
        return Decision(book.encode(), actions)
    actions = _forming(view, question, yaw, pitch, FORM_UP_TICKS - ticks)
    if is_leaving_hangar(view):
        actions = dataclasses.replace(actions, activity=Activity.IN_FORMATION)
    return Decision(book.encode(), actions)


def _begin_forming(
    view: CraftView, book: notebook.Notebook, tick: int
) -> notebook.Notebook:
    scratch = _FORMING.pack(view.think_interval, view.yaw, view.pitch)
    scratch += bytes(notebook.SCRATCH_BYTES - _FORMING.size)
    logger.info(
        "arrival.forming_up object=%d leader=%d ticks=%d interval=%d",
        view.object_index,
        view.leader,
        FORM_UP_TICKS,
        view.think_interval,
    )
    return notebook.Notebook(CODE, FORMING_UP, book.dice, tick, scratch)


def _form_up_end(view: CraftView, question: Question, ticks: int) -> int | None:
    """Return why the craft's form-up ends at this think, or None."""
    if ticks >= FORM_UP_TICKS:
        return ON_TIME
    if _attacked(view, question):
        return ATTACKED
    if not _flight_forms(view, question):
        return WITH_FLIGHT
    return None


def _forming(
    view: CraftView, question: Question, yaw: int, pitch: int, left: int
) -> Actions:
    """Return a forming craft's steering, throttle and think interval: a
    follower's to its place, a leader's on the heading it launched on; its
    next think comes no later than the left ticks."""
    leader = _leader(view, question)
    if leader is not None:
        actions = formation.fly(view, question, leader)
    else:
        own = tactics.own_contact(view, question)
        bulky = own is not None and tactics.is_bulky(own)
        interval = formation.INTERVAL_BULKY if bulky else formation.INTERVAL
        actions = Actions(
            steer=Steer(yaw, pitch, view.roll),
            throttle=formation.EASED_THROTTLE,
            think_interval=interval,
        )
        logger.debug(
            "arrival.leading object=%d yaw=%d pitch=%d throttle=%d left=%d",
            view.object_index,
            yaw,
            pitch,
            formation.EASED_THROTTLE,
            left,
        )
    interval = min(actions.think_interval, left)
    return dataclasses.replace(actions, think_interval=interval)


def _flight_forms(view: CraftView, question: Question) -> bool:
    """Whether the craft's flight forms up with it: a follower's leader is
    on its way out of a hangar, shown and with the follower's place given;
    a leader has a craft of its flight that no player flies on its way out."""
    if view.leader != NO_OBJECT:
        return _leader(view, question) is not None
    return any(
        contact.object_index != view.object_index
        and contact.flight_group == view.flight_group
        and contact.team == view.team
        and not contact.player_flown
        and contact.activity in LAUNCHING
        for contact in question.contacts
    )


def _leader(view: CraftView, question: Question) -> Contact | None:
    """Return a follower's leader while it is on its way out of a hangar."""
    return formation.leader_for(view, question, _launching)


def _launching(contact: Contact) -> bool:
    return contact.activity in LAUNCHING


def _attacked(view: CraftView, question: Question) -> bool:
    """Whether an enemy is after the craft or a homing warhead is coming at it."""
    return view.threat_count > 0 or attack.pursuer(view, question) is not None


def _log_formed_up(view: CraftView, ticks: int, interval: int, why: int) -> None:
    logger.info(
        "arrival.formed_up object=%d ticks=%d interval=%d why=%d",
        view.object_index,
        ticks,
        interval,
        why,
    )


def _begin(view: CraftView, book: notebook.Notebook, tick: int) -> Decision:
    scratch = _SCRATCH.pack(view.think_interval)
    scratch += bytes(notebook.SCRATCH_BYTES - _SCRATCH.size)
    actions = Actions(
        think_interval=ARRIVAL_INTERVAL,
        arrival=Arrival(arriving=True, speed=JUMP_SPEED),
    )
    logger.info(
        "arrival.started object=%d activity=%d ticks=%d",
        view.object_index,
        view.activity,
        JUMP_TICKS,
    )
    began = notebook.Notebook(CODE, JUMPING, book.dice, tick, scratch)
    return Decision(began.encode(), actions)


def _log_finished(view: CraftView, ticks: int, interval: int) -> None:
    logger.info(
        "arrival.finished object=%d activity=%d ticks=%d interval=%d leader=%d",
        view.object_index,
        view.activity,
        ticks,
        interval,
        view.leader,
    )
