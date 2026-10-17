"""Fence for Wait and Wait 2: a craft stops for the order's time, set once
for each order slot, defends itself while it runs, and is done once the
timer has run out.

The craft flies at the origin facing +y, level, at full skill, on a Wait of
3 units in slot 0 with Wait 2 next; contact 0 is its own.
"""

from __future__ import annotations

import pytest
from shot_setup import aimed

from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import waiting
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Question

ME = 4
CHASER = 12
# The order's 3 units of variable1 at 1997's 1,180 ticks each.
THREE_UNITS = 3540


@pytest.fixture
def craft(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "team": 1,
            "x": 0,
            "y": 0,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "skill": 0xFFFF,
            "think_interval": attack.FIGHT_INTERVAL,
            "enemy_mask": 0xFFFFFFFE,
            "cannon_group_count": 1,
            "order_code": OrderCode.WAIT,
            "order_codes": (19, 20, 0, 0),
            "order_slot": 0,
            "order_variable1": 3,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def chaser(contact):
    """An enemy fighter on the craft's tail, targeting it."""
    return contact(object_index=CHASER, team=2, x=0, y=-0x2000, z=0, target=ME, genus=0)


@pytest.fixture
def ask(contact):
    def decide(me, *others, tick=1000):
        own = contact(object_index=ME, team=1, x=0, y=0, z=0)
        return waiting.think(me, Question(tick, (me,), (own, *others)))

    return decide


def test_a_craft_on_wait_stops_and_sets_its_timer(craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    decision = ask(craft())
    assert decision.actions == Actions(
        throttle=0,
        activity=Activity.WAITING,
        target=NO_OBJECT,
        lasers=(HOLD_FIRE, HOLD_FIRE),
        maneuver_timer=THREE_UNITS,
    )
    assert f"waiting.start object={ME}" in caplog.text


def test_a_craft_whose_timer_runs_sets_it_no_more(craft, ask, caplog) -> None:
    first = ask(craft())
    caplog.set_level("INFO")
    later = ask(
        craft(notebook=first.notebook, activity=Activity.WAITING, maneuver_timer=900),
        tick=1016,
    )
    assert later.actions == Actions(
        throttle=0,
        activity=Activity.WAITING,
        target=NO_OBJECT,
        lasers=(HOLD_FIRE, HOLD_FIRE),
    )
    assert later.notebook == first.notebook
    assert "waiting.start" not in caplog.text


def test_the_order_is_done_once_its_timer_has_run_out(craft, ask) -> None:
    book = ask(craft()).notebook
    assert not waiting.done(
        craft(notebook=book, activity=Activity.WAITING, maneuver_timer=1)
    )
    assert waiting.done(craft(notebook=book, activity=Activity.WAITING))


def test_a_wait_not_begun_is_not_done(craft) -> None:
    assert not waiting.done(craft())


def test_wait_then_wait_2_sets_the_timer_again(craft, ask) -> None:
    book = ask(craft()).notebook
    nxt = {"order_slot": 1, "order_code": OrderCode.WAIT_2, "notebook": book}
    assert not waiting.done(craft(**nxt, activity=Activity.WAITING))
    second = ask(craft(**nxt, order_variable1=2), tick=5000)
    assert second.actions.maneuver_timer == 2360
    assert second.actions.activity == Activity.WAITING
    running = {"notebook": second.notebook, "maneuver_timer": 900}
    third = ask(craft(**(nxt | running), activity=Activity.WAITING), tick=5016)
    assert third.actions.maneuver_timer is None


def test_the_think_its_time_is_up_gives_activity_none(craft, ask) -> None:
    book = ask(craft()).notebook
    last = ask(craft(notebook=book, activity=Activity.WAITING), tick=5000)
    assert last.actions.activity == Activity.NONE
    assert last.actions.maneuver_timer is None
    assert last.actions.throttle == 0


def test_a_craft_on_wait_fights_the_enemy_after_it(craft, chaser, ask) -> None:
    actions = ask(craft(), chaser).actions
    assert actions.activity == Activity.WAITING
    assert actions.maneuver_timer == THREE_UNITS
    assert actions.steer is not None and actions.throttle != 0
    assert actions.target == CHASER


def test_the_timer_runs_on_through_a_fight(craft, chaser, ask) -> None:
    """Attack's notebook stands in while the craft fights; its WAITING
    activity says the timer was set, so the craft sets none again."""
    fought = ask(craft(), chaser).notebook
    assert notebook.decode(fought).behavior == attack.CODE
    mid = craft(notebook=fought, activity=Activity.WAITING, maneuver_timer=900)
    assert ask(mid, chaser, tick=1016).actions.maneuver_timer is None
    clear = ask(mid, tick=1016)
    assert clear.actions.maneuver_timer is None and clear.actions.throttle == 0
    assert waiting.done(craft(notebook=clear.notebook, activity=Activity.WAITING))


def test_a_wait_ending_in_a_fight_is_done_and_gives_activity_none(
    craft, chaser, ask
) -> None:
    fought = ask(craft(), chaser).notebook
    last = craft(notebook=fought, activity=Activity.WAITING)
    assert waiting.done(last)
    actions = ask(last, chaser, tick=5000).actions
    assert actions.activity == Activity.NONE and actions.steer is not None


@pytest.mark.parametrize("order", [OrderCode.WAIT, OrderCode.WAIT_2])
def test_both_orders_fly_the_wait(craft, order) -> None:
    me = craft(order_code=order, order_codes=(order, 0, 0, 0))
    said = think(Question(1000, (me,), ())).craft[0]
    assert said.actions.maneuver_timer == THREE_UNITS
    assert said.actions.order is None


def test_a_craft_on_wait_moves_to_wait_2_once_its_time_is_up(craft, ask) -> None:
    book = ask(craft()).notebook
    running = craft(notebook=book, activity=Activity.WAITING, maneuver_timer=900)
    assert think(Question(4000, (running,), ())).craft[0].actions.order is None
    up = craft(notebook=book, activity=Activity.WAITING)
    assert think(Question(5000, (up,), ())).craft[0].actions.order == OrderMove(1, 2)


def test_released_from_wait_a_craft_gets_activity_none(craft) -> None:
    plain = Decision(b"", Actions(throttle=5))
    assert waiting.ended(craft(activity=Activity.WAITING), plain).actions == Actions(
        throttle=5, activity=Activity.NONE
    )
    going = Decision(b"", Actions(activity=Activity.GOING_HOME))
    assert waiting.ended(craft(activity=Activity.WAITING), going) == going
    assert waiting.ended(craft(), plain) == plain


def test_a_homing_shot_closing_makes_it_weave_then_stop_again(craft, ask) -> None:
    first = ask(aimed(craft(), (0, -0x1000, 0)))
    assert notebook.decode(first.notebook).phase == attack.EVADE
    assert first.actions.weaving
    clear = ask(craft(notebook=first.notebook), tick=1016)
    assert clear.actions.steer is None and clear.actions.throttle == 0
