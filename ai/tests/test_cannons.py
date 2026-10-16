"""Fence for cannons: the aim, the firing range, the link mode and the ion hold.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; its target is a contact flying the same way.
"""

from __future__ import annotations

import math

import pytest

from jedimaster_ai import cannons
from jedimaster_ai import geometry
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode

# Shot types (object/laser.h): PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and
# _ION_LASER, the ion cannon; 142 and 147 are the ion turbolasers.
LASER = 139
ION_CANNON = 141


@pytest.fixture
def craft(view):
    def make(**changes):
        mine = {
            "x": 0,
            "y": 0,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "skill": 0xFFFF,
            "cannon_group_count": 1,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    def make(**changes):
        return contact(**({"yaw": 0, "speed": 0, "genus": 0} | changes))

    return make


def off_by(angle: int, reach: int = 0x3000) -> int:
    """Return the sideways offset that puts a target ``reach`` ahead at
    ``angle`` yaw units off the nose."""
    return round(reach * math.tan(2 * math.pi * angle / geometry.TURN))


def test_a_target_dead_ahead_is_lined_up(craft, foe) -> None:
    aim = cannons.aim(craft(), foe(x=0, y=0x3000, z=0), 2)
    assert aim == cannons.Aim(0x3000, 0, LEVEL, 0, True)


def test_the_aim_reports_the_bearing_and_how_far_off_the_nose(craft, foe) -> None:
    target = foe(x=0x3000, y=0x3000, z=0)
    aim = cannons.aim(craft(yaw=0x1000), target, 2)
    assert aim.reach == geometry.distance(0x3000, 0x3000, 0)
    assert (aim.yaw, aim.pitch) == geometry.bearing(0x3000, 0x3000, 0)
    assert aim.off_nose == geometry.angle_between(aim.yaw, 0x1000)
    assert not aim.lined_up


@pytest.mark.parametrize("axis", ["x", "z"])
@pytest.mark.parametrize(("tier", "lined_up"), [(0, True), (1, False), (2, False)])
def test_the_cone_narrows_with_skill_in_yaw_and_pitch_alike(
    craft, foe, axis, tier, lined_up
) -> None:
    """0x700 off the nose, in yaw or in pitch: inside tier 0's cone of 0x800,
    outside tier 1's 0x600 and tier 2's 0x400."""
    target = foe(**{"x": 0, "y": 0x3000, "z": 0} | {axis: off_by(0x700)})
    assert cannons.aim(craft(), target, tier).lined_up is lined_up


@pytest.mark.parametrize(("reach", "lined_up"), [(0x9FFF, True), (0xA000, False)])
def test_a_target_at_the_firing_range_is_out_of_it(craft, foe, reach, lined_up) -> None:
    assert cannons.aim(craft(), foe(x=0, y=reach, z=0), 2).lined_up is lined_up


@pytest.mark.parametrize(
    ("kinds", "disabling", "fired"),
    [
        ((LASER, ION_CANNON), False, (True, False)),
        ((LASER, ION_CANNON), True, (False, True)),
        ((ION_CANNON, ION_CANNON), False, (False, False)),
        ((142, 147), False, (True, True)),
        ((142, 147), True, (False, False)),
    ],
)
def test_ion_cannons_fire_only_for_a_disable_order_and_alone(
    craft, kinds, disabling, fired
) -> None:
    """1997's rule: the ion cannon fires at a Disable order's target, where
    every other cannon holds; the ion turbolasers fire as other cannons do."""
    me = craft(cannon_group_count=2, cannon_type=kinds)
    burst = CannonGroup(LinkMode.ALL, 5)
    lasers = cannons.bursts(me, 0x1800, 2, disabling)
    assert lasers == tuple(burst if fires else HOLD_FIRE for fires in fired)


def test_the_1997_range_changes_apply(craft, foe) -> None:
    me = craft(skill=0xFFFF)
    assert cannons.fire_range(me, foe(speed=10), 2) == 0xA000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x1000), 2) == 0x6000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x4800), 2) == 0x8000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x8000), 2) == 0xA000
    # 1997 added 0x6000 for a starship, measured from its position; the
    # range now runs to its hull, without it.
    assert cannons.fire_range(me, foe(genus=4), 2) == 0xA000


@pytest.mark.parametrize(
    ("genus", "reach", "lined_up"), [(4, 20000, True), (3, 39140, False)]
)
def test_a_starships_reach_runs_to_its_hull_any_other_targets_to_its_position(
    craft, foe, genus, reach, lined_up
) -> None:
    """A Star Destroyer's box beam-on, its flank 20,000 ahead and its
    position 39,140: tier 0 fires at it within 24,576 of the hull; a
    freighter with the same box is measured from its position."""
    ship = foe(
        x=0,
        y=39140,
        z=0,
        yaw=0x4000,
        pitch=LEVEL,
        genus=genus,
        box_min=(-19140, -40940, -7132),
        box_max=(19140, 24456, 12570),
    )
    aim = cannons.aim(craft(skill=0), ship, 0)
    assert (aim.reach, aim.lined_up) == (reach, lined_up)
    assert (aim.yaw, aim.off_nose) == (0, 0)
