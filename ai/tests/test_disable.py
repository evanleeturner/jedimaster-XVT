"""Fence for disable: Attack's dogfight at targets that still work, ion cannons alone.

The craft flies at the origin facing +y (yaw 0), level, at tier 2, its first
cannon group ion cannons and its second lasers; contact 0 is its own, contact
1 its order's target, ahead and in range. Where a test needs it, Attack flies
the same question first, to show the situation fires.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import disable
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import BEHAVIORS
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
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
FOE = 9
OTHER = 10
THIRD = 13
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
# PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and _ION_TURBO_LASER (object/laser.h).
LASER = 139
ION_TURBOLASER = 142
# Tier 2's burst at the target's 0x3000 units: pairs, five shots.
BURST = CannonGroup(LinkMode.PAIRS, 5)
DISABLED = ContactFlags.DISABLED
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
# An enemy after the craft, ahead of it and lined up, that hit it last.
CHASER = {"object_index": 12, "y": 0x3000, "target": ME}


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
            "order_code": OrderCode.DISABLE,
            "order_target_mask": 0b10,
            "enemy_mask": ALL_BUT_OWN,
            "turret_slot_mask": 0,
            "cannon_group_count": 2,
            "cannon_type": (disable.ION_CANNON, LASER),
            "warhead_launcher_count": 0,
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
    """Return the craft's Disable decision with its own contact first."""

    def decide(me, *others, tick=1000, **mine):
        return disable.think(me, shown(me, *others, tick=tick, **mine))

    return decide


def memory_of(decision) -> attack.Memory:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch)


def test_the_order_is_flown_by_disable() -> None:
    assert BEHAVIORS[OrderCode.DISABLE] is disable.think


def test_the_ion_cannons_fire_and_every_other_cannon_holds(
    craft, foe, shown, ask
) -> None:
    me = craft()
    assert attack.think(me, shown(me, foe())).actions.lasers == (BURST, BURST)
    actions = ask(me, foe()).actions
    assert actions.target == FOE
    assert actions.lasers == (BURST, HOLD_FIRE)


@pytest.mark.parametrize(
    "kinds, lasers",
    [
        ((LASER, disable.ION_CANNON), (HOLD_FIRE, BURST)),
        ((LASER, LASER), (HOLD_FIRE, HOLD_FIRE)),
        ((ION_TURBOLASER, disable.ION_CANNON), (HOLD_FIRE, BURST)),
    ],
)
def test_only_the_ion_cannon_fires_so_a_craft_without_one_holds_fire(
    craft, foe, ask, kinds, lasers
) -> None:
    """A craft with no ion cannon flies at its target and never fires a cannon:
    the mission wants the target whole. The ion turbolaser holds, as in 1997."""
    actions = ask(craft(cannon_type=kinds), foe()).actions
    assert actions.target == FOE
    assert actions.steer.yaw == 0 and actions.steer.pitch == LEVEL
    assert actions.lasers == lasers


def test_a_craft_whose_subsystems_failed_is_never_its_target(craft, foe, ask) -> None:
    far = foe(object_index=OTHER, y=0x5000)
    me = craft(order_target_mask=0b110)
    assert ask(me, foe(flags=DISABLED), far).actions.target == OTHER


def test_a_target_whose_subsystems_fail_is_dropped_for_the_next(
    craft, foe, ask
) -> None:
    far = foe(object_index=OTHER, y=0x5000)
    me = craft(order_target_mask=0b110)
    first = ask(me, foe(), far)
    assert first.actions.target == FOE
    then = ask(
        craft(order_target_mask=0b110, notebook=first.notebook),
        foe(flags=DISABLED),
        far,
    )
    assert then.actions.target == OTHER


def test_with_every_target_disabled_it_holds_fire_and_keeps_course(
    craft, foe, ask
) -> None:
    decision = ask(craft(), foe(flags=DISABLED))
    actions = decision.actions
    assert actions.steer is None and actions.throttle is None
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert notebook.decode(decision.notebook).phase == attack.SEEK


def test_no_warhead_is_fired_counted_or_logged(craft, foe, shown, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    me = craft(**LOCKED)
    assert attack.think(me, shown(me, foe(), target=FOE)).actions.warhead == Warhead(0)
    caplog.clear()
    decision = ask(me, foe(), target=FOE)
    assert decision.actions.target == FOE and decision.actions.warhead is None
    assert memory_of(decision).shots == 0
    assert "attack.warhead" not in caplog.text


def test_a_chaser_is_fought_with_every_cannon_and_warhead(craft, foe, ask) -> None:
    """With no target that works, a chaser is fought as Attack fights back: it
    is not the order's target."""
    me = craft(order_target_mask=0, **LOCKED)
    actions = ask(me, foe(**CHASER), last_attacker=12, target=12).actions
    assert actions.target == 12
    assert actions.lasers == (BURST, BURST)
    assert actions.warhead == Warhead(0)


def test_a_target_after_the_craft_is_fought_with_ion_cannons_until_disabled(
    craft, foe, ask
) -> None:
    """The order's target hit the craft and is after it: still its Disable
    target, so the lasers hold; once its subsystems fail it is no chaser
    either, and is left whole."""
    attacker = foe(target=ME)
    first = ask(craft(), attacker, last_attacker=FOE)
    assert first.actions.target == FOE
    assert first.actions.lasers == (BURST, HOLD_FIRE)
    dead = foe(target=ME, flags=DISABLED)
    then = ask(craft(notebook=first.notebook), dead, last_attacker=FOE)
    assert then.actions.target == NO_OBJECT
    assert then.actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_a_disabled_chaser_is_never_fought(craft, foe, ask) -> None:
    actions = ask(
        craft(order_target_mask=0), foe(**CHASER, flags=DISABLED), last_attacker=12
    ).actions
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_a_commanded_craft_is_fought_with_ion_cannons_until_disabled(
    craft, foe, ask
) -> None:
    near = foe(y=0x2800)
    me = craft(command_target=OTHER)
    first = ask(me, near, foe(object_index=OTHER))
    assert first.actions.target == OTHER
    assert first.actions.lasers == (BURST, HOLD_FIRE)
    then = ask(me, near, foe(object_index=OTHER, flags=DISABLED))
    assert then.actions.target == FOE


def test_once_a_commanded_craft_is_disabled_an_ignored_craft_stays_ignored(
    craft, foe, ask
) -> None:
    me = craft(order_target_mask=0b1010, command_target=OTHER, avoid_target=FOE)
    commanded = foe(object_index=OTHER, flags=DISABLED)
    far = foe(object_index=THIRD, y=0x5000)
    assert ask(me, foe(y=0x2000), commanded, far).actions.target == THIRD


def test_an_evade_command_is_flown_as_attack_flies_it(craft, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(commands=Commands.EVADE), foe(), chaser)
    assert decision.actions.evaded and decision.actions.target == NO_OBJECT
    assert notebook.decode(decision.notebook).phase == attack.EVADE


def test_the_targets_line_records_the_sort(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    far = foe(object_index=OTHER, y=0x5000)
    ask(craft(order_target_mask=0b110), foe(flags=DISABLED), far)
    assert (
        f"disable.targets object={ME} named=2 working=1 chosen=1 ion_groups=1"
        in caplog.text
    )
    caplog.clear()
    one_group = craft(cannon_group_count=1, cannon_type=(LASER, disable.ION_CANNON))
    ask(one_group, foe())
    assert "ion_groups=0" in caplog.text


def test_actions_that_set_no_cannons_are_left_as_they_are(craft) -> None:
    assert disable.ion_only(craft(), Actions(throttle=7)) == Actions(throttle=7)
