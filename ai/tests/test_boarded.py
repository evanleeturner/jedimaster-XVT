"""Fence for boarded: a craft that waits to be boarded, on Disabled or Await
Boarding.

1997's ``disabledpln`` and ``waitforboardpln`` flew the stop
(``paiman_awaitboardmaneuver``: throttle 0 every think) under the status line
of their plan; their goal was met once the counted boardings reached the
order's variable1 (``old:pai.c``, ``variable1 <= goal_progress[slot]``). Each
order's answer is fenced whole, the goal test at its edge and on its own
slot, and the line given back once another behavior flies the craft.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import wire
from jedimaster_ai.behaviors import boarded
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question

# A notebook another behavior wrote, which waiting must not touch.
WRITTEN = bytes(range(1, NOTEBOOK_BYTES + 1))


def test_the_order_codes_are_the_orders_own() -> None:
    assert boarded.DISABLED == OrderCode.DISABLED
    assert boarded.AWAIT_BOARDING == OrderCode.AWAIT_BOARDING
    assert boarded.ORDERS == {OrderCode.DISABLED, OrderCode.AWAIT_BOARDING}


@pytest.mark.parametrize(
    ("code", "line"),
    [
        (OrderCode.DISABLED, Activity.DISABLED),
        (OrderCode.AWAIT_BOARDING, Activity.HOLDING_STATION),
    ],
)
def test_a_waiting_craft_stops_under_its_orders_line(view, code, line) -> None:
    me = view(
        order_code=code, throttle=40000, activity=Activity.WAITING, notebook=WRITTEN
    )
    question = Question(1000, (me,))
    decision = boarded.think(me, question)
    wire.check_shown(decision.actions, me, question)
    assert decision == Decision(WRITTEN, Actions(throttle=0, activity=line))


@pytest.mark.parametrize(
    ("code", "line"),
    [
        (OrderCode.DISABLED, Activity.DISABLED),
        (OrderCode.AWAIT_BOARDING, Activity.HOLDING_STATION),
    ],
)
def test_the_line_is_sent_again_while_it_shows(view, code, line) -> None:
    me = view(order_code=code, activity=line)
    assert boarded.think(me, Question(1000, (me,))).actions.activity is line


@pytest.mark.parametrize(
    ("counted", "variable1", "met"),
    [
        ((9, 2, 9, 9), 3, False),
        ((0, 3, 0, 0), 3, True),
        ((0, 4, 0, 0), 3, True),
        ((5, 0, 5, 5), 0, True),
    ],
)
def test_the_goal_is_met_once_its_slots_boardings_reach_variable1(
    view, counted, variable1, met
) -> None:
    me = view(
        order_code=OrderCode.AWAIT_BOARDING,
        order_slot=1,
        order_progress=counted,
        order_variable1=variable1,
    )
    assert boarded.done(me) is met


@pytest.mark.parametrize("line", [Activity.DISABLED, Activity.HOLDING_STATION])
def test_the_line_goes_back_once_another_behavior_names_none(view, line) -> None:
    flown = Decision(WRITTEN, Actions(throttle=123))
    decision = boarded.ended(view(activity=line), flown)
    assert decision == Decision(WRITTEN, Actions(throttle=123, activity=Activity.NONE))


def test_a_line_another_behavior_names_stays(view) -> None:
    flown = Decision(WRITTEN, Actions(throttle=123, activity=Activity.GOING_HOME))
    assert boarded.ended(view(activity=Activity.HOLDING_STATION), flown) is flown


def test_another_line_is_left_alone(view) -> None:
    flown = Decision(WRITTEN, Actions(throttle=123))
    assert boarded.ended(view(activity=Activity.WAITING), flown) is flown
