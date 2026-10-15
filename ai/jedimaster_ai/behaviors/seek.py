"""Seek: where a craft on a fighting order flies while it has nothing to fight.

Purpose:
    Attack, Disable, Protect, and Escort with no craft escorted, fly 1997's
    cruise maneuver while they find no target (``attack.fight`` with
    ``cruising``), as the 1997 plans of those orders did. Each think, until
    a target turns up:

    - with waypoint 0 on, the patrol's cruise round its group's waypoints
      (``patrol.advance``, ``patrol.cruise``); a follower flies its place
      beside a leader still on its order, as 1997's followers did
    - with no waypoint on, a follower flies its place the same way; any
      other craft makes for its group's start point (``CraftView.
      start_point``, given with the START_POINT book) at the order's
      throttle and circles it. 1997 aimed such a craft at the point its
      group arrived at, then, once there, at waypoint 0's stored position,
      on or not, and back after each fight; that second leg is dropped: a
      waypoint that is off names no place. A starship or freighter within
      reach of the start point stops there, and a starship STARSHIP_TURN or
      more off its course has throttle 0, as on the patrol's cruise
    - with no waypoint on and no start point given, it holds its course

    The route is the caller's to keep: it outlasts a fight, so the craft
    goes on to the waypoint it was making for, as 1997's waypoint index
    did; a route written on another order slot starts again from waypoint
    0. Waypoint 0 off counts as no waypoint, as the patrol reads it.

Invariants:
    - Pure: the decision depends only on the view, the question and the
      route.
    - It names no target and fires nothing.
"""

from __future__ import annotations

import logging

from .. import geometry
from .. import tactics
from ..world import Actions
from ..world import Books
from ..world import Contact
from ..world import CraftView
from ..world import Question
from ..world import Steer
from . import avoidance
from . import formation
from . import patrol

logger = logging.getLogger(__name__)


def fly(
    view: CraftView, question: Question, route: patrol.Route
) -> tuple[patrol.Route, Actions]:
    """Return the route and the craft's steering and throttle while it finds
    no target: round its waypoints, else to its group's start point, else
    on its course."""
    if route.slot != view.order_slot:
        route = patrol.Route(slot=view.order_slot)
    own = tactics.own_contact(view, question)
    if view.waypoint_on(0):
        route = patrol.advance(view, route, None if own is None else own.genus)
        return route, patrol.cruise(view, question, route, own)
    leader = formation.leader_for(view, question, formation.on_order)
    if leader is not None:
        return route, formation.fly(view, question, leader)
    if not view.books & Books.START_POINT:
        logger.debug("seek.no_point object=%d", view.object_index)
        return route, Actions()
    return route, _to_start(view, question, own)


def _to_start(view: CraftView, question: Question, own: Contact | None) -> Actions:
    """Return the steering and throttle toward the group's start point; a
    starship or freighter within reach of it stops."""
    genus = None if own is None else own.genus
    dx, dy, dz = (
        view.start_point[0] - view.x,
        view.start_point[1] - view.y,
        view.start_point[2] - view.z,
    )
    distance = geometry.distance(dx, dy, dz)
    reach = patrol.REACH_STARSHIP if genus == tactics.GENUS_STARSHIP else patrol.REACH
    if genus in patrol.BULKY_GENERA and distance < reach:
        actions = Actions(throttle=0)
    else:
        yaw, pitch = geometry.bearing(dx, dy, dz)
        if own is not None:
            yaw = avoidance.clear_yaw(view, question, yaw, pitch, own.target)
        throttle = formation.eased(view, question, patrol.cruise_throttle(view))
        if (
            genus == tactics.GENUS_STARSHIP
            and geometry.angle_between(view.yaw, yaw) >= patrol.STARSHIP_TURN
        ):
            throttle = 0
        actions = Actions(steer=Steer(yaw, pitch, view.roll), throttle=throttle)
    logger.debug(
        "seek.start_point object=%d distance=%d throttle=%d",
        view.object_index,
        distance,
        actions.throttle,
    )
    return actions
