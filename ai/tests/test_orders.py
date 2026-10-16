"""Fence for orders: which behavior flies each order code."""

from __future__ import annotations

import pytest

from jedimaster_ai import orders
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import board
from jedimaster_ai.behaviors import boarded
from jedimaster_ai.behaviors import disable
from jedimaster_ai.behaviors import drift
from jedimaster_ai.behaviors import dropoff
from jedimaster_ai.behaviors import escort
from jedimaster_ai.behaviors import hold
from jedimaster_ai.behaviors import home
from jedimaster_ai.behaviors import kamikaze
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.behaviors import protect
from jedimaster_ai.behaviors import selfdestruct
from jedimaster_ai.behaviors import starship
from jedimaster_ai.behaviors import waiting


def test_each_order_has_its_behavior_and_every_other_order_drifts() -> None:
    expected = {code: drift.think for code in orders.OrderCode}
    for code in (0, 28, 30, 33, 34, 35, 38):
        expected[orders.OrderCode(code)] = hold.think
    expected[orders.OrderCode.GO_HOME] = home.think
    expected[orders.OrderCode.SS_GO_HOME] = home.think
    expected[orders.OrderCode.ATTACK] = attack.think
    for code in (2, 3, 21, 26):
        expected[orders.OrderCode(code)] = patrol.think
    expected[orders.OrderCode.PROTECT] = protect.think
    expected[orders.OrderCode.ESCORT] = escort.think
    expected[orders.OrderCode.DISABLE] = disable.think
    for code in (19, 20):
        expected[orders.OrderCode(code)] = waiting.think
    expected[orders.OrderCode.SS_AWAIT_RETURN] = starship.await_return
    expected[orders.OrderCode.SS_LAUNCH] = starship.await_launch
    for code in (24, 25):
        expected[orders.OrderCode(code)] = starship.protect
    expected[orders.OrderCode.SS_PATROL_AND_DISABLE] = starship.patrol_and_disable
    expected[orders.OrderCode.SELF_DESTRUCT] = selfdestruct.think
    expected[orders.OrderCode.KAMIKAZE] = kamikaze.think
    expected[orders.OrderCode.DROP_OFF] = dropoff.think
    for code in (12, 13, 14, 15, 16, 17, 31, 32):
        expected[orders.OrderCode(code)] = board.think
    for code in (5, 6):
        expected[orders.OrderCode(code)] = boarded.think
    assert dict(orders.BEHAVIORS) == expected


def test_there_are_40_order_codes() -> None:
    assert [code.value for code in orders.OrderCode] == list(range(40))


def test_the_table_cannot_be_changed() -> None:
    with pytest.raises(TypeError):
        orders.BEHAVIORS[0] = drift.think  # type: ignore[index]


def test_a_known_code_gets_its_behavior_quietly(view, caplog) -> None:
    def attack(craft, tick):  # a stand-in for a written behavior
        raise AssertionError("not called")

    table = {orders.OrderCode.ATTACK: attack}
    assert orders.behavior_for(view(order_code=7), table) is attack
    assert caplog.text == ""


@pytest.mark.parametrize("code", [40, 255])
def test_a_code_without_a_behavior_drifts_after_a_warning(view, caplog, code) -> None:
    assert orders.behavior_for(view(order_code=code), orders.BEHAVIORS) is drift.think
    assert f"order.no_behavior object=4 code={code}" in caplog.text
