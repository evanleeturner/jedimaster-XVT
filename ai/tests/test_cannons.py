"""Fence for cannons: the firing range, the link mode and the ion hold.

The craft flies facing +y (yaw 0), level, at tier 2 unless a test says
otherwise; its target is a contact flying the same way.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import cannons
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
        mine = {"yaw": 0, "pitch": LEVEL, "skill": 0xFFFF, "cannon_group_count": 1}
        return view(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    def make(**changes):
        return contact(**({"yaw": 0, "speed": 0, "genus": 0} | changes))

    return make


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
    lasers = cannons.cannon_fire(me, 0x1800, 2, disabling)
    assert lasers == tuple(burst if fires else HOLD_FIRE for fires in fired)


def test_the_1997_range_changes_apply(craft, foe) -> None:
    me = craft(skill=0xFFFF)
    assert cannons.fire_range(me, foe(speed=10), 2) == 0xA000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x1000), 2) == 0x6000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x4800), 2) == 0x8000
    assert cannons.fire_range(me, foe(speed=40, yaw=0x8000), 2) == 0xA000
    assert cannons.fire_range(me, foe(genus=4), 2) == 0x10000
