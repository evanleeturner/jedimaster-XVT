"""Fence for Self Destruct: the craft stops and asks once for its countdown."""

from __future__ import annotations

import logging

import pytest

from jedimaster_ai import dice
from jedimaster_ai import notebook
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import selfdestruct
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Question

ME = 4
TICK = 5000
# The ticks in one unit of the order's variable1, 1997's five simulated
# seconds.
UNIT_TICKS = 1180


@pytest.fixture
def scuttling(view):
    """Return a maker of a craft on Self Destruct in slot 3, the only slot
    retail missions give it, asking for 3 units."""

    def make(**changes):
        mine = {
            "order_code": OrderCode.SELF_DESTRUCT,
            "order_codes": (OrderCode.ATTACK, 0, 0, OrderCode.SELF_DESTRUCT),
            "order_slot": 3,
            "order_variable1": 3,
            "books": 0,
        }
        return view(**(mine | changes))

    return make


def ask(view, tick=TICK):
    return selfdestruct.think(view, Question(tick, (view,), ()))


def test_the_first_think_stops_and_asks_for_the_orders_delay(scuttling) -> None:
    assert ask(scuttling()).actions == Actions(throttle=0, self_destruct=3)


def test_the_notebook_names_the_slot_answered_and_keeps_the_dice(scuttling) -> None:
    me = scuttling()
    assert notebook.decode(ask(me).notebook) == notebook.Notebook(
        behavior=12,
        phase=1,
        dice=dice.seed(me.random_seed, ME),
        phase_tick=TICK,
        scratch=bytes([3]) + bytes(51),
    )


def test_a_later_think_sits_still_and_asks_nothing_more(scuttling, caplog) -> None:
    first = ask(scuttling())
    caplog.set_level(logging.DEBUG)
    later = ask(scuttling(notebook=first.notebook), TICK + 16)
    assert later.notebook == first.notebook
    assert later.actions == Actions(throttle=0)
    assert "selfdestruct" not in caplog.text


@pytest.mark.parametrize("variable1", [1, 36, 55])
def test_the_delay_is_the_orders_variable1_up_to_55_units(scuttling, variable1) -> None:
    decision = ask(scuttling(order_variable1=variable1))
    assert decision.actions == Actions(throttle=0, self_destruct=variable1)


@pytest.mark.parametrize("variable1", [56, 255])
def test_a_delay_past_55_units_is_never_asked_for(scuttling, caplog, variable1) -> None:
    caplog.set_level(logging.DEBUG)
    first = ask(scuttling(order_variable1=variable1))
    assert first.actions == Actions(throttle=0)
    assert [(record.levelname, record.getMessage()) for record in caplog.records] == [
        ("WARNING", f"selfdestruct.too_long object={ME} slot=3 units={variable1}")
    ]
    caplog.clear()
    later = ask(scuttling(order_variable1=variable1, notebook=first.notebook))
    assert later.actions == Actions(throttle=0)
    assert later.notebook == first.notebook
    assert caplog.records == []


def test_a_variable1_of_0_rolls_the_delay_on_the_crafts_dice(scuttling) -> None:
    me = scuttling(order_variable1=0)
    decision = ask(me)
    roll, state = dice.roll(dice.seed(me.random_seed, ME), 4)
    assert decision.actions == Actions(throttle=0, self_destruct=2 + roll)
    assert notebook.decode(decision.notebook).dice == state


def test_the_rolled_delays_are_2_to_5_units_and_nothing_else(scuttling) -> None:
    delays = {
        ask(scuttling(order_variable1=0, object_index=index)).actions.self_destruct
        for index in range(200)
    }
    assert delays == {2, 3, 4, 5}


def test_a_new_order_slot_asks_again(scuttling) -> None:
    earlier = scuttling(order_slot=0, order_codes=(36, 0, 0, 36))
    first = ask(earlier)
    assert first.actions == Actions(throttle=0, self_destruct=3)
    again = ask(scuttling(notebook=first.notebook))
    assert again.actions == Actions(throttle=0, self_destruct=3)
    assert notebook.decode(again.notebook).scratch[0] == 3


def test_another_behaviors_notebook_is_started_again_with_its_dice(scuttling) -> None:
    attack = notebook.Notebook(
        BehaviorCode.ATTACK, 1, 0x1234567, 100, bytes([3]) + bytes(51)
    )
    decision = ask(scuttling(order_variable1=0, notebook=attack.encode()))
    roll, state = dice.roll(0x1234567, 4)
    assert decision.actions == Actions(throttle=0, self_destruct=2 + roll)
    book = notebook.decode(decision.notebook)
    assert (book.behavior, book.phase, book.dice, book.phase_tick) == (
        12,
        1,
        state,
        TICK,
    )


@pytest.mark.parametrize("variable1", [0, 3])
def test_the_ask_is_logged_at_info_with_the_delay_in_ticks(
    scuttling, caplog, variable1
) -> None:
    caplog.set_level(logging.DEBUG)
    units = ask(scuttling(order_variable1=variable1)).actions.self_destruct
    assert [(record.levelname, record.getMessage()) for record in caplog.records] == [
        (
            "INFO",
            f"selfdestruct.start object={ME} slot=3 ordered={variable1} "
            f"units={units} ticks={units * UNIT_TICKS}",
        )
    ]
