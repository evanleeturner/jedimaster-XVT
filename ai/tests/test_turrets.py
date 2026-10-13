"""Fence for turrets: every craft's turrets answer its threats, nearest first."""

from __future__ import annotations

import pytest

from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import turrets
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
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
            "order_code": 0,
            "order_target_mask": 0,
            "turret_slot_mask": 0b10110,
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
        starship(order_target_mask=0b10000),
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
