"""Cannons: when a fighting craft's cannons fire at its target, and how many.

Purpose:
    The fire control of a craft's cannon groups in a dogfight, by the 1997
    pilot's numbers: the aim cone, the firing range and the burst by skill
    tier, the range's changes for a fast target (``fire_range``), whether
    the cannons are lined up on a target (``aim``), and, by range, whether
    all cannons fire together, in pairs or one at a time (``bursts``). The
    range runs to a starship's or platform's hull, to any other target's
    position (``hull.nearest``, rulings 98 and 99). No craft fires farther
    than its cannons' shots fly (``bolt_reach``). Its ion cannons hold,
    as in 1997: only the Disable order fires them, at its target, where
    every other cannon holds. ``behaviors.attack``, ``behaviors.disable`` and
    ``behaviors.kamikaze`` fire by these rules.

Invariants:
    - It fires only the cannon groups the view counts.
    - Ion cannons fire only for a craft disabling its target, and there no
      other cannon does.
    - Pure: the answer depends only on the view, the target and the tier.
"""

from __future__ import annotations

import dataclasses

from . import geometry
from . import hull
from . import tactics
from .world import CANNON_GROUPS
from .world import CannonGroup
from .world import Contact
from .world import CraftView
from .world import HOLD_FIRE
from .world import LinkMode

# By skill tier, 0 to 4; 3 and 4 are the Sith ranks'. The cone narrows with
# skill: a better pilot holds fire until the shot is good. The Sith tiers
# fire from no farther than a Jedi, and no tier farther than its shots fly
# (``fire_range``). Their bursts stay a Jedi's: each answer sets the burst
# again, 16 or 8 ticks on.
AIM_CONE = (0x800, 0x600, 0x400, 0x300, 0x200)
FIRE_RANGE = (0x6000, 0x8000, 0xA000, 0xA000, 0xA000)
BURST = (3, 4, 5, 5, 5)

# The 1997 pilot's range changes: a fast target flying the same way, or
# across, is harder to hit. 1997 also added 0x6000 for a starship or
# platform, measured from its position; the range now runs to the hull
# instead, without it (ruling 99).
FAST_SPEED = 25
SAME_HEADING = 0x2000
SAME_HEADING_CUT = 0x4000
OBLIQUE_HEADING = 0x5000
OBLIQUE_CUT = 0x2000
# Closer than these, all cannons fire together, then in pairs; farther, one
# at a time.
ALL_CANNONS_RANGE = 0x2000
PAIRS_RANGE = 0x4000
# The ion cannon's shot type (PROJECTILE_OBJECT_TYPE_ION_LASER in the
# engine's object/laser.h). As in 1997, it fires only at a Disable order's
# target, where every other cannon holds; the ion turbolasers, 142 and 147,
# fire as other cannons do.
ION_CANNON = 141
# The engine's cannon shots by object type (object/laser.h; their figures in
# g_projectile_type_data, object/laser.c): speed, whole simulated seconds of
# life and a further 65,536ths of one, and how far ahead of its cannon the
# shot is placed. A shot fired by a craft adds the craft's speed to its own.
_CANNON_SHOTS = {
    137: (2000, 1, 0, 921),
    138: (2000, 1, 0x8000, 921),
    139: (1800, 1, 0, 921),
    140: (1800, 1, 0x8000, 921),
    141: (1400, 1, 0x8000, 921),
    142: (1600, 2, 0, 921),
    145: (1000, 3, 0, 921),
    146: (900, 3, 0, 921),
    147: (400, 5, 0, 921),
}
# The engine moves an object (4,660 times its speed + 128) / 256 world units a
# simulated second, 236 ticks (object/object.c).
SPEED_TO_WORLD_SCALE = 4660
SIMULATED_SECOND_TICKS = 236


@dataclasses.dataclass(frozen=True)
class Aim:
    """Where a target lies from a craft, and whether its cannons are lined
    up on it."""

    # How far the target is: from its hull for a starship or platform.
    reach: int
    yaw: int
    pitch: int
    # How far the target's bearing lies from the craft's heading, in yaw.
    off_nose: int
    lined_up: bool


def aim(view: CraftView, target: Contact, tier: int) -> Aim:
    """Return how far the target lies (``hull.nearest``), the yaw and pitch
    that point at its position, how far that yaw lies off the craft's nose,
    and whether the cannons are lined up: the target inside the aim cone in
    yaw and in pitch alike, and nearer than the firing range
    (``fire_range``). A bolt aimed at a big ship's position meets its hull
    first."""
    dx, dy, dz = tactics.offset(view, target)
    reach = hull.nearest(view, target).distance
    yaw, pitch = geometry.bearing(dx, dy, dz)
    off_nose = geometry.angle_between(yaw, view.yaw)
    off_level = geometry.angle_between(pitch, view.pitch)
    lined_up = (
        off_nose < AIM_CONE[tier]
        and off_level < AIM_CONE[tier]
        and reach < fire_range(view, target, tier)
    )
    return Aim(reach, yaw, pitch, off_nose, lined_up)


def fire_range(view: CraftView, target: Contact, tier: int) -> int:
    """Return how far from a target's nearest part (``hull.nearest``) the
    craft fires, by the 1997 pilot's rules, but never farther than the
    shortest reach of its cannon groups' shots (``bolt_reach``): 1997's
    Jedi range, 40,960, outran an Imperial laser by over 7,000 units."""
    groups = view.cannon_type[: view.cannon_group_count]
    reaches = [bolt_reach(view, kind) for kind in groups]
    reach = min([FIRE_RANGE[tier], *(each for each in reaches if each is not None)])
    if target.speed > FAST_SPEED:
        apart = geometry.angle_between(view.yaw, target.yaw)
        if apart < SAME_HEADING:
            reach -= SAME_HEADING_CUT
        elif apart < OBLIQUE_HEADING:
            reach -= OBLIQUE_CUT
    return reach


def bolt_reach(view: CraftView, shot_type: int) -> int | None:
    """Return about how far a cannon shot of this type flies from the craft:
    where it is placed, then its speed and the craft's over its life; None
    for a type that is no cannon shot. The engine drops the move of the step
    a shot's life ends on, so a shot stops up to one step short of this."""
    shot = _CANNON_SHOTS.get(shot_type)
    if shot is None:
        return None
    speed, seconds, fraction, placed = shot
    life = SIMULATED_SECOND_TICKS * seconds + (
        (SIMULATED_SECOND_TICKS * fraction + 0x8000) >> 16
    )
    per_second = (SPEED_TO_WORLD_SCALE * (speed + view.speed) + 128) >> 8
    return placed + per_second * life // SIMULATED_SECOND_TICKS


def bursts(
    view: CraftView, reach: int, tier: int, disabling: bool = False
) -> tuple[CannonGroup, CannonGroup]:
    """Return a burst for each cannon group the craft has, by range and tier:
    with ``disabling`` for its ion cannons alone, else for every group but
    them."""
    if reach < ALL_CANNONS_RANGE:
        mode = LinkMode.ALL
    elif reach < PAIRS_RANGE:
        mode = LinkMode.PAIRS
    else:
        mode = LinkMode.ONE_AT_A_TIME
    burst = CannonGroup(mode, BURST[tier])
    first, second = (
        burst
        if group < view.cannon_group_count
        and (view.cannon_type[group] == ION_CANNON) == disabling
        else HOLD_FIRE
        for group in range(CANNON_GROUPS)
    )
    return first, second
