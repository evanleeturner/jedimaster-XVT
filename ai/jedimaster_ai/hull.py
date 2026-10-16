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
    point = tuple(
        min(max(at, low), high)
        for at, low, high in zip(craft, contact.box_min, contact.box_max, strict=True)
    )
    gap = (point[0] - craft[0], point[1] - craft[1], point[2] - craft[2])
    distance = geometry.distance(*gap)
    if distance == 0:
        return Nearest(0, *offset)
    return Nearest(distance, *to_world(contact, gap))


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


def to_model(contact: Contact, offset: Vector) -> Vector:
    """Return a world offset from the contact's position in its model's axes."""
    side, up, forward = axes(contact.yaw, contact.pitch, contact.roll)
    return (
        _dot(offset, side) >> _UNIT_BITS,
        -(_dot(offset, forward) >> _UNIT_BITS),
        _dot(offset, up) >> _UNIT_BITS,
    )


def to_world(contact: Contact, point: Vector) -> Vector:
    """Return an offset in the contact's model axes in world axes."""
    side, up, forward = axes(contact.yaw, contact.pitch, contact.roll)
    x, y, z = point
    return (
        (x * side[0] + z * up[0] - y * forward[0]) >> _UNIT_BITS,
        (x * side[1] + z * up[1] - y * forward[1]) >> _UNIT_BITS,
        (x * side[2] + z * up[2] - y * forward[2]) >> _UNIT_BITS,
    )


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
