"""Fence for wait: a craft a player told to wait stops, defends itself, and
gets its activity back once released.

The craft flies at the origin facing +y, level, at full skill, its wait
command standing; contact 0 is its own.
"""

from __future__ import annotations

import pytest
from shot_setup import aimed

from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import wait
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Commands
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

ME = 4
CHASER = 12
# A hull below three quarters of its strength: the craft may not fight back.
HURT = {"hull_strength": 400, "hull_damage": 101}


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
            "commands": Commands.WAIT,
            "enemy_mask": 0xFFFFFFFE,
            "cannon_group_count": 1,
            "notebook": bytes(range(len(view().notebook))),
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def chaser(contact):
    """An enemy fighter on the craft's tail, targeting it."""
    return contact(object_index=CHASER, team=2, x=0, y=-0x2000, z=0, target=ME, genus=0)


@pytest.fixture
def ask(contact):
    def decide(me, *others, tick=1000, **mine):
        own = contact(object_index=ME, team=1, x=0, y=0, z=0, **mine)
        return wait.think(me, Question(tick, (me,), (own, *others)))

    return decide


def test_a_waiting_craft_stops_and_leaves_its_notebook(craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    me = craft()
    decision = ask(me)
    assert decision.actions == Actions(
        throttle=0,
        activity=Activity.WAITING_FOR_GO,
        target=NO_OBJECT,
        lasers=(HOLD_FIRE, HOLD_FIRE),
    )
    assert decision.notebook == me.notebook
    assert f"wait.start object={ME}" in caplog.text


def test_a_craft_already_waiting_logs_no_new_start(craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    ask(craft(activity=Activity.WAITING_FOR_GO))
    assert "wait.start" not in caplog.text


def test_a_waiting_craft_fights_the_enemy_after_it(craft, chaser, ask) -> None:
    actions = ask(craft(), chaser).actions
    assert actions.activity == Activity.WAITING_FOR_GO
    assert actions.steer is not None and actions.throttle != 0
    assert actions.target == CHASER


def test_a_hurt_waiting_craft_weaves_then_stops_again(craft, chaser, ask) -> None:
    first = ask(craft(), chaser, **HURT)
    assert notebook.decode(first.notebook).phase == attack.EVADE
    assert first.actions.throttle == attack.FULL_THROTTLE
    assert first.actions.target == NO_OBJECT
    assert first.actions.activity == Activity.WAITING_FOR_GO
    clear = ask(craft(notebook=first.notebook), tick=1016, **HURT)
    assert clear.actions.steer is None and clear.actions.throttle == 0
    assert clear.actions.activity == Activity.WAITING_FOR_GO


def test_a_craft_told_to_wait_mid_evade_flies_its_weave_out(
    craft, contact, ask
) -> None:
    """An evade command's weave under way when the wait begins flies on,
    with no chaser; the craft stops once it ends."""
    own = contact(object_index=ME, team=1, x=0, y=0, z=0)
    evading = attack.think(
        craft(commands=Commands.EVADE), Question(1000, (craft(),), (own,))
    )
    held = ask(craft(notebook=evading.notebook), tick=1016)
    assert held.actions.steer is not None
    assert held.actions.throttle == attack.FULL_THROTTLE
    assert held.actions.activity == Activity.WAITING_FOR_GO


def test_released_a_craft_gets_activity_none(craft, caplog) -> None:
    caplog.set_level("INFO")
    plain = Decision(b"", Actions(throttle=5))
    was = craft(commands=0, activity=Activity.WAITING_FOR_GO)
    assert wait.ended(was, plain).actions.activity == Activity.NONE
    assert f"wait.end object={ME}" in caplog.text


def test_released_a_craft_keeps_an_activity_its_behavior_names(craft) -> None:
    going = Decision(b"", Actions(activity=Activity.GOING_HOME))
    was = craft(commands=0, activity=Activity.WAITING_FOR_GO)
    assert wait.ended(was, going) == going
    plain = Decision(b"", Actions(throttle=5))
    assert wait.ended(craft(commands=0), plain) == plain


def test_a_homing_shot_closing_makes_it_weave_then_stop_again(craft, ask) -> None:
    first = ask(aimed(craft(), (0, -0x1000, 0)))
    assert notebook.decode(first.notebook).phase == attack.EVADE
    assert first.actions.weaving
    assert first.actions.activity == Activity.WAITING_FOR_GO
    clear = ask(craft(notebook=first.notebook), tick=1016)
    assert clear.actions.steer is None and clear.actions.throttle == 0
