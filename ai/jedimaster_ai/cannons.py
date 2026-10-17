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

    Where the cannons point (``aim``): ahead of the target by the craft's
    share of the lead (``lead``, AI rulings 228-231), where a shot fired now
    meets it flying straight on at its speed (``meet``); none at skill level
    0, which points at the target's position, a fifth more each level, the
    whole lead from the Jedi level up. The cone is measured to that point,
    the range to the target as before. Where a target will be (``predict``)
    as its contact's turn record shows it turning and speeding up, and the
    meeting on that path: the Sith Lord's lead (``lord.aim``).

Invariants:
    - It fires only the cannon groups the view counts.
    - Ion cannons fire only for a craft disabling its target, and there no
      other cannon does.
    - Pure: the answer depends only on the view, the target and the tier.
    - At skill level 0 the cannons point at the target's position, the
      1997 pilot's aim, bit for bit.
    - Integers only, as ``geometry``, so every machine predicts alike.
"""

from __future__ import annotations

import dataclasses

from . import geometry
from . import hull
from . import tactics
from .world import ARRIVAL_SPEED_MAX
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
# again, 16 or 8 ticks on. The Sith Lord's cone, measured offline (AI ruling
# 240): from 0x100 to 0x280 it hit within 1% as often, at 0x300 4% less.
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
# A target's predicted flight (``predict``) is stepped this many ticks at a
# time, each step on its middle heading, and a shot's time to meet it found
# in this many passes (``meet``). Measured offline against scripted targets
# (AI ruling 240): steps of 2 to 32 ticks and 2 to 8 passes moved the meeting
# point by less than 30 world units and changed no hit, so both stay.
PREDICT_STEP_TICKS = 8
LEAD_PASSES = 4
# A predicted speed stays between 0 and the fastest any craft flies, the speed
# out of hyperspace.
PREDICTED_SPEED_MAX = ARRIVAL_SPEED_MAX
# The share of the whole lead a craft's cannons point ahead, in fifths, by
# skill level 0 to 7 (AI rulings 229 and 231): Rookie none, Officer to Top
# Ace a fifth more each, Jedi and Sith Apprentice the whole; the Sith Lord's
# whole lead follows the target's turn (``lord.aim``).
LEAD_FIFTHS = (0, 1, 2, 3, 4, 5, 5, 5)
FIFTHS = 5


@dataclasses.dataclass(frozen=True)
class Meeting:
    """Where a cannon shot fired now meets a target: the point's offset from
    the craft in world units, and the ticks the shot takes to it."""

    dx: int
    dy: int
    dz: int
    ticks: int


@dataclasses.dataclass(frozen=True)
class Aim:
    """Where a target lies from a craft, where its cannons point, and
    whether they are lined up."""

    # How far the target is: from its hull for a starship or platform.
    reach: int
    # The heading the cannons point along: at the lead (``lead``).
    yaw: int
    pitch: int
    # How far the target's own bearing lies from the craft's heading, in yaw.
    off_nose: int
    lined_up: bool


def aim(view: CraftView, target: Contact, tier: int, disabling: bool = False) -> Aim:
    """Return how far the target lies (``hull.nearest``), the yaw and pitch
    that point at the craft's lead (``lead``; ``disabling`` picks its ion
    cannons' shot), how far the target's own bearing lies off the nose, and
    whether the cannons are lined up there (``fires_at``). A bolt aimed at a
    big ship's position meets its hull first."""
    dx, dy, dz = tactics.offset(view, target)
    reach = hull.nearest(view, target).distance
    yaw, pitch = geometry.bearing(dx, dy, dz)
    off_nose = geometry.angle_between(yaw, view.yaw)
    ahead = lead(view, target, disabling)
    if ahead != (dx, dy, dz):
        yaw, pitch = geometry.bearing(*ahead)
    lined_up = fires_at(view, target, tier, reach, yaw, pitch)
    return Aim(reach, yaw, pitch, off_nose, lined_up)


def fires_at(
    view: CraftView, target: Contact, tier: int, reach: int, yaw: int, pitch: int
) -> bool:
    """Return whether cannons pointing along yaw and pitch are lined up: that
    heading inside the tier's aim cone of the nose in yaw and in pitch alike,
    and the target nearer than the firing range (``fire_range``)."""
    return (
        geometry.angle_between(yaw, view.yaw) < AIM_CONE[tier]
        and geometry.angle_between(pitch, view.pitch) < AIM_CONE[tier]
        and reach < fire_range(view, target, tier)
    )


def lead(
    view: CraftView, target: Contact, disabling: bool = False
) -> tuple[int, int, int]:
    """Return the offset from the craft its cannons point at: the target's
    position moved toward where a shot of its first firing group
    (``firing_shot``) meets the target flying straight on (``meet``), by
    LEAD_FIFTHS of its skill level, rounded toward 0. The position itself
    at level 0 and for a craft with no known cannon shot."""
    dx, dy, dz = tactics.offset(view, target)
    fifths = LEAD_FIFTHS[min(view.skill_level, len(LEAD_FIFTHS) - 1)]
    if fifths == 0:
        return dx, dy, dz
    meeting = meet(view, target, firing_shot(view, disabling))
    if meeting is None:
        return dx, dy, dz
    return (
        dx + _toward_zero((meeting.dx - dx) * fifths, FIFTHS),
        dy + _toward_zero((meeting.dy - dy) * fifths, FIFTHS),
        dz + _toward_zero((meeting.dz - dz) * fifths, FIFTHS),
    )


def firing_shot(view: CraftView, disabling: bool = False) -> int:
    """Return the shot type of the first cannon group that fires: its ion
    cannons alone when disabling, else every other group (``bursts``); 0,
    no shot, when none does."""
    for group in range(view.cannon_group_count):
        kind = view.cannon_type[group]
        if (kind == ION_CANNON) == disabling:
            return kind
    return 0


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
    speed, _, _, placed = shot
    return placed + pace(speed + view.speed) * _life(shot) // SIMULATED_SECOND_TICKS


def pace(speed: int) -> int:
    """Return the world units a speed moves in a simulated second, by the
    engine's motion rule."""
    return (SPEED_TO_WORLD_SCALE * speed + 128) >> 8


def _life(shot: tuple[int, int, int, int]) -> int:
    """Return a cannon shot's life in ticks, from its whole seconds and
    65,536ths of one."""
    _, seconds, fraction, _ = shot
    return SIMULATED_SECOND_TICKS * seconds + (
        (SIMULATED_SECOND_TICKS * fraction + 0x8000) >> 16
    )


def heading_at(target: Contact, ticks: int) -> tuple[int, int, int]:
    """Return the target's yaw, pitch and speed ticks on, turning and
    speeding up at the rates its turn record shows: each change over its
    ``turn_ticks``, rounded toward 0, so a left turn mirrors a right. With no
    record it holds them; the speed stays within 0 and PREDICTED_SPEED_MAX."""
    over = target.turn_ticks
    if over == 0:
        return target.yaw, target.pitch, target.speed
    yaw = target.yaw + _toward_zero(target.yaw_turned * ticks, over)
    pitch = target.pitch + _toward_zero(target.pitch_turned * ticks, over)
    speed = target.speed + _toward_zero(target.speed_changed * ticks, over)
    return (
        yaw % geometry.TURN,
        pitch % geometry.TURN,
        min(max(speed, 0), PREDICTED_SPEED_MAX),
    )


def predict(target: Contact, ticks: int) -> tuple[int, int, int]:
    """Return where the target will be ticks on: flown from its position at
    its pace (``pace``), PREDICT_STEP_TICKS at a time, each step along the
    heading at its middle (``heading_at``), so a target turning steadily is
    followed round its curve and one flying straight goes straight on."""
    scale = SIMULATED_SECOND_TICKS << 16
    # With no turn record every step flies one heading: one step is the same.
    stride = PREDICT_STEP_TICKS if target.turn_ticks else max(ticks, 1)
    x = y = z = 0
    done = 0
    while done < ticks:
        step = min(stride, ticks - done)
        yaw, pitch, speed = heading_at(target, done + step // 2)
        ux, uy, uz = geometry.forward(yaw, pitch)
        moved = pace(speed) * step
        x, y, z = x + ux * moved, y + uy * moved, z + uz * moved
        done += step
    return (
        target.x + _toward_zero(x, scale),
        target.y + _toward_zero(y, scale),
        target.z + _toward_zero(z, scale),
    )


def _toward_zero(numerator: int, denominator: int) -> int:
    """Return a quotient of a positive denominator rounded toward 0, so a
    mirrored input gives the mirrored answer."""
    quotient = abs(numerator) // denominator
    return -quotient if numerator < 0 else quotient


def meet(
    view: CraftView, target: Contact, shot_type: int, turning: bool = False
) -> Meeting | None:
    """Return where a cannon shot of this type, fired now from the craft,
    meets the target: flying straight on along its heading at its speed, or,
    ``turning``, on its predicted flight (``predict``). LEAD_PASSES passes of
    the ticks the shot takes to reach where the target will be then, from
    where the shot is placed, at its speed and the craft's. None for a type
    that is no cannon shot; the meeting may lie past the shot's life, which
    the firing range answers for."""
    shot = _CANNON_SHOTS.get(shot_type)
    if shot is None:
        return None
    if not turning:
        target = dataclasses.replace(
            target, yaw_turned=0, pitch_turned=0, speed_changed=0, turn_ticks=0
        )
    speed, _, _, placed = shot
    per_second = pace(speed + view.speed)
    ticks = 0
    for _ in range(LEAD_PASSES):
        x, y, z = predict(target, ticks)
        reach = geometry.distance(x - view.x, y - view.y, z - view.z)
        ticks = max(reach - placed, 0) * SIMULATED_SECOND_TICKS // per_second
    x, y, z = predict(target, ticks)
    return Meeting(x - view.x, y - view.y, z - view.z, ticks)


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
