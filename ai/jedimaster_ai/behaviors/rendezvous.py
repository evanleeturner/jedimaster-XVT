"""Rendezvous (order 4): fly to the rendezvous point and wait there to be
boarded.

Purpose:
    1997's plans are the blueprint: rendezvous1pln, the leader's, cruised to
    its group's mission point 12; within NEAR of it (NEARTARGETORDR,
    ``old:paiorder.c:1994-2001``) it switched for good to rendezvous2pln,
    which stopped the craft (AWAITBOARDMANR) and counted the boardings done
    to it; the order was done once they reached the order's variable1, and
    only from rendezvous2pln (``old:pai.c:1084-1094``). A follower, on
    rendezvousflw1pln, flew in formation and reached rendezvous2pln the same
    way, or flew rendezvous1pln itself once its leader was gone.

    Here the engine names the point (RENDEZVOUS_POINT,
    ``CraftView.rendezvous_point``: point 12 as the mission file holds it,
    on or off) and counts the boardings in ``order_progress``,
    nothing more. The craft:

    - flies to the point: steers at it at the order's cruise throttle
      (``patrol.cruise_throttle``, 1997's cruise), eased while it leads a
      follower (``formation.eased``); a starship STARSHIP_TURN or more off its
      course has throttle 0, as on its waypoints; its line "Flying to
      rendezvous point" (FLYING_TO_RENDEZVOUS)
    - a follower flies to its place beside its leader (``formation.fly``)
      under the same line while the question shows the leader on the order,
      else to the point itself
    - within NEAR of the point it waits, for good on the order slot:
      throttle 0, no steer, its line "Waiting for boarding craft"
      (WAITING_FOR_BOARDING_CRAFT). The line tells a boarder the craft may
      be boarded (``board.may_board``), and tells this behavior, while
      attack's notebook stands in for a fight, that the craft waits

    Done (``done``, ``progress``'s goal test): it waits and the boardings
    counted for its slot have reached variable1, so a variable1 of 0 is done
    on arrival, as 1997's. Its books send it home by its departure and
    abort, the two the engine keeps for Rendezvous.

    It defends itself as a craft on Wait does
    (``waiting``): it fights an enemy after it while it may fight back
    (``attack.fights_back``), else weaves while an enemy sits on its tail,
    under its line; then it flies on, or stops again where it is. 1997's
    rendezvous1pln answered hits too (AVOIDHITORDR).

    ``ended`` gives the line back, NONE, once another behavior flies the
    craft and names none.

Phases, the notebook's phase byte, 0 before the behavior first writes it
for the order slot:

    FLYING     to the point
    FOLLOWING  a follower to its place
    WAITING    near the point, stopped
    NO_POINT   no point named: held still, one WARN line

The state, SLOT.size bytes at the start of the scratch: u8 the order slot
the phase belongs to; a phase written for another slot reads as 0.

Invariants:
    - Once it waits on an order slot it never flies to the point again on
      that slot.
    - Its cannons hold unless it fights back.
    - Pure: the decision depends only on the view and the question.
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
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import attack
from . import BehaviorCode
from . import Decision
from . import formation
from . import patrol

logger = logging.getLogger(__name__)

CODE = BehaviorCode.RENDEZVOUS
# The Rendezvous order's code (``orders.OrderCode.RENDEZVOUS``). orders.py
# imports this module, so the number is written here;
# tests/test_rendezvous.py holds it to OrderCode.
RENDEZVOUS = 4

FLYING = 1
FOLLOWING = 2
WAITING = 3
NO_POINT = 4

# Within this many world units of the point the craft waits, 1997's
# NEARTARGETORDR (``old:paiorder.c:1994-2001``); the distance itself is out.
NEAR = 0x4000

# The two status lines, 1997's for its plans.
FLYING_LINE = Activity.FLYING_TO_RENDEZVOUS
WAITING_LINE = Activity.WAITING_FOR_BOARDING_CRAFT
LINES = frozenset({FLYING_LINE, WAITING_LINE})

SLOT = struct.Struct("<B")
_HOLD = (HOLD_FIRE, HOLD_FIRE)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: on its way to the point, waiting there,
    or defending itself on either."""
    book = notebook.open_for(view, CODE, question.tick)
    (slot,) = SLOT.unpack_from(book.scratch)
    was = book.phase if slot == view.order_slot else 0
    phase, actions = _decide(view, question, was)
    chaser = attack.pursuer(view, question)
    fights = chaser is not None and attack.fights_back(view, question, chaser)
    chased = attack.tail_threat(view, question) is not None
    if fights or chased or attack.weaving(view):
        fight = attack.fight(view, question, [chaser] if fights else [])
        if fight.actions.steer is None:
            return Decision(fight.notebook, actions)
        line = WAITING_LINE if phase == WAITING else FLYING_LINE
        return dataclasses.replace(
            fight, actions=dataclasses.replace(fight.actions, activity=line)
        )
    if phase != was:
        book = book.in_phase(phase, question.tick)
    scratch = SLOT.pack(view.order_slot) + book.scratch[SLOT.size :]
    return Decision(dataclasses.replace(book, scratch=scratch).encode(), actions)


def done(view: CraftView) -> bool:
    """Return whether the craft's Rendezvous is done (``progress.GOALS``): it
    waits at the point and the boardings counted for its order slot have
    reached the order's variable1."""
    return (
        _waits(view, _phase(view))
        and view.order_progress[view.order_slot] >= view.order_variable1
    )


def ended(view: CraftView, decision: Decision) -> Decision:
    """Return the decision of a craft another behavior now flies: its
    activity NONE when it still showed a Rendezvous line and that behavior
    names none."""
    if view.activity not in LINES or decision.actions.activity is not None:
        return decision
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)


def _decide(view: CraftView, question: Question, was: int) -> tuple[int, Actions]:
    """Return the phase the craft is in and its actions, with no enemy to
    answer."""
    if _waits(view, was):
        if view.activity != WAITING_LINE:
            logger.info(
                "rendezvous.waiting object=%d left=%d boarded=%d needed=%d",
                view.object_index,
                _left(view),
                view.order_progress[view.order_slot],
                view.order_variable1,
            )
        return WAITING, Actions(
            throttle=0, activity=WAITING_LINE, target=NO_OBJECT, lasers=_HOLD
        )
    if not view.books & Books.RENDEZVOUS_POINT:
        if was != NO_POINT:
            logger.warning(
                "rendezvous.no_point object=%d books=%d", view.object_index, view.books
            )
        return NO_POINT, Actions(throttle=0, target=NO_OBJECT, lasers=_HOLD)
    leader = formation.leader_for(view, question, formation.on_order)
    if leader is not None:
        actions = formation.fly(view, question, leader)
        return FOLLOWING, dataclasses.replace(
            actions, activity=FLYING_LINE, target=NO_OBJECT, lasers=_HOLD
        )
    return FLYING, _fly(view, question)


def _fly(view: CraftView, question: Question) -> Actions:
    """Return the steering and throttle to the point."""
    dx, dy, dz = _offset(view)
    yaw, pitch = geometry.bearing(dx, dy, dz)
    throttle = formation.eased(view, question, patrol.cruise_throttle(view))
    own = tactics.own_contact(view, question)
    if (
        own is not None
        and own.genus == tactics.GENUS_STARSHIP
        and geometry.angle_between(view.yaw, yaw) >= patrol.STARSHIP_TURN
    ):
        throttle = 0
    logger.debug(
        "rendezvous.fly object=%d left=%d throttle=%d",
        view.object_index,
        geometry.distance(dx, dy, dz),
        throttle,
    )
    return Actions(
        steer=Steer(yaw, pitch, view.roll),
        throttle=throttle,
        activity=FLYING_LINE,
        target=NO_OBJECT,
        lasers=_HOLD,
    )


def _waits(view: CraftView, phase: int) -> bool:
    """Return whether the craft waits at the point: it waited already on
    this order slot (its phase, or its line while attack's notebook stands
    in), or it is now within NEAR of a point the engine names."""
    return (
        phase == WAITING
        or view.activity == WAITING_LINE
        or (bool(view.books & Books.RENDEZVOUS_POINT) and _left(view) < NEAR)
    )


def _phase(view: CraftView) -> int:
    """Return the phase the craft's own notebook holds for its order slot,
    0 for none."""
    try:
        book = notebook.decode(view.notebook)
    except notebook.NotebookError:
        return 0
    if (
        book is None
        or book.behavior != CODE
        or SLOT.unpack_from(book.scratch)[0] != view.order_slot
    ):
        return 0
    return book.phase


def _offset(view: CraftView) -> tuple[int, int, int]:
    px, py, pz = view.rendezvous_point
    return px - view.x, py - view.y, pz - view.z


def _left(view: CraftView) -> int:
    return geometry.distance(*_offset(view))
