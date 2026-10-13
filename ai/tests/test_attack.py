"""Fence for attack: the dogfight's choices, each in the situation that asks for it.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target.
"""

from __future__ import annotations

import math
import random

import pytest

from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import attack
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.think import think
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

ME = 4
FOE = 9
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
TIER_SKILL = (0x0000, 0x8000, 0xFFFF)


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
            "roll": 0,
            "skill": 0xFFFF,
            "think_interval": attack.FIGHT_INTERVAL,
            "order_code": 7,
            "order_target_mask": 0b10,
            "enemy_mask": ALL_BUT_OWN,
            "turret_slot_mask": 0,
            "cannon_group_count": 1,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    def make(**changes):
        theirs = {
            "object_index": FOE,
            "team": 2,
            "x": 0,
            "y": 0x3000,
            "z": 0,
            "yaw": 0,
            "speed": 0,
            "target": NO_OBJECT,
            "genus": 0,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def ask(contact):
    """Return the craft's decision with its own contact first, then the others."""

    def decide(me, *others, tick=1000):
        own = contact(
            object_index=me.object_index, team=me.team, x=me.x, y=me.y, z=me.z
        )
        return attack.think(me, Question(tick, (me,), (own, *others)))

    return decide


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


def test_with_no_target_it_holds_fire_and_keeps_course(craft, ask) -> None:
    decision = ask(craft(order_target_mask=0, think_interval=59))
    actions = decision.actions
    assert actions.steer is None and actions.throttle is None
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert actions.think_interval == attack.FIGHT_INTERVAL
    assert phase_of(decision) == attack.SEEK


def test_a_lined_up_target_in_range_is_fired_on(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    actions = ask(craft(), foe()).actions
    assert actions.target == FOE
    assert actions.steer.yaw == 0 and actions.steer.pitch == LEVEL
    assert actions.lasers == (CannonGroup(LinkMode.PAIRS, 5), HOLD_FIRE)
    assert actions.think_interval is None
    assert f"attack.target object={ME} target={FOE} distance=12288" in caplog.text


def test_a_target_off_the_nose_is_steered_at_without_firing(craft, foe, ask) -> None:
    actions = ask(craft(), foe(x=0x3000, y=0x3000)).actions
    assert angle_between(actions.steer.yaw, 0x2000) <= 4
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)


@pytest.mark.parametrize(("tier", "fires"), [(0, False), (1, True), (2, True)])
def test_skill_sets_the_firing_range(craft, foe, ask, tier, fires) -> None:
    actions = ask(craft(skill=TIER_SKILL[tier]), foe(y=0x7000)).actions
    assert (actions.lasers[0].burst == attack.BURST[tier]) is fires


@pytest.mark.parametrize(
    ("reach", "mode"),
    [
        (0x1800, LinkMode.ALL),
        (0x3000, LinkMode.PAIRS),
        (0x5000, LinkMode.ONE_AT_A_TIME),
    ],
)
def test_closer_targets_get_more_cannons_at_once(craft, foe, ask, reach, mode) -> None:
    actions = ask(craft(cannon_group_count=2), foe(y=reach)).actions
    assert actions.lasers == (CannonGroup(mode, 5), CannonGroup(mode, 5))


def test_the_1997_range_changes_apply(craft, foe) -> None:
    me = craft(skill=0xFFFF)
    assert attack.fire_range(me, foe(speed=10), 2) == 0xA000
    assert attack.fire_range(me, foe(speed=40, yaw=0x1000), 2) == 0x6000
    assert attack.fire_range(me, foe(speed=40, yaw=0x4800), 2) == 0x8000
    assert attack.fire_range(me, foe(speed=40, yaw=0x8000), 2) == 0xA000
    assert attack.fire_range(me, foe(genus=4), 2) == 0x10000


def test_a_rookie_holds_back_its_throttle(craft, foe, ask) -> None:
    assert ask(craft(skill=0), foe(y=0x9000)).actions.throttle == attack.ROOKIE_THROTTLE
    assert ask(craft(), foe(y=0x9000)).actions.throttle == attack.FULL_THROTTLE
    assert ask(craft(), foe(y=0x1800)).actions.throttle == attack.CLOSE_THROTTLE


def test_too_close_ahead_it_breaks_off_and_comes_round(craft, foe, ask) -> None:
    first = ask(craft(), foe(y=0x800), tick=1000)
    assert phase_of(first) == attack.BREAK
    turned = first.actions.steer.yaw
    assert turned in (0x4000, 0xC000)
    assert first.actions.throttle == attack.FULL_THROTTLE
    assert first.actions.lasers == (HOLD_FIRE, HOLD_FIRE)

    held = ask(craft(notebook=first.notebook, yaw=0x2000), foe(y=0x2000), tick=1100)
    assert held.actions.steer.yaw == turned

    again = ask(
        craft(notebook=held.notebook), foe(y=0x3000), tick=1000 + attack.BREAK_TICKS
    )
    assert phase_of(again) == attack.ATTACK
    assert again.actions.steer.yaw == 0


def test_an_enemy_on_the_tail_makes_it_jink(craft, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    jink = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    assert phase_of(jink) == attack.EVADE
    swing = angle_between(jink.actions.steer.yaw, 0)
    assert (
        attack.JINK_YAW_LEAST <= swing < attack.JINK_YAW_LEAST + attack.JINK_YAW_SPREAD
    )
    assert angle_between(jink.actions.steer.pitch, LEVEL) <= attack.JINK_PITCH_SPREAD
    assert jink.actions.target == FOE


def test_a_lined_up_shot_is_taken_even_with_an_enemy_on_the_tail(
    craft, foe, ask
) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    actions = ask(craft(), foe(), chaser).actions
    assert actions.lasers[0].burst == 5


def test_an_enemy_ahead_or_not_targeting_it_is_no_tail_threat(craft, foe) -> None:
    me = craft()
    question = Question(1000, (me,), (foe(y=0x2000, target=ME), foe(y=-0x2000)))
    assert attack.tail_threat(me, question) is None


def test_only_a_chaser_the_engine_marks_hostile_is_a_tail_threat(craft, foe) -> None:
    contacts = (foe(object_index=12, y=-0x2000, target=ME), foe(y=0x2000))
    hostile = craft(enemy_mask=0b01)
    friendly = craft(enemy_mask=0b10)
    assert (
        attack.tail_threat(hostile, Question(1000, (hostile,), contacts)) == contacts[0]
    )
    assert attack.tail_threat(friendly, Question(1000, (friendly,), contacts)) is None


def test_it_keeps_its_target_until_it_looks_again(craft, foe, ask) -> None:
    first = ask(craft(order_target_mask=0b10), foe(y=0x6000), tick=1000)
    nearer = foe(object_index=15, y=0x2000)
    mask = 0b110
    kept = ask(
        craft(notebook=first.notebook, order_target_mask=mask),
        foe(y=0x6000),
        nearer,
        tick=1100,
    )
    assert kept.actions.target == FOE
    later = 1000 + attack.LOOK_AGAIN_TICKS[2]
    moved = ask(
        craft(notebook=kept.notebook, order_target_mask=mask),
        foe(y=0x6000),
        nearer,
        tick=later,
    )
    assert moved.actions.target == 15


def test_a_target_gone_is_replaced_at_once(craft, foe, ask) -> None:
    first = ask(craft(), foe())
    second = ask(
        craft(notebook=first.notebook), foe(object_index=15, y=0x6000), tick=1016
    )
    assert second.actions.target == 15


def test_a_new_craft_in_its_targets_slot_is_a_new_target(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    first = ask(craft(), foe())
    caplog.clear()
    ask(craft(notebook=first.notebook), foe(), tick=1016)
    assert "attack.target" not in caplog.text
    ask(craft(notebook=first.notebook), foe(object_signature=99), tick=1016)
    assert f"attack.target object={ME} target={FOE}" in caplog.text


@pytest.mark.parametrize("kind", [3, 4, 5, 6])
def test_a_craft_breaking_up_or_in_hyperspace_is_not_fought(
    craft, foe, ask, kind
) -> None:
    assert ask(craft(), foe(object_kind=kind)).actions.target == NO_OBJECT


def test_a_craft_the_engine_does_not_mark_hostile_is_never_fought(
    craft, foe, ask
) -> None:
    assert ask(craft(enemy_mask=0), foe()).actions.target == NO_OBJECT


def random_question(
    dice: random.Random, tick: int, notebooks: dict, contact, view
) -> Question:
    count = dice.randint(1, 9)
    spread = dice.choice((0x800, 0x8000))
    others = tuple(
        contact(
            object_index=index,
            object_signature=dice.randrange(3),
            team=dice.randrange(3),
            genus=dice.choice((0, 4, 5)),
            object_kind=dice.choice((0, 0, 0, 2, 3, 4)),
            x=dice.randint(-spread, spread),
            y=dice.randint(-spread, spread),
            z=dice.randint(-spread // 4, spread // 4),
            yaw=dice.randrange(0x10000),
            speed=dice.randrange(60),
            target=dice.choice((ME, NO_OBJECT, 1)),
            last_attacker=dice.choice((NO_OBJECT, 1, 2)),
        )
        for index in range(1, count)
    )
    # The engine marks other teams hostile when the mission allies none.
    hostile = sum(1 << c.object_index for c in others if c.team != 1)
    me = view(
        object_index=ME,
        team=1,
        order_code=7,
        skill=dice.randrange(0x10000),
        x=0,
        y=0,
        z=0,
        yaw=dice.randrange(0x10000),
        pitch=dice.randrange(0x8001),
        cannon_group_count=dice.randint(0, 2),
        order_target_mask=dice.getrandbits(count),
        enemy_mask=hostile,
        turret_slot_mask=dice.getrandbits(16),
        think_interval=dice.choice((16, 59)),
        notebook=notebooks.get(ME, bytes(64)),
    )
    own = contact(
        object_index=ME,
        team=1,
        x=0,
        y=0,
        z=0,
        last_attacker=dice.choice((NO_OBJECT, 2)),
    )
    return Question(tick, (me,), (own, *others))


def test_every_answer_keeps_the_engines_rules(contact, view) -> None:
    dice = random.Random(97)
    phases = set()
    for _ in range(60):
        notebooks: dict = {}
        for step in range(15):
            question = random_question(dice, 1000 + 16 * step, notebooks, contact, view)
            answer = think(question)
            for asked, said in zip(question.craft, answer.craft, strict=True):
                wire.check_shown(said.actions, asked, question)
                notebooks[asked.object_index] = said.notebook
                phases.add(notebook.decode(said.notebook).phase)
            assert wire.read_answer(wire.answer_bytes(answer)[4:], question) == answer
            assert think(question) == answer
    assert phases == {attack.SEEK, attack.ATTACK, attack.BREAK, attack.EVADE}


def test_an_attacker_runs_down_a_fleeing_target_and_fires(craft, foe, ask) -> None:
    """A toy flight: the craft turns at most TURN per think toward its steer
    and flies 300 units a think; the target flies straight along +x at 100."""
    turn = 0x300
    x = y = z = 0
    yaw, pitch = 0x8000, LEVEL
    target_x, target_y = 0x6000, 0x6000
    book = bytes(64)
    for step in range(200):
        tick = 1000 + attack.FIGHT_INTERVAL * step
        me = craft(x=x, y=y, z=z, yaw=yaw, pitch=pitch, notebook=book)
        decision = ask(me, foe(x=target_x, y=target_y, yaw=0x4000, speed=20), tick=tick)
        book = decision.notebook
        if decision.actions.lasers[0].burst:
            break
        steer = decision.actions.steer
        if steer is not None:
            yaw = (
                yaw
                + max(-turn, min(turn, (steer.yaw - yaw + 0x8000) % 0x10000 - 0x8000))
            ) % 0x10000
            pitch += max(-turn, min(turn, steer.pitch - pitch))
        radians = yaw * 2 * math.pi / 0x10000
        x += round(300 * math.sin(radians))
        y += round(300 * math.cos(radians))
        target_x += 100
    else:
        pytest.fail("the attacker never fired")
    assert step > 5


def test_the_decision_is_pure(craft, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    first = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    again = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    assert first == again
