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

    Over or under such a hull every yaw enters its clearance
    (``hull.ALL_ROUND``), so no turn clears a heading that reaches it there:
    the craft keeps its yaw and pitches EDGE_PAD past the clearance's pitch
    edge, up when the hull lies below and down when above
    (``Obstacle.past_pitch``), flying level across the deck until it is past
    the hull's edge; only the nearest such hull is cleared so, and the yaw
    then turns past the others in the way at that pitch, as above. Without
    it, a craft over a parked starship dived through its deck at a point
    under it: the yaws in the way were a quarter turn either side of the
    yaw straight down, which names no side, and the dive's lay outside them.

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

    A Drop Off approach whose drop point lies inside a bulky craft's
    clearance flies straight in while its line there keeps clear of that
    craft's body (issue #311): the box itself for a starship or platform,
    the ball of three quarters of its size for a freighter, grown by the
    craft's reach, or by the point's own distance off the body where that
    is less. Only a heading dead on the point (``DEAD_ON``) goes in, past
    only the craft whose clearance holds the point; another bulky craft in
    the way, a line that comes nearer, or a turn there that swings the nose
    across one it passes while inside its clearance, and the craft steers
    round them all as before. Without it, every heading at a point under a
    parked starship entered its clearance, and the carriers circled the
    ship to the end of the flight.

    ``in_the_way`` is the test itself, for a behavior that weighs a heading
    before it steers (the attack's break), and ``clear_heading`` the turn,
    for one that reads its throttle from the heading it will fly (a
    starship's patrol).

Invariants:
    - Only a decision that steers changes, and only its steer: a craft left
      drifting keeps drifting (ruling 9). The notebook never changes.
    - Every craft steers clear of bulky craft, a bulky craft of the others
      too; no craft steers clear of a small one.
    - A craft enters a bulky craft's clearance only on a Drop Off approach
      to a point inside it, on a straight line that comes no nearer the
      body than its reach, or than the point itself where that is nearer.
    - The craft's target is its behavior's to handle while the heading
      asked enters the target's clearance (the attack aims at it); a heading
      that does not (a break) steers clear of the target too.
    - No turn avoidance asks swings the nose across a bulky craft whose
      clearance the craft is inside.
    - A bulky craft counts once the craft is within LOOK_AHEAD of its
      clearance; of several in the way, the nearest part decides.
    - In the way means within the clearance's edge angle in yaw and in
      pitch, apart: the box the attack's aim cone uses too. For a hull the
      yaw's and the pitch's edges differ (``hull.Seen``); over or under it
      every yaw is in the way, and only the pitch clears it.
    - Avoidance changes a heading's pitch only over or under a hull; every
      other turn keeps the pitch.
"""

from __future__ import annotations

import dataclasses
import logging
import math
import typing as t

from .. import geometry
from .. import hull
from .. import tactics
from ..world import Books
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
# A Drop Off approach's straight line is weighed at this many steps along it.
SEGMENT_STEPS = 0x10000

Vector = hull.Vector


@dataclasses.dataclass(frozen=True)
class Obstacle:
    """A bulky craft in a heading's way: how far its nearest part lies, its
    clearance from there, the yaw halfway across the headings that enter
    that clearance and how far they spread either side, the pitch and its
    spread alike, the offset to its nearest part, and how far off a ball of
    radius clearance would lie to look as wide (``as_ball``: the distance
    itself for a freighter's ball, for any craft whose clearance the craft
    is inside, and for a hull it is over or under)."""

    contact: Contact
    distance: int
    clearance: int
    yaw: int
    edge: int
    pitch: int
    pitch_edge: int
    offset: tuple[int, int, int]
    as_ball: int

    @property
    def all_round(self) -> bool:
        """Whether every yaw enters the clearance: the craft is over or under
        a starship's or platform's hull (``hull.ALL_ROUND``)."""
        return self.edge >= hull.ALL_ROUND

    def past_pitch(self, pitch: int) -> int:
        """Return the pitch EDGE_PAD past the clearance's pitch edge on the
        level side: up when the clearance lies below the craft, down when
        above, and on pitch's side when it lies level, up for a level pitch;
        never past straight up or straight down."""
        if self.pitch == geometry.LEVEL:
            up = pitch <= geometry.LEVEL
        else:
            up = self.pitch > geometry.LEVEL
        turn = self.pitch_edge + EDGE_PAD
        past = self.pitch - turn if up else self.pitch + turn
        return min(max(past, 0), geometry.HALF_TURN)

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
    if _flies_in(view, question, steer, target, own.size * 3 // 4):
        return decision
    yaw, pitch = clear_heading(view, question, steer.yaw, steer.pitch, target)
    if (yaw, pitch) == (steer.yaw, steer.pitch):
        return decision
    actions = dataclasses.replace(decision.actions, steer=Steer(yaw, pitch, steer.roll))
    return dataclasses.replace(decision, actions=actions)


def clear_heading(
    view: CraftView, question: Question, yaw: int, pitch: int, skip: int
) -> tuple[int, int]:
    """Return the yaw and pitch that turn a heading clear of the bulky craft
    in its way, the contact skip aside, logging each turn: over or under the
    nearest hull in the way the pitch past its edge (``Obstacle.
    past_pitch``); then the yaw past the nearest other in the way at that
    pitch, or the yaw the search finds when that turn is not free; the
    heading itself when none is in the way and the turn there is free."""
    near = nearby(view, question, pitch, skip)
    over = _nearest(o for o in near if o.all_round)
    if over is not None:
        pitch = over.past_pitch(pitch)
        logger.debug(
            "avoidance.level object=%d around=%d distance=%d clearance=%d pitch=%d",
            view.object_index,
            over.contact.object_index,
            over.distance,
            over.clearance,
            pitch,
        )
        near = nearby(view, question, pitch, skip)
    near = [o for o in near if not o.all_round]
    obstacle = _nearest(o for o in near if geometry.angle_between(yaw, o.yaw) < o.edge)
    clear = yaw if obstacle is None else obstacle.past_edge(yaw, view.yaw)
    searched = not _free(view.yaw, clear, near)
    if searched:
        clear = _search(view.yaw, yaw, near)
        obstacle = _nearest(near)
    if obstacle is None:
        return yaw, pitch
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
    return clear, pitch


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
    return Obstacle(contact, reach, keep, toward, edge, up, edge, offset, reach)


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
    if near.distance < keep or looks.yaw_edge >= hull.ALL_ROUND:
        as_ball = near.distance
    else:
        # Rounded up: a heading EDGE_PAD past the edge passes at keep, not short.
        as_ball = -(-(keep << 16) // max(geometry.sine(looks.yaw_edge), 1))
    return Obstacle(
        contact,
        near.distance,
        keep,
        looks.yaw,
        looks.yaw_edge,
        looks.pitch,
        looks.pitch_edge,
        (near.dx, near.dy, near.dz),
        as_ball,
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


def _flies_in(
    view: CraftView, question: Question, steer: Steer, skip: int, reach: int
) -> bool:
    """Whether a Drop Off approach flies straight in to its drop point: it
    steers dead on the point, every bulky craft in its way but those it may
    pass (``_way_in``) leaves the heading free, and the turn there swings
    the nose across none of those whose clearance the craft is inside."""
    if not view.books & Books.DROP_POINT:
        return False
    px, py, pz = view.drop_point
    point = (px - view.x, py - view.y, pz - view.z)
    yaw, pitch = geometry.bearing(*point)
    if (
        geometry.angle_between(steer.yaw, yaw) > DEAD_ON
        or geometry.angle_between(steer.pitch, pitch) > DEAD_ON
    ):
        return False
    near = nearby(view, question, steer.pitch, skip)
    passed = [o for o in near if _way_in(view, o.contact, point, reach)]
    if not passed:
        return False
    others = [o for o in near if o not in passed]
    return _free(view.yaw, steer.yaw, others) and not any(
        o.distance < o.clearance and geometry.sweeps(view.yaw, steer.yaw, o.yaw)
        for o in passed
    )


@dataclasses.dataclass(frozen=True)
class _Body:
    """A bulky craft's body and the line to a drop point, in the body's own
    frame: a starship's or platform's box in its model axes (``hull``), a
    freighter's ball of radius round its position; the craft at start, the
    point at end, and how far off the body the clearance reaches."""

    contact: Contact
    start: Vector
    end: Vector
    radius: int
    keep: int

    @classmethod
    def of(cls, view: CraftView, contact: Contact, point: Vector, reach: int) -> _Body:
        """Return a contact's body and the line from the craft to a point
        offset from it; a hull keeps ``hull.KEEP`` and the craft's reach."""
        start = (view.x - contact.x, view.y - contact.y, view.z - contact.z)
        end = (start[0] + point[0], start[1] + point[1], start[2] + point[2])
        if hull.by_hull(contact):
            return cls(
                contact,
                hull.to_model(contact, start),
                hull.to_model(contact, end),
                0,
                hull.KEEP + reach,
            )
        radius = tactics.clearance(contact) - tactics.CLEARANCE_MARGIN
        return cls(contact, start, end, radius, tactics.CLEARANCE_MARGIN)

    def gap(self, at: Vector) -> Vector:
        """Return the offset from the body to a point, along which its
        distance off the body grows: from the box's nearest point, or from
        the ball's middle."""
        return hull.off_box(self.contact, at) if hull.by_hull(self.contact) else at

    def off(self, at: Vector) -> int:
        """Return how far a point lies off the body, 0 inside it."""
        return max(geometry.distance(*self.gap(at)) - self.radius, 0)

    def least(self) -> int:
        """Return how near the line comes to the body: the point's own
        distance off it when the line still closes on the body there, else
        the least a ternary search over SEGMENT_STEPS finds, the distance
        off a box or a ball being convex along a line."""
        way = tuple(e - s for e, s in zip(self.end, self.start, strict=True))
        if sum(g * w for g, w in zip(self.gap(self.end), way, strict=True)) <= 0:
            return self.off(self.end)
        low, high = 0, SEGMENT_STEPS
        while high - low > 2:
            first = low + (high - low) // 3
            second = high - (high - low) // 3
            if self._off_at(way, first) <= self._off_at(way, second):
                high = second
            else:
                low = first
        return min(self._off_at(way, step) for step in range(low, high + 1))

    def _off_at(self, way: tuple[int, ...], step: int) -> int:
        """Return how far off the body the line's point step lies."""
        at = tuple(
            s + w * step // SEGMENT_STEPS for s, w in zip(self.start, way, strict=True)
        )
        return self.off(at)


def _way_in(view: CraftView, contact: Contact, point: Vector, reach: int) -> bool:
    """Whether a craft may pass inside a bulky craft's clearance to a point
    offset from it: the clearance holds the point, the point lies off the
    body, and the straight line there comes no nearer the body than the
    craft's reach, or than the point's own distance off it where that is
    less. Logs the line for every craft whose clearance holds the point."""
    body = _Body.of(view, contact, point, reach)
    off = body.off(body.end)
    if off >= body.keep:
        return False
    least = body.least() if off else 0
    clear = off > 0 and least >= min(reach, off)
    logger.debug(
        "avoidance.way_in object=%d around=%d off=%d least=%d clear=%d",
        view.object_index,
        contact.object_index,
        off,
        least,
        int(clear),
    )
    return clear


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
