"""Fence for attack: the target it picks, the cannons' fire and the engine's rules.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target
(``attack_setup``).
"""

from __future__ import annotations

import math
import random

import pytest
from attack_setup import BULK_FREIGHTER_SIZE
from attack_setup import FOE
from attack_setup import FREIGHTER
from attack_setup import ISD_BOX
from attack_setup import ME
from attack_setup import phase_of
from attack_setup import STARSHIP
from attack_setup import TIER_SKILL

from jedimaster_ai import cannons
from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import attack
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.think import think
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question


def test_with_no_target_and_nowhere_to_cruise_it_holds_fire_and_keeps_course(
    craft, ask
) -> None:
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
    assert (actions.lasers[0].burst == cannons.BURST[tier]) is fires


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


# Shot types (object/laser.h): PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and
# _ION_LASER, the ion cannon; 142 and 147 are the ion turbolasers.
LASER = 139
ION_CANNON = 141


def test_an_attack_holds_its_ion_cannons(craft, foe, ask) -> None:
    me = craft(cannon_group_count=2, cannon_type=(ION_CANNON, LASER))
    actions = ask(me, foe()).actions
    assert actions.target == FOE
    assert actions.lasers == (HOLD_FIRE, CannonGroup(LinkMode.PAIRS, 5))


def test_a_rookie_holds_back_its_throttle(craft, foe, ask) -> None:
    assert ask(craft(skill=0), foe(y=0x9000)).actions.throttle == attack.ROOKIE_THROTTLE
    assert ask(craft(), foe(y=0x9000)).actions.throttle == attack.FULL_THROTTLE
    assert ask(craft(), foe(y=0x1800)).actions.throttle == attack.CLOSE_THROTTLE


def test_a_close_target_off_the_nose_is_not_yet_caught(craft, foe, ask) -> None:
    """Two thirds throttle only once the target is close and inside the aim
    cone: 0x1800 away, outside its clearance, and 0x1000 off the nose, the
    craft keeps full throttle."""
    x = round(0x1800 * math.sin(math.pi / 8))
    y = round(0x1800 * math.cos(math.pi / 8))
    assert ask(craft(), foe(x=x, y=y)).actions.throttle == attack.FULL_THROTTLE


# A third of full throttle, where a player's craft turns fastest.
THIRD_THROTTLE = 0x5555


@pytest.mark.parametrize("level", [6, 7])
@pytest.mark.parametrize(
    ("y", "throttle"),
    [(0x3FFF, THIRD_THROTTLE), (0x4000, 0xFFFF), (0x1800, THIRD_THROTTLE)],
    ids=["inside", "at-range", "caught"],
)
def test_a_sith_on_a_fighters_tail_drops_to_a_third(
    craft, foe, ask, level, y, throttle
) -> None:
    """The foe flies away ahead of it: within 0x4000 a Sith craft sits on its
    tail at a third of full throttle, caught or not; at 0x4000 it flies on."""
    assert ask(craft(skill_level=level), foe(y=y)).actions.throttle == throttle


@pytest.mark.parametrize(
    ("changes", "throttle"),
    [
        ({"yaw": 0x1FFF}, THIRD_THROTTLE),
        ({"yaw": 0x2000}, 0xFFFF),
        ({"yaw": 0x8000}, 0xFFFF),
        ({"genus": 1}, 0xFFFF),
        ({"flags": ContactFlags.DISABLED}, 0xFFFF),
    ],
    ids=["behind", "at-the-angle", "head-on", "transport", "disabled"],
)
def test_the_tail_is_a_starfighters_and_behind_it(
    craft, foe, ask, changes, throttle
) -> None:
    """More than 0x6000 off the foe's nose, from where it flies; a transport
    or a disabled fighter is no tail to sit on."""
    target = foe(y=0x3000, **changes)
    assert ask(craft(skill_level=6), target).actions.throttle == throttle


def test_a_jedi_keeps_full_throttle_on_a_tail(craft, foe, ask) -> None:
    assert ask(craft(skill_level=5), foe(y=0x3000)).actions.throttle == 0xFFFF
    assert ask(craft(skill_level=5), foe(y=0x1800)).actions.throttle == 0xAAAA


def test_the_tail_trick_is_logged(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    ask(craft(skill_level=7), foe(y=0x3000))
    expected = f"attack.tail_trick object={ME} target={FOE} distance=12288 angle=32768"
    assert expected in caplog.text


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


@pytest.mark.parametrize(("level", "ticks"), [(6, 125), (7, 64)], ids=["app", "lord"])
def test_a_sith_looks_again_sooner_than_a_jedi(craft, foe, ask, level, ticks) -> None:
    """A Sith Apprentice looks again for a nearer target after 125 ticks, a
    Sith Lord after 64; a Jedi keeps its target for 250."""
    first = ask(craft(skill_level=level), foe(y=0x6000), tick=1000)
    nearer = foe(object_index=15, y=0x2000)

    def target_at(tick: int) -> int:
        me = craft(skill_level=level, notebook=first.notebook, order_target_mask=0b110)
        return ask(me, foe(y=0x6000), nearer, tick=tick).actions.target

    assert target_at(1000 + ticks - 1) == FOE
    assert target_at(1000 + ticks) == 15


@pytest.mark.parametrize(
    ("level", "thinks_at", "asks"),
    [(5, 16, None), (6, 16, None), (7, 16, 8), (7, 8, None)],
    ids=["jedi", "apprentice", "lord-asks", "lord-thinks-at-it"],
)
def test_a_sith_lord_fights_at_8_ticks(craft, foe, ask, level, thinks_at, asks) -> None:
    """A Sith Lord thinks every 8 ticks in a fight; a Jedi and an Apprentice
    every 16. The craft asks only for an interval it is not at already."""
    me = craft(skill_level=level, think_interval=thinks_at)
    assert ask(me, foe()).actions.think_interval == asks


@pytest.mark.parametrize(("fighter_y", "picked"), [(20000, FOE), (9000, 15)])
def test_the_nearest_target_is_measured_to_a_big_ships_hull(
    craft, foe, ask, caplog, fighter_y, picked
) -> None:
    """Ruling 93: a Star Destroyer whose flank lies 10,000 off, its position
    29,140 off, is nearer than a fighter 20,000 off, not than one 9,000 off."""
    caplog.set_level("INFO")
    flank = ISD_BOX["box_max"][0]
    ship = foe(
        y=flank + 10000, yaw=0x4000, pitch=LEVEL, genus=STARSHIP, size=65396, **ISD_BOX
    )
    fighter = foe(object_index=15, y=fighter_y)
    decision = ask(craft(order_target_mask=0b110), ship, fighter)
    assert decision.actions.target == picked
    reach = 10000 if picked == FOE else fighter_y
    assert f"attack.target object={ME} target={picked} distance={reach}" in caplog.text


def test_of_two_targets_as_near_the_lower_index_is_picked(craft, foe, ask) -> None:
    right, left = foe(object_index=15, x=0x3000, y=0), foe(x=-0x3000, y=0)
    decision = ask(craft(order_target_mask=0b110), right, left)
    assert decision.actions.target == FOE


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


@pytest.mark.parametrize("start", range(0, 0x10000, 0x1000))
def test_a_bomber_never_rams_the_freighter_it_attacks(craft, foe, ask, start) -> None:
    """A toy flight from a network game where bombers rammed a freighter 10
    times in 10: per 16-tick think the bomber turns at most 764 yaw units and
    flies 190 units, the freighter flies 48 along +x (as measured in that
    game). The bomber starts 20,000 units out, its nose a 64th of a turn off
    the freighter, and must never come within 3,410 units of it: the
    freighter's farthest corner, 3,069 units from its position, and the
    bomber's own, 341 (test_tactics.py)."""
    turn, pace, freighter_pace = 764, 190, 48
    freighter = {"genus": 3, "size": 4960, "yaw": 0x4000, "speed": 40}
    radians = start * 2 * math.pi / 0x10000
    x, y = round(-20000 * math.sin(radians)), round(-20000 * math.cos(radians))
    yaw = (start + 0x400) % 0x10000
    freighter_x = 0
    book = bytes(64)
    nearest, breaks = 20000, 0
    for step in range(400):
        tick = 1000 + attack.FIGHT_INTERVAL * step
        me = craft(x=x, y=y, yaw=yaw, notebook=book, random_seed=start)
        decision = ask(me, foe(x=freighter_x, y=0, **freighter), tick=tick)
        breaks += phase_of(decision) == attack.BREAK and decision.notebook != book
        book = decision.notebook
        steer = decision.actions.steer
        yaw = (
            yaw + max(-turn, min(turn, (steer.yaw - yaw + 0x8000) % 0x10000 - 0x8000))
        ) % 0x10000
        radians = yaw * 2 * math.pi / 0x10000
        x += round(pace * math.sin(radians))
        y += round(pace * math.cos(radians))
        freighter_x += freighter_pace
        nearest = min(nearest, math.isqrt((x - freighter_x) ** 2 + y * y))
    assert breaks >= 2
    assert nearest > 3069 + 341


def test_a_fighter_keeps_off_a_freighter_pair_12000_apart(craft, foe, shown) -> None:
    """The bomber's toy flight above, against one of two still freighters
    12,000 units apart, through think (the attack, then avoidance), from 32
    starts 20,000 units out. The fighter takes the freighter it targets as
    its own contact's target, as the engine does. It comes no nearer than
    3,964 units to either, outside 3,410, the freighter's corner and the
    fighter's own (3 of 32 starts come inside at 9,000 apart); with
    avoidance's side taken from the behavior's heading (AI5e), 2 starts
    grazed (3,334) and 23 of 32 at 9,000; before the break from a bulky
    target anywhere inside its clearance, 1,784 (15 of 32)."""
    turn, pace, gap = 764, 190, 12000
    freighter = {"genus": FREIGHTER, "size": BULK_FREIGHTER_SIZE, "yaw": 0x4000}
    nearest = 20000
    for start in range(0, 0x10000, 0x800):
        radians = start * 2 * math.pi / 0x10000
        x, y = round(-20000 * math.sin(radians)), round(-20000 * math.cos(radians))
        yaw, book, mine = (start + 0x400) % 0x10000, bytes(64), NO_OBJECT
        for step in range(500):
            me = craft(x=x, y=y, yaw=yaw, notebook=book, random_seed=start)
            pair = (
                foe(x=0, y=0, **freighter),
                foe(object_index=FOE + 1, x=gap, y=0, **freighter),
            )
            question = shown(
                me, *pair, tick=1000 + attack.FIGHT_INTERVAL * step, target=mine
            )
            answer = think(question).craft[0]
            actions, book = answer.actions, answer.notebook
            mine = mine if actions.target is None else actions.target
            yaw = (
                yaw
                + max(
                    -turn,
                    min(turn, (actions.steer.yaw - yaw + 0x8000) % 0x10000 - 0x8000),
                )
            ) % 0x10000
            radians = yaw * 2 * math.pi / 0x10000
            x += round(pace * math.sin(radians))
            y += round(pace * math.cos(radians))
            nearest = min(
                nearest, math.isqrt(x * x + y * y), math.isqrt((x - gap) ** 2 + y * y)
            )
    assert nearest > 3069 + 341


def test_the_decision_is_pure(craft, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    first = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    again = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    assert first == again
