"""Fence for protect: Attack's dogfight at the enemies attacking the order's craft.

The craft flies at the origin facing +y (yaw 0), level, at tier 2, with two
cannon groups; contact 0 is its own, contact 1 the friendly craft its order
names, far ahead, and the enemies sit ahead of it in range. The engine marks
every contact hostile but the first two.
"""

from __future__ import annotations

import pytest
from shot_setup import aimed

from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import protect
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import BEHAVIORS
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Commands
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import Lock
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Warhead

ME = 4
FRIEND = 20
FOE = 9
OTHER = 10
# The engine marks every contact hostile but the craft's own and its friend's.
ENEMIES = 0xFFFFFFFC
# PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and _ION_LASER (object/laser.h).
LASER = 139
ION_CANNON = 141
# Tier 2's burst at 0x3000 units: pairs, five shots.
BURST = CannonGroup(LinkMode.PAIRS, 5)
ATTACKED = ContactFlags.ATTACKED
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
            "order_code": OrderCode.PROTECT,
            "order_target_mask": 0b10,
            "enemy_mask": ENEMIES,
            "turret_slot_mask": 0,
            "cannon_group_count": 2,
            "cannon_type": (LASER, ION_CANNON),
            "warhead_launcher_count": 0,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def friend(contact):
    """Return the craft the order names, hit by some team unless changed."""

    def make(**changes):
        ours = {
            "object_index": FRIEND,
            "team": 1,
            "x": 0,
            "y": 0x9000,
            "z": 0,
            "flags": ATTACKED,
            "target": NO_OBJECT,
        }
        return contact(**(ours | changes))

    return make


@pytest.fixture
def foe(contact):
    """Return an enemy ahead in range, aimed at the friend unless changed."""

    def make(**changes):
        theirs = {
            "object_index": FOE,
            "team": 2,
            "x": 0,
            "y": 0x3000,
            "z": 0,
            "yaw": 0,
            "speed": 0,
            "target": FRIEND,
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
    """Return the craft's Protect decision with its own contact first."""

    def decide(me, *others, tick=1000, **mine):
        return protect.think(me, shown(me, *others, tick=tick, **mine))

    return decide


def test_the_order_is_flown_by_protect() -> None:
    assert BEHAVIORS[OrderCode.PROTECT] is protect.think


def test_an_enemy_aimed_at_the_hit_craft_is_fought_with_every_cannon_but_the_ion(
    craft, friend, foe, ask
) -> None:
    actions = ask(craft(), friend(), foe()).actions
    assert actions.target == FOE
    assert actions.steer.yaw == 0 and actions.steer.pitch == LEVEL
    assert actions.lasers == (BURST, HOLD_FIRE)


def test_a_craft_no_team_has_hit_has_no_attackers(craft, friend, foe, ask) -> None:
    decision = ask(craft(), friend(flags=0), foe())
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert notebook.decode(decision.notebook).phase == attack.SEEK


def test_the_enemy_whose_shot_hit_it_last_is_an_attacker(
    craft, friend, foe, ask
) -> None:
    actions = ask(craft(), friend(hit_by=FOE), foe(target=33)).actions
    assert actions.target == FOE


def test_an_enemy_aimed_at_a_craft_the_order_does_not_name_is_left_alone(
    craft, friend, foe, ask
) -> None:
    """A friendly craft the order does not name was hit, one enemy aims at it
    and the other hit it: neither attacks the craft the order names."""
    stranger = friend(object_index=21, hit_by=OTHER)
    me = craft(enemy_mask=ENEMIES & ~0b1000)
    hitter = foe(object_index=OTHER, target=33)
    others = (friend(), foe(target=21), stranger, hitter)
    assert ask(me, *others).actions.target == NO_OBJECT


def test_the_nearest_attacker_is_fought(craft, friend, foe, ask) -> None:
    far = foe(y=0x5000)
    near = foe(object_index=OTHER, target=33, y=0x2000)
    assert ask(craft(), friend(hit_by=OTHER), far, near).actions.target == OTHER


def test_a_craft_the_engine_does_not_mark_hostile_is_never_fought(
    craft, friend, foe, ask
) -> None:
    """1997 checked hostility only for a craft a player flew; here every
    target is hostile, as for every behavior."""
    me = craft(enemy_mask=ENEMIES & ~0b100)
    ally = foe(team=1)
    assert ask(me, friend(hit_by=FOE), ally).actions.target == NO_OBJECT


def test_an_ignored_attacker_is_never_picked(craft, friend, foe, ask) -> None:
    far = foe(object_index=OTHER, y=0x5000)
    actions = ask(craft(avoid_target=FOE), friend(), foe(), far).actions
    assert actions.target == OTHER


def test_a_commanded_craft_comes_before_the_attackers(craft, friend, foe, ask) -> None:
    other = foe(object_index=OTHER, target=33, y=0x5000)
    actions = ask(craft(command_target=OTHER), friend(), foe(), other).actions
    assert actions.target == OTHER


def test_with_no_attacker_a_chaser_is_fought_back(craft, friend, foe, ask) -> None:
    chaser = foe(object_index=12, target=ME)
    actions = ask(craft(), friend(flags=0), chaser, last_attacker=12).actions
    assert actions.target == 12


def test_with_no_attacker_or_chaser_and_nowhere_to_cruise_it_holds_its_course(
    craft, friend, foe, ask
) -> None:
    """An enemy in range that attacks neither the named craft nor this one is
    left alone."""
    actions = ask(craft(), friend(), foe(target=33)).actions
    assert actions.steer is None and actions.throttle is None
    assert actions.target == NO_OBJECT


def test_warheads_fire_as_attack_fires_them(craft, friend, foe, ask) -> None:
    actions = ask(craft(**LOCKED), friend(), foe(), target=FOE).actions
    assert actions.warhead == Warhead(0)


def test_an_evade_command_is_flown_as_attack_flies_it(craft, friend, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(commands=Commands.EVADE), friend(), foe(), chaser)
    assert decision.actions.evaded and decision.actions.target == NO_OBJECT
    assert notebook.decode(decision.notebook).phase == attack.EVADE


def test_the_attackers_line_records_the_sort(craft, friend, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    untouched = friend(object_index=21, flags=0)
    me = craft(order_target_mask=0b110, enemy_mask=ENEMIES & ~0b100)
    ask(me, friend(), untouched, foe())
    assert f"protect.attackers object={ME} named=2 hit=1 attackers=1" in caplog.text


def test_a_homing_shot_closing_makes_it_weave(craft, friend, ask) -> None:
    decision = ask(aimed(craft(), (0, -0x1000, 0)), friend())
    assert notebook.decode(decision.notebook).phase == attack.EVADE
    assert decision.actions.weaving
