"""Fence for orders: which behavior flies each order code."""

from __future__ import annotations

import pytest

from jedimaster_ai import orders
from jedimaster_ai.behaviors import drift


def test_every_order_is_flown_by_drift_for_now() -> None:
    assert dict(orders.BEHAVIORS) == {code: drift.think for code in orders.OrderCode}


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
