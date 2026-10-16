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
# _ION_LASER, the ion cannon; 142 and 147 are the ion turbolasers, 137 the
# rebel laser.
REBEL_LASER = 137
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


@pytest.mark.parametrize("axis", ["x", "z"])
@pytest.mark.parametrize(
    ("angle", "tier", "lined_up"),
    [(0x380, 2, True), (0x380, 3, False), (0x280, 3, True), (0x280, 4, False)],
)
def test_the_sith_cones_narrow_past_a_jedis(
    craft, foe, axis, angle, tier, lined_up
) -> None:
    """Tier 3's cone is 0x300 and tier 4's 0x200, inside tier 2's 0x400."""
    target = foe(**{"x": 0, "y": 0x3000, "z": 0} | {axis: off_by(angle)})
    assert cannons.aim(craft(), target, tier).lined_up is lined_up


@pytest.mark.parametrize("tier", [2, 3, 4])
@pytest.mark.parametrize(("reach", "lined_up"), [(0x9FFF, True), (0xA000, False)])
def test_a_target_at_the_firing_range_is_out_of_it(
    craft, foe, tier, reach, lined_up
) -> None:
    """The Sith tiers fire from no farther than a Jedi, 0xA000."""
    assert cannons.aim(craft(), foe(x=0, y=reach, z=0), tier).lined_up is lined_up


@pytest.mark.parametrize("tier", [2, 3, 4])
def test_the_sith_tiers_burst_as_a_jedi(craft, tier) -> None:
    """Each answer sets the burst again, so the Sith keep a Jedi's 5 shots."""
    lasers = cannons.bursts(craft(cannon_type=(LASER, 0)), 0x5000, tier)
    assert lasers[0] == CannonGroup(LinkMode.ONE_AT_A_TIME, 5)


# A shot's reach: placed 921 ahead, then (4,660 x speed + 128) / 256 a
# simulated second over its life, its speed plus the craft's (laser.c's table).
@pytest.mark.parametrize(
    ("kinds", "speed", "reach"),
    [
        ((LASER, 0), 0, 921 + 32766),  # 1,800 for 236 ticks
        ((LASER, 0), 249, 921 + 37298),  # a TIE Interceptor at full speed
        ((REBEL_LASER, 0), 0, 921 + 36406),  # 2,000 for 236 ticks
        ((ION_CANNON, 0), 0, 921 + 25484 * 354 // 236),  # 1,400 for 354
        ((ION_CANNON, LASER), 0, 921 + 32766),  # the shorter group's
        ((REBEL_LASER, 0), 225, 0xA000),  # an X-wing's 41,423: a Jedi's range
    ],
    ids=["imperial", "imperial-fast", "rebel", "ion", "shorter", "jedi-cap"],
)
def test_no_tier_fires_farther_than_its_shots_fly(
    craft, foe, kinds, speed, reach
) -> None:
    """#308: a Jedi TIE fired from 40,960 in 1997, where its shots stop at
    33,687, and the Sith tiers already stopped there."""
    me = craft(cannon_type=kinds, cannon_group_count=2, speed=speed)
    for tier in (2, 3, 4):
        assert cannons.aim(me, foe(x=0, y=reach - 1, z=0), tier).lined_up
        assert not cannons.aim(me, foe(x=0, y=reach, z=0), tier).lined_up


@pytest.mark.parametrize(("tier", "reach"), [(0, 0x6000), (1, 0x8000)])
def test_tiers_0_and_1_keep_their_1997_range(craft, foe, tier, reach) -> None:
    """Both lie inside every cannon shot's reach: the shortest, an
    Imperial laser's from a craft at rest, is 33,687."""
    me = craft(cannon_type=(LASER, 0), cannon_group_count=1, speed=0)
    assert cannons.fire_range(me, foe(), tier) == reach


def test_a_shot_short_of_tier_0s_range_stops_tier_0_too(
    craft, foe, monkeypatch
) -> None:
    """No tier fires past its shots, though no shot in the engine's table
    flies short of tier 0's range: a made-up one at 1,000 for 236 ticks
    reaches 921 + 18,203."""
    monkeypatch.setitem(cannons._CANNON_SHOTS, 1, (1000, 1, 0, 921))
    me = craft(cannon_type=(1, 0), cannon_group_count=1, speed=0)
    for tier in (0, 1):
        assert cannons.aim(me, foe(x=0, y=19123, z=0), tier).lined_up
        assert not cannons.aim(me, foe(x=0, y=19124, z=0), tier).lined_up


def test_a_group_the_craft_lacks_sets_no_reach(craft, foe) -> None:
    """Only the groups the view counts: a second group's shorter shot, past
    the count, leaves a Sith ion craft its own reach, 39,147."""
    me = craft(cannon_type=(ION_CANNON, LASER), cannon_group_count=1)
    assert cannons.aim(me, foe(x=0, y=39146, z=0), 3).lined_up


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


def test_a_fast_targets_cut_comes_off_the_shots_reach(craft, foe) -> None:
    """A Jedi TIE's shots fly 33,687; a fast target flying its way takes
    16,384 off that, not off 1997's 40,960."""
    me = craft(cannon_type=(LASER, 0), cannon_group_count=1, speed=0)
    assert cannons.fire_range(me, foe(speed=40, yaw=0x1000), 2) == 17303


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
