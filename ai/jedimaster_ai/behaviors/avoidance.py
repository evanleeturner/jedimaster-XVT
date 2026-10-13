"""Avoidance: a small craft steers clear of the bulky craft in its way.

Purpose:
    After a craft's order behavior decides where to steer, a freighter,
    starship or platform whose clearance (``tactics.clearance``) that
    heading enters turns it aside: the craft takes the nearer edge of the
    clearance as seen from where it is, EDGE_PAD past it, and keeps its
    pitch. Inside a clearance the edge is a quarter turn off the centre, so
    the craft turns outward. The next think looks again. Ruling 24 names
    the bulky kinds; without this step craft flew into their own carrier.

Invariants:
    - Only a decision that steers changes, and only its steer: a craft left
      drifting keeps drifting (ruling 9). The notebook never changes.
    - A freighter, starship or platform steers clear of nothing.
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


def steer_clear(view: CraftView, question: Question, decision: Decision) -> Decision:
    """Return the decision with its steer turned clear of the nearest bulky
    craft in its way, or unchanged when none is."""
    steer = decision.actions.steer
    if steer is None:
        return decision
    own = tactics.own_contact(view, question)
    if own is None or tactics.is_bulky(own):
        return decision
    target = own.target if decision.actions.target is None else decision.actions.target
    nearest = None
    for contact in question.contacts:
        if contact.object_index in (view.object_index, target):
            continue
        if not tactics.is_bulky(contact):
            continue
        dx, dy, dz = tactics.offset(view, contact)
        reach = geometry.distance(dx, dy, dz)
        keep = tactics.clearance(contact)
        if reach >= keep + LOOK_AHEAD:
            continue
        yaw, pitch = geometry.bearing(dx, dy, dz)
        edge = edge_angle(reach, keep)
        if geometry.angle_between(steer.yaw, yaw) >= edge:
            continue
        if geometry.angle_between(steer.pitch, pitch) >= edge:
            continue
        if nearest is None or (reach, contact.object_index) < nearest[0]:
            nearest = ((reach, contact.object_index), contact, keep, yaw, edge)
    if nearest is None:
        return decision
    (reach, _), contact, keep, yaw, edge = nearest
    # The side the behavior's heading already lies on, clockwise when dead on.
    clockwise = (steer.yaw - yaw) % geometry.TURN < geometry.HALF_TURN
    turn = edge + EDGE_PAD
    clear_yaw = (yaw + (turn if clockwise else -turn)) % geometry.TURN
    logger.debug(
        "avoidance.steer object=%d around=%d distance=%d clearance=%d yaw=%d",
        view.object_index,
        contact.object_index,
        reach,
        keep,
        clear_yaw,
    )
    actions = dataclasses.replace(
        decision.actions, steer=Steer(clear_yaw, steer.pitch, steer.roll)
    )
    return dataclasses.replace(decision, actions=actions)


def edge_angle(reach: int, keep: int) -> int:
    """Return how far off the line to a craft's position its clearance's edge
    lies, seen from reach away; a quarter turn from inside it."""
    if reach <= keep:
        return geometry.QUARTER_TURN
    return geometry.arctangent(keep, math.isqrt(reach * reach - keep * keep))
