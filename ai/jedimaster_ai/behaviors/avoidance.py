"""Avoidance: a craft steers clear of the bulky craft in its way.

Purpose:
    After a craft's order behavior decides where to steer, a freighter,
    starship or platform whose clearance (``tactics.clearance``) that
    heading enters turns it aside: the craft takes the nearer edge of the
    clearance as seen from where it is, EDGE_PAD past it, and keeps its
    pitch. Inside a clearance the edge is a quarter turn off the centre, so
    the craft turns outward. The next think looks again. Ruling 24 names
    the bulky kinds; without this step craft flew into their own carrier,
    and the bulky craft of one group, flying to the group's same points,
    flew into each other.

    ``in_the_way`` is the test itself, for a behavior that weighs a heading
    before it steers (the attack's break), and ``clear_yaw`` the turn, for
    one that reads its throttle from the heading it will fly (a starship's
    patrol).

Invariants:
    - Only a decision that steers changes, and only its steer: a craft left
      drifting keeps drifting (ruling 9). The notebook never changes.
    - Every craft steers clear of bulky craft, a bulky craft of the others
      too; no craft steers clear of a small one.
    - The craft's target is its behavior's to handle: the attack breaks off
      at the target's own clearance.
    - A bulky craft counts once the craft is within LOOK_AHEAD of its
      clearance; of several in the way, the nearest decides.
    - In the way means within the clearance's edge angle in yaw and in
      pitch, apart: the box the attack's aim cone uses too.
"""

from __future__ import annotations

import dataclasses
import logging
import math

from .. import geometry
from .. import tactics
from ..world import Contact
from ..world import CraftView
from ..world import Question
from ..world import Steer
from . import Decision

logger = logging.getLogger(__name__)

# 8,192 units, about 200 m: a bulky craft farther from its clearance than
# this is not yet in the way.
LOOK_AHEAD = 0x2000
# A 64th of a turn past the clearance's edge.
EDGE_PAD = 0x400


@dataclasses.dataclass(frozen=True)
class Obstacle:
    """A bulky craft in a heading's way: how far, its clearance, the yaw
    that points at it and its clearance's edge angle from there."""

    contact: Contact
    distance: int
    clearance: int
    yaw: int
    edge: int

    def past_edge(self, yaw: int) -> int:
        """Return the yaw EDGE_PAD past the clearance's edge on the side yaw
        already lies on, clockwise when dead on."""
        clockwise = (yaw - self.yaw) % geometry.TURN < geometry.HALF_TURN
        turn = self.edge + EDGE_PAD
        return (self.yaw + (turn if clockwise else -turn)) % geometry.TURN


def steer_clear(view: CraftView, question: Question, decision: Decision) -> Decision:
    """Return the decision with its steer turned clear of the nearest bulky
    craft in its way, or unchanged when none is."""
    steer = decision.actions.steer
    if steer is None:
        return decision
    own = tactics.own_contact(view, question)
    if own is None:
        return decision
    target = own.target if decision.actions.target is None else decision.actions.target
    yaw = clear_yaw(view, question, steer.yaw, steer.pitch, target)
    if yaw == steer.yaw:
        return decision
    actions = dataclasses.replace(
        decision.actions, steer=Steer(yaw, steer.pitch, steer.roll)
    )
    return dataclasses.replace(decision, actions=actions)


def clear_yaw(
    view: CraftView, question: Question, yaw: int, pitch: int, skip: int
) -> int:
    """Return the yaw that turns a heading of yaw and pitch clear of the
    nearest bulky craft in its way, the contact skip aside, logging the turn;
    yaw itself when none is in the way."""
    obstacle = in_the_way(view, question, yaw, pitch, skip)
    if obstacle is None:
        return yaw
    clear = obstacle.past_edge(yaw)
    logger.debug(
        "avoidance.steer object=%d around=%d distance=%d clearance=%d yaw=%d",
        view.object_index,
        obstacle.contact.object_index,
        obstacle.distance,
        obstacle.clearance,
        clear,
    )
    return clear


def in_the_way(
    view: CraftView,
    question: Question,
    yaw: int,
    pitch: int,
    skip: int,
) -> Obstacle | None:
    """Return the nearest bulky craft whose clearance a heading of yaw and
    pitch enters, the craft itself and the contact skip aside, or None."""
    nearest = None
    for contact in question.contacts:
        if contact.object_index in (view.object_index, skip):
            continue
        if not tactics.is_bulky(contact):
            continue
        dx, dy, dz = tactics.offset(view, contact)
        reach = geometry.distance(dx, dy, dz)
        keep = tactics.clearance(contact)
        if reach >= keep + LOOK_AHEAD:
            continue
        toward, up = geometry.bearing(dx, dy, dz)
        edge = edge_angle(reach, keep)
        if geometry.angle_between(yaw, toward) >= edge:
            continue
        if geometry.angle_between(pitch, up) >= edge:
            continue
        if nearest is None or (reach, contact.object_index) < (
            nearest.distance,
            nearest.contact.object_index,
        ):
            nearest = Obstacle(contact, reach, keep, toward, edge)
    return nearest


def edge_angle(reach: int, keep: int) -> int:
    """Return how far off the line to a craft's position its clearance's edge
    lies, seen from reach away; a quarter turn from inside it."""
    if reach <= keep:
        return geometry.QUARTER_TURN
    return geometry.arctangent(keep, math.isqrt(reach * reach - keep * keep))
