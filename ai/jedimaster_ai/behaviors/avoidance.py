"""Avoidance: a craft steers clear of the bulky craft in its way.

Purpose:
    After a craft's order behavior decides where to steer, a freighter,
    starship or platform whose clearance that heading enters turns it
    aside: the craft takes the edge of the clearance, as seen from where it
    is, on the side its nose lies, EDGE_PAD past it, and keeps its pitch, so
    the turn never swings the nose across the craft it steers round. Inside
    a clearance the edge is a quarter turn off its nearest part, so the
    craft turns outward. The next think looks again. Ruling 24 names
    the bulky kinds; without this step craft flew into their own carrier,
    and the bulky craft of one group, flying to the group's same points,
    flew into each other.

    A freighter's clearance is a ball round its position
    (``tactics.clearance``). A starship's or platform's runs off its hull
    (ruling 94): its box grown by ``hull.KEEP`` and the craft's own reach,
    three quarters of its size, and the headings into it are the grown
    box's (``hull.seen``). A ball round such a ship's position turned every
    heading with any part toward its middle while the craft was inside it,
    up to some 250,000 units from a Super Star Destroyer; now a fighter
    heads toward the flank until it is within LOOK_AHEAD of that keep, and
    a starship keeps its own length off another's hull.

    Where that heading is not free, entering another clearance or reached
    by a turn that swings the nose across a bulky craft whose clearance the
    craft is inside (a fighter between two freighters whose clearances
    overlap), it searches: of the headings the nose reaches turning either
    way without swinging across such a craft, SEARCH_STEP apart, the one
    whose nearest pass (``Obstacle.passes``) cuts least into any clearance,
    then the nearest the heading asked, then the shortest turn. One past a
    half turn is flown as a quarter turn that way, so the engine turns the
    long way round. Without it, a fighter between two freighters broke off
    from one into the other, or turned back to its target across the other
    (AI5e-3, the escort network game's bombers).

    ``in_the_way`` is the test itself, for a behavior that weighs a heading
    before it steers (the attack's break), and ``clear_yaw`` the turn, for
    one that reads its throttle from the heading it will fly (a starship's
    patrol).

Invariants:
    - Only a decision that steers changes, and only its steer: a craft left
      drifting keeps drifting (ruling 9). The notebook never changes.
    - Every craft steers clear of bulky craft, a bulky craft of the others
      too; no craft steers clear of a small one.
    - The craft's target is its behavior's to handle while the heading
      asked enters the target's clearance (the attack aims at it); a heading
      that does not (a break) steers clear of the target too.
    - No turn avoidance asks swings the nose across a bulky craft whose
      clearance the craft is inside.
    - A bulky craft counts once the craft is within LOOK_AHEAD of its
      clearance; of several in the way, the nearest part decides.
    - In the way means within the clearance's edge angle in yaw and in
      pitch, apart: the box the attack's aim cone uses too. For a hull the
      yaw's and the pitch's edges differ (``hull.Seen``).
"""

from __future__ import annotations

import dataclasses
import logging
import math
import typing as t

from .. import geometry
from .. import hull
from .. import tactics
from ..world import Contact
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import Decision

logger = logging.getLogger(__name__)

# 8,192 units, about 200 m: a bulky craft farther from its clearance than
# this is not yet in the way.
LOOK_AHEAD = 0x2000
# A 64th of a turn past the clearance's edge.
EDGE_PAD = 0x400
# Within this many yaw units the nose points dead at a craft: the
# arctangent's own error (``geometry``), too small to name a side.
DEAD_ON = 4
# The headings the search weighs lie a 128th of a turn apart.
SEARCH_STEP = 0x200


@dataclasses.dataclass(frozen=True)
class Obstacle:
    """A bulky craft in a heading's way: how far its nearest part lies, its
    clearance from there, the yaw halfway across the headings that enter
    that clearance and how far they spread either side, the offset to its
    nearest part, and how far off a ball of radius clearance would lie to
    look as wide (``as_ball``: the distance itself for a freighter's ball,
    and for any craft whose clearance the craft is inside)."""

    contact: Contact
    distance: int
    clearance: int
    yaw: int
    edge: int
    offset: tuple[int, int, int]
    as_ball: int

    def past_edge(self, yaw: int, nose: int) -> int:
        """Return the yaw EDGE_PAD past the clearance's edge on the side the
        nose lies on, so the turn there never swings the nose across the
        craft; on the side yaw lies on when the nose points dead at it, and
        clockwise when yaw does too."""
        side = yaw if geometry.angle_between(nose, self.yaw) <= DEAD_ON else nose
        clockwise = (side - self.yaw) % geometry.TURN < geometry.HALF_TURN
        turn = self.edge + EDGE_PAD
        return (self.yaw + (turn if clockwise else -turn)) % geometry.TURN

    def passes(self, yaw: int) -> int:
        """Return how near a craft flying straight on along yaw passes this
        one, seen as its ball (``as_ball``), the angle between them EDGE_PAD
        less: that ball's distance when the angle is a quarter turn or more.
        A heading past the edge passes at the clearance or farther."""
        off = geometry.angle_between(yaw, self.yaw) - EDGE_PAD
        if off <= 0:
            return 0
        if off >= geometry.QUARTER_TURN:
            return self.as_ball
        return self.as_ball * geometry.sine(off) >> 16


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
    if not _aims_at(view, question, steer, target):
        target = NO_OBJECT
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
    nearest bulky craft in its way, the contact skip aside, or the yaw the
    search finds when that turn is not free, logging the turn; yaw itself
    when none is in the way and the turn there is free."""
    near = nearby(view, question, pitch, skip)
    obstacle = _nearest(o for o in near if geometry.angle_between(yaw, o.yaw) < o.edge)
    clear = yaw if obstacle is None else obstacle.past_edge(yaw, view.yaw)
    searched = not _free(view.yaw, clear, near)
    if searched:
        clear = _search(view.yaw, yaw, near)
        obstacle = _nearest(near)
    if obstacle is None:
        return yaw
    logger.debug(
        "avoidance.steer object=%d around=%d distance=%d clearance=%d yaw=%d "
        "searched=%d",
        view.object_index,
        obstacle.contact.object_index,
        obstacle.distance,
        obstacle.clearance,
        clear,
        int(searched),
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
    return _nearest(
        obstacle
        for obstacle in nearby(view, question, pitch, skip)
        if geometry.angle_between(yaw, obstacle.yaw) < obstacle.edge
    )


def nearby(
    view: CraftView, question: Question, pitch: int, skip: int
) -> list[Obstacle]:
    """Return every bulky craft within LOOK_AHEAD of its clearance whose
    clearance takes in pitch, the craft itself and the contact skip aside:
    a freighter's ball (``_ball``), a starship's or platform's hull
    (``_hull``)."""
    own = tactics.own_contact(view, question)
    reach = 0 if own is None else own.size * 3 // 4
    found = []
    for contact in question.contacts:
        if contact.object_index in (view.object_index, skip):
            continue
        if not tactics.is_bulky(contact):
            continue
        if hull.by_hull(contact):
            obstacle = _hull(view, contact, hull.KEEP + reach, pitch)
        else:
            obstacle = _ball(view, contact, pitch)
        if obstacle is not None:
            found.append(obstacle)
    return found


def _ball(view: CraftView, contact: Contact, pitch: int) -> Obstacle | None:
    """Return a freighter as an obstacle, its clearance a ball round its
    position (``tactics.clearance``), or None when it lies farther than
    LOOK_AHEAD from the ball or the ball's edge angle leaves out pitch."""
    offset = tactics.offset(view, contact)
    reach = geometry.distance(*offset)
    keep = tactics.clearance(contact)
    if reach >= keep + LOOK_AHEAD:
        return None
    toward, up = geometry.bearing(*offset)
    edge = edge_angle(reach, keep)
    if geometry.angle_between(pitch, up) >= edge:
        return None
    return Obstacle(contact, reach, keep, toward, edge, offset, reach)


def _hull(view: CraftView, contact: Contact, keep: int, pitch: int) -> Obstacle | None:
    """Return a starship or platform as an obstacle, its clearance keep off
    its hull (``hull.seen``), or None when its hull lies farther than
    LOOK_AHEAD from that clearance or the headings into it leave out pitch.

    keep is ``hull.KEEP`` and the craft's own reach, three quarters of its
    size, so a starship keeps its own length off another's hull too.
    """
    near = hull.nearest(view, contact)
    if near.distance >= keep + LOOK_AHEAD:
        return None
    looks = hull.seen(view, contact, keep)
    if geometry.angle_between(pitch, looks.pitch) >= looks.pitch_edge:
        return None
    if near.distance < keep:
        as_ball = near.distance
    else:
        # Rounded up: a heading EDGE_PAD past the edge passes at keep, not short.
        as_ball = -(-(keep << 16) // max(geometry.sine(looks.yaw_edge), 1))
    offset = (near.dx, near.dy, near.dz)
    return Obstacle(
        contact, near.distance, keep, looks.yaw, looks.yaw_edge, offset, as_ball
    )


def _nearest(obstacles: t.Iterable[Obstacle]) -> Obstacle | None:
    """The nearest of obstacles, the lower object index of two as near; None
    when there are none."""
    return min(
        obstacles,
        key=lambda obstacle: (obstacle.distance, obstacle.contact.object_index),
        default=None,
    )


def _aims_at(view: CraftView, question: Question, steer: Steer, target: int) -> bool:
    """Whether a steer heads into the clearance of the craft's target, true
    when the target is not a bulky craft near enough to weigh."""
    for obstacle in nearby(view, question, steer.pitch, NO_OBJECT):
        if obstacle.contact.object_index == target:
            return geometry.angle_between(steer.yaw, obstacle.yaw) < obstacle.edge
    return True


def _free(nose: int, heading: int, near: list[Obstacle]) -> bool:
    """Whether a heading enters the clearance of none of near, and the turn
    from the nose to it the shortest way swings the nose across none whose
    clearance the craft is inside."""
    if any(geometry.angle_between(heading, o.yaw) < o.edge for o in near):
        return False
    return not any(
        o.distance < o.clearance and geometry.sweeps(nose, heading, o.yaw) for o in near
    )


def _search(nose: int, yaw: int, near: list[Obstacle]) -> int:
    """Return the yaw to steer when the way to yaw is not free: of the
    headings the nose reaches turning either way before it points at a bulky
    craft whose clearance the craft is inside, the one whose nearest pass
    cuts least into a clearance of near, then the nearest yaw, then the
    shortest turn, the way toward yaw first; one past a half turn as a
    quarter turn that way."""
    walls = [o.yaw for o in near if o.distance < o.clearance]
    toward = 1 if geometry.signed(yaw - nose) >= 0 else -1
    best = None
    for way in (toward, -toward):
        limit = min(
            ((wall - nose) * way % geometry.TURN for wall in walls if wall != nose),
            default=geometry.TURN,
        )
        for span in range(0, limit, SEARCH_STEP):
            heading = (nose + way * span) % geometry.TURN
            cut = max(max(o.clearance - o.passes(heading) for o in near), 0)
            key = (cut, geometry.angle_between(heading, yaw), span)
            if best is None or key < best[0]:
                best = key, heading, way, span
    _, heading, way, span = best
    if span >= geometry.HALF_TURN:
        return (nose + way * geometry.QUARTER_TURN) % geometry.TURN
    return heading


def edge_angle(reach: int, keep: int) -> int:
    """Return how far off the line to a craft's position its clearance's edge
    lies, seen from reach away; a quarter turn from inside it."""
    if reach <= keep:
        return geometry.QUARTER_TURN
    return geometry.arctangent(keep, math.isqrt(reach * reach - keep * keep))
