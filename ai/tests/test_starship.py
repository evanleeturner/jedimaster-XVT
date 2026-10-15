"""Fence for the starship orders: SS Await Return stops, SS Protect guards."""

from __future__ import annotations

import dataclasses

import pytest

from jedimaster_ai.behaviors import patrol
from jedimaster_ai.behaviors import starship
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Turret

ME = 4


@pytest.fixture
def ship(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "team": 1,
            "x": 0,
            "y": 0,
            "z": 0,
            "throttle": 0xFFFF,
            "order_target_mask": 0,
            "turret_slot_mask": 0,
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


def test_ss_await_return_stops_and_keeps_its_notebook(ship) -> None:
    me = ship(order_code=OrderCode.SS_AWAIT_RETURN, notebook=bytes(range(64)))
    decision = starship.await_return(me, Question(1000, (me,), ()))
    assert decision.notebook == bytes(range(64))
    assert decision.actions == Actions(throttle=0)


def test_a_craft_on_ss_await_return_stops_until_its_groups_are_back(ship) -> None:
    me = ship(order_code=OrderCode.SS_AWAIT_RETURN, order_codes=(22, 0, 0, 0))
    answer = think(Question(1000, (me,), ()))
    assert answer.craft[0].actions == Actions(throttle=0)


def test_a_craft_on_ss_await_return_leaves_its_turrets_to_defend_it(
    ship, other
) -> None:
    me = ship(order_code=OrderCode.SS_AWAIT_RETURN, turret_slot_mask=0b110)
    answer = think(Question(1000, (me,), (other(9, target=ME),)))
    assert answer.craft[0].actions == Actions(
        throttle=0, turrets=(Turret(1, 9), Turret(2, 9))
    )


FRIEND = 6


@pytest.fixture
def friend(contact):
    """Return a maker of the friend the order protects, contact position 0,
    hit by some team."""

    def make(**changes):
        mine = {"object_index": FRIEND, "team": 1, "x": 0, "y": 0x800, "z": 0}
        return contact(**(mine | {"flags": ContactFlags.ATTACKED} | changes))

    return make


@pytest.fixture
def guarding(ship):
    """Return a maker of a starship on SS Protect: its order protects
    contact 0, its enemies are contacts 1 up, and it has two waypoints."""

    def make(**changes):
        mine = {
            "order_code": OrderCode.SS_PROTECT,
            "order_codes": (24, 0, 0, 0),
            "order_target_mask": 0b1,
            "enemy_mask": ~0b1 & 0xFFFFFFFF,
            "turret_slot_mask": 0b110,
            "waypoint_mask": 0b11,
            "waypoint_x": (0x20000, 0x20000, 0, 0, 0, 0, 0, 0),
            "waypoint_y": (0, 0x20000, 0, 0, 0, 0, 0, 0),
        }
        return ship(**(mine | changes))

    return make


def test_ss_protect_flies_the_patrol_cruise(guarding, friend, other) -> None:
    me = guarding(turret_slot_mask=0)
    question = Question(1000, (me,), (friend(), other(9, target=FRIEND)))
    assert starship.protect(me, question) == patrol.think(me, question)


@pytest.mark.parametrize("order", [OrderCode.SS_PROTECT, OrderCode.SS_PROTECT_2])
def test_a_craft_on_ss_protect_aims_its_turrets_at_the_friends_attacker(
    guarding, friend, other, order
) -> None:
    me = guarding(order_code=order, order_codes=(order, 0, 0, 0))
    question = Question(1000, (me,), (friend(), other(9, target=FRIEND)))
    said = think(question).craft[0]
    cruise = patrol.think(me, question)
    assert said.notebook == cruise.notebook
    assert said.actions == dataclasses.replace(
        cruise.actions, turrets=(Turret(1, 9), Turret(2, 9))
    )


def test_a_craft_on_ss_protect_answers_its_own_attacker_first(
    guarding, friend, other
) -> None:
    me = guarding(turret_slot_mask=0b1)
    question = Question(
        1000, (me,), (friend(), other(3, target=FRIEND), other(9, target=ME))
    )
    assert think(question).craft[0].actions.turrets == (Turret(0, 9),)
