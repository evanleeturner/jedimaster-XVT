"""Fence for the starship orders: SS Await Return stops."""

from __future__ import annotations

import pytest

from jedimaster_ai.behaviors import starship
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
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
