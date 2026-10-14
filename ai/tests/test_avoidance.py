"""Fence for avoidance: a craft steering clear of the bulky craft in its way.

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
FREIGHTER = 3
BULK_FREIGHTER_SIZE = 4960
FIGHTER_SIZE = 901
# The nearest two bulky craft of one group started, in the flights measured.
SPAWN_NEAREST = 17685
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

    def decide(
        *others, heading=AHEAD, target=None, own_genus=0, own_target=NO_OBJECT, nose=0
    ):
        me = view(object_index=ME, x=0, y=0, z=0, yaw=nose, pitch=LEVEL, roll=0)
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


def test_the_nose_picks_the_side_so_the_turn_never_crosses_the_frigate(
    ship, steer
) -> None:
    """The behavior's heading lies a little left of the frigate dead ahead
    and the nose a little right: the craft goes round the right, the side
    its nose is on; the left edge would swing its nose across the frigate
    (AI5e's Y-wing in raw10). With the nose dead on, the heading's side."""
    frigate = ship(16000)
    clockwise = avoidance.edge_angle(16000, keep_of(frigate)) + avoidance.EDGE_PAD
    left = Steer(TURN - 0x200, LEVEL, 0)
    turned = steer(frigate, heading=left, nose=0x800)
    assert turned.actions.steer == Steer(clockwise, LEVEL, 0)
    dead_on = steer(frigate, heading=left, nose=0)
    assert dead_on.actions.steer == Steer(TURN - clockwise, LEVEL, 0)


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


def test_a_bulky_craft_steers_clear_of_other_bulky_craft(ship, steer) -> None:
    frigate = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(frigate))
    decision = steer(frigate, own_genus=STARSHIP)
    assert decision.actions.steer == Steer(edge + avoidance.EDGE_PAD, LEVEL, 0)
    assert steer(ship(16000), own_genus=FREIGHTER).actions.steer != AHEAD


@pytest.mark.parametrize("own_genus", [0, FREIGHTER, STARSHIP])
def test_no_craft_steers_clear_of_a_small_craft(ship, steer, own_genus) -> None:
    fighter = ship(3000, genus=0, size=FIGHTER_SIZE)
    assert steer(fighter, own_genus=own_genus).actions.steer == AHEAD


def test_a_bulky_group_starts_outside_its_neighbours_look_ahead(ship, steer) -> None:
    """Two freighters of one group were seen to start 17,685 units apart at
    the least (three retail missions flown, one networked); dead ahead at
    that distance a freighter's neighbour is not yet in its way, so a
    group's starting formation is not turned apart."""
    freighter = ship(SPAWN_NEAREST, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    assert keep_of(freighter) + avoidance.LOOK_AHEAD < SPAWN_NEAREST
    assert steer(freighter, own_genus=FREIGHTER).actions.steer == AHEAD


def test_in_the_way_names_the_nearest_and_skips_one(view, contact, ship) -> None:
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    near = ship(8000, object_index=21, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    question = Question(1000, (me,), (ship(16000), near))
    found = avoidance.in_the_way(me, question, 0, LEVEL, NO_OBJECT)
    assert (found.contact.object_index, found.distance) == (21, 8000)
    assert found.clearance == keep_of(near)
    assert (found.yaw, found.edge) == (0, avoidance.edge_angle(8000, keep_of(near)))
    assert (
        avoidance.in_the_way(me, question, 0, LEVEL, 21).contact.object_index == FRIGATE
    )
    assert avoidance.in_the_way(me, question, QUARTER_TURN, LEVEL, NO_OBJECT) is None


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
