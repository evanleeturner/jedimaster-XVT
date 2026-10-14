"""Fence for patrol: Circle, Circle and Evade and the starship patrols flying
their group's waypoints.

Each rule once: the first waypoint at the order's throttle; reaching one, by
exact distance, within 4,095 units (8,191 for a starship); back to waypoint 0
past the last one on, a round counted; rounds kept per order slot; the goals
of the four orders; one waypoint (bug #205 as a check), and a starship or
freighter stopping on it; no waypoint on; five times the order's speed at
full throttle (ruling 39); a starship off its course, and turned round a
bulky craft in its way without waiting on it for good; Circle and Evade
weaving while chased and going back to its round (ruling 29), Circle not; a
follower flying the round too; the log lines. Then a real case: a
three-point Circle flown round once, done, and going home.
"""

from __future__ import annotations

import dataclasses

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import progress
from jedimaster_ai import weave
from jedimaster_ai import wire
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Books
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
CHASER = 12
# The craft sits at (-5, 6, 7) (conftest's VIEW), heading +x toward P0.
HERE = (-5, 6, 7)
P0 = (100_000, 6, 7)
P1 = (100_000, 100_000, 7)
P2 = (-5, 100_000, 7)
CIRCLE = 2
CIRCLE_AND_EVADE = 3
SS_PATROL_LOOP = 21
SS_PATROL_AND_ATTACK = 26
STARSHIP = 4
FREIGHTER = 3
FIGHTER = 0
HALF = 0x8000


def toward(point, origin=HERE, roll=300) -> Steer:
    yaw, pitch = geometry.bearing(*(p - o for p, o in zip(point, origin, strict=True)))
    return Steer(yaw, pitch, roll)


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


def book(slot=0, waypoint=0, rounds=0, held=0) -> bytes:
    """Return a patrol notebook (behavior 4) with this route."""
    scratch = bytes((slot, waypoint, rounds, held))
    scratch += bytes(notebook.SCRATCH_BYTES - len(scratch))
    return notebook.Notebook(4, 0, 1, 0, scratch).encode()


def route(raw: bytes) -> tuple[int, int, int, int]:
    """Return a patrol notebook's slot, waypoint, rounds and held bytes."""
    decoded = notebook.decode(raw)
    assert decoded.behavior == 4
    return tuple(decoded.scratch[:4])


def flown(raw: bytes) -> weave.Weave:
    return weave.Weave.read(notebook.decode(raw).scratch[4:])


@pytest.fixture
def mine(contact):
    """Return the craft's own contact, of genus FIGHTER unless named."""

    def make(craft, **changes):
        fields = {"object_index": ME, "team": 1, "genus": FIGHTER, "target": NO_OBJECT}
        fields |= {"x": craft.x, "y": craft.y, "z": craft.z}
        return contact(**(fields | changes))

    return make


@pytest.fixture
def enemy(contact):
    """Return a small enemy craft on the craft's tail, 0x2000 behind it."""

    def make(**changes):
        fields = {"object_index": CHASER, "team": 2, "genus": 0, "target": ME}
        fields |= {"x": -5 - 0x2000, "y": 6, "z": 7, "speed": 0}
        return contact(**(fields | changes))

    return make


@pytest.fixture
def patrolling(view):
    """Return a Circle craft with waypoints P0 to P2, nose on P0, at half
    throttle, its fields changed as named."""

    def make(**changes):
        fields = {
            "order_code": CIRCLE,
            "order_codes": (CIRCLE, 0, 0, 0),
            "order_throttle": HALF,
            "yaw": toward(P0).yaw,
            "pitch": geometry.LEVEL,
        } | points(P0, P1, P2)
        return view(**(fields | changes))

    return make


def decide(craft, contacts, tick=1000):
    """Return the patrol's decision, its actions checked against the engine's
    reader."""
    question = Question(tick, (craft,), tuple(contacts))
    decision = patrol.think(craft, question)
    wire.check_shown(decision.actions, craft, question)
    return decision


def test_a_fresh_craft_makes_for_waypoint_0_at_its_order_throttle(
    patrolling, mine
) -> None:
    craft = patrolling()
    decision = decide(craft, [mine(craft)])
    assert decision.actions == Actions(steer=toward(P0), throttle=HALF)
    assert route(decision.notebook) == (0, 0, 0, 0)


@pytest.mark.parametrize(
    ("genus", "gap", "reached"),
    [
        (FIGHTER, 4095, True),
        (FIGHTER, 4096, False),
        (FREIGHTER, 4095, True),
        (FREIGHTER, 4096, False),
        (STARSHIP, 8191, True),
        (STARSHIP, 8192, False),
    ],
)
def test_a_waypoint_is_reached_within_4095_or_8191_for_a_starship(
    patrolling, mine, genus, gap, reached
) -> None:
    at = (P0[0] - gap, 6, 7)
    craft = patrolling(x=at[0])
    decision = decide(craft, [mine(craft, genus=genus)])
    assert route(decision.notebook)[1] == (1 if reached else 0)
    assert decision.actions.steer == toward(P1 if reached else P0, origin=at)


def test_past_the_last_waypoint_on_it_goes_back_to_0_and_counts_a_round(
    patrolling, mine
) -> None:
    craft = patrolling(x=P2[0], y=P2[1], notebook=book(waypoint=2, rounds=4))
    decision = decide(craft, [mine(craft)])
    assert route(decision.notebook) == (0, 0, 5, 0)
    assert decision.actions.steer == toward(P0, origin=P2)


def test_waypoint_7_is_flown_and_after_it_back_to_0(patrolling, mine) -> None:
    eight = [(1000 * n, 6, 7) for n in range(1, 9)]
    at_6 = patrolling(**points(*eight), x=7000, notebook=book(waypoint=6))
    assert route(decide(at_6, [mine(at_6)]).notebook) == (0, 7, 0, 0)
    at_7 = patrolling(**points(*eight), x=8000, notebook=book(waypoint=7))
    assert route(decide(at_7, [mine(at_7)]).notebook) == (0, 0, 1, 0)


def test_rounds_count_for_the_order_slot_they_were_flown_on(
    patrolling, mine, enemy
) -> None:
    chased = patrolling(
        order_code=CIRCLE_AND_EVADE, notebook=book(waypoint=2, rounds=3)
    )
    evading = decide(chased, [mine(chased), enemy()])
    assert patrol.rounds(patrolling(notebook=evading.notebook)) == 3
    assert flown(evading.notebook).flying
    craft = patrolling(order_slot=1, notebook=evading.notebook)
    assert patrol.rounds(craft) == 0
    decision = decide(craft, [mine(craft)])
    assert route(decision.notebook) == (1, 0, 0, 0)
    assert not flown(decision.notebook).flying


@pytest.mark.parametrize(
    ("variable1", "rounds", "done"), [(2, 1, False), (2, 2, True), (0, 0, True)]
)
def test_rounds_are_done_once_they_reach_variable1(
    patrolling, variable1, rounds, done
) -> None:
    craft = patrolling(order_variable1=variable1, notebook=book(rounds=rounds))
    assert patrol.rounds_done(craft) is done


def test_a_notebook_another_behavior_wrote_holds_no_rounds(patrolling) -> None:
    attack_book = notebook.Notebook(1, 0, 1, 0, bytes((0, 0, 9, 0)) + bytes(48))
    assert patrol.rounds(patrolling(notebook=attack_book.encode())) == 0
    assert patrol.rounds(patrolling(notebook=bytes(64))) == 0


def test_bug_205_one_waypoint_counts_a_round_each_return_not_each_think(
    patrolling, mine
) -> None:
    lone = points(P0)
    craft = patrolling(**lone, x=P0[0] - 100)
    first = decide(craft, [mine(craft)])
    assert route(first.notebook) == (0, 0, 1, 1)
    craft = patrolling(**lone, x=P0[0] - 100, notebook=first.notebook)
    second = decide(craft, [mine(craft)], tick=1016)
    assert route(second.notebook) == (0, 0, 1, 1)
    assert second.actions.throttle == HALF
    away = patrolling(**lone, x=P0[0] + 5000, notebook=second.notebook)
    third = decide(away, [mine(away)], tick=1032)
    assert route(third.notebook) == (0, 0, 1, 0)
    back = patrolling(**lone, x=P0[0] + 100, notebook=third.notebook)
    assert route(decide(back, [mine(back)], tick=1048).notebook) == (0, 0, 2, 1)


@pytest.mark.parametrize("genus", [FREIGHTER, STARSHIP])
def test_a_starship_or_freighter_stops_for_good_on_its_one_waypoint(
    patrolling, mine, genus
) -> None:
    craft = patrolling(order_code=SS_PATROL_LOOP, **points(P0), x=P0[0] - 100)
    stopped = decide(craft, [mine(craft, genus=genus)])
    assert stopped.actions == Actions(throttle=0)
    assert route(stopped.notebook) == (0, 0, 1, 1)
    drifted = patrolling(**points(P0), x=P0[0] + 9000, notebook=stopped.notebook)
    later = decide(drifted, [mine(drifted, genus=genus)], tick=1016)
    assert later.actions == Actions(throttle=0)
    assert route(later.notebook) == (0, 0, 1, 1)


def test_with_no_waypoint_on_it_stops_and_counts_one_round(patrolling, mine) -> None:
    craft = patrolling(**points())
    first = decide(craft, [mine(craft)])
    assert first.actions == Actions(throttle=0)
    assert route(first.notebook) == (0, 0, 1, 1)
    again = patrolling(**points(), notebook=first.notebook)
    assert route(decide(again, [mine(again)]).notebook) == (0, 0, 1, 1)


@pytest.mark.parametrize(
    ("order_throttle", "speed", "throttle"),
    [
        (0xFFFF, 10, 10922),  # speed 50 of full speed 300
        (0xFFFF, 0, 0xFFFF),
        (HALF, 10, HALF),
        (0xFFFF, 60, 0xFFFF),  # 300 is all it has
    ],
)
def test_full_throttle_with_a_speed_flies_five_times_the_speed(
    patrolling, mine, order_throttle, speed, throttle
) -> None:
    craft = patrolling(order_throttle=order_throttle, order_speed=speed)
    assert decide(craft, [mine(craft)]).actions.throttle == throttle


@pytest.mark.parametrize(
    ("genus", "off", "throttle"),
    [
        (STARSHIP, 0x1000, 0),
        (STARSHIP, -0x1000, 0),
        (STARSHIP, 0x0FFF, HALF),
        (FREIGHTER, 0x1000, HALF),
    ],
)
def test_a_starship_4096_or_more_off_its_course_stops(
    patrolling, mine, genus, off, throttle
) -> None:
    craft = patrolling(order_code=SS_PATROL_LOOP, yaw=(toward(P0).yaw + off) % 0x10000)
    actions = decide(craft, [mine(craft, genus=genus)]).actions
    assert actions == Actions(steer=toward(P0), throttle=throttle)


@pytest.fixture
def frigate(contact):
    """Return a frigate on the course to P0, 20,005 units ahead: within its
    clearance (11,872) and LOOK_AHEAD."""

    def make(**changes):
        fields = {"object_index": 30, "team": 1, "genus": STARSHIP, "size": 10368}
        fields |= {"x": 20_000, "y": 6, "z": 7, "target": NO_OBJECT}
        return contact(**(fields | changes))

    return make


def test_a_starship_turns_round_a_starship_in_its_way_and_moves_on(
    patrolling, mine, frigate, caplog
) -> None:
    craft = patrolling(order_code=SS_PATROL_LOOP)
    around = avoidance.in_the_way(
        craft,
        Question(1000, (craft,), (frigate(),)),
        toward(P0).yaw,
        geometry.LEVEL,
        NO_OBJECT,
    )
    clear = around.past_edge(toward(P0).yaw, craft.yaw)
    assert geometry.angle_between(clear, toward(P0).yaw) >= patrol.STARSHIP_TURN
    with caplog.at_level("DEBUG"):
        turning = decide(craft, [mine(craft, genus=STARSHIP), frigate()])
    assert turning.actions == Actions(
        steer=dataclasses.replace(toward(P0), yaw=clear), throttle=0
    )
    assert "avoidance.steer object=4 around=30" in caplog.text
    facing = patrolling(order_code=SS_PATROL_LOOP, yaw=clear)
    moving = decide(facing, [mine(facing, genus=STARSHIP), frigate()])
    assert moving.actions == Actions(
        steer=dataclasses.replace(toward(P0), yaw=clear), throttle=HALF
    )


def test_a_starship_does_not_turn_round_its_own_target(
    patrolling, mine, frigate
) -> None:
    craft = patrolling(order_code=SS_PATROL_LOOP)
    own = mine(craft, genus=STARSHIP, target=30)
    assert decide(craft, [own, frigate()]).actions.steer == toward(P0)


def test_think_turns_a_patrolling_starship_round_once(
    patrolling, mine, frigate
) -> None:
    craft = patrolling(
        order_code=SS_PATROL_LOOP,
        order_codes=(SS_PATROL_LOOP, 0, 0, 0),
        order_variable1=1,
    )
    alone = decide(craft, [mine(craft, genus=STARSHIP), frigate()]).actions.steer
    answer = think(Question(1000, (craft,), (mine(craft, genus=STARSHIP), frigate())))
    assert answer.craft[0].actions.steer == alone
    assert alone != toward(P0)


def test_circle_and_evade_weaves_about_its_course_with_an_enemy_on_its_tail(
    patrolling, mine, enemy, caplog
) -> None:
    caplog.set_level("DEBUG")
    # Its nose 0x1000 off its course to P0: the weave is about the course.
    nose = (toward(P0).yaw + 0x1000) % 0x10000
    craft = patrolling(order_code=CIRCLE_AND_EVADE, skill=0, yaw=nose)
    decision = decide(craft, [mine(craft), enemy()])
    course = toward(P0).yaw
    assert flown(decision.notebook).flying
    assert flown(decision.notebook).course == course
    assert decision.actions.steer.yaw in {
        (course + d) % 0x10000 for d in (0x3000, -0x3000)
    }
    assert decision.actions.throttle == 0xFFFF
    assert f"weave.start object={ME} threat={CHASER} course={course}" in caplog.text


def test_circle_leaves_an_enemy_on_its_tail_to_its_turrets(
    patrolling, mine, enemy
) -> None:
    craft = patrolling()
    decision = decide(craft, [mine(craft), enemy()])
    assert decision.actions == Actions(steer=toward(P0), throttle=HALF)
    assert not flown(decision.notebook).flying


def test_circle_and_evade_goes_back_to_its_round_once_the_tail_is_clear(
    patrolling, mine, enemy
) -> None:
    craft = patrolling(order_code=CIRCLE_AND_EVADE)
    evading = decide(craft, [mine(craft), enemy()])
    later = patrolling(order_code=CIRCLE_AND_EVADE, notebook=evading.notebook)
    clear = decide(later, [mine(later), enemy(target=15)], tick=1016)
    assert clear.actions == Actions(steer=toward(P0), throttle=HALF)
    assert not flown(clear.notebook).flying
    assert route(clear.notebook) == (0, 0, 0, 0)


def test_a_follower_flies_the_round_too_and_moves_no_slot(patrolling, mine) -> None:
    craft = patrolling(leader=7, order_variable1=1, notebook=book(rounds=1))
    assert decide(craft, [mine(craft)]).actions.steer == toward(P0)
    assert progress.decide(craft) == progress.STAY


@pytest.mark.parametrize("order", [CIRCLE, CIRCLE_AND_EVADE, SS_PATROL_LOOP])
def test_an_order_whose_rounds_reach_variable1_is_done(patrolling, order) -> None:
    craft = patrolling(
        order_code=order,
        order_codes=(order, CIRCLE, 0, 0),
        order_variable1=2,
        notebook=book(rounds=2),
    )
    assert progress.decide(craft) == progress.Progress(move=OrderMove(1, 2))
    fewer = dataclasses.replace(craft, notebook=book(rounds=1))
    assert progress.decide(fewer) == progress.STAY


def test_ss_patrol_and_attack_is_done_once_no_target_is_left(patrolling) -> None:
    fields = {
        "order_code": SS_PATROL_AND_ATTACK,
        "order_codes": (SS_PATROL_AND_ATTACK, 0, 0, 0),
        "order_variable1": 0,
    }
    left = patrolling(**fields, books=Books.TARGETS_LEFT, notebook=book(rounds=9))
    assert progress.decide(left) == progress.STAY
    none = patrolling(**fields, books=0)
    assert progress.decide(none) == progress.Progress(home=True, move=OrderMove(0, 2))


def test_the_log_names_each_waypoint_reached_and_a_hold(
    patrolling, mine, caplog
) -> None:
    caplog.set_level("DEBUG")
    craft = patrolling(x=P0[0] - 10)
    decide(craft, [mine(craft)])
    held = patrolling(**points())
    decide(held, [mine(held)])
    assert f"patrol.reached object={ME} waypoint=0 next=1 rounds=0" in caplog.text
    assert f"patrol.hold object={ME} rounds=1" in caplog.text


def test_the_view_log_carries_the_waypoints(patrolling, mine, caplog) -> None:
    caplog.set_level("DEBUG")
    craft = patrolling(**points(P0, (-1, -2, -3)))
    think(Question(1000, (craft,), (mine(craft),)))
    assert " waypoints=3 " in caplog.text
    assert f"think.waypoint object={ME} waypoint=0 x=100000 y=6 z=7" in caplog.text
    assert f"think.waypoint object={ME} waypoint=1 x=-1 y=-2 z=-3" in caplog.text
    assert "waypoint=2 " not in caplog.text


def test_a_three_point_circle_flown_round_once_is_done_and_goes_home(
    patrolling, mine
) -> None:
    craft = patrolling(order_variable1=1)
    tick = 1000
    for stop in (P0, P1, P2):
        assert progress.decide(craft) == progress.STAY
        at = patrolling(
            order_variable1=1, x=stop[0], y=stop[1], notebook=craft.notebook
        )
        decision = decide(at, [mine(at)], tick=tick)
        craft = patrolling(order_variable1=1, notebook=decision.notebook)
        tick += 16
    assert route(craft.notebook) == (0, 0, 1, 0)
    assert progress.decide(craft) == progress.Progress(home=True, move=OrderMove(0, 2))


def test_an_unreadable_notebook_holds_no_rounds(patrolling) -> None:
    unreadable = bytes((9,)) + bytes(63)
    assert patrol.rounds(patrolling(notebook=unreadable)) == 0


def test_rounds_stop_at_255(patrolling, mine) -> None:
    craft = patrolling(x=P2[0], y=P2[1], notebook=book(waypoint=2, rounds=255))
    assert route(decide(craft, [mine(craft)]).notebook) == (0, 0, 255, 0)


def test_a_waypoint_that_is_off_sends_it_back_to_waypoint_0(patrolling, mine) -> None:
    craft = patrolling(notebook=book(waypoint=5, rounds=1))
    decision = decide(craft, [mine(craft)])
    assert decision.actions.steer == toward(P0)
    assert route(decision.notebook) == (0, 0, 1, 0)


def test_circle_and_evade_rests_before_weaving_again(patrolling, mine, enemy) -> None:
    rest = weave.Weave(rest_until=5000).write()
    scratch = bytes(4) + rest + bytes(notebook.SCRATCH_BYTES - 4 - len(rest))
    rested = notebook.Notebook(4, 0, 1, 0, scratch).encode()
    craft = patrolling(order_code=CIRCLE_AND_EVADE, notebook=rested)
    decision = decide(craft, [mine(craft), enemy()])
    assert decision.actions == Actions(steer=toward(P0), throttle=HALF)
    assert not flown(decision.notebook).flying
