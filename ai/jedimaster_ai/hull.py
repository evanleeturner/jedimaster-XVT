"""The hull: how far a craft is from a big ship's hull, not from its position.

Purpose:
    A starship or platform is long: a Star Destroyer's hull reaches 46,909
    units from its position, a Super Star Destroyer's 173,850. Measured from
    the position, as the 1997 pilot measured every target, the firing range
    never reaches past such a ship's clearance (``tactics.clearance``), so
    no cannon fired at a Super Star Destroyer and the lowest skill tier
    never fired at a Star Destroyer (issue #302). A craft fights a starship
    or platform by its hull instead: the nearest point of the box around
    its model (rulings 97 and 98), for its firing range, its break and its
    pick of the nearest target (ruling 93). ``nearest`` gives that
    point for those targets, and the target's position for every other, as
    1997 did; ``keep`` is how near it a craft comes before it breaks off.
    ``seen`` is how the box, grown by a margin, lies from a craft: the
    headings that reach it, which avoidance steers clear of (ruling 94).
    From over or under the box every yaw reaches it (``ALL_ROUND``), so
    only the pitch can steer clear: a quarter turn either side of the
    nearest point's yaw, which straight down or up names no side, left a
    dive at the deck free from three quarters of the yaws. ``off_box`` is how a point lies off the box, which avoidance measures a
    Drop Off approach's straight line by (issue #311).

The box comes from the engine: its smallest and largest corners in the
model's own axes (``Contact.box_min``, ``Contact.box_max``) and the craft's
roll. A model point turns into the world as the engine's
``flight_geometry_local_to_world`` turns it: x along the craft's side, z
along its up, y against its forward. Yaw and pitch give the side, up and
forward (``fview_calcrotatemove``); roll then turns the side and up about
the forward (``fview_calcrotateorient``).

Invariants:
    - Integers only, with ``geometry``'s sine, so every machine and every
      Python gets the same numbers; the axes are in 65,536ths.
    - A craft inside the box is 0 from the hull, and the offset then points
      at the position, so a break turns away from the middle of the ship.
    - A box from (0, 0, 0) to (0, 0, 0) is the position alone.
"""

from __future__ import annotations

import dataclasses

from . import geometry
from . import tactics
from .world import Contact
from .world import CraftView

# How near a starship's or platform's hull a craft comes before it breaks
# off (ruling 100): the margin a fighter target's clearance already keeps
# past its position (``tactics.CLEARANCE_MARGIN``).
KEEP = 0x1000
# A yaw spread that takes in every yaw: no two yaws lie more than a half turn
# apart (``geometry.angle_between``).
ALL_ROUND = geometry.HALF_TURN + 1
# The axes' unit, as ``geometry.sine`` gives it.
_UNIT_BITS = 16

Vector = tuple[int, int, int]


@dataclasses.dataclass(frozen=True)
class Nearest:
    """The nearest part of a target to a craft: how far it is, and the
    offset from the craft to it."""

    distance: int
    dx: int
    dy: int
    dz: int


def by_hull(contact: Contact) -> bool:
    """Return whether a craft measures a contact from its hull: a starship
    or a platform, the targets 1997 gave its large-target range bonus
    (ruling 98)."""
    return tactics.is_large(contact)


def keep(contact: Contact) -> int:
    """Return how near a contact's nearest part (``nearest``) a craft comes
    before it breaks off: KEEP off a starship's or platform's hull, else the
    contact's clearance from its position."""
    return KEEP if by_hull(contact) else tactics.clearance(contact)


def nearest(view: CraftView, contact: Contact) -> Nearest:
    """Return the nearest part of a contact to the craft: the nearest point
    of its box for a starship or platform (``by_hull``), else its
    position."""
    offset = tactics.offset(view, contact)
    if not by_hull(contact):
        return Nearest(geometry.distance(*offset), *offset)
    craft = to_model(contact, (-offset[0], -offset[1], -offset[2]))
    point = _clamped(craft, contact.box_min, contact.box_max)
    gap = (point[0] - craft[0], point[1] - craft[1], point[2] - craft[2])
    distance = geometry.distance(*gap)
    if distance == 0:
        return Nearest(0, *offset)
    return Nearest(distance, *to_world(contact, gap))


@dataclasses.dataclass(frozen=True)
class Seen:
    """How a box lies from a craft outside it: the yaw and the pitch halfway
    across the headings that reach it, and how far those headings spread
    either side of each, a quarter turn at most, or ALL_ROUND for the yaw
    from over or under the box."""

    yaw: int
    yaw_edge: int
    pitch: int
    pitch_edge: int


def seen(view: CraftView, contact: Contact, grow: int) -> Seen:
    """Return how a contact's box, grown by ``grow`` on every side, lies from
    the craft; a quarter turn either side of its nearest point when the
    craft is inside the grown box, every yaw when it is over or under the
    box itself there.

    The yaws spread as far as the grown box's eight corners: seen from
    outside it, no point of a box lies farther round than a corner. From
    over or under the box they go all the way round, and the spread is
    ALL_ROUND. The pitches spread as far as the corners and the grown box's
    nearest point, which stands for the steepest part of the face nearest
    the craft. Inside the grown box the craft is over or under the box when
    the box's own corners' yaws go all the way round; inside the box itself
    (a hangar launch) its nearest part is the position, and a quarter turn
    either side of it stands.
    """
    offset = tactics.offset(view, contact)
    turned = axes(contact.yaw, contact.pitch, contact.roll)
    low = tuple(at - grow for at in contact.box_min)
    high = tuple(at + grow for at in contact.box_max)
    craft = _model(turned, (-offset[0], -offset[1], -offset[2]))
    near = _clamped(craft, low, high)
    if near == craft:
        inside = nearest(view, contact)
        yaw, pitch = geometry.bearing(inside.dx, inside.dy, inside.dz)
        yaw_edge = geometry.QUARTER_TURN
        if inside.distance:
            box = _corners(contact.box_min, contact.box_max)
            yaws = [yaw for yaw, _ in _bearings(offset, turned, box)]
            if _spread(yaws, yaw)[1] == ALL_ROUND:
                yaw_edge = ALL_ROUND
        return Seen(yaw, yaw_edge, pitch, geometry.QUARTER_TURN)
    bearings = _bearings(offset, turned, [near, *_corners(low, high)])
    yaw, yaw_edge = _spread([yaw for yaw, _ in bearings], bearings[0][0])
    pitches = [pitch for _, pitch in bearings]
    pitch = (max(pitches) + min(pitches)) // 2
    return Seen(yaw, yaw_edge, pitch, (max(pitches) - min(pitches) + 1) // 2)


def _corners(low: Vector, high: Vector) -> list[Vector]:
    """Return the eight corners of a box from low to high."""
    return [
        (x, y, z)
        for x in (low[0], high[0])
        for y in (low[1], high[1])
        for z in (low[2], high[2])
    ]


def _bearings(
    offset: Vector, turned: tuple[Vector, Vector, Vector], points: list[Vector]
) -> list[tuple[int, int]]:
    """Return the yaw and pitch from a craft to each point in a contact's
    model axes, the contact's side, up and forward ``turned`` and its
    position ``offset`` from the craft."""
    return [geometry.bearing(*_added(offset, _world(turned, at))) for at in points]


def _spread(yaws: list[int], toward: int) -> tuple[int, int]:
    """Return the yaw halfway across yaws and how far they spread either side
    of it, counted round from toward, a yaw among theirs (the nearest
    point's); toward and ALL_ROUND when they spread a half turn or more, all
    the way round the craft."""
    turns = [geometry.signed(yaw - toward) for yaw in yaws]
    if max(turns) - min(turns) >= geometry.HALF_TURN:
        return toward, ALL_ROUND
    middle = (toward + (max(turns) + min(turns)) // 2) % geometry.TURN
    return middle, (max(turns) - min(turns) + 1) // 2


def axes(yaw: int, pitch: int, roll: int) -> tuple[Vector, Vector, Vector]:
    """Return a craft's side, up and forward, each a unit vector in
    65,536ths, as the engine builds them from its yaw, pitch and roll."""
    sin_yaw = geometry.sine(yaw)
    cos_yaw = geometry.sine(yaw + geometry.QUARTER_TURN)
    sin_pitch = geometry.sine(pitch)
    cos_pitch = geometry.sine(pitch + geometry.QUARTER_TURN)
    side = (cos_yaw, -sin_yaw, 0)
    up = (
        -sin_yaw * cos_pitch >> _UNIT_BITS,
        -cos_yaw * cos_pitch >> _UNIT_BITS,
        sin_pitch,
    )
    forward = geometry.forward(yaw, pitch)
    if roll % geometry.TURN:
        sin_roll = geometry.sine(roll)
        cos_roll = geometry.sine(roll + geometry.QUARTER_TURN)
        side, up = (
            _turned(side, cos_roll, up, sin_roll),
            _turned(up, cos_roll, side, -sin_roll),
        )
    return side, up, forward


def off_box(contact: Contact, point: Vector) -> Vector:
    """Return the offset to a point in the contact's model axes from the
    nearest point of its box: (0, 0, 0) for a point inside it."""
    near = _clamped(point, contact.box_min, contact.box_max)
    return point[0] - near[0], point[1] - near[1], point[2] - near[2]


def to_model(contact: Contact, offset: Vector) -> Vector:
    """Return a world offset from the contact's position in its model's axes."""
    return _model(axes(contact.yaw, contact.pitch, contact.roll), offset)


def to_world(contact: Contact, point: Vector) -> Vector:
    """Return an offset in the contact's model axes in world axes."""
    return _world(axes(contact.yaw, contact.pitch, contact.roll), point)


def _model(turned: tuple[Vector, Vector, Vector], offset: Vector) -> Vector:
    """Return a world offset in the model axes a craft's side, up and forward
    give."""
    side, up, forward = turned
    return (
        _dot(offset, side) >> _UNIT_BITS,
        -(_dot(offset, forward) >> _UNIT_BITS),
        _dot(offset, up) >> _UNIT_BITS,
    )


def _world(turned: tuple[Vector, Vector, Vector], point: Vector) -> Vector:
    """Return a point in the model axes a craft's side, up and forward give in
    world axes."""
    side, up, forward = turned
    x, y, z = point
    return (
        (x * side[0] + z * up[0] - y * forward[0]) >> _UNIT_BITS,
        (x * side[1] + z * up[1] - y * forward[1]) >> _UNIT_BITS,
        (x * side[2] + z * up[2] - y * forward[2]) >> _UNIT_BITS,
    )


def _clamped(point: Vector, low: Vector, high: Vector) -> Vector:
    """Return the point of a box from low to high nearest a point."""
    return tuple(
        min(max(at, least), most)
        for at, least, most in zip(point, low, high, strict=True)
    )


def _added(first: Vector, second: Vector) -> Vector:
    """Return two vectors' sum."""
    return first[0] + second[0], first[1] + second[1], first[2] + second[2]


def _turned(axis: Vector, cosine: int, other: Vector, sine: int) -> Vector:
    """Return axis times cosine plus other times sine, in 65,536ths."""
    return (
        (axis[0] * cosine + other[0] * sine) >> _UNIT_BITS,
        (axis[1] * cosine + other[1] * sine) >> _UNIT_BITS,
        (axis[2] * cosine + other[2] * sine) >> _UNIT_BITS,
    )


def _dot(first: Vector, second: Vector) -> int:
    """Return two vectors' dot product."""
    return first[0] * second[0] + first[1] * second[1] + first[2] * second[2]
