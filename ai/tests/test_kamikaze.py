"""Fence for kamikaze: a dive at the order's target at full throttle, else the waypoints.

The craft flies at the origin facing +y (yaw 0), level, at tier 2, thinking
at Attack's interval, its first cannon group ion cannons and its second
lasers; contact 0 is its own, contact 1 its order's target, a starfighter
ahead and in range. Rulings K1 (warheads as 1997), K2 (the waypoints and a
weave with no target) and K3 (avoidance turns it round every bulky craft
but its target) each have a test. Where a test needs it, Attack flies the
same question first, to show the situation breaks off or fires.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai import weave
from jedimaster_ai import wire
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import kamikaze
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import BEHAVIORS
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Commands
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import Lock
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Warhead

ME = 4
NOTEBOOK = bytes(range(NOTEBOOK_BYTES))
FOE = 9
OTHER = 10
CHASER = 12
# The engine marks every contact hostile but the craft's own, the first.
ALL_BUT_OWN = 0xFFFFFFFE
# PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER and _ION_LASER (object/laser.h).
LASER = 139
ION_CANNON = 141
STARSHIP = 4
FREIGHTER = 3
# The retail cruiser model's longest side (test_tactics.py) and the
# clearance it gives: three quarters of it, plus 4,096.
CRUISER_SIZE = 18449
CRUISER_KEEP = 18449 * 3 // 4 + 0x1000
# The same model's box (test_hull.py's source): level at yaw 0, its stern
# lies 9,343 behind its position.
CRUISER_BOX = {"box_min": (-2846, -9106, -1076), "box_max": (2845, 9343, 2030)}
# Tier 2's burst at the target's 0x3000 units: pairs, five shots.
BURST = CannonGroup(LinkMode.PAIRS, 5)
HALF = 0x8000
# A launcher whose wait runs: the engine made the warhead the last look asked
# for.
MADE = {"launcher_cooldown": (456, 0)}
# A craft whose engine declared its lock, with one launcher of the named
# warhead ready; its lock ticks past the count needed, so the log can tell
# the two apart.
LOCKED = {
    "warhead_launcher_count": 1,
    "warheads_left": (4, 0),
    "launcher_cooldown": (0, 0),
    "lock_ticks": 720,
    "lock_needed": 708,
    "lock_state": Lock.DECLARED,
}
# Three waypoints ahead and to the right.
P0 = (0, 0x8000, 0)
P1 = (0x8000, 0x8000, 0)
P2 = (0x8000, 0, 0)
WAYPOINTS = {
    "waypoint_mask": 0b111,
    "waypoint_x": (0, 0x8000, 0x8000, 0, 0, 0, 0, 0),
    "waypoint_y": (0x8000, 0x8000, 0, 0, 0, 0, 0, 0),
    "waypoint_z": (0,) * 8,
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
            "order_code": OrderCode.KAMIKAZE,
            "order_codes": (OrderCode.KAMIKAZE, 0, 0, 0),
            "order_throttle": HALF,
            "order_target_mask": 0b110,
            "enemy_mask": ALL_BUT_OWN,
            "turret_slot_mask": 0,
            "cannon_group_count": 2,
            "cannon_type": (ION_CANNON, LASER),
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
            "genus": 0,
            "target": NO_OBJECT,
        }
        return Question(tick, (me,), (contact(**(fields | mine)), *others))

    return make


@pytest.fixture
def ask(shown):
    """Return the craft's Kamikaze decision, its actions checked against the
    engine's reader."""

    def decide(me, *others, tick=1000, **mine):
        question = shown(me, *others, tick=tick, **mine)
        decision = kamikaze.think(me, question)
        wire.check_shown(decision.actions, me, question)
        return decision

    return decide


def memory_of(decision) -> kamikaze.Memory:
    return kamikaze.Memory.read(notebook.decode(decision.notebook).scratch)


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


def written(phase: int, memory: kamikaze.Memory) -> bytes:
    """Return a Kamikaze notebook in phase holding memory."""
    book = notebook.Notebook(
        kamikaze.CODE, phase, dice=1, phase_tick=0, scratch=memory.write()
    )
    return book.encode()


def test_the_order_is_flown_by_kamikaze() -> None:
    assert BEHAVIORS[OrderCode.KAMIKAZE] is kamikaze.think


def test_it_dives_at_its_target_at_full_throttle(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(), foe())
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.throttle == 0xFFFF
    assert decision.actions.target == FOE
    assert phase_of(decision) == kamikaze.DIVE
    assert memory_of(decision).target == FOE
    assert (
        f"kamikaze.dive object={ME} target={FOE} distance={0x3000} genus=0 targets=1"
        in caplog.text
    )


def test_it_steers_at_the_targets_position_above_and_aside(craft, foe, ask) -> None:
    decision = ask(craft(), foe(x=0x1000, y=0x2000, z=0x1800))
    assert decision.actions.steer == Steer(*geometry.bearing(0x1000, 0x2000, 0x1800), 0)


def test_a_dive_asks_for_attacks_think_interval(craft, foe, ask) -> None:
    assert ask(craft(), foe()).actions.think_interval is None
    slow = ask(craft(think_interval=59), foe())
    assert slow.actions.think_interval == attack.FIGHT_INTERVAL


def test_a_sith_lords_dive_thinks_every_8_ticks(craft, foe, ask) -> None:
    assert ask(craft(skill_level=7), foe()).actions.think_interval == 8
    assert (
        ask(craft(skill_level=7, think_interval=8), foe()).actions.think_interval
        is None
    )


def test_it_never_breaks_off_inside_a_bulky_targets_clearance(
    craft, foe, ask, shown
) -> None:
    cruiser = foe(
        y=9343 + 0x1000 - 1,
        pitch=LEVEL,
        genus=STARSHIP,
        size=CRUISER_SIZE,
        **CRUISER_BOX,
    )
    me = craft(order_code=OrderCode.ATTACK)
    broke = attack.think(me, shown(me, cruiser))
    assert notebook.decode(broke.notebook).phase == attack.BREAK
    decision = ask(craft(), cruiser)
    assert phase_of(decision) == kamikaze.DIVE
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.throttle == 0xFFFF


def test_it_picks_the_nearest_target_and_keeps_it_when_another_comes_nearer(
    craft, foe, ask
) -> None:
    first = ask(craft(), foe(), foe(object_index=OTHER, y=0x5000))
    assert first.actions.target == FOE
    nearer = foe(object_index=OTHER, y=0x1000)
    kept = ask(craft(notebook=first.notebook), foe(), nearer, tick=1016)
    assert kept.actions.target == FOE
    assert ask(craft(), foe(), nearer).actions.target == OTHER


def test_a_target_gone_gives_way_to_the_next_with_a_new_dive(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    first = ask(craft(), foe(), foe(object_index=OTHER, y=0x5000))
    caplog.clear()
    gone = ask(craft(notebook=first.notebook), foe(object_index=OTHER, y=0x5000))
    assert gone.actions.target == OTHER
    assert f"kamikaze.dive object={ME} target={OTHER}" in caplog.text


def test_another_craft_in_the_targets_slot_starts_a_new_dive(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    other = foe(object_index=OTHER, y=0x5000)
    first = ask(craft(), foe(), other)
    caplog.clear()
    again = ask(craft(notebook=first.notebook), foe(object_signature=12), other)
    assert memory_of(again).signature == 12
    assert f"kamikaze.dive object={ME} target={FOE}" in caplog.text
    caplog.clear()
    ask(craft(notebook=again.notebook), foe(object_signature=12), other)
    assert "kamikaze.dive" not in caplog.text
    # The craft now in the slot is no longer the one kept: the nearest is.
    farther = foe(object_signature=12, y=0x6000)
    assert ask(craft(notebook=first.notebook), farther, other).actions.target == OTHER


def test_a_players_attack_command_turns_the_dive(craft, foe, ask) -> None:
    first = ask(craft(), foe(), foe(object_index=OTHER, y=0x5000))
    me = craft(notebook=first.notebook, command_target=OTHER)
    turned = ask(me, foe(), foe(object_index=OTHER, y=0x5000))
    assert turned.actions.target == OTHER


def test_an_ignore_command_drops_the_target(craft, foe, ask) -> None:
    first = ask(craft(), foe(), foe(object_index=OTHER, y=0x5000))
    me = craft(notebook=first.notebook, avoid_target=FOE)
    assert ask(me, foe(), foe(object_index=OTHER, y=0x5000)).actions.target == OTHER
    alone = ask(craft(avoid_target=FOE), foe())
    assert phase_of(alone) == kamikaze.CRUISE


def test_it_dives_at_a_disabled_target(craft, foe, ask) -> None:
    decision = ask(craft(), foe(flags=ContactFlags.DISABLED))
    assert decision.actions.target == FOE


def test_lined_up_it_fires_its_cannons_never_its_ion_cannons(craft, foe, ask) -> None:
    assert ask(craft(), foe()).actions.lasers == (HOLD_FIRE, BURST)
    ion_only = craft(cannon_group_count=1, cannon_type=(ION_CANNON, 0))
    assert ask(ion_only, foe()).actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_off_attacks_cone_it_holds_its_cannons(craft, foe, ask) -> None:
    # 0x1000 aside at 0x3000 ahead is about 0xD1C off the nose; tier 2's cone
    # is 0x400.
    decision = ask(craft(), foe(x=0x1000))
    assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert decision.actions.target == FOE


def test_the_aim_cone_narrows_with_skill_as_attacks_does(craft, foe, ask) -> None:
    # 1,823 aside at 0x3000 ahead is 0x602 off the nose: inside tier 0's
    # 0x800 cone, outside tier 2's 0x400.
    # 1,823 above is the same 0x602 off level.
    for off in (foe(x=1823), foe(z=1823)):
        tier_0 = ask(craft(skill=0), off).actions.lasers
        assert tier_0 == (HOLD_FIRE, CannonGroup(LinkMode.PAIRS, 3))
        assert ask(craft(), off).actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_it_fires_inside_its_shots_reach_at_tier_2(craft, foe, ask) -> None:
    # Its laser's shots fly 34,415 from a craft at speed 40, short of tier
    # 2's 40,960 (#308).
    within = ask(craft(), foe(y=34414)).actions.lasers
    assert within == (HOLD_FIRE, CannonGroup(LinkMode.ONE_AT_A_TIME, 5))
    assert ask(craft(), foe(y=34415)).actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_at_a_star_destroyer_it_fires_by_the_distance_to_its_hull(
    craft, foe, ask
) -> None:
    """Its flank, 19,140 off its position, lies 6,000 ahead: inside tier 0's
    24,576 and the 8,192 that fires all cannons at once; its position,
    25,140 ahead, lies outside both."""
    destroyer = foe(
        y=25140,
        yaw=0x4000,
        pitch=LEVEL,
        genus=STARSHIP,
        size=65396,
        box_min=(-19140, -40940, -7132),
        box_max=(19140, 24456, 12570),
    )
    lasers = ask(craft(skill=0), destroyer).actions.lasers
    assert lasers == (HOLD_FIRE, CannonGroup(LinkMode.ALL, 3))


def test_it_fires_a_space_bomb_at_a_starship_as_it_dives(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    decision = ask(bomber, target, target=FOE)
    assert decision.actions.warhead == Warhead(0)
    assert (memory_of(decision).shots, memory_of(decision).pending) == (0, 1)
    assert (
        f"kamikaze.warhead object={ME} target={FOE} launcher=0 "
        f"type={warheads.SPACE_BOMB} lock=720 coming=0 shots=1"
    ) in caplog.text


def test_it_holds_its_warheads_without_the_lock(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    decision = ask(bomber, target, target=OTHER)
    assert decision.actions.warhead is None
    assert (
        f"kamikaze.closing object={ME} target={FOE} distance={0x3000} off_nose=0 "
        'off_level=0 lined_up=1 held="lock" shots=0'
    ) in caplog.text


def test_a_shot_at_a_starfighter_waits_for_the_lock_to_lapse(craft, foe, ask) -> None:
    missile = {**LOCKED, "warhead_type": (warheads.CONCUSSION_MISSILE, 0)}
    fighter = foe(hull_strength=5000)
    first = ask(craft(**missile), fighter, target=FOE)
    assert first.actions.warhead == Warhead(0)
    assert memory_of(first).fresh == 0
    made = {**missile, **MADE, "notebook": first.notebook}
    settled = ask(craft(**made), fighter, target=FOE)
    assert memory_of(settled).fresh == 1
    held = ask(craft(**missile, notebook=settled.notebook), fighter, target=FOE)
    assert held.actions.warhead is None
    lapsed = craft(**(missile | {"lock_state": 0}), notebook=held.notebook)
    assert memory_of(ask(lapsed, fighter, target=FOE)).fresh == 0


def test_a_new_dive_counts_its_warheads_again(craft, foe, ask) -> None:
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    other = foe(object_index=OTHER, y=0x5000, genus=STARSHIP)
    first = ask(bomber, target, other, target=FOE)
    assert memory_of(first).pending == 1
    made = craft(
        **LOCKED | MADE, warhead_type=(warheads.SPACE_BOMB, 0), notebook=first.notebook
    )
    settled = ask(made, target, other, target=FOE)
    assert memory_of(settled).shots == 1
    later = craft(notebook=settled.notebook)
    assert memory_of(ask(later, other)).shots == 0


def test_the_asking_look_counts_nothing_and_the_next_counts_once_made(
    craft, foe, ask
) -> None:
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    first = ask(bomber, target, target=FOE)
    assert (memory_of(first).shots, memory_of(first).pending) == (0, 1)
    made = craft(
        **LOCKED | MADE, warhead_type=(warheads.SPACE_BOMB, 0), notebook=first.notebook
    )
    second = ask(made, target, target=FOE)
    assert (memory_of(second).shots, memory_of(second).pending) == (1, 0)


def test_a_look_with_no_wait_counts_none_logs_it_and_fires_again(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    first = ask(bomber, target, target=FOE)
    again = ask(
        craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0), notebook=first.notebook),
        target,
        target=FOE,
    )
    assert f"kamikaze.warhead_unmade object={ME} target={FOE} launcher=0" in caplog.text
    assert memory_of(again).shots == 0
    assert again.actions.warhead is not None


def test_an_unmade_shot_is_forgotten_when_the_craft_then_holds(craft, foe, ask) -> None:
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    first = ask(bomber, target, target=FOE)
    lapsed = craft(
        **LOCKED | {"lock_state": Lock.COUNTING},
        warhead_type=(warheads.SPACE_BOMB, 0),
        notebook=first.notebook,
    )
    held = ask(lapsed, target, target=FOE)
    assert held.actions.warhead is None
    assert (memory_of(held).shots, memory_of(held).pending) == (0, 0)


def test_a_new_target_drops_the_shot_the_last_look_asked_for(craft, foe, ask) -> None:
    bomber = craft(**LOCKED, warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=STARSHIP, hull_strength=5000)
    other = foe(object_index=OTHER, y=0x5000, genus=STARSHIP)
    first = ask(bomber, target, other, target=FOE)
    gone = craft(
        **LOCKED | MADE, warhead_type=(warheads.SPACE_BOMB, 0), notebook=first.notebook
    )
    second = ask(gone, other, target=OTHER)
    assert memory_of(second).target == OTHER
    assert (memory_of(second).shots, memory_of(second).fresh) == (0, 0)


@pytest.fixture
def cruising(craft):
    """Return a craft with waypoints P0 to P2, its nose on P0, and no target
    in the battle."""

    def make(**changes):
        return craft(**(WAYPOINTS | changes))

    return make


def test_with_no_target_it_cruises_its_waypoints_at_the_order_throttle(
    cruising, ask
) -> None:
    decision = ask(cruising())
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.throttle == HALF
    assert decision.actions.target is None
    assert phase_of(decision) == kamikaze.CRUISE
    assert memory_of(decision).route == patrol.Route()


def test_reaching_a_waypoint_it_makes_for_the_next(cruising, ask) -> None:
    decision = ask(cruising(y=0x8000 - 0x0FFF))
    assert memory_of(decision).route.waypoint == 1
    assert decision.actions.steer.yaw == geometry.bearing(0x8000, 0x0FFF, 0)[0]


def test_a_new_order_slot_starts_the_route_again(cruising, ask) -> None:
    flown = written(kamikaze.CRUISE, kamikaze.Memory(route=patrol.Route(waypoint=2)))
    same = ask(cruising(notebook=flown))
    assert memory_of(same).route.waypoint == 2
    moved = ask(cruising(notebook=flown, order_slot=1))
    assert memory_of(moved).route == patrol.Route(slot=1)


def test_the_cruise_after_a_dive_is_logged(cruising, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    first = ask(cruising(), foe())
    caplog.clear()
    ask(cruising(notebook=first.notebook))
    assert f"kamikaze.cruise object={ME} targets=0 weaving=0 waypoint=0" in caplog.text


def test_with_no_target_it_weaves_from_an_enemy_on_its_tail(cruising, foe, ask) -> None:
    chaser = foe(object_index=CHASER, y=-0x2000, target=ME)
    decision = ask(cruising(order_target_mask=0), chaser)
    assert memory_of(decision).weave.flying
    assert decision.actions.throttle == 0xFFFF
    assert decision.actions.steer.yaw in {0x3000, 0x10000 - 0x3000}
    assert phase_of(decision) == kamikaze.CRUISE


def test_a_weave_under_way_is_flown_out_before_the_dive(cruising, foe, ask) -> None:
    """The chaser is contact 1 and the order names contact 2: a weave starts
    with no target shown, goes on once the target shows, ends once the tail
    is clear, and the dive comes at the next think."""
    chaser = foe(object_index=CHASER, y=-0x2000, target=ME)
    me = cruising(order_target_mask=0b100)
    weaving = ask(me, chaser)
    assert memory_of(weaving).weave.flying
    me = cruising(order_target_mask=0b100, notebook=weaving.notebook)
    still = ask(me, chaser, foe(), tick=1016)
    assert phase_of(still) == kamikaze.CRUISE
    assert memory_of(still).weave.flying
    me = cruising(order_target_mask=0b100, notebook=still.notebook)
    clear = ask(me, foe(object_index=CHASER, y=-0x2000), foe(), tick=1032)
    assert not memory_of(clear).weave.flying
    me = cruising(order_target_mask=0b100, notebook=clear.notebook)
    dive = ask(me, foe(object_index=CHASER, y=-0x2000), foe(), tick=1048)
    assert dive.actions.target == FOE


def test_a_dive_takes_no_notice_of_an_enemy_on_its_tail(craft, foe, ask) -> None:
    chaser = foe(object_index=CHASER, y=-0x2000, target=ME)
    decision = ask(craft(order_target_mask=0b10), foe(), chaser)
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert not memory_of(decision).weave.flying


def test_an_evade_command_drops_the_target_and_weaves(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    first = ask(craft(), foe())
    me = craft(notebook=first.notebook, commands=Commands.EVADE)
    decision = ask(me, foe())
    assert decision.actions.evaded
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert phase_of(decision) == kamikaze.EVADE
    assert memory_of(decision).ordered == 1
    assert memory_of(decision).weave.flying
    assert f"kamikaze.evade_ordered object={ME} chaser={NO_OBJECT}" in caplog.text


def test_a_sith_lord_ordered_to_evade_weaves_every_8_ticks(craft, foe, ask) -> None:
    first = ask(craft(skill_level=7), foe())
    lord = craft(skill_level=7, notebook=first.notebook, commands=Commands.EVADE)
    assert ask(lord, foe()).actions.think_interval == 8


def test_the_evade_weave_is_flown_out_unchased_then_it_dives(craft, foe, ask) -> None:
    flying = weave.Weave(swerves=1, swerve_ends=1100)
    ordered = written(kamikaze.EVADE, kamikaze.Memory(weave=flying, ordered=1))
    still = ask(craft(notebook=ordered), foe())
    assert phase_of(still) == kamikaze.EVADE
    assert still.actions.target == NO_OBJECT
    last = weave.Weave(swerves=weave.SWERVES_MAX, swerve_ends=900)
    done = written(kamikaze.EVADE, kamikaze.Memory(weave=last, ordered=1))
    dive = ask(craft(notebook=done), foe())
    assert dive.actions.target == FOE
    assert phase_of(dive) == kamikaze.DIVE
    assert memory_of(dive).ordered == 0


def test_avoidance_leaves_its_target_alone_and_turns_it_round_another(
    craft, foe, shown
) -> None:
    cruiser = foe(y=CRUISER_KEEP - 1, genus=STARSHIP, size=CRUISER_SIZE)
    me = craft()
    question = shown(me, cruiser)
    dive = kamikaze.think(me, question)
    assert avoidance.steer_clear(me, question, dive) == dive
    far = foe(y=0x10000, genus=STARSHIP)
    freighter = foe(object_index=OTHER, y=0x4000, genus=FREIGHTER, size=0x2000)
    question = shown(craft(order_target_mask=0b10), far, freighter)
    dive = kamikaze.think(craft(order_target_mask=0b10), question)
    assert dive.actions.target == FOE
    turned = avoidance.steer_clear(craft(order_target_mask=0b10), question, dive)
    assert turned.actions.steer.yaw != 0


def test_every_phase_shows_kamikaze_attack(craft, foe, cruising, ask) -> None:
    """1997's line 191 for all three plans: the dive, the cruise and an evade
    command's weave."""
    dive = ask(craft(), foe())
    cruise = ask(cruising())
    evade = ask(craft(notebook=dive.notebook, commands=Commands.EVADE), foe())
    phases = [phase_of(decision) for decision in (dive, cruise, evade)]
    assert phases == [kamikaze.DIVE, kamikaze.CRUISE, kamikaze.EVADE]
    for decision in (dive, cruise, evade):
        assert decision.actions.activity == Activity.KAMIKAZE_ATTACK


def test_another_behavior_gives_the_line_back(craft) -> None:
    """A craft still showing "Kamikaze attack" under a behavior that names no
    line shows none; a line that behavior names, or another left behind,
    stays."""
    quiet = Decision(NOTEBOOK, Actions(throttle=0))
    showing = craft(activity=Activity.KAMIKAZE_ATTACK)
    given_back = Decision(NOTEBOOK, Actions(throttle=0, activity=Activity.NONE))
    assert kamikaze.ended(showing, quiet) == given_back
    named = Decision(NOTEBOOK, Actions(activity=Activity.GOING_HOME))
    assert kamikaze.ended(showing, named) == named
    other = craft(activity=Activity.SELF_DESTRUCTING)
    assert kamikaze.ended(other, quiet) == quiet
