"""Fence for attack: the dogfight's choices, each in the situation that asks for it.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target.
"""

from __future__ import annotations

import math
import random

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai import weave
from jedimaster_ai import wire
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.think import think
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Commands
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import Lock
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Warhead

ME = 4
FOE = 9
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
FREIGHTER = 3
BULK_FREIGHTER_SIZE = 4960
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
def shown(contact):
    """Return the question showing the craft's own contact first, its fields
    changed as named, then the others."""

    def make(me, *others, tick=1000, **mine):
        fields = {
            "object_index": me.object_index,
            "team": me.team,
            "x": me.x,
            "y": me.y,
            "z": me.z,
        }
        return Question(tick, (me,), (contact(**(fields | mine)), *others))

    return make


@pytest.fixture
def ask(shown):
    """Return the craft's decision with its own contact first, then the others."""

    def decide(me, *others, tick=1000, **mine):
        return attack.think(me, shown(me, *others, tick=tick, **mine))

    return decide


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


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


# Shot types (object/laser.h): PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and
# _ION_LASER, the ion cannon; 142 and 147 are the ion turbolasers.
LASER = 139
ION_CANNON = 141


@pytest.mark.parametrize(
    ("kinds", "disabling", "fired"),
    [
        ((LASER, ION_CANNON), False, (True, False)),
        ((LASER, ION_CANNON), True, (False, True)),
        ((ION_CANNON, ION_CANNON), False, (False, False)),
        ((142, 147), False, (True, True)),
        ((142, 147), True, (False, False)),
    ],
)
def test_ion_cannons_fire_only_for_a_disable_order_and_alone(
    craft, kinds, disabling, fired
) -> None:
    """1997's rule: the ion cannon fires at a Disable order's target, where
    every other cannon holds; the ion turbolasers fire as other cannons do."""
    me = craft(cannon_group_count=2, cannon_type=kinds)
    burst = CannonGroup(LinkMode.ALL, 5)
    lasers = attack.cannon_fire(me, 0x1800, 2, disabling)
    assert lasers == tuple(burst if fires else HOLD_FIRE for fires in fired)


def test_an_attack_holds_its_ion_cannons(craft, foe, ask) -> None:
    me = craft(cannon_group_count=2, cannon_type=(ION_CANNON, LASER))
    actions = ask(me, foe()).actions
    assert actions.target == FOE
    assert actions.lasers == (HOLD_FIRE, CannonGroup(LinkMode.PAIRS, 5))


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


def at(yaw: int, reach: int) -> dict:
    """Return the x and y of a point reach away from the origin along a yaw."""
    radians = yaw * 2 * math.pi / 0x10000
    return {
        "x": round(reach * math.sin(radians)),
        "y": round(reach * math.cos(radians)),
    }


@pytest.mark.parametrize(
    ("heading", "toward", "held"),
    [
        (0x0000, 0x0A00, 0xC000),
        (0x0000, 0xF600, 0x4000),
        (0x7000, 0x7400, 0x3000),
        (0x1000, 0x0400, 0x5000),
        (0xF800, 0x0400, 0xB800),
    ],
)
def test_too_close_ahead_it_breaks_off_away_from_the_target(
    craft, foe, ask, heading, toward, held
) -> None:
    first = ask(craft(yaw=heading), foe(**at(toward, 0x800)), tick=1000)
    assert phase_of(first) == attack.BREAK
    assert first.actions.steer.yaw == held
    assert first.actions.steer.pitch == LEVEL
    assert first.actions.throttle == attack.FULL_THROTTLE
    assert first.actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_only_a_target_dead_ahead_lets_the_dice_pick_the_side(craft, foe, ask) -> None:
    assert attack.break_heading(0x1000, 0x1005, 77) == (0xD000, 77)
    assert attack.break_heading(0x1000, 0x0FFB, 77) == (0x5000, 77)
    for toward in (0x0FFC, 0x1000, 0x1004):
        held, state = attack.break_heading(0x1000, toward, 77)
        assert held in (0x5000, 0xD000) and state != 77
    sides = {
        ask(craft(random_seed=seed), foe(y=0x800)).actions.steer.yaw
        for seed in range(16)
    }
    assert sides == {0x4000, 0xC000}


def test_a_break_holds_until_the_craft_is_outside_the_clearance(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    held = ask(craft(notebook=first.notebook), foe(x=0x200, y=0xF00), tick=1700)
    assert held.notebook == first.notebook
    assert held.actions.steer.yaw == 0xC000
    assert held.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    caplog.clear()
    out = ask(craft(notebook=held.notebook), foe(y=0x1000), tick=1716)
    assert phase_of(out) == attack.ATTACK
    assert out.actions.steer.yaw == 0
    assert (
        f'attack.break_end object={ME} target={FOE} reason="outside" ticks=716 '
        "distance=4096 clearance=4096"
    ) in caplog.text


def test_a_break_ends_at_its_limit_even_inside_the_clearance(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    held = ask(craft(notebook=first.notebook), foe(y=-0x800), tick=1999)
    assert held.actions.steer.yaw == 0xC000
    caplog.clear()
    ended = ask(craft(notebook=first.notebook), foe(y=-0x800), tick=2000)
    assert phase_of(ended) == attack.ATTACK
    assert ended.actions.steer.yaw == 0x8000
    assert (
        f'attack.break_end object={ME} target={FOE} reason="limit" ticks=1000 '
        "distance=2048 clearance=4096"
    ) in caplog.text


def test_a_break_ends_when_its_target_is_gone(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    caplog.clear()
    gone = ask(craft(notebook=first.notebook), tick=1100)
    assert phase_of(gone) == attack.SEEK
    assert f"attack.break_lost object={ME} target={FOE} ticks=100" in caplog.text


def test_it_breaks_off_from_a_cruiser_at_its_clearance(craft, foe, ask) -> None:
    # The retail cruiser model's longest side (test_tactics.py).
    cruiser = {"genus": 4, "size": 18449}
    keep = 18449 * 3 // 4 + 0x1000
    assert phase_of(ask(craft(), foe(y=keep - 1, **cruiser))) == attack.BREAK
    assert phase_of(ask(craft(), foe(y=keep, **cruiser))) == attack.ATTACK


@pytest.mark.parametrize(
    ("toward", "held"), [(0x4000, 0xC000), (0xC000, 0x4000), (0xA000, 0x4000)]
)
def test_inside_a_bulky_targets_clearance_it_breaks_wherever_the_target_lies(
    craft, foe, ask, toward, held
) -> None:
    freighter = foe(**at(toward, 3000), genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    decision = ask(craft(), freighter)
    assert phase_of(decision) == attack.BREAK
    assert decision.actions.steer == Steer(held, LEVEL, 0)
    fighter = foe(**at(toward, 3000))
    assert phase_of(ask(craft(), fighter)) == attack.ATTACK


def test_a_break_another_freighters_clearance_blocks_heads_away_from_both(
    craft, foe, ask, caplog
) -> None:
    """The target behind on the left makes the usual break a turn to +x,
    where a second freighter sits 6,184 units off, behind on the right and a
    little above, inside its clearance; the way out, away from both, turns
    a little right, across neither, and dives a little."""
    target = foe(x=-2560, y=-451, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    other = foe(
        object_index=FOE + 1,
        x=3000,
        y=-5196,
        z=1500,
        genus=FREIGHTER,
        size=BULK_FREIGHTER_SIZE,
    )
    me = craft()
    assert attack.break_heading(0, geometry.bearing(-2560, -451, 0)[0], 0)[0] == 0x4000
    caplog.set_level("DEBUG")
    decision = ask(me, target, other)
    assert phase_of(decision) == attack.BREAK
    yaw, pitch = attack.escape(me, target, other)
    assert decision.actions.steer == Steer(yaw, pitch, 0)
    assert pitch > LEVEL
    for contact in (target, other):
        toward = geometry.bearing(contact.x, contact.y, 0)[0]
        assert geometry.angle_between(yaw, toward) > geometry.QUARTER_TURN
    assert f"attack.break object={ME} target={FOE} " in caplog.text
    assert f"blocked={FOE + 1}" in caplog.text
    alone = ask(me, target)
    assert alone.actions.steer == Steer(0x4000, LEVEL, 0)
    assert "blocked=65535" in caplog.text


@pytest.mark.parametrize(
    ("target_at", "other_at", "held", "crossed"),
    [
        ((-2000, 2000, 0), (6000, 1000, 1500), 0x4000, "target"),
        ((6568, 4104, 0), (-5426, 4553, 0), 0xC000, "other"),
    ],
)
def test_an_escape_that_swings_the_nose_across_either_keeps_the_quarter_turn(
    craft, foe, ask, caplog, target_at, other_at, held, crossed
) -> None:
    """Away from both lies nearly behind, and the shortest turn there swings
    the nose across one of them: the target, ahead on the left with the
    other off on the right and a little above (AI5e's network bomber); or
    the other, ahead on the left with the target ahead on the right, the
    craft flying into the gap between them (the toy's column 12,000). The
    break keeps its quarter turn away from the target, and no craft is
    named as blocking it."""
    shape = {"genus": FREIGHTER, "size": BULK_FREIGHTER_SIZE}
    target = foe(**dict(zip("xyz", target_at, strict=True)), **shape)
    other = foe(
        object_index=FOE + 1, **dict(zip("xyz", other_at, strict=True)), **shape
    )
    me = craft()
    yaw, _ = attack.escape(me, target, other)
    swept = {"target": target_at, "other": other_at}[crossed]
    assert geometry.sweeps(0, yaw, geometry.bearing(*swept)[0])
    caplog.set_level("DEBUG")
    decision = ask(me, target, other)
    assert phase_of(decision) == attack.BREAK
    assert decision.actions.steer == Steer(held, LEVEL, 0)
    assert "blocked=65535" in caplog.text


def test_escape_points_away_from_both_unless_nearly_in_line(craft, foe) -> None:
    me = craft()
    north, south = foe(x=0, y=3000), foe(x=0, y=-3000)
    assert attack.escape(me, north, south) is None
    east = foe(x=3000, y=0)
    yaw, pitch = attack.escape(me, north, east)
    assert geometry.angle_between(yaw, 0xA000) <= 4
    assert pitch == LEVEL
    above = foe(x=0, y=0, z=3000)
    assert attack.escape(me, north, above)[1] > LEVEL


@pytest.mark.parametrize("tier", range(3))
def test_every_tier_fires_on_a_mon_cal_from_outside_its_clearance(
    craft, foe, ask, tier
) -> None:
    keep = 53188 * 3 // 4 + 0x1000
    decision = ask(craft(skill=TIER_SKILL[tier]), foe(y=keep, genus=4, size=53188))
    assert phase_of(decision) == attack.ATTACK
    assert decision.actions.lasers != (HOLD_FIRE, HOLD_FIRE)


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
    alone would skip the target (AI5e's Y-wing in raw10)."""
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


# Rulings 34, 42 and 43: with no order target in the battle, the enemy after
# the craft. HURT leaves 299 of 400, under three quarters.
HURT = {"hull_strength": 400, "hull_damage": 101}
BEHIND = {"object_index": 12, "y": -0x2000, "target": ME}
AHEAD = BEHIND | {"y": 0x2000}


def test_with_no_order_target_it_turns_on_the_enemy_that_hit_it(
    craft, foe, ask
) -> None:
    decision = ask(craft(order_target_mask=0), foe(**AHEAD), last_attacker=12)
    assert phase_of(decision) == attack.ATTACK
    assert decision.actions.target == 12
    assert decision.actions.lasers != (HOLD_FIRE, HOLD_FIRE)


def test_with_no_order_target_it_weaves_from_a_chaser_then_fights_it(
    craft, foe, ask
) -> None:
    first = ask(craft(order_target_mask=0), foe(**BEHIND), tick=1000)
    assert phase_of(first) == attack.EVADE and first.actions.target == 12
    me = craft(order_target_mask=0, notebook=first.notebook)
    overshot = ask(me, foe(**AHEAD), tick=1016)
    assert phase_of(overshot) == attack.ATTACK and overshot.actions.target == 12


def test_a_damaged_craft_with_no_order_target_only_weaves(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(order_target_mask=0), foe(**BEHIND), **HURT)
    assert phase_of(decision) == attack.EVADE
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert (
        f"attack.pursued object={ME} pursuer=12 hull_left=299 hull_strength=400 "
        "fights=0" in caplog.text
    )


def test_a_weaves_rest_outlasts_the_search(craft, foe, ask) -> None:
    """A damaged craft with no order target that cannot shake its chaser rests
    after a full weave, as one with a target does."""
    decision, phases = None, []
    for tick in range(1000, 1000 + 400 * 16, 16):
        notes = {} if decision is None else {"notebook": decision.notebook}
        decision = ask(
            craft(order_target_mask=0, **notes), foe(**BEHIND), tick=tick, **HURT
        )
        phases.append((tick, phase_of(decision)))
    first_end = next(at for at, phase in phases if phase == attack.SEEK)
    assert 1000 + 1416 <= first_end < 1000 + 1886 + 16
    again = next(at for at, phase in phases if phase == attack.EVADE and at > first_end)
    assert first_end + 1886 <= again < first_end + 1886 + 16


def test_an_enemy_after_the_craft_hit_it_or_sits_on_its_tail(craft, foe, shown) -> None:
    me = craft(order_target_mask=0)
    assert attack.pursuer(me, shown(me, foe(**BEHIND))).object_index == 12
    assert attack.pursuer(me, shown(me, foe(**AHEAD))) is None
    hit = shown(me, foe(**AHEAD), last_attacker=12)
    assert attack.pursuer(me, hit).object_index == 12
    elsewhere = foe(**BEHIND | {"target": 15})
    assert attack.pursuer(me, shown(me, elsewhere, last_attacker=12)) is None
    friendly = craft(order_target_mask=0, enemy_mask=0)
    assert attack.pursuer(friendly, shown(friendly, foe(**BEHIND))) is None
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    assert attack.pursuer(me, shown(me, foe(**BEHIND), nearer)).object_index == 15


@pytest.mark.parametrize("genus", [3, 4, 5])
def test_a_freighter_starship_or_platform_is_never_after_the_craft(
    craft, foe, shown, genus
) -> None:
    me = craft(order_target_mask=0)
    big = foe(**BEHIND | {"genus": genus})
    assert attack.pursuer(me, shown(me, big, last_attacker=12)) is None


def test_the_enemy_it_fights_stays_after_it_while_it_targets_the_craft(
    craft, foe, ask, shown
) -> None:
    fought = ask(craft(order_target_mask=0), foe(**AHEAD), last_attacker=12)
    me = craft(order_target_mask=0, notebook=fought.notebook)
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    assert attack.pursuer(me, shown(me, foe(**AHEAD), nearer)).object_index == 12
    turned = foe(**AHEAD | {"target": 15})
    assert attack.pursuer(me, shown(me, turned)) is None


DISABLED = {"flags": ContactFlags.DISABLED}


def test_a_disabled_enemy_is_never_after_the_craft(craft, foe, ask, shown) -> None:
    """With none of its subsystems working it can neither fly nor fire: no
    pursuer as the last hitter or as the enemy fought, and on no tail."""
    me = craft(order_target_mask=0)
    dead = foe(**BEHIND | DISABLED)
    assert attack.pursuer(me, shown(me, dead, last_attacker=12)) is None
    assert attack.tail_threat(me, shown(me, dead)) is None
    fought = ask(me, foe(**AHEAD), last_attacker=12)
    me = craft(order_target_mask=0, notebook=fought.notebook)
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    question = shown(me, foe(**AHEAD | DISABLED), nearer)
    assert attack.pursuer(me, question).object_index == 15


def test_with_only_a_disabled_enemy_behind_and_nowhere_to_cruise_it_holds_its_course(
    craft, foe, ask
) -> None:
    """Neither fought back nor woven from."""
    dead = foe(**BEHIND | DISABLED)
    decision = ask(craft(order_target_mask=0), dead, last_attacker=12)
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.steer is None
    assert phase_of(decision) == attack.SEEK


@pytest.mark.parametrize(
    ("strength", "damage", "fights"),
    [(400, 100, True), (400, 101, False), (400, 400, False), (0, 0, True)],
)
def test_it_fights_back_at_three_quarters_of_its_hull_or_more(
    craft, foe, shown, strength, damage, fights
) -> None:
    """Shields never count: both are down in every case."""
    me, chaser = craft(), foe(**BEHIND)
    question = shown(
        me,
        chaser,
        hull_strength=strength,
        hull_damage=damage,
        front_shield=0,
        rear_shield=0,
    )
    assert attack.fights_back(me, question, chaser) is fights


@pytest.mark.parametrize("changes", [{"cannon_group_count": 0}, {"full_speed": 0}])
def test_a_craft_without_cannons_or_engines_never_fights_back(
    craft, foe, shown, changes
) -> None:
    me, chaser = craft(**changes), foe(**BEHIND)
    assert not attack.fights_back(me, shown(me, chaser), chaser)


def test_a_craft_whose_own_contact_is_not_shown_counts_as_whole(craft, foe) -> None:
    me, chaser = craft(), foe(**BEHIND)
    assert attack.fights_back(me, Question(1000, (me,), (chaser,)), chaser)


def test_only_an_attack_notebook_names_the_enemy_it_fights(craft, foe, shown) -> None:
    """The same scratch bytes name craft 12 (signature 11) under attack's code
    and mean nothing under another behavior's."""
    scratch = attack.Memory(target=12, signature=11).write()
    for code, after in ((attack.CODE, 12), (3, None)):
        book = notebook.Notebook(code, attack.ATTACK, 7, 990, scratch).encode()
        me = craft(order_target_mask=0, notebook=book)
        found = attack.pursuer(me, shown(me, foe(**AHEAD)))
        assert (found and found.object_index) == after


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


# A craft whose engine declared its lock, with one launcher of concussion
# missiles ready.
LOCKED = {
    "warhead_launcher_count": 1,
    "warhead_type": (warheads.CONCUSSION_MISSILE, 0),
    "warheads_left": (4, 0),
    "launcher_cooldown": (0, 0),
    "lock_ticks": 708,
    "lock_needed": 708,
    "lock_state": Lock.DECLARED,
}


def memory_of(decision) -> attack.Memory:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch)


def test_the_memory_keeps_the_runs_shots_fresh_mark_and_reason() -> None:
    memory = attack.Memory(target=FOE, signature=3, ordered=1, shots=5, fresh=1, held=6)
    assert attack.Memory.read(memory.write()) == memory


def test_a_declared_lock_fires_a_guided_warhead(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(**LOCKED), foe(), target=FOE)
    assert decision.actions.warhead == Warhead(0)
    assert decision.actions.target == FOE
    assert (
        f"attack.warhead object={ME} target={FOE} launcher=0 type=144 lock=708 "
        "coming=0 shots=1" in caplog.text
    )
    memory = memory_of(decision)
    assert (memory.shots, memory.fresh, memory.held) == (1, 1, warheads.Held.NONE)


def test_a_warhead_fires_with_the_target_off_the_cannons_cone(craft, foe, ask) -> None:
    actions = ask(craft(**LOCKED), foe(x=0x3000, y=0x3000), target=FOE).actions
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert actions.warhead == Warhead(0)


def test_after_a_shot_at_a_fighter_the_lock_must_lapse_and_the_run_is_done(
    craft, foe, ask
) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    held = ask(craft(**LOCKED, notebook=first.notebook), foe(), target=FOE, tick=1016)
    assert held.actions.warhead is None
    assert memory_of(held).held == warheads.Held.FRESH
    counting = LOCKED | {"lock_state": Lock.COUNTING, "notebook": held.notebook}
    lapsed = ask(craft(**counting), foe(), target=FOE, tick=1032)
    assert memory_of(lapsed).fresh == 0
    again = ask(craft(**LOCKED, notebook=lapsed.notebook), foe(), target=FOE, tick=1048)
    assert again.actions.warhead is None
    assert memory_of(again).held == warheads.Held.CAPS


def test_a_big_ship_takes_six_shots_in_one_run(craft, foe, ask) -> None:
    ship = foe(genus=4, hull_strength=90_000)
    changes = LOCKED | {"warheads_left": (9, 0)}
    fired = []
    for step in range(8):
        decision = ask(craft(**changes), ship, target=FOE, tick=1000 + 16 * step)
        fired.append(decision.actions.warhead is not None)
        changes = changes | {"notebook": decision.notebook}
    assert fired == [True] * 6 + [False] * 2
    assert memory_of(decision).held == warheads.Held.CAPS


def test_a_run_starts_again_after_a_break(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    counting = LOCKED | {"lock_state": Lock.COUNTING, "notebook": first.notebook}
    broke = ask(craft(**counting), foe(y=0x800), target=FOE, tick=1016)
    assert phase_of(broke) == attack.BREAK
    back = ask(craft(**LOCKED, notebook=broke.notebook), foe(), target=FOE, tick=1032)
    assert phase_of(back) == attack.ATTACK
    assert back.actions.warhead == Warhead(0)
    assert memory_of(back).shots == 1


def test_a_new_target_starts_a_new_run(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    nearer = foe(object_index=FOE + 1, object_signature=1, y=0x2000)
    later = 1000 + attack.LOOK_AGAIN_TICKS[2]
    changes = LOCKED | {"notebook": first.notebook, "order_target_mask": 0b110}
    decision = ask(craft(**changes), foe(), nearer, target=FOE + 1, tick=later)
    assert decision.actions.target == FOE + 1
    assert decision.actions.warhead == Warhead(0)


def test_a_held_reason_is_logged_once_until_it_changes(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    bomber = LOCKED | {"warhead_type": (warheads.SPACE_BOMB, 0)}
    first = ask(craft(**bomber), foe(), target=FOE)
    second = ask(craft(**bomber, notebook=first.notebook), foe(), target=FOE, tick=1016)
    empty = LOCKED | {"warheads_left": (0, 0), "notebook": second.notebook}
    ask(craft(**empty), foe(), target=FOE, tick=1032)
    lines = [m for m in caplog.messages if m.startswith("attack.warhead_held")]
    assert lines == [
        f'attack.warhead_held object={ME} target={FOE} reason="kind" lock=708 '
        "lock_state=2 coming=0 coming_damage=0 shots=0",
        f'attack.warhead_held object={ME} target={FOE} reason="launcher" lock=708 '
        "lock_state=2 coming=0 coming_damage=0 shots=0",
    ]


def test_a_craft_with_no_launchers_holds_no_reason(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(**LOCKED | {"warhead_launcher_count": 0}), foe(), target=FOE)
    assert decision.actions.warhead is None
    assert memory_of(decision).held == warheads.Held.NONE
    assert "attack.warhead_held" not in caplog.text


def test_a_weave_fires_no_warhead(craft, foe, ask) -> None:
    chaser = foe(object_index=FOE + 1, y=-0x2000, target=ME)
    decision = ask(craft(**LOCKED), foe(x=0x3000, y=0x3000), chaser, target=FOE)
    assert phase_of(decision) == attack.EVADE
    assert decision.actions.warhead is None


# A Jedi (skill level 5) with no lock, flying at speed 225 with one launcher
# of proton torpedoes, ready; its target a corvette 20,000 ahead crossing to
# +x at speed 150. The shot's meeting point lies 6,622 to the side, at yaw
# 3,336 (``warheads.lead``, its line harvested).
UNGUIDED = {
    "skill_level": 5,
    "speed": 225,
    "warhead_launcher_count": 1,
    "warhead_type": (warheads.PROTON_TORPEDO, 0),
    "warheads_left": (9, 0),
    "launcher_cooldown": (0, 0),
    "lock_ticks": 0,
    "lock_needed": 708,
    "lock_state": Lock.NONE,
}
CORVETTE = {
    "genus": 4,
    "size": 6011,
    "hull_strength": 90_000,
    "y": 20_000,
    "yaw": 0x4000,
    "pitch": LEVEL,
    "speed": 150,
}
LEAD_YAW = 3336


def test_a_jedi_steers_ahead_of_a_crossing_ship_while_the_aim_holds_its_shot(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(**UNGUIDED), foe(**CORVETTE))
    assert decision.actions.steer == Steer(LEAD_YAW, LEVEL, 0)
    assert decision.actions.target == FOE
    assert decision.actions.warhead is None
    assert memory_of(decision).held == warheads.Held.AIM
    assert (
        f'attack.warhead_held object={ME} target={FOE} reason="aim" lock=0 '
        "lock_state=0 coming=0 coming_damage=0 shots=0" in caplog.text
    )


def test_with_its_nose_on_the_point_a_jedi_fires_unguided(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(**UNGUIDED, yaw=LEAD_YAW), foe(**CORVETTE))
    assert decision.actions.steer == Steer(LEAD_YAW, LEVEL, 0)
    assert decision.actions.warhead == Warhead(0)
    assert (
        f"attack.unguided object={ME} target={FOE} launcher=0 type=143 lock=0 "
        "ticks=575 miss=0 shots=1" in caplog.text
    )
    assert "attack.warhead " not in caplog.text
    memory = memory_of(decision)
    assert (memory.shots, memory.fresh, memory.held) == (1, 0, warheads.Held.NONE)


def test_while_its_launcher_reloads_a_jedi_steers_at_the_ship(craft, foe, ask) -> None:
    reloading = UNGUIDED | {"launcher_cooldown": (100, 0)}
    decision = ask(craft(**reloading, yaw=LEAD_YAW), foe(**CORVETTE))
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert memory_of(decision).held == warheads.Held.LAUNCHER


def test_below_jedi_it_steers_at_the_ship_and_waits_for_its_lock(
    craft, foe, ask
) -> None:
    decision = ask(craft(**UNGUIDED | {"skill_level": 4}), foe(**CORVETTE))
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert memory_of(decision).held == warheads.Held.LOCK


def test_a_locked_jedi_steers_at_the_ship_and_fires_guided(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    locked = UNGUIDED | {"lock_ticks": 708, "lock_state": Lock.DECLARED}
    decision = ask(craft(**locked), foe(**CORVETTE), target=FOE)
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.warhead == Warhead(0)
    assert "attack.warhead object=" in caplog.text
    assert "attack.unguided" not in caplog.text


def test_a_jedi_fires_six_unguided_shots_in_a_run_then_steers_at_the_ship(
    craft, foe, ask
) -> None:
    changes = UNGUIDED | {"yaw": LEAD_YAW}
    fired = []
    for step in range(8):
        decision = ask(craft(**changes), foe(**CORVETTE), tick=1000 + 16 * step)
        fired.append(decision.actions.warhead is not None)
        changes = changes | {"notebook": decision.notebook}
    assert fired == [True] * 6 + [False] * 2
    assert memory_of(decision).held == warheads.Held.CAPS
    assert decision.actions.steer == Steer(0, LEVEL, 0)
