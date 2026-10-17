"""Fence for attack: the weave from an enemy on the tail, and a player's commands.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target
(``attack_setup``).
"""

from __future__ import annotations

from attack_setup import BULK_FREIGHTER_SIZE
from attack_setup import FOE
from attack_setup import FREIGHTER
from attack_setup import ISD_BOX
from attack_setup import ME
from attack_setup import phase_of
from attack_setup import STARSHIP

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import weave
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import Commands
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer


def test_an_enemy_on_the_tail_makes_it_weave(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    first = ask(craft(), foe(x=0x4000, y=0x1000), chaser)
    assert phase_of(first) == attack.EVADE
    actions = first.actions
    assert actions.steer.yaw in (0x3000, 0xD000)
    assert actions.steer.roll == (0x4000 if actions.steer.yaw == 0x3000 else 0xC000)
    assert angle_between(actions.steer.pitch, LEVEL) <= 0x1800
    assert actions.target == FOE and actions.throttle == 0xFFFF
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert f"weave.start object={ME} threat=12 course=0 " in caplog.text


def test_a_weave_steers_clear_of_its_own_bulky_target(craft, foe, ask) -> None:
    """The freighter it attacks lies 10,000 units off, 0x3000 to the right,
    outside its clearance but within the look past it, where the weave's
    first swerve points (dice seed 1). The swerve turns past the clearance's
    edge, on the nose's side, at the start and at the next think; avoidance
    alone would skip the target (the Y-wing in raw10)."""
    freighter = foe(x=9239, y=3827, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    toward = geometry.bearing(9239, 3827, 0)[0]
    reach = geometry.distance(9239, 3827, 0)
    clear = (
        toward
        - avoidance.edge_angle(reach, avoidance.tactics.clearance(freighter))
        - avoidance.EDGE_PAD
    )
    first = ask(craft(random_seed=1), freighter, chaser)
    assert phase_of(first) == attack.EVADE
    assert weave_of(first).swerves == 1
    assert first.actions.steer.yaw == clear
    then = ask(
        craft(random_seed=1, notebook=first.notebook), freighter, chaser, tick=1016
    )
    assert phase_of(then) == attack.EVADE
    assert then.actions.steer.yaw == clear


def test_a_weave_over_a_big_ships_deck_levels_off(craft, foe, ask, contact) -> None:
    """6,000 over a Star Destroyer's deck, the weave's first swerve (dice
    seed 1) dives at pitch 20,662, into the keep: the pitches into it run
    24,771 and 7,997 either side, so the swerve keeps its yaw and takes
    24,771 - 7,997 - 1,024 = 15,750 (``avoidance.clear_heading``)."""
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    target = foe(x=0x4000, y=0x1000)
    alone = ask(craft(random_seed=1), target, chaser).actions.steer
    assert alone.pitch == 20662
    deck = contact(
        object_index=20,
        team=1,
        genus=STARSHIP,
        size=65396,
        x=0,
        y=0,
        z=-(ISD_BOX["box_max"][2] + 6000),
        yaw=0,
        pitch=LEVEL,
        roll=0,
        target=NO_OBJECT,
        **ISD_BOX,
    )
    over = ask(craft(random_seed=1), target, chaser, deck)
    assert phase_of(over) == attack.EVADE
    assert over.actions.steer == Steer(alone.yaw, 15750, alone.roll)


def test_a_weave_flies_on_and_ends_when_the_tail_is_clear(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    target, chaser = foe(x=0x4000, y=0x1000), foe(object_index=12, y=-0x2000, target=ME)
    first = ask(craft(), target, chaser, tick=1000)
    held = ask(craft(notebook=first.notebook), target, chaser, tick=1016)
    assert phase_of(held) == attack.EVADE
    assert held.actions.steer == first.actions.steer
    assert held.actions.target is None
    caplog.clear()
    out = ask(craft(notebook=held.notebook), target, tick=1032)
    assert phase_of(out) == attack.ATTACK
    assert out.actions.steer.yaw == 0x3604 and out.actions.target == FOE
    assert f'weave.end object={ME} reason="clear" swerves=1 rest_until=0' in (
        caplog.text
    )


def test_every_answer_of_a_weave_says_weaving_until_the_tail_clears(
    craft, foe, ask
) -> None:
    """The engine moves a weaving craft's shield power to its engines while
    each answer carries the flag, and stops at the first answer without it."""
    target, chaser = foe(x=0x4000, y=0x1000), foe(object_index=12, y=-0x2000, target=ME)
    first = ask(craft(), target, chaser, tick=1000)
    held = ask(craft(notebook=first.notebook), target, chaser, tick=1016)
    assert phase_of(first) == phase_of(held) == attack.EVADE
    assert first.actions.weaving and held.actions.weaving
    out = ask(craft(notebook=held.notebook), target, tick=1032)
    assert phase_of(out) == attack.ATTACK and not out.actions.weaving


def weave_of(decision) -> weave.Weave:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch).weave


def test_an_evade_with_no_weave_under_way_starts_one_on_its_course(
    craft, foe, ask, caplog
) -> None:
    """A notebook saved in the evade phase before the weave, its scratch
    holding no weave, starts a weave on the craft's own course."""
    caplog.set_level("DEBUG")
    old = notebook.Notebook(attack.CODE, attack.EVADE, 77, 990).encode()
    chaser = foe(object_index=12, x=-0x600, y=-0x2000, target=ME)
    decision = ask(craft(yaw=0x1000, notebook=old), foe(x=0x4000, y=0x1000), chaser)
    assert weave_of(decision).course == 0x1000
    assert f"weave.start object={ME} threat=12 course=4096 " in caplog.text


def test_after_a_full_weave_it_fights_before_weaving_again(craft, foe, ask) -> None:
    target, chaser = foe(x=0x4000, y=0x1000), foe(object_index=12, y=-0x2000, target=ME)
    decision, tick, phases = None, 1000, []
    while len(phases) < 400:
        me = craft() if decision is None else craft(notebook=decision.notebook)
        decision = ask(me, target, chaser, tick=tick)
        phases.append((tick, phase_of(decision)))
        tick += 16
    evading = [at for at, phase in phases if phase == attack.EVADE]
    first_end = next(at for at, phase in phases if phase == attack.ATTACK)
    assert 1000 + 1416 <= first_end < 1000 + 1886 + 16
    again = next(at for at in evading if at > first_end)
    assert first_end + 1886 <= again < first_end + 1886 + 16
    assert all(
        phase == attack.ATTACK for at, phase in phases if first_end <= at < again
    )


def test_each_swerve_moves_the_craft_dice_on(craft, foe, ask) -> None:
    target, chaser = foe(x=0x4000, y=0x1000), foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(), target, chaser, tick=1000)
    swerves = 0
    for tick in range(1016, 2400, 16):
        before = notebook.decode(decision.notebook).dice
        yaw = decision.actions.steer.yaw
        decision = ask(craft(notebook=decision.notebook), target, chaser, tick=tick)
        after = notebook.decode(decision.notebook).dice
        if decision.actions.steer.yaw == yaw:
            assert after == before
        else:
            swerves += 1
            assert after != before
    assert swerves == 2


def test_a_weaving_top_pilot_drops_its_flare(craft, foe, ask) -> None:
    target, chaser = foe(x=0x4000, y=0x1000), foe(object_index=12, y=-0x2000, target=ME)
    ready = {
        "countermeasure": 2,
        "countermeasures_left": 4,
        "countermeasure_working": 1,
    }
    flares = {}
    for skill in (0xFFFF, 0x8000):
        flares[skill] = 0
        for seed in range(40):
            decision = None
            for step in range(120):
                me = craft(skill=skill, random_seed=seed, **ready)
                if decision is not None:
                    me = craft(
                        skill=skill,
                        random_seed=seed,
                        notebook=decision.notebook,
                        **ready,
                    )
                decision = ask(me, target, chaser, tick=1000 + 16 * step)
                flares[skill] += decision.actions.countermeasure
    assert flares[0xFFFF] > 0 and flares[0x8000] == 0


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


# ---- A player's commands ------------------------------------------------

OTHER = 15


def test_a_commanded_target_comes_before_a_nearer_order_target(
    craft, foe, ask, caplog
) -> None:
    """The craft an attack or cover order named is fought first, though the
    order names a nearer one: 1997's target search took the candidate first."""
    caplog.set_level("INFO")
    named = foe(object_index=OTHER, y=0x6000)
    decision = ask(craft(command_target=OTHER), foe(y=0x2000), named)
    assert decision.actions.target == OTHER
    assert f"attack.target object={ME} target={OTHER} distance=24576" in caplog.text


def test_a_command_target_it_may_not_fight_changes_nothing(craft, foe, ask) -> None:
    """A friendly craft (a supply call names the player's own), a craft not in
    the battle, or none: the order's target is fought."""
    friend = foe(object_index=OTHER, team=1, y=0x1000)
    for command_target in (OTHER, 77, NO_OBJECT):
        me = craft(command_target=command_target, enemy_mask=0b10)
        assert ask(me, foe(y=0x2000), friend).actions.target == FOE


def test_an_ignored_craft_is_never_picked(craft, foe, ask) -> None:
    far = foe(object_index=OTHER, y=0x6000)
    two = ask(craft(order_target_mask=0b110, avoid_target=FOE), foe(y=0x2000), far)
    assert two.actions.target == OTHER
    alone = ask(craft(avoid_target=FOE), foe(y=0x2000))
    assert alone.actions.target == NO_OBJECT


def test_an_evade_command_drops_the_target_and_weaves(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(commands=Commands.EVADE), foe(y=0x2000), chaser)
    actions = decision.actions
    assert actions.evaded and actions.target == NO_OBJECT
    assert actions.steer.yaw in (0x3000, 0xD000) and actions.throttle == 0xFFFF
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert phase_of(decision) == attack.EVADE
    memory = attack.Memory.read(notebook.decode(decision.notebook).scratch)
    assert (memory.ordered, memory.target) == (1, NO_OBJECT)
    assert f"attack.evade_ordered object={ME} chaser=12" in caplog.text


def test_a_sith_lord_ordered_to_evade_thinks_every_8_ticks(craft, foe, ask) -> None:
    lord = craft(commands=Commands.EVADE, skill_level=7)
    assert ask(lord, foe(y=0x2000)).actions.think_interval == 8


def test_an_ordered_weave_flies_every_swerve_with_no_chaser(
    craft, foe, ask, caplog
) -> None:
    """With nothing on its tail the weave still flies its three swerves, then
    ends clear, with no rest, and the craft fights its target again."""
    caplog.set_level("DEBUG")
    target = foe(x=0x4000, y=0x1000)
    decision = ask(craft(commands=Commands.EVADE), target, tick=1000)
    tick, swerves, last_ends = 1000, set(), 0
    while phase_of(decision) == attack.EVADE and tick < 5000:
        swerves.add(weave_of(decision).swerves)
        if weave_of(decision).swerves == weave.SWERVES_MAX:
            last_ends = weave_of(decision).swerve_ends
        tick += attack.FIGHT_INTERVAL
        decision = ask(craft(notebook=decision.notebook), target, tick=tick)
    assert swerves == {1, 2, weave.SWERVES_MAX}
    assert tick >= last_ends > 0
    assert phase_of(decision) == attack.ATTACK and decision.actions.target == FOE
    memory = attack.Memory.read(notebook.decode(decision.notebook).scratch)
    assert memory.ordered == 0
    assert f'reason="clear" swerves={weave.SWERVES_MAX} rest_until=0' in caplog.text


def test_an_evade_command_says_weaving_on_every_swerve_and_not_after(
    craft, foe, ask
) -> None:
    target = foe(x=0x4000, y=0x1000)
    decision = ask(craft(commands=Commands.EVADE), target, tick=1000)
    assert decision.actions.weaving and decision.actions.evaded
    tick, flags = 1000, []
    while phase_of(decision) == attack.EVADE and tick < 5000:
        flags.append(decision.actions.weaving)
        tick += attack.FIGHT_INTERVAL
        decision = ask(craft(notebook=decision.notebook), target, tick=tick)
    assert len(flags) > 1 and all(flags)
    assert phase_of(decision) == attack.ATTACK and not decision.actions.weaving


def test_the_memory_keeps_the_ordered_weave() -> None:
    memory = attack.Memory(target=9, signature=11, look_again=5, ordered=1)
    assert attack.Memory.read(memory.write()) == memory


def test_weaving_reads_the_notebook(craft, foe, ask) -> None:
    evading = ask(craft(commands=Commands.EVADE), foe(y=0x2000))
    fighting = ask(craft(), foe(y=0x2000))
    assert attack.weaving(craft(notebook=evading.notebook))
    assert not attack.weaving(craft(notebook=fighting.notebook))
    assert not attack.weaving(craft())
    flying = attack.Memory(weave=weave.Weave(swerves=1)).write()
    other = notebook.Notebook(BehaviorCode.HOME, attack.EVADE, 77, 990, flying)
    assert not attack.weaving(craft(notebook=other.encode()))
