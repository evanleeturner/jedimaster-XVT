"""Fence for attack's lead: a fighting craft steers at its plain lead by its
skill (``cannons.aim``), with its ion cannons' shot on a Disable order.

The craft is ``attack_setup.py``'s at tier 2, flown at speed 300 with
rebel lasers; its target crosses along +x 0x4000 ahead at a type 8's size
and speed. The Sith Lord's own aim is ``test_attack_lord_aim.py``'s.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import cannons
from jedimaster_ai import geometry
from jedimaster_ai.behaviors import attack
from jedimaster_ai.world import Question

REBEL_LASER = 137
ION_CANNON = 141
JEDI = 5
APPRENTICE = 6
TICK = 1000


@pytest.fixture
def fighter(craft):
    def make(**changes):
        mine = {"speed": 300, "cannon_type": (REBEL_LASER, 0), "skill_level": JEDI}
        return craft(**(mine | changes))

    return make


@pytest.fixture
def crosser(foe):
    def make(**changes):
        crossing = {
            "y": 0x4000,
            "yaw": 0x4000,
            "pitch": geometry.LEVEL,
            "speed": 300,
            "size": 400,
        }
        return foe(**(crossing | changes))

    return make


def steer_of(me, target, disabling=False):
    question = Question(TICK, (me,), ())
    steer = attack.fight(me, question, [target], disabling=disabling).actions.steer
    return steer.yaw, steer.pitch


@pytest.mark.parametrize(("level", "tier"), [(0, 2), (JEDI, 2), (APPRENTICE, 3)])
def test_each_rank_steers_at_its_plain_lead(fighter, crosser, level, tier) -> None:
    me, target = fighter(skill_level=level), crosser()
    plain = cannons.aim(me, target, tier)
    assert steer_of(me, target) == (plain.yaw, plain.pitch)
    at_target = geometry.bearing(target.x, target.y, target.z)
    assert (steer_of(me, target) == at_target) is (level == 0)


def test_a_disabling_craft_steers_at_its_ion_shots_lead(fighter, crosser) -> None:
    me = fighter(cannon_type=(REBEL_LASER, ION_CANNON), cannon_group_count=2)
    target = crosser()
    ion = cannons.aim(me, target, 2, disabling=True)
    assert steer_of(me, target, disabling=True) == (ion.yaw, ion.pitch)
    assert steer_of(me, target) != (ion.yaw, ion.pitch)
