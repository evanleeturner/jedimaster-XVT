"""Fence for escort: station beside the escorted craft, Attack's dogfight at the
enemies after its flight group.

The craft flies at the origin facing +y (yaw 0), level, at tier 2, with two
cannon groups; contact 0 is its own, contact 1 the friendly craft it escorts,
ahead, flying +y at speed 200 in flight group 5; the station beside it is
off to the right. Enemies sit ahead in range. The engine marks every contact
hostile but the first two.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import escort
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import BEHAVIORS
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Books
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
HOST = 20
MATE = 21
FOE = 9
OTHER = 10
GROUP = 5
# The engine marks every contact hostile but the craft's own and the host's.
ENEMIES = 0xFFFFFFFC
# PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and _ION_LASER (object/laser.h).
LASER = 139
ION_CANNON = 141
# Tier 2's burst at 0x3000 units: pairs, five shots.
BURST = CannonGroup(LinkMode.PAIRS, 5)
# The host 0x2000 ahead, its station 0x800 to its right: formation's look
# ahead aims 0x4000 ahead along the host's heading.
HOST_Y = 0x2000
STATION = (0x800, HOST_Y, 0)
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
            "order_code": OrderCode.ESCORT,
            "order_target_mask": 0b10,
            "enemy_mask": ENEMIES,
            "turret_slot_mask": 0,
            "cannon_group_count": 2,
            "cannon_type": (LASER, ION_CANNON),
            "warhead_launcher_count": 0,
            "full_speed": 1000,
            "books": Books.TARGETS_LEFT | Books.STATION,
            "escorted": HOST,
            "station": STATION,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def host(contact):
    """Return the craft escorted, ahead, small, flying +y."""

    def make(**changes):
        ours = {
            "object_index": HOST,
            "team": 1,
            "flight_group": GROUP,
            "x": 0,
            "y": HOST_Y,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "speed": 200,
            "size": 0,
            "target": NO_OBJECT,
        }
        return contact(**(ours | changes))

    return make


@pytest.fixture
def foe(contact):
    """Return an enemy ahead in range, aimed at the host unless changed."""

    def make(**changes):
        theirs = {
            "object_index": FOE,
            "team": 2,
            "flight_group": 7,
            "x": 0,
            "y": 0x3000,
            "z": 0,
            "yaw": 0,
            "speed": 0,
            "target": HOST,
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
    """Return the craft's Escort decision with its own contact first."""

    def decide(me, *others, tick=1000, **mine):
        return escort.think(me, shown(me, *others, tick=tick, **mine))

    return decide


def _at_station(actions, ahead: int = HOST_Y) -> bool:
    """Whether the actions are formation's flight to a station ahead units
    in front of the craft, 0x800 to the right, beside the host: aimed 0x2000
    past it along the host's heading, at the host's speed plus ahead // 32,
    full throttle at the craft's full speed of 1000 or more."""
    speed = 200 + ahead // 32
    throttle = 0xFFFF if speed >= 1000 else (speed << 16) // 1000
    return (
        actions.steer is not None
        and (actions.steer.yaw, actions.steer.pitch)
        == geometry.bearing(0x800, ahead + 0x2000, 0)
        and actions.throttle == throttle
    )


def test_the_order_is_flown_by_escort() -> None:
    assert BEHAVIORS[OrderCode.ESCORT] is escort.think


def test_an_enemy_aimed_at_the_escorted_craft_is_fought(craft, host, foe, ask) -> None:
    actions = ask(craft(), host(), foe()).actions
    assert actions.target == FOE
    assert actions.steer.yaw == 0 and actions.steer.pitch == LEVEL
    assert actions.lasers == (BURST, BURST)


def test_an_enemy_aimed_at_another_craft_of_its_group_is_fought(
    craft, host, foe, ask
) -> None:
    mate = host(object_index=MATE, x=0x4000)
    me = craft(enemy_mask=ENEMIES & ~0b100)
    assert ask(me, host(), mate, foe(target=MATE)).actions.target == FOE


def test_an_enemy_aimed_at_a_craft_of_another_group_is_left_alone(
    craft, host, foe, ask
) -> None:
    stranger = host(object_index=MATE, flight_group=6, x=0x4000)
    me = craft(enemy_mask=ENEMIES & ~0b100)
    actions = ask(me, host(), stranger, foe(target=MATE)).actions
    assert actions.target == NO_OBJECT


def test_an_enemy_within_0x40000_is_fought_and_one_at_it_is_not(
    craft, host, foe, ask
) -> None:
    assert ask(craft(), host(), foe(y=0x40000 - 1)).actions.target == FOE
    assert ask(craft(), host(), foe(y=0x40000)).actions.target == NO_OBJECT


def test_the_nearest_enemy_after_the_group_is_fought(craft, host, foe, ask) -> None:
    far = foe(y=0x5000)
    near = foe(object_index=OTHER, y=0x2800)
    assert ask(craft(), host(), far, near).actions.target == OTHER


def test_a_craft_the_engine_does_not_mark_hostile_is_never_fought(
    craft, host, foe, ask
) -> None:
    """1997 took any craft after the escorted group; here every target is
    hostile, as for every behavior."""
    me = craft(enemy_mask=ENEMIES & ~0b100)
    assert ask(me, host(), foe(team=1)).actions.target == NO_OBJECT


def test_an_ignored_enemy_is_never_picked(craft, host, foe, ask) -> None:
    far = foe(object_index=OTHER, y=0x5000)
    actions = ask(craft(avoid_target=FOE), host(), foe(), far).actions
    assert actions.target == OTHER


def test_a_commanded_craft_comes_before_the_enemies_after_the_group(
    craft, host, foe, ask
) -> None:
    other = foe(object_index=OTHER, target=33, y=0x5000)
    actions = ask(craft(command_target=OTHER), host(), foe(), other).actions
    assert actions.target == OTHER


def test_with_no_enemy_after_the_group_a_chaser_is_fought_back(
    craft, host, foe, ask
) -> None:
    chaser = foe(object_index=12, target=ME)
    actions = ask(craft(), host(), chaser, last_attacker=12).actions
    assert actions.target == 12


def test_a_hurt_craft_with_an_enemy_on_its_tail_weaves(craft, host, foe, ask) -> None:
    """Its hull at half: it may not fight back, and leaves its station to
    weave as Attack seeks."""
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(), host(), chaser, hull_damage=100, hull_strength=200)
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.steer is not None
    assert notebook.decode(decision.notebook).phase == attack.EVADE


def test_with_no_enemy_it_flies_to_its_station_fire_held(craft, host, foe, ask) -> None:
    """An enemy in range after neither the group nor the craft is left alone."""
    decision = ask(craft(), host(), foe(target=33))
    actions = decision.actions
    assert _at_station(actions)
    assert actions.think_interval == 29
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert notebook.decode(decision.notebook).phase == attack.SEEK


def test_after_a_fight_it_goes_back_to_its_station(craft, host, foe, ask) -> None:
    fought = ask(craft(), host(), foe())
    assert notebook.decode(fought.notebook).phase == attack.ATTACK
    decision = ask(craft(notebook=fought.notebook), host(), tick=1100)
    assert _at_station(decision.actions)
    assert notebook.decode(decision.notebook).phase == attack.SEEK


def test_past_0x8000_from_a_small_host_it_flies_at_it(craft, host, ask) -> None:
    near = ask(craft(station=(0x800, 0x8000, 0)), host(y=0x8000)).actions
    assert _at_station(near, 0x8000)
    actions = ask(craft(), host(y=0x8001)).actions
    assert actions.steer.yaw == 0 and actions.steer.pitch == LEVEL
    assert actions.throttle == 0x4000
    assert actions.think_interval == 29
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_past_0x10000_it_flies_at_the_host_at_full_throttle(craft, host, ask) -> None:
    assert ask(craft(), host(y=0x10000)).actions.throttle == 0x4000
    assert ask(craft(), host(y=0x10001)).actions.throttle == 0xFFFF


def test_a_host_of_size_3000_keeps_its_escort_within_0x20000(craft, host, ask) -> None:
    station = (0x800, 0x20000, 0)
    big = ask(craft(station=station), host(y=0x20000, size=3000)).actions
    # Formation's far case: its place over 0x10000 away and ahead, straight
    # at it.
    assert big.steer.yaw == geometry.bearing(0x800, 0x20000, 0)[0]
    small = ask(craft(station=station), host(y=0x20000, size=2999)).actions
    assert small.steer.yaw == 0 and small.throttle == 0xFFFF
    beyond = ask(craft(station=station), host(y=0x20001, size=3000)).actions
    assert beyond.steer.yaw == 0 and beyond.throttle == 0xFFFF


def test_a_disabled_host_is_circled_a_quarter_turn_off(craft, host, ask) -> None:
    disabled = host(flags=ContactFlags.DISABLED)
    actions = ask(craft(), disabled).actions
    assert actions.steer.yaw == 0x4000 and actions.steer.pitch == LEVEL
    assert actions.throttle == 0x4000
    assert actions.target == NO_OBJECT


def test_with_no_station_given_it_holds_its_course(craft, host, foe, ask) -> None:
    """Before the engine names the escorted craft, an enemy after the host is
    no threat it knows of."""
    me = craft(books=Books.TARGETS_LEFT)
    actions = ask(me, host(), foe()).actions
    assert actions.steer is None and actions.throttle is None
    assert actions.target == NO_OBJECT


def test_with_the_host_not_shown_it_holds_its_course(craft, foe, ask) -> None:
    actions = ask(craft(), foe()).actions
    assert actions.steer is None and actions.throttle is None
    assert actions.target == NO_OBJECT


def test_warheads_fire_as_attack_fires_them(craft, host, foe, ask) -> None:
    actions = ask(craft(**LOCKED), host(), foe(), target=FOE).actions
    assert actions.warhead == Warhead(0)


def test_an_evade_command_is_flown_as_attack_flies_it(craft, host, foe, ask) -> None:
    chaser = foe(object_index=12, y=-0x2000, target=ME)
    decision = ask(craft(commands=Commands.EVADE), host(), foe(), chaser)
    assert decision.actions.evaded and decision.actions.target == NO_OBJECT
    assert notebook.decode(decision.notebook).phase == attack.EVADE


def test_the_lines_record_the_threats_and_the_station(
    craft, host, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    mate = host(object_index=MATE, x=0x4000)
    me = craft(enemy_mask=ENEMIES & ~0b100)
    ask(me, host(), mate, foe(target=33))
    assert (
        f"escort.threats object={ME} escorted={HOST} group=2 threats=0" in caplog.text
    )
    assert (
        f"escort.station object={ME} escorted={HOST} distance={HOST_Y} "
        f"range={0x8000} disabled=0" in caplog.text
    )
