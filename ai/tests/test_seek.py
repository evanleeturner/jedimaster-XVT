"""Fence for seek: where a craft on a fighting order flies while it has nothing
to fight. The craft sits at the origin facing +y (yaw 0), level, on the Attack
order at half throttle, with no target; contact 0 is its own. Nowhere to
cruise means no waypoint on and no start point given.
"""

from __future__ import annotations

import dataclasses
import pathlib
import subprocess
import sys

import pytest

import jedimaster_ai
from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import disable
from jedimaster_ai.behaviors import escort
from jedimaster_ai.behaviors import formation
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.behaviors import protect
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import Commands
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
FOE = 9
CHASER = 12
LEADER = 7
ALL_BUT_OWN = 0xFFFFFFFE
HALF = 0x8000
FIGHTER = 0
FREIGHTER = 3
STARSHIP = 4
ATTACK = 7
PROTECT = 9
ESCORT = 10
DISABLE = 11
P0 = (0, 0x10000, 0)
P1 = (0x10000, 0x10000, 0)
P2 = (0x10000, 0, 0)
START = (-0x10000, 0, 0)
HELD = (HOLD_FIRE, HOLD_FIRE)
HURT = {"hull_strength": 400, "hull_damage": 101}
BEHIND = {"object_index": CHASER, "y": -0x2000, "target": ME}


def toward(point, origin=(0, 0, 0)) -> Steer:
    yaw, pitch = geometry.bearing(*(p - o for p, o in zip(point, origin, strict=True)))
    return Steer(yaw, pitch, 0)


def points(*positions) -> dict:
    """Return the view fields with waypoints 0, 1, ... on at these positions,
    the rest off."""
    axes = [[0] * 8 for _ in range(3)]
    for number, position in enumerate(positions):
        for axis in range(3):
            axes[axis][number] = position[axis]
    return {
        "waypoint_mask": (1 << len(positions)) - 1,
        "waypoint_x": tuple(axes[0]),
        "waypoint_y": tuple(axes[1]),
        "waypoint_z": tuple(axes[2]),
    }


ROUND = points(P0, P1, P2)
GIVEN_START = {"books": Books.TARGETS_LEFT | Books.START_POINT, "start_point": START}


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
            "order_code": ATTACK,
            "order_target_mask": 0,
            "order_throttle": HALF,
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
            "speed": 0,
            "target": NO_OBJECT,
            "genus": FIGHTER,
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
            "flight_group": me.flight_group,
            "genus": FIGHTER,
            "x": me.x,
            "y": me.y,
            "z": me.z,
        }
        return Question(tick, (me,), (contact(**(fields | mine)), *others))

    return make


@pytest.fixture
def ask(shown):
    """Return the decision of the craft's order behavior, its actions checked
    against the engine's reader."""

    def decide(me, *others, behavior=attack.think, tick=1000, **mine):
        question = shown(me, *others, tick=tick, **mine)
        decision = behavior(me, question)
        wire.check_shown(decision.actions, me, question)
        return decision

    return decide


def memory_of(decision) -> attack.Memory:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch)


def seeking(decision) -> bool:
    return notebook.decode(decision.notebook).phase == attack.SEEK


def test_with_no_target_it_makes_for_waypoint_0_at_the_order_throttle(
    craft, ask
) -> None:
    decision = ask(craft(**ROUND))
    assert decision.actions == Actions(
        steer=toward(P0), throttle=HALF, target=NO_OBJECT, lasers=HELD
    )
    assert seeking(decision)
    assert memory_of(decision).route == patrol.Route(slot=0, waypoint=0)


def test_a_cruise_keeps_the_fight_think_interval(craft, ask) -> None:
    actions = ask(craft(think_interval=59, **ROUND)).actions
    assert actions.think_interval == attack.FIGHT_INTERVAL


def test_a_waypoint_reached_sends_it_on_to_the_next(craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    near = (0, 0x10000 - 4000, 0)
    decision = ask(craft(x=near[0], y=near[1], **ROUND))
    assert decision.actions.steer == toward(P1, near)
    assert memory_of(decision).route.waypoint == 1
    assert f"patrol.reached object={ME} waypoint=0 next=1 rounds=0" in caplog.text


@pytest.fixture
def at_waypoint_1(craft, ask):
    """Return the notebook of a craft that has just reached waypoint 0."""
    return ask(craft(y=0x10000 - 4000, **ROUND)).notebook


def test_the_route_outlasts_a_fight(craft, foe, ask, at_waypoint_1) -> None:
    fought = ask(craft(order_target_mask=0b10, notebook=at_waypoint_1, **ROUND), foe())
    assert fought.actions.target == FOE
    after = ask(craft(notebook=fought.notebook, **ROUND))
    assert after.actions.steer == toward(P1)
    assert memory_of(after).route.waypoint == 1


def test_the_route_outlasts_a_weave(craft, foe, ask, at_waypoint_1) -> None:
    weaving = ask(craft(notebook=at_waypoint_1, **ROUND), foe(**BEHIND), **HURT)
    assert notebook.decode(weaving.notebook).phase == attack.EVADE
    assert memory_of(weaving).route.waypoint == 1


def test_the_route_outlasts_an_evade_command(craft, ask, at_waypoint_1) -> None:
    evading = ask(craft(commands=Commands.EVADE, notebook=at_waypoint_1, **ROUND))
    assert evading.actions.evaded
    assert memory_of(evading).route.waypoint == 1


def test_a_route_written_on_another_order_slot_starts_again(craft, ask) -> None:
    written = attack.Memory(route=patrol.Route(slot=0, waypoint=2)).write()
    book = notebook.Notebook(BehaviorCode.ATTACK, attack.SEEK, 77, 990, written)
    same = ask(craft(order_slot=0, notebook=book.encode(), **ROUND))
    assert same.actions.steer == toward(P2)
    other = ask(craft(order_slot=1, notebook=book.encode(), **ROUND))
    assert other.actions.steer == toward(P0)
    assert memory_of(other).route == patrol.Route(slot=1, waypoint=0)


@pytest.mark.parametrize("waypoints", [ROUND, {}])
def test_a_follower_flies_its_place_beside_its_leader(
    craft, contact, shown, waypoints
) -> None:
    me = craft(
        leader=LEADER,
        books=Books.TARGETS_LEFT | Books.PLACE | Books.START_POINT,
        place=(0x400, 0x800, 0),
        start_point=START,
        full_speed=300,
        **waypoints,
    )
    leader = contact(
        object_index=LEADER,
        team=1,
        y=0x1000,
        speed=200,
        activity=Activity.NONE,
        target=NO_OBJECT,
    )
    question = shown(me, leader)
    flown = formation.fly(me, question, leader)
    assert flown.steer is not None
    actions = attack.think(me, question).actions
    assert actions == dataclasses.replace(
        flown, think_interval=None, target=NO_OBJECT, lasers=HELD
    )


def test_with_no_waypoint_it_makes_for_its_group_start_point(
    craft, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(**GIVEN_START))
    assert decision.actions == Actions(
        steer=toward(START), throttle=HALF, target=NO_OBJECT, lasers=HELD
    )
    assert seeking(decision)
    assert f"seek.start_point object={ME} distance=65536 throttle=32768" in caplog.text


def test_a_fighter_at_its_start_point_flies_on_round_it(craft, ask) -> None:
    near = (-0x10000 + 4000, 0, 0)
    actions = ask(craft(x=near[0], **GIVEN_START)).actions
    assert actions.steer == toward(START, near)
    assert actions.throttle == HALF


@pytest.mark.parametrize(
    ("genus", "gap", "throttle"),
    [
        (FREIGHTER, 4095, 0),
        (FREIGHTER, 4096, HALF),
        (STARSHIP, 8191, 0),
        (STARSHIP, 8192, HALF),
    ],
)
def test_a_starship_or_freighter_stops_within_reach_of_its_start_point(
    craft, ask, genus, gap, throttle
) -> None:
    """A starship's reach is 8191, a freighter's 4095; it faces the point."""
    yaw = toward(START).yaw
    me = craft(x=-0x10000 + gap, yaw=yaw, **GIVEN_START)
    assert ask(me, genus=genus).actions.throttle == throttle


@pytest.mark.parametrize(("off", "throttle"), [(0x0FFF, HALF), (0x1000, 0)])
def test_a_starship_turned_0x1000_off_its_start_point_waits_on_its_turn(
    craft, ask, off, throttle
) -> None:
    yaw = toward(START).yaw
    actions = ask(
        craft(yaw=(yaw + off) & 0xFFFF, **GIVEN_START), genus=STARSHIP
    ).actions
    assert actions.steer.yaw == yaw
    assert actions.throttle == throttle


def test_a_bulky_craft_in_the_way_turns_it_on_its_way_to_the_start_point(
    craft, contact, shown
) -> None:
    me = craft(**GIVEN_START)
    hulk = contact(
        object_index=20, team=2, genus=STARSHIP, x=-0x2000, y=0, z=0, size=4000
    )
    question = shown(me, hulk)
    straight = toward(START)
    turned = avoidance.clear_heading(
        me, question, straight.yaw, straight.pitch, NO_OBJECT
    )
    assert turned[0] != straight.yaw
    steer = attack.think(me, question).actions.steer
    assert (steer.yaw, steer.pitch) == turned


def test_over_a_big_ships_deck_its_dive_to_the_start_point_levels_off(
    craft, contact, shown
) -> None:
    """The start point lies 45 degrees down to the west, past a Star
    Destroyer's deck 6,000 below: its steer carries the pitch avoidance
    levels the dive off to, not the dive (``avoidance.clear_heading``)."""
    start = (-0x10000, 0, -0x10000)
    me = craft(books=GIVEN_START["books"], start_point=start)
    deck = contact(
        object_index=20,
        team=1,
        genus=STARSHIP,
        size=65396,
        x=0,
        y=0,
        z=-(12570 + 6000),
        yaw=0,
        pitch=LEVEL,
        roll=0,
        target=NO_OBJECT,
        box_min=(-19140, -40940, -7132),
        box_max=(19140, 24456, 12570),
    )
    question = shown(me, deck)
    dive = toward(start)
    leveled = avoidance.clear_heading(me, question, dive.yaw, dive.pitch, NO_OBJECT)
    assert leveled[0] == dive.yaw and leveled[1] < LEVEL < dive.pitch
    steer = attack.think(me, question).actions.steer
    assert (steer.yaw, steer.pitch) == leveled


def test_with_nowhere_to_cruise_it_holds_its_course(craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    actions = ask(craft()).actions
    assert actions.steer is None and actions.throttle is None
    assert f"seek.no_point object={ME}" in caplog.text


def test_a_weave_comes_before_the_cruise(craft, foe, ask) -> None:
    decision = ask(craft(**ROUND), foe(**BEHIND), **HURT)
    assert notebook.decode(decision.notebook).phase == attack.EVADE
    assert decision.actions.steer != toward(P0)


def test_a_fight_a_behavior_gives_cruises_only_when_asked(craft, shown) -> None:
    """``home`` turns on an attacker with ``attack.fight``: with no target left
    it never makes for a waypoint."""
    me = craft(**ROUND)
    actions = attack.fight(me, shown(me), []).actions
    assert actions.steer is None and actions.throttle is None
    asked = attack.fight(me, shown(me), [], cruising=True).actions
    assert asked.steer == toward(P0)


@pytest.mark.parametrize(
    ("order", "behavior"),
    [(DISABLE, disable.think), (PROTECT, protect.think), (ESCORT, escort.think)],
)
def test_disable_protect_and_escort_with_no_host_cruise(
    craft, ask, order, behavior
) -> None:
    decision = ask(craft(order_code=order, **ROUND), behavior=behavior)
    assert decision.actions.steer == toward(P0)
    assert decision.actions.throttle == HALF


def test_an_escort_keeping_station_flies_no_route(craft, contact, ask, caplog) -> None:
    """Its route stays where it was, and no waypoint line speaks for it."""
    caplog.set_level("DEBUG")
    host = contact(object_index=2, team=1, flight_group=2, x=0, y=0x1000, z=0)
    me = craft(
        order_code=ESCORT,
        y=0x10000 - 4000,
        books=Books.TARGETS_LEFT | Books.STATION,
        escorted=2,
        enemy_mask=0,
        station=(0x800, 0x1000, 0),
        **ROUND,
    )
    decision = ask(me, host, behavior=escort.think)
    assert seeking(decision)
    assert memory_of(decision).route.waypoint == 0
    assert "patrol.reached" not in caplog.text


def test_the_memory_keeps_the_route() -> None:
    memory = attack.Memory(target=9, shots=2, route=patrol.Route(1, 2, 3, 1))
    assert attack.Memory.read(memory.write()) == memory


@pytest.mark.parametrize("first", ["patrol", "attack", "seek"])
def test_each_module_of_the_cruise_imports_first_on_its_own(first) -> None:
    """patrol and attack import each other: each loads cleanly before the
    other has."""
    script = (
        f"import jedimaster_ai.behaviors.{first}\n"
        "from jedimaster_ai.behaviors import attack\n"
        "attack.Memory()\n"
    )
    package_root = pathlib.Path(jedimaster_ai.__file__).parents[1]
    result = subprocess.run(
        [sys.executable, "-c", script],
        capture_output=True,
        text=True,
        cwd=package_root,
        check=False,
    )
    assert result.returncode == 0, result.stderr
