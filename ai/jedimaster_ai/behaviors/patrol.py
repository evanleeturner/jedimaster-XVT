"""Patrol: Circle, Circle and Evade, SS Patrol Loop and SS Patrol and Attack fly
their group's waypoints round and round.

Purpose:
    The craft flies to its group's waypoints (``CraftView.waypoint``: mission
    points 4 to 11) in turn, from waypoint 0. A waypoint is reached within
    REACH world units, REACH_STARSHIP for a starship, by exact distance; the
    craft then makes for the next one, and when the next is off or past the
    last, back to waypoint 0, which ends a round. ``rounds`` reads the rounds
    for the order's goal (``progress.GOALS``): Circle, Circle and Evade and SS
    Patrol Loop are done once they reach the order's variable1, SS Patrol and
    Attack once no target is left. SS Protect flies the same cruise, its
    turrets on guard (``starship.protect``).

    The blueprint is 1997's cruise maneuver (``paiman_cruisemaneuver`` and
    ``paiman_advance_order_waypoint``, deleted with the 1997 AI), with bug
    #205 fixed: there, a craft with one waypoint counted a round at every think
    near it; here it counts one each time it comes back within reach. As in
    1997:

    - a starship or freighter stops for good on its one waypoint;
    - the throttle is the order's, and for an order at full throttle with a
      speed, the one that gives SPEED_SCALE times that speed (ruling 39),
      never more than full;
    - a starship STARSHIP_TURN or more off its course to the waypoint has
      throttle 0. The course is read past the bulky craft in its way
      (``avoidance.clear_yaw``), so a starship stopped to turn round another
      moves on once it faces the way round, and never waits on it for good.

    With no waypoint on, the craft stops where it is and counts one round, as
    a starship does on its one waypoint. Circle and Evade weaves while an
    enemy sits on its tail (``weave``), about its course to the waypoint, at
    full throttle, and goes back to its round once the weave ends (ruling
    29); Circle and the starship orders leave attackers to the turrets.

The state, ROUTE_BYTES at the start of the scratch: u8 the order slot it
counts for, u8 the waypoint it makes for, u8 the rounds (at most ROUNDS_MAX),
u8 HELD once stopped for good, or once within reach of its one waypoint (a
small craft, until it leaves reach); then the weave's ``weave.STATE_BYTES``.

Invariants:
    - A notebook written on another order slot starts the route again: no
      round flown for one order counts for the next.
    - The waypoint the craft makes for is always on, while waypoint 0 is.
    - Pure: the decision depends only on the view and the question.
    - Random choices come from the craft's dice in its notebook.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import geometry
from .. import notebook
from .. import tactics
from .. import weave
from ..world import Actions
from ..world import Contact
from ..world import CraftView
from ..world import Question
from ..world import Steer
from ..world import WAYPOINTS
from . import attack
from . import avoidance
from . import BehaviorCode
from . import Decision
from . import formation

logger = logging.getLogger(__name__)

CODE = BehaviorCode.PATROL
# The order that weaves (orders.OrderCode.CIRCLE_AND_EVADE; orders imports
# the behaviors).
CIRCLE_AND_EVADE = 3
FULL_THROTTLE = 0xFFFF
# The 1997 reach of a waypoint: 0x1000, and 0x2000 for a starship.
REACH = 0x1000
REACH_STARSHIP = 0x2000
# 1997 flew an order at full throttle with a speed at five times that speed
# (paiman_initmaneuver's commanded speed).
SPEED_SCALE = 5
# A starship this far or more off its course stops (1997: 0x1000 yaw units
# while turning).
STARSHIP_TURN = 0x1000
ROUNDS_MAX = 0xFF
HELD = 1
BULKY_GENERA = (tactics.GENUS_FREIGHTER, tactics.GENUS_STARSHIP)

_ROUTE = struct.Struct("<BBBB")
ROUTE_BYTES = _ROUTE.size


@dataclasses.dataclass(frozen=True)
class Route:
    """A patrol's state, kept at the start of its scratch."""

    slot: int = 0
    waypoint: int = 0
    rounds: int = 0
    held: int = 0

    @classmethod
    def read(cls, raw: bytes) -> Route:
        """Return the route ROUTE_BYTES of raw hold."""
        return cls(*_ROUTE.unpack_from(raw))

    def write(self) -> bytes:
        """Return the route as ROUTE_BYTES bytes."""
        return _ROUTE.pack(self.slot, self.waypoint, self.rounds, self.held)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step round its waypoints."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    route = Route.read(book.scratch)
    flown = weave.Weave.read(book.scratch[ROUTE_BYTES:])
    if route.slot != view.order_slot:
        route, flown = Route(slot=view.order_slot), weave.Weave()
    own = tactics.own_contact(view, question)
    genus = None if own is None else own.genus
    route = _advance(view, route, genus)
    actions = _cruise(view, question, route, own)
    state = book.dice
    if view.order_code == CIRCLE_AND_EVADE:
        flown, actions, state = _evade(view, question, flown, actions, state)
    scratch = route.write() + flown.write()
    scratch += bytes(notebook.SCRATCH_BYTES - len(scratch))
    book = dataclasses.replace(book, dice=state, scratch=scratch)
    return Decision(book.encode(), actions)


def rounds(view: CraftView) -> int:
    """Return the rounds the craft flew on its present order slot, from its
    notebook: 0 when its notebook is no patrol's on that slot."""
    try:
        book = notebook.decode(view.notebook)
    except notebook.NotebookError:
        return 0
    if book is None or book.behavior != CODE:
        return 0
    route = Route.read(book.scratch)
    return route.rounds if route.slot == view.order_slot else 0


def rounds_done(view: CraftView) -> bool:
    """Return whether the craft's rounds reached its order's variable1, the
    1997 goal of Circle, Circle and Evade and SS Patrol Loop."""
    return rounds(view) >= view.order_variable1


def cruise_throttle(view: CraftView) -> int:
    """Return the throttle the order flies at: its own, or at full throttle
    with a speed, the throttle that gives SPEED_SCALE times that speed."""
    if view.order_throttle == FULL_THROTTLE and view.order_speed:
        return tactics.throttle_for(view, view.order_speed * SPEED_SCALE)
    return view.order_throttle


def _advance(view: CraftView, route: Route, genus: int | None) -> Route:
    """Return the route after the craft reaches what it makes for, if it
    does at this think."""
    if not view.waypoint_on(0):
        if route.held:
            return route
        route = dataclasses.replace(route, rounds=_more(route.rounds), held=HELD)
        logger.debug("patrol.hold object=%d rounds=%d", view.object_index, route.rounds)
        return route
    if not view.waypoint_on(route.waypoint):
        route = dataclasses.replace(route, waypoint=0)
    point = view.waypoint(route.waypoint)
    reach = REACH_STARSHIP if genus == tactics.GENUS_STARSHIP else REACH
    within = geometry.distance(*_offset(view, point)) < reach
    following = _next(view, route.waypoint)
    if following == route.waypoint:
        return _lone(view, route, within, genus in BULKY_GENERA)
    if not within:
        return route
    if following == 0:
        route = dataclasses.replace(route, rounds=_more(route.rounds))
    _log_reached(view, route.waypoint, following, route.rounds)
    return dataclasses.replace(route, waypoint=following)


def _lone(view: CraftView, route: Route, within: bool, bulky: bool) -> Route:
    """Return the route of a craft with one waypoint: a round each time it
    comes within reach; a starship or freighter stays held once there."""
    if not within:
        if route.held and not bulky:
            return dataclasses.replace(route, held=0)
        return route
    if route.held:
        return route
    route = dataclasses.replace(route, rounds=_more(route.rounds), held=HELD)
    _log_reached(view, route.waypoint, route.waypoint, route.rounds)
    return route


def _cruise(
    view: CraftView, question: Question, route: Route, own: Contact | None
) -> Actions:
    """Return the craft's steering and throttle toward its waypoint, or
    throttle 0 when it is stopped for good; a follower's to its place in
    formation while its leader flies the order."""
    leader = formation.leader_for(view, question, formation.on_order)
    if leader is not None:
        return formation.fly(view, question, leader)
    genus = None if own is None else own.genus
    lone = _next(view, route.waypoint) == route.waypoint
    if not view.waypoint_on(0) or (lone and route.held and genus in BULKY_GENERA):
        return Actions(throttle=0)
    yaw, pitch = geometry.bearing(*_offset(view, view.waypoint(route.waypoint)))
    if own is not None:
        yaw = avoidance.clear_yaw(view, question, yaw, pitch, own.target)
    throttle = formation.eased(view, question, cruise_throttle(view))
    if (
        genus == tactics.GENUS_STARSHIP
        and geometry.angle_between(view.yaw, yaw) >= STARSHIP_TURN
    ):
        throttle = 0
    return Actions(steer=Steer(yaw, pitch, view.roll), throttle=throttle)


def _evade(
    view: CraftView,
    question: Question,
    flown: weave.Weave,
    actions: Actions,
    state: int,
) -> tuple[weave.Weave, Actions, int]:
    """Return the weave, actions and dice of a Circle and Evade craft: it
    weaves about its course while an enemy sits on its tail."""
    tick = question.tick
    threat = attack.tail_threat(view, question)
    move = None
    if flown.flying:
        flown, move, state = weave.fly(view, flown, threat is not None, state, tick)
    elif threat is not None and weave.may_start(flown, tick):
        course = None if actions.steer is None else actions.steer.yaw
        flown, move, state = weave.start(
            view, threat.object_index, flown, state, tick, course
        )
    if move is not None:
        actions = dataclasses.replace(
            actions,
            steer=move.steer,
            throttle=FULL_THROTTLE,
            countermeasure=move.flare,
        )
    return flown, actions, state


def _next(view: CraftView, waypoint: int) -> int:
    """Return the waypoint after this one: the next when it is on, else 0."""
    following = waypoint + 1
    if following < WAYPOINTS and view.waypoint_on(following):
        return following
    return 0


def _more(count: int) -> int:
    return min(count + 1, ROUNDS_MAX)


def _offset(view: CraftView, point: tuple[int, int, int]) -> tuple[int, int, int]:
    return point[0] - view.x, point[1] - view.y, point[2] - view.z


def _log_reached(view: CraftView, waypoint: int, following: int, count: int) -> None:
    logger.debug(
        "patrol.reached object=%d waypoint=%d next=%d rounds=%d",
        view.object_index,
        waypoint,
        following,
        count,
    )
