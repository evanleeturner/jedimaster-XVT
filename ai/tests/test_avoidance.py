"""Fence for avoidance: a small craft steering clear of the bulky craft in its way.

The craft flies at the origin facing +y (yaw 0), level; its behavior steers
it straight on unless a test says otherwise. The frigate has the retail
frigate model's measured size (``test_tactics.py``).
"""

from __future__ import annotations

import pytest

from jedimaster_ai import tactics
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.geometry import QUARTER_TURN
from jedimaster_ai.geometry import TURN
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
FRIGATE = 20
FRIGATE_SIZE = 10368
STARSHIP = 4
AHEAD = Steer(0, LEVEL, 0)
NOTEBOOK = bytes(range(NOTEBOOK_BYTES))


@pytest.fixture
def ship(contact):
    def make(y, **changes):
        theirs = {
            "object_index": FRIGATE,
            "genus": STARSHIP,
            "size": FRIGATE_SIZE,
            "x": 0,
            "y": y,
            "z": 0,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def steer(view, contact):
    """Return the decision after avoidance, from a decision steering as given."""

    def decide(*others, heading=AHEAD, target=None, own_genus=0, own_target=NO_OBJECT):
        me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
        own = contact(
            object_index=ME, genus=own_genus, x=0, y=0, z=0, target=own_target
        )
        actions = Actions(steer=heading, throttle=0x8000, target=target)
        question = Question(1000, (me,), (own, *others))
        return avoidance.steer_clear(me, question, Decision(NOTEBOOK, actions))

    return decide


def keep_of(ship) -> int:
    return tactics.clearance(ship)


def test_a_frigate_dead_ahead_turns_the_craft_past_its_edge(ship, steer) -> None:
    frigate = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(frigate))
    decision = steer(frigate)
    assert decision.actions.steer == Steer(edge + avoidance.EDGE_PAD, LEVEL, 0)
    assert decision.actions.throttle == 0x8000
    assert decision.notebook == NOTEBOOK


def test_a_frigate_past_the_look_ahead_is_not_yet_in_the_way(ship, steer) -> None:
    keep = keep_of(ship(0))
    assert steer(ship(keep + avoidance.LOOK_AHEAD)).actions.steer == AHEAD
    assert steer(ship(keep + avoidance.LOOK_AHEAD - 1)).actions.steer != AHEAD


def test_a_heading_past_the_edge_is_kept(ship, steer) -> None:
    frigate = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(frigate))
    beside = Steer(edge, LEVEL, 0)
    assert steer(frigate, heading=beside).actions.steer == beside
    inside = Steer(edge - 1, LEVEL, 0)
    assert steer(frigate, heading=inside).actions.steer != inside
    over = Steer(0, LEVEL - edge, 0)
    assert steer(frigate, heading=over).actions.steer == over


def test_the_craft_turns_to_the_side_its_heading_lies_on(ship, steer) -> None:
    frigate = ship(16000)
    turn = avoidance.edge_angle(16000, keep_of(frigate)) + avoidance.EDGE_PAD
    right = steer(frigate, heading=Steer(0x100, LEVEL, 0))
    assert right.actions.steer.yaw == turn
    left = steer(frigate, heading=Steer(TURN - 0x100, LEVEL, 0))
    assert left.actions.steer.yaw == TURN - turn


def test_inside_a_clearance_the_craft_turns_outward(ship, steer) -> None:
    decision = steer(ship(5000))
    assert decision.actions.steer.yaw == QUARTER_TURN + avoidance.EDGE_PAD
    assert angle_between(decision.actions.steer.yaw, 0) > QUARTER_TURN


def test_its_target_is_left_to_its_behavior(ship, steer) -> None:
    assert steer(ship(16000), target=FRIGATE).actions.steer == AHEAD
    assert steer(ship(16000), own_target=FRIGATE).actions.steer == AHEAD
    assert (
        steer(ship(16000), target=NO_OBJECT, own_target=FRIGATE).actions.steer != AHEAD
    )


@pytest.mark.parametrize(
    ("genus", "turned"), [(0, False), (1, False), (3, True), (4, True), (5, True)]
)
def test_only_bulky_craft_are_steered_around(ship, steer, genus, turned) -> None:
    assert (steer(ship(16000, genus=genus)).actions.steer != AHEAD) == turned


def test_a_bulky_craft_steers_clear_of_nothing(ship, steer) -> None:
    assert steer(ship(16000), own_genus=STARSHIP).actions.steer == AHEAD


def test_a_craft_that_does_not_steer_keeps_drifting(ship, steer) -> None:
    decision = steer(ship(16000), heading=None)
    assert decision.actions == Actions(throttle=0x8000)


def test_of_two_in_the_way_the_nearest_decides(ship, steer) -> None:
    freighter = ship(8000, object_index=21, genus=3, size=4960)
    edge = avoidance.edge_angle(8000, keep_of(freighter))
    decision = steer(ship(16000), freighter)
    assert decision.actions.steer.yaw == edge + avoidance.EDGE_PAD


def test_every_order_behavior_steers_clear(view, contact, ship) -> None:
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    own = contact(object_index=ME, genus=0, x=0, y=0, z=0, target=NO_OBJECT)
    straight_on = {
        me.order_code: lambda view, question: Decision(NOTEBOOK, Actions(steer=AHEAD))
    }
    answer = think(Question(1000, (me,), (own, ship(16000))), straight_on)
    assert answer.craft[0].actions.steer != AHEAD
