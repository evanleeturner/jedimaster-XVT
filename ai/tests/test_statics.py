"""Fence for the static objects an order names: Attack fights them, as 1997 did.

The craft flies at the origin facing +y (yaw 0), level, at tier 2, on the
Attack order; contact 0 is its own, contact 1 an enemy craft. A static is
a nav buoy, mine, probe or comm sat the question lists after its contacts,
flagged STATIC, with what the engine gives one: its object record's index,
signature, type, place and heading, its type's genus and size, its group's
team, and 0 or NO_OBJECT for all a static lacks. Two rules each have a
test: no alliance test, as 1997's scan of the statics had none, and Attack
alone fights them.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import tactics
from jedimaster_ai import warheads
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import disable
from jedimaster_ai.behaviors import kamikaze
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import Lock
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
FOE = 9
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
# The first static slot's object index: 32 craft, 160 shots, 16 debris, 16
# explosions, 256 data and 8 local debris slots come first (mission.c:6141).
FIRST_STATIC = 488
MINE_GENUS = 8
MINE_TYPE = 75
BREAKING_UP = 3


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
            "order_code": OrderCode.ATTACK,
            "order_target_mask": 0,
            "static_target_mask": 0b1,
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
def static(contact):
    """Return a maker of the mine in static slot n, 0x3000 ahead unless
    placed, its fields changed as named."""

    def make(n=0, **changes):
        fields = {
            "object_index": FIRST_STATIC + n,
            "object_signature": 3,
            "object_type": MINE_TYPE,
            "genus": MINE_GENUS,
            "team": 2,
            "flight_group": 6,
            "object_kind": 0,
            "flags": ContactFlags.STATIC,
            "x": 0,
            "y": 0x3000,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "speed": 0,
            "target": NO_OBJECT,
            "last_attacker": NO_OBJECT,
            "front_shield": 0,
            "rear_shield": 0,
            "hull_damage": 0,
            "hull_strength": 0,
            "activity": 0,
            "size": 0,
            "warheads_coming": 0,
            "warheads_coming_damage": 0,
            "hit_by": NO_OBJECT,
        }
        return contact(**(fields | changes))

    return make


@pytest.fixture
def shown(contact):
    """Return the question showing the craft's own contact first, then the
    other contacts, then the statics."""

    def make(me, *others, statics=()):
        fields = {
            "object_index": me.object_index,
            "team": me.team,
            "x": me.x,
            "y": me.y,
            "z": me.z,
        }
        return Question(1000, (me,), (contact(**fields), *others), tuple(statics))

    return make


def test_the_order_names_the_statics_its_mask_sets_in_list_order(
    craft, static, shown
) -> None:
    statics = [static(n) for n in range(3)]
    named = tactics.order_statics(
        craft(static_target_mask=0b101), shown(craft(), statics=statics)
    )
    assert named == [statics[0], statics[2]]
    assert (
        tactics.order_statics(
            craft(static_target_mask=0), shown(craft(), statics=statics)
        )
        == []
    )


def test_the_last_of_64_statics_is_named_by_bit_63(craft, static, shown) -> None:
    statics = [static(n) for n in range(64)]
    named = tactics.order_statics(
        craft(static_target_mask=1 << 63), shown(craft(), statics=statics)
    )
    assert named == [statics[63]]


def test_a_static_of_the_crafts_own_team_is_named(craft, static, shown) -> None:
    ours = static(team=1)
    assert tactics.order_statics(craft(), shown(craft(), statics=[ours])) == [ours]


def test_a_static_breaking_up_is_not_named(craft, static, shown) -> None:
    going = static(object_kind=BREAKING_UP)
    assert tactics.order_statics(craft(), shown(craft(), statics=[going])) == []


def test_an_attack_fires_on_a_lone_static_lined_up_in_range(
    craft, static, shown, caplog
) -> None:
    caplog.set_level("INFO")
    actions = attack.think(craft(), shown(craft(), statics=[static()])).actions
    assert actions.target == FIRST_STATIC
    assert actions.steer == Steer(0, LEVEL, 0)
    assert actions.lasers == (CannonGroup(LinkMode.PAIRS, 5), HOLD_FIRE)
    assert (
        f"attack.target object={ME} target={FIRST_STATIC} distance=12288" in caplog.text
    )


@pytest.mark.parametrize(
    ("static_y", "chosen"), [(0x2000, FIRST_STATIC), (0x4000, FOE)]
)
def test_the_nearer_of_a_craft_and_a_static_is_fought(
    craft, foe, static, shown, static_y, chosen
) -> None:
    me = craft(order_target_mask=0b10)
    question = shown(me, foe(), statics=[static(y=static_y)])
    assert attack.think(me, question).actions.target == chosen


def test_a_static_an_ignore_order_names_is_left_alone(craft, static, shown) -> None:
    me = craft(avoid_target=FIRST_STATIC)
    actions = attack.think(me, shown(me, statics=[static()])).actions
    assert actions.target == NO_OBJECT
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_a_static_listed_but_not_named_is_left_alone(craft, static, shown) -> None:
    me = craft(static_target_mask=0b10)
    actions = attack.think(me, shown(me, statics=[static()])).actions
    assert actions.target == NO_OBJECT


def test_a_jedi_fires_no_warhead_at_a_static(craft, static, shown, caplog) -> None:
    caplog.set_level("DEBUG")
    me = craft(
        skill_level=5,
        speed=225,
        warhead_launcher_count=1,
        warhead_type=(warheads.PROTON_TORPEDO, 0),
        warheads_left=(9, 0),
        launcher_cooldown=(0, 0),
        lock_ticks=0,
        lock_needed=708,
        lock_state=Lock.NONE,
    )
    actions = attack.think(me, shown(me, statics=[static()])).actions
    assert actions.target == FIRST_STATIC
    assert actions.warhead is None
    assert (
        f'attack.warhead_held object={ME} target={FIRST_STATIC} reason="lock" '
        in caplog.text
    )
    assert "attack.unguided" not in caplog.text


# Disable holds its target at none; Kamikaze, with no waypoint, names none.
@pytest.mark.parametrize(
    ("order", "behavior", "target"),
    [
        (OrderCode.DISABLE, disable.think, NO_OBJECT),
        (OrderCode.KAMIKAZE, kamikaze.think, None),
    ],
)
def test_only_the_attack_order_fights_a_static(
    craft, static, shown, order, behavior, target
) -> None:
    me = craft(order_code=order)
    actions = behavior(me, shown(me, statics=[static()])).actions
    assert actions.target == target
