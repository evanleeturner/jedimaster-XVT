"""Fence for turrets: every craft's turrets answer its threats, nearest first."""

from __future__ import annotations

import pytest

from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import turrets
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Turret

ME = 4


@pytest.fixture
def starship(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "team": 1,
            "x": 0,
            "y": 0,
            "z": 0,
            "order_code": 10,
            "order_target_mask": 0,
            "turret_slot_mask": 0b10110,
            "enemy_mask": 0xFFFFFFFF,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def other(contact):
    def make(index, **changes):
        theirs = {"object_index": index, "team": 2, "x": 0, "y": 0x1000 * index, "z": 0}
        return contact(**(theirs | {"target": NO_OBJECT} | changes))

    return make


def defend(me, *contacts):
    return turrets.defend(me, Question(1000, (me,), contacts), Decision(me.notebook))


def aims(decision) -> list[tuple[int, int]]:
    return [(turret.slot, turret.target) for turret in decision.actions.turrets]


def test_a_craft_without_turrets_is_left_alone(starship, other) -> None:
    me = starship(turret_slot_mask=0)
    assert defend(me, other(9, target=ME)) == Decision(me.notebook)


def test_a_behaviors_own_turret_choice_is_kept(starship, other) -> None:
    me = starship()
    chosen = Decision(me.notebook, Actions(turrets=(Turret(1, NO_OBJECT),)))
    question = Question(1000, (me,), (other(9, target=ME),))
    assert turrets.defend(me, question, chosen) is chosen


def test_with_no_threat_every_turret_holds(starship, other) -> None:
    decision = defend(starship(), other(9))
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_threats_are_shared_out_nearest_first(starship, other, contact) -> None:
    own = contact(object_index=ME, team=1, x=0, y=0, z=0, last_attacker=7)
    decision = defend(
        # Craft 5, at position 3, is on the starship's team: not hostile.
        starship(order_target_mask=0b10000, enemy_mask=0b10110),
        own,
        other(9, target=ME),
        other(7),
        other(5, team=1, target=ME),
        other(3),
    )
    assert aims(decision) == [(1, 3), (2, 7), (4, 9)]


def test_a_threat_beyond_turret_range_is_left(starship, other) -> None:
    decision = defend(starship(), other(9, y=turrets.TURRET_RANGE + 1, target=ME))
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_one_threat_takes_every_turret(starship, other) -> None:
    decision = defend(starship(), other(9, target=ME))
    assert aims(decision) == [(1, 9), (2, 9), (4, 9)]


def test_the_notebook_is_never_changed(starship, other) -> None:
    me = starship(notebook=bytes(range(64)))
    assert defend(me, other(9, target=ME)).notebook == bytes(range(64))


def test_a_drifting_craft_still_defends_itself(starship, other) -> None:
    me = starship()
    answer = think(Question(1000, (me,), (other(9, target=ME),)))
    said = answer.craft[0]
    assert said.notebook == me.notebook
    assert said.actions == Actions(turrets=(Turret(1, 9), Turret(2, 9), Turret(4, 9)))


FRIEND = 6


def guard(me, *contacts):
    return turrets.guard(me, Question(1000, (me,), contacts), Decision(me.notebook))


@pytest.fixture
def friend(contact):
    """Return a maker of the friend the starship's order protects: contact
    position 0, on the starship's team, hit by some team."""

    def make(**changes):
        mine = {"object_index": FRIEND, "team": 1, "x": 0, "y": 0x800, "z": 0}
        return contact(**(mine | {"flags": ContactFlags.ATTACKED} | changes))

    return make


@pytest.fixture
def guarding(starship):
    """Return a maker of a starship whose order protects contact 0, its
    friend, and whose enemies are contacts 1 up."""

    def make(**changes):
        mine = {"order_target_mask": 0b1, "enemy_mask": ~0b1 & 0xFFFFFFFF}
        return starship(**(mine | changes))

    return make


def test_on_guard_the_turrets_take_a_protected_crafts_attacker(
    guarding, friend, other
) -> None:
    me = guarding()
    contacts = (friend(), other(9, target=FRIEND))
    assert aims(guard(me, *contacts)) == [(1, 9), (2, 9), (4, 9)]
    assert aims(defend(me, *contacts)) == [
        (1, NO_OBJECT),
        (2, NO_OBJECT),
        (4, NO_OBJECT),
    ]


def test_on_guard_the_craft_whose_shot_hit_the_protected_one_is_taken(
    guarding, friend, other
) -> None:
    decision = guard(guarding(), friend(hit_by=7), other(7))
    assert aims(decision) == [(1, 7), (2, 7), (4, 7)]


def test_a_protected_craft_no_team_has_hit_is_not_guarded(
    guarding, friend, other
) -> None:
    decision = guard(
        guarding(), friend(flags=0, hit_by=7), other(7), other(9, target=FRIEND)
    )
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_a_craft_the_order_does_not_name_is_not_guarded(
    guarding, friend, other
) -> None:
    decision = guard(guarding(order_target_mask=0), friend(), other(9, target=FRIEND))
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_a_protected_craft_a_player_said_to_leave_alone_is_not_guarded(
    guarding, friend, other
) -> None:
    decision = guard(guarding(avoid_target=FRIEND), friend(), other(9, target=FRIEND))
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_an_attacker_the_craft_may_not_fight_is_left(guarding, friend, other) -> None:
    # Craft 9, at position 1, is no enemy of the starship's.
    decision = guard(guarding(enemy_mask=0), friend(), other(9, target=FRIEND))
    assert aims(decision) == [(1, NO_OBJECT), (2, NO_OBJECT), (4, NO_OBJECT)]


def test_an_attacker_beyond_turret_range_is_left(guarding, friend, other) -> None:
    far = other(9, y=turrets.TURRET_RANGE + 1, target=FRIEND)
    assert aims(guard(guarding(), friend(), far)) == [
        (1, NO_OBJECT),
        (2, NO_OBJECT),
        (4, NO_OBJECT),
    ]


def test_on_guard_the_craft_own_threats_come_first(guarding, friend, other) -> None:
    # Craft 3 attacks the friend from nearer; craft 9 attacks the starship
    # from farther, and still comes first.
    decision = guard(
        guarding(),
        friend(),
        other(9, target=ME),
        other(3, target=FRIEND),
        other(2, target=FRIEND),
    )
    assert aims(decision) == [(1, 9), (2, 2), (4, 3)]


def test_on_guard_a_threat_that_also_attacks_the_friend_is_aimed_once(
    guarding, friend, other
) -> None:
    # Craft 7 last hit the starship and attacks the friend: it has one place
    # in the list, after the nearer threat 9, and the fourth turret wraps to 9.
    decision = guard(
        guarding(turret_slot_mask=0b1111),
        friend(),
        other(9, target=ME),
        other(8, target=FRIEND),
        other(7, y=0x9800, target=FRIEND, last_attacker=FRIEND),
        friend(object_index=ME, x=0, y=0, flags=0, last_attacker=7),
    )
    assert aims(decision) == [(0, 9), (1, 7), (2, 8), (3, 9)]


def test_on_guard_a_craft_without_turrets_is_left_alone(
    guarding, friend, other
) -> None:
    me = guarding(turret_slot_mask=0)
    assert guard(me, friend(), other(9, target=FRIEND)) == Decision(me.notebook)


def test_on_guard_a_behaviors_own_turret_choice_is_kept(
    guarding, friend, other
) -> None:
    me = guarding()
    chosen = Decision(me.notebook, Actions(turrets=(Turret(1, NO_OBJECT),)))
    question = Question(1000, (me,), (friend(), other(9, target=FRIEND)))
    assert turrets.guard(me, question, chosen) is chosen
