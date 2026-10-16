"""Drop Off (order 18): fly to the drop point, and the engine brings the
order's flight group into the battle there.

Purpose:
    A carrier on Drop Off delivers a flight group: it flies to a point near
    where that group belongs, stops, and the group arrives from it one craft
    at a time. 1997's plans are the blueprint: dropoffldr1pln flew the
    approach (``paiorder_dropoffdestorder``), dropoffldr2pln ran the drop
    (``paiman_dropoffmaneuver``), and followers flew in formation. The drop
    is the engine's (ruling 133): the craft flies the approach and sends one
    action.

    The approach: while the craft leads or flies alone and the group named
    by the order's variable2 still has craft to deliver (DROP_POINT), the
    engine names the point (``CraftView.drop_point``: 1997's, slot 0 of the
    group, 932 units up). The craft steers at it at its order's throttle,
    full when the order gives 0; nearer than NEAR units at NEAR_THROTTLE and
    nearer than CLOSE at CLOSE_THROTTLE, 1997's settings, which replace the
    order's even when it is slower. At the point (AT_DROP_POINT: within
    2,048 units, none of the group arrived) it stops and sends ``drop_off``
    1.

    The drop: while it runs (``drop_phase`` not 0) the engine steers the
    craft, sets its throttle and refuses a target and the maneuver timer, so
    the craft sends none of them, and ``think`` routes it here whatever its
    order and books say until the drop ends. Sent home meanwhile (STOP_BOOKS:
    1997's STOPGOHOME and ABORTATK left both plans for flyhomepln), it stops
    the drop with ``drop_off`` 0 and goes home at its next think.

    Done: ALL_DROPPED, every place delivered, or nothing to deliver (ruling
    134: a group number outside the mission, the group already arrived, a
    group of none); ``progress`` marks the order done by ``done``. The
    craft sends nothing more for the order, and ends its status line.

    A follower flies to its place beside its leader (``formation.fly``)
    while the question shows the leader on its order; else it holds still.

    Its status line, from the approach to the drop's end: "Deploying craft"
    (DEPLOYING_CRAFT), 1997's for both plans; a follower's is left as it
    is. Its cannons never fire; its turrets defend it (``turrets.defend``),
    and avoidance steers its approach round other craft.

Phases, the notebook's phase byte, 0 before the behavior first writes it
for the order slot:

    APPROACH   steer at the drop point, the throttle by the distance left
    AT_POINT   at the drop point, stopped, asking for the drop
    DROPPING   the engine drops
    DONE       ALL_DROPPED
    FOLLOWING  a follower flies to its place
    NO_LEADER  a follower with no leader to follow holds still
    NO_POINT   no drop point and not done: held still, one WARN line

The state, SLOT.size bytes at the start of the scratch: u8 the order slot
the phase belongs to; a phase written for another slot reads as 0.

Invariants:
    - Its cannons never fire.
    - While the engine drops, it sends no steering, throttle, target or
      maneuver timer.
    - It sends ``drop_off`` 1 only with AT_DROP_POINT and no drop running,
      and 0 only while one runs.
    - Pure: the decision depends only on the view and the question. The
      engine applies none of an answer it refuses, the notebook included,
      so a phase's INFO line is written again only after a refusal.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import geometry
from .. import notebook
from .. import tactics
from ..world import Actions
from ..world import Activity
from ..world import Books
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_ACTIONS
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import BehaviorCode
from . import Decision
from . import formation

logger = logging.getLogger(__name__)

CODE = BehaviorCode.DROP_OFF
# The Drop Off order's code (``orders.OrderCode.DROP_OFF``). orders.py imports
# this module, so the number is written here; tests/test_dropoff.py holds it
# to OrderCode.
DROP_OFF = 18

APPROACH = 1
AT_POINT = 2
DROPPING = 3
DONE = 4
FOLLOWING = 5
NO_LEADER = 6
NO_POINT = 7

# The approach's throttle near the point, 1997's (``old:paiorder.c:653-657``):
# NEAR_THROTTLE nearer than NEAR units, CLOSE_THROTTLE nearer than CLOSE.
NEAR = 0x4000
CLOSE = 4096
NEAR_THROTTLE = 0xC000
CLOSE_THROTTLE = 0x6000

# The books that stop a running drop and send the craft home.
STOP_BOOKS = Books.ABORTED | Books.DEPARTURE_DUE

SLOT = struct.Struct("<B")
_HOLD = (HOLD_FIRE, HOLD_FIRE)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: the drop the engine runs, a follower's
    place, the order done, the ask for the drop, or the approach."""
    book = notebook.open_for(view, CODE, question.tick)
    (slot,) = SLOT.unpack_from(book.scratch)
    was = book.phase if slot == view.order_slot else 0
    phase, actions = _decide(view, question, was)
    if phase != was:
        book = book.in_phase(phase, question.tick)
    scratch = SLOT.pack(view.order_slot) + book.scratch[SLOT.size :]
    return Decision(dataclasses.replace(book, scratch=scratch).encode(), actions)


def done(view: CraftView) -> bool:
    """Return whether the craft's Drop Off is done (``progress.GOALS``):
    ALL_DROPPED."""
    return bool(view.books & Books.ALL_DROPPED)


def throttle_for(view: CraftView, left: int) -> int:
    """Return the approach's throttle for the distance left to the drop
    point: CLOSE_THROTTLE nearer than CLOSE, NEAR_THROTTLE nearer than NEAR,
    else the order's, full when it gives 0."""
    if left < CLOSE:
        return CLOSE_THROTTLE
    if left < NEAR:
        return NEAR_THROTTLE
    return view.order_throttle or tactics.FULL_THROTTLE


def _decide(view: CraftView, question: Question, was: int) -> tuple[int, Actions]:
    """Return the phase the craft is in and its actions."""
    if view.drop_phase:
        return DROPPING, _dropping(view, was != DROPPING)
    if view.leader != NO_OBJECT:
        return _follow(view, question, was)
    if view.books & Books.ALL_DROPPED:
        if was != DONE:
            logger.info(
                "dropoff.done object=%d group=%d delivered=%d",
                view.object_index,
                view.order_variable2,
                view.order_progress[view.order_slot],
            )
        ended = Activity.NONE if view.activity == Activity.DEPLOYING_CRAFT else None
        return DONE, Actions(activity=ended) if ended is not None else NO_ACTIONS
    if view.books & Books.AT_DROP_POINT:
        if was != AT_POINT:
            logger.info(
                "dropoff.at_point object=%d group=%d",
                view.object_index,
                view.order_variable2,
            )
        return AT_POINT, Actions(
            throttle=0,
            activity=_changed(view),
            target=NO_OBJECT,
            lasers=_HOLD,
            drop_off=1,
        )
    if view.books & Books.DROP_POINT:
        return APPROACH, _approach(view)
    if was != NO_POINT:
        logger.warning(
            "dropoff.no_point object=%d group=%d books=%d",
            view.object_index,
            view.order_variable2,
            view.books,
        )
    return NO_POINT, Actions(throttle=0, lasers=_HOLD)


def _approach(view: CraftView) -> Actions:
    """Return the steering and throttle to the drop point."""
    px, py, pz = view.drop_point
    dx, dy, dz = px - view.x, py - view.y, pz - view.z
    left = geometry.distance(dx, dy, dz)
    throttle = throttle_for(view, left)
    logger.debug(
        "dropoff.approach object=%d group=%d left=%d throttle=%d",
        view.object_index,
        view.order_variable2,
        left,
        throttle,
    )
    yaw, pitch = geometry.bearing(dx, dy, dz)
    return Actions(
        steer=Steer(yaw, pitch, view.roll),
        throttle=throttle,
        activity=_changed(view),
        target=NO_OBJECT,
        lasers=_HOLD,
    )


def _dropping(view: CraftView, entered: bool) -> Actions:
    """Return the actions while the engine drops: the stop when the craft is
    sent home, else only its status line."""
    delivered = view.order_progress[view.order_slot]
    if view.books & STOP_BOOKS:
        logger.info(
            "dropoff.stopped object=%d delivered=%d books=%d",
            view.object_index,
            delivered,
            view.books,
        )
        return Actions(lasers=_HOLD, drop_off=0)
    if entered:
        logger.info(
            "dropoff.dropping object=%d group=%d",
            view.object_index,
            view.order_variable2,
        )
    logger.debug(
        "dropoff.drop object=%d phase=%d delivered=%d",
        view.object_index,
        view.drop_phase,
        delivered,
    )
    return Actions(activity=_changed(view), lasers=_HOLD)


def _follow(view: CraftView, question: Question, was: int) -> tuple[int, Actions]:
    """Return a follower's phase and its flight to its place, or its hold
    with no leader to follow."""
    leader = formation.leader_for(view, question, formation.on_order)
    if leader is None:
        if was != NO_LEADER:
            logger.debug(
                "dropoff.no_leader object=%d leader=%d", view.object_index, view.leader
            )
        return NO_LEADER, Actions(throttle=0, lasers=_HOLD)
    actions = formation.fly(view, question, leader)
    return FOLLOWING, dataclasses.replace(actions, lasers=_HOLD)


def _changed(view: CraftView) -> Activity | None:
    """Return DEPLOYING_CRAFT to send, None when the craft shows it already."""
    return (
        None if view.activity == Activity.DEPLOYING_CRAFT else Activity.DEPLOYING_CRAFT
    )
