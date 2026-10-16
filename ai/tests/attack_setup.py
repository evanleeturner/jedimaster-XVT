"""Shared setup for the attack fences: the craft, its foe, the question
and the decision, the retail boxes and two small helpers.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target.
``conftest.py`` loads this file as a plugin, so its fixtures serve every
test file that does not make its own.
"""

from __future__ import annotations

import math

import pytest

from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

ME = 4
FOE = 9
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
FREIGHTER = 3
BULK_FREIGHTER_SIZE = 4960
TIER_SKILL = (0x0000, 0x8000, 0xFFFF)
STARSHIP = 4
# Retail models' boxes, smallest and largest corners in the model's axes
# (test_hull.py's source). Level at yaw 0, a ship's stern lies its box's
# largest y behind its position; at yaw 0x4000 its flank, its largest x,
# faces a craft at the origin from ahead.
CRUISER = {"genus": STARSHIP, "size": 18449, "pitch": LEVEL}
CRUISER_BOX = {"box_min": (-2846, -9106, -1076), "box_max": (2845, 9343, 2030)}
ISD_BOX = {"box_min": (-19140, -40940, -7132), "box_max": (19140, 24456, 12570)}
SSD_BOX = {"box_min": (-52487, -162468, -11526), "box_max": (52487, 165215, 13158)}


@pytest.fixture
def craft(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "team": 1,
            "x": 0,
            "y": 0,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "roll": 0,
            "skill": 0xFFFF,
            "think_interval": attack.FIGHT_INTERVAL,
            "order_code": 7,
            "order_target_mask": 0b10,
            "enemy_mask": ALL_BUT_OWN,
            "turret_slot_mask": 0,
            "cannon_group_count": 1,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    def make(**changes):
        theirs = {
            "object_index": FOE,
            "team": 2,
            "x": 0,
            "y": 0x3000,
            "z": 0,
            "yaw": 0,
            "speed": 0,
            "target": NO_OBJECT,
            "genus": 0,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def shown(contact):
    """Return the question showing the craft's own contact first, its fields
    changed as named, then the others."""

    def make(me, *others, tick=1000, **mine):
        fields = {
            "object_index": me.object_index,
            "team": me.team,
            "x": me.x,
            "y": me.y,
            "z": me.z,
        }
        return Question(tick, (me,), (contact(**(fields | mine)), *others))

    return make


@pytest.fixture
def ask(shown):
    """Return the craft's decision with its own contact first, then the others."""

    def decide(me, *others, tick=1000, **mine):
        return attack.think(me, shown(me, *others, tick=tick, **mine))

    return decide


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


def at(yaw: int, reach: int) -> dict:
    """Return the x and y of a point reach away from the origin along a yaw."""
    radians = yaw * 2 * math.pi / 0x10000
    return {
        "x": round(reach * math.sin(radians)),
        "y": round(reach * math.cos(radians)),
    }
