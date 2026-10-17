"""Fence for Self Destruct: the craft stops, defends itself as a waiting
craft does, and asks for its countdown while no lifetime timer runs."""

from __future__ import annotations

import logging

import pytest
from shot_setup import aimed

from jedimaster_ai import dice
from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import selfdestruct
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Turret

ME = 4
NOTEBOOK = bytes(range(NOTEBOOK_BYTES))
CHASER = 12
TICK = 5000
# The ticks in one unit of the order's variable1, 1997's five simulated
# seconds.
UNIT_TICKS = 1180
# A hull below three quarters of its strength: the craft may not fight back.
HURT = {"hull_strength": 400, "hull_damage": 101}


def stopped(**changes) -> Actions:
    """Return the actions of a craft sitting still under its status line."""
    return Actions(throttle=0, activity=Activity.SELF_DESTRUCTING, **changes)


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
    assert ask(scuttling()).actions == stopped(self_destruct=3)


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
    assert later.actions == stopped()
    assert "selfdestruct" not in caplog.text


@pytest.mark.parametrize("variable1", [1, 36, 55])
def test_the_delay_is_the_orders_variable1_up_to_55_units(scuttling, variable1) -> None:
    decision = ask(scuttling(order_variable1=variable1))
    assert decision.actions == stopped(self_destruct=variable1)


@pytest.mark.parametrize("variable1", [56, 255])
def test_a_delay_past_55_units_is_never_asked_for(scuttling, caplog, variable1) -> None:
    caplog.set_level(logging.DEBUG)
    first = ask(scuttling(order_variable1=variable1))
    assert first.actions == stopped()
    assert [(record.levelname, record.getMessage()) for record in caplog.records] == [
        ("WARNING", f"selfdestruct.too_long object={ME} slot=3 units={variable1}")
    ]
    caplog.clear()
    later = ask(scuttling(order_variable1=variable1, notebook=first.notebook))
    assert later.actions == stopped()
    assert later.notebook == first.notebook
    assert caplog.records == []


def test_a_variable1_of_0_rolls_the_delay_on_the_crafts_dice(scuttling) -> None:
    me = scuttling(order_variable1=0)
    decision = ask(me)
    roll, state = dice.roll(dice.seed(me.random_seed, ME), 4)
    assert decision.actions == stopped(self_destruct=2 + roll)
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
    assert first.actions == stopped(self_destruct=3)
    again = ask(scuttling(notebook=first.notebook))
    assert again.actions == stopped(self_destruct=3)
    assert notebook.decode(again.notebook).scratch[0] == 3


def test_another_behaviors_notebook_is_started_again_with_its_dice(scuttling) -> None:
    attack = notebook.Notebook(
        BehaviorCode.ATTACK, 1, 0x1234567, 100, bytes([3]) + bytes(51)
    )
    decision = ask(scuttling(order_variable1=0, notebook=attack.encode()))
    roll, state = dice.roll(0x1234567, 4)
    assert decision.actions == stopped(self_destruct=2 + roll)
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


def test_a_craft_that_skipped_to_self_destruct_asks_once_and_fights_back(
    scuttling, contact
) -> None:
    skipped = {
        "books": Books.SKIPPED,
        "turret_slot_mask": 0b110,
        "enemy_mask": 0xFFFFFFFF,
    }
    attacker = contact(object_index=9, team=2, target=ME)
    first = think(Question(TICK, (scuttling(**skipped),), (attacker,))).craft[0]
    defend = (Turret(1, 9), Turret(2, 9))
    assert first.actions.self_destruct == 3
    assert first.actions.target == 9 and first.actions.steer is not None
    assert first.actions.turrets == defend
    running = {"notebook": first.notebook, "lifetime_timer": 3 * UNIT_TICKS - 16}
    again = scuttling(**skipped, **running)
    later = think(Question(TICK + 16, (again,), (attacker,))).craft[0]
    assert later.actions.self_destruct is None
    assert later.actions.steer is not None and later.actions.turrets == defend


@pytest.mark.parametrize("timer", [1, 3 * UNIT_TICKS, 0xFFFF])
def test_a_running_lifetime_timer_is_never_asked_again(
    scuttling, caplog, timer
) -> None:
    caplog.set_level(logging.DEBUG)
    me = scuttling(lifetime_timer=timer)
    decision = ask(me)
    assert decision == ask(scuttling(lifetime_timer=timer))
    assert decision.actions == stopped()
    assert decision.notebook == me.notebook
    assert "selfdestruct" not in caplog.text


def test_a_later_self_destruct_slot_keeps_the_running_countdown(scuttling) -> None:
    """1997 set the timer only while none ran: a skip from one Self Destruct
    slot to another asks for nothing new."""
    earlier = scuttling(order_slot=0, order_codes=(36, 0, 0, 36))
    first = ask(earlier)
    running = scuttling(notebook=first.notebook, lifetime_timer=2 * UNIT_TICKS)
    assert ask(running).actions == stopped()


@pytest.fixture
def fighter(scuttling):
    """Return a maker of a Self Destruct craft at the origin facing +y,
    level, at full skill, on team 1."""

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
        }
        return scuttling(**(mine | changes))

    return make


@pytest.fixture
def chaser(contact):
    """An enemy fighter on the craft's tail, targeting it."""
    return contact(object_index=CHASER, team=2, x=0, y=-0x2000, z=0, target=ME, genus=0)


@pytest.fixture
def fly(contact):
    def decide(me, *others, tick=TICK, **mine):
        own = contact(object_index=ME, team=1, x=0, y=0, z=0, **mine)
        return selfdestruct.think(me, Question(tick, (me,), (own, *others)))

    return decide


def test_the_craft_fights_the_enemy_after_it_and_asks_for_its_countdown(
    fighter, chaser, fly, caplog
) -> None:
    caplog.set_level(logging.DEBUG)
    actions = fly(fighter(), chaser).actions
    assert actions.target == CHASER
    assert actions.steer is not None and actions.throttle != 0
    assert actions.self_destruct == 3
    starts = [
        r.getMessage() for r in caplog.records if "selfdestruct" in r.getMessage()
    ]
    assert starts == [
        f"selfdestruct.start object={ME} slot=3 ordered=3 units=3 ticks=3540"
    ]


def test_once_the_countdown_runs_the_craft_fights_on_and_asks_nothing(
    fighter, chaser, fly, caplog
) -> None:
    first = fly(fighter(), chaser)
    assert notebook.decode(first.notebook).behavior == BehaviorCode.ATTACK
    caplog.set_level(logging.DEBUG)
    running = fighter(notebook=first.notebook, lifetime_timer=3 * UNIT_TICKS - 16)
    later = fly(running, chaser, tick=TICK + 16)
    assert later.actions.steer is not None and later.actions.throttle != 0
    assert later.actions.self_destruct is None
    assert notebook.decode(later.notebook).behavior == BehaviorCode.ATTACK
    assert "selfdestruct" not in caplog.text


def test_clear_of_enemies_it_lets_go_then_stops(fighter, chaser, fly) -> None:
    """The think after the fight still ends its weave: the craft drops its
    target, holds fire and stops; from the next it only holds."""
    first = fly(fighter(), chaser)
    timer = {"lifetime_timer": 3 * UNIT_TICKS - 16}
    letting_go = fly(fighter(notebook=first.notebook, **timer), tick=TICK + 16)
    assert letting_go.actions == stopped(
        target=NO_OBJECT, lasers=(HOLD_FIRE, HOLD_FIRE)
    )
    after = fighter(notebook=letting_go.notebook, **timer)
    held = fly(after, tick=TICK + 32)
    assert held.actions == stopped()
    assert held.notebook == after.notebook


def test_a_hurt_craft_weaves_then_stops_again(fighter, chaser, fly) -> None:
    running = {"lifetime_timer": 3 * UNIT_TICKS}
    first = fly(fighter(**running), chaser, **HURT)
    assert notebook.decode(first.notebook).phase == attack.EVADE
    assert first.actions.throttle == attack.FULL_THROTTLE
    assert first.actions.target == NO_OBJECT
    assert first.actions.self_destruct is None
    clear = fly(fighter(notebook=first.notebook, **running), tick=TICK + 16, **HURT)
    assert clear.actions.steer is None and clear.actions.throttle == 0


def test_a_delay_past_55_units_is_warned_of_once_clear_of_the_fight(
    fighter, chaser, fly, caplog
) -> None:
    caplog.set_level(logging.DEBUG)
    fighting = fly(fighter(order_variable1=56), chaser)
    assert fighting.actions.target == CHASER
    assert fighting.actions.self_destruct is None
    letting_go = fly(
        fighter(order_variable1=56, notebook=fighting.notebook), tick=TICK + 16
    )
    assert letting_go.actions.self_destruct is None
    assert "selfdestruct" not in caplog.text
    clear = fly(
        fighter(order_variable1=56, notebook=letting_go.notebook), tick=TICK + 32
    )
    assert clear.actions == stopped()
    assert _own_lines(caplog) == [
        ("WARNING", f"selfdestruct.too_long object={ME} slot=3 units=56")
    ]
    caplog.clear()
    calm = fly(fighter(order_variable1=56, notebook=clear.notebook), tick=TICK + 48)
    assert calm.actions == stopped()
    assert _own_lines(caplog) == []


def test_a_craft_that_warned_of_a_long_delay_still_fights_back(
    fighter, chaser, fly, caplog
) -> None:
    warned = fly(fighter(order_variable1=56))
    caplog.set_level(logging.DEBUG)
    caplog.clear()
    attacked = fly(fighter(order_variable1=56, notebook=warned.notebook), chaser)
    assert attacked.actions.target == CHASER
    assert attacked.actions.self_destruct is None
    assert _own_lines(caplog) == []


def _own_lines(caplog) -> list[tuple[str, str]]:
    """Return the level and message of each selfdestruct line logged."""
    return [
        (record.levelname, record.getMessage())
        for record in caplog.records
        if record.getMessage().startswith("selfdestruct.")
    ]


def test_a_fighting_craft_keeps_its_status_line(fighter, chaser, fly) -> None:
    assert fly(fighter(), chaser).actions.activity == Activity.SELF_DESTRUCTING


def test_another_behavior_gives_the_line_back(view) -> None:
    """A craft still showing "Self-destructing" under a behavior that names no
    line shows none; a line that behavior names, or another left behind,
    stays."""
    quiet = Decision(NOTEBOOK, Actions(throttle=0))
    showing = view(activity=Activity.SELF_DESTRUCTING)
    given_back = Decision(NOTEBOOK, Actions(throttle=0, activity=Activity.NONE))
    assert selfdestruct.ended(showing, quiet) == given_back
    named = Decision(NOTEBOOK, Actions(activity=Activity.GOING_HOME))
    assert selfdestruct.ended(showing, named) == named
    other = view(activity=Activity.KAMIKAZE_ATTACK)
    assert selfdestruct.ended(other, quiet) == quiet


def test_a_homing_shot_closing_makes_it_weave(fighter, fly) -> None:
    decision = fly(aimed(fighter(), (0, -0x1000, 0)))
    assert notebook.decode(decision.notebook).phase == attack.EVADE
    assert decision.actions.weaving and decision.actions.steer is not None
