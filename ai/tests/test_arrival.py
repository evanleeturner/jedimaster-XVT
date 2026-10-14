"""Arrival: craft flown in out of hyperspace, and out of a mothership's hangar."""

from __future__ import annotations

import dataclasses
import logging
import typing as t

import pytest

from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import arrival
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Arrival
from jedimaster_ai.world import Contact
from jedimaster_ai.world import CraftView
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

START_TICK = 1000
OWN_INTERVAL = 236
OWN_THROTTLE = 0x5555
LEADER = 20
FOE = 9


@pytest.fixture
def arriving(view):
    def make(activity: Activity, **changes) -> CraftView:
        mine = {
            "activity": activity,
            "think_interval": OWN_INTERVAL,
            "throttle": OWN_THROTTLE,
            "order_code": OrderCode.ATTACK,
            "turret_slot_mask": 0,
            "leader": NO_OBJECT,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def leader(contact):
    """Return the leader's contact, arriving until a tick after the start."""

    def make(until: int) -> t.Callable[[int], tuple[Contact, ...]]:
        def contacts(tick: int) -> tuple[Contact, ...]:
            jumping = tick - START_TICK < until
            activity = Activity.ARRIVING_FROM_HYPERSPACE if jumping else Activity.NONE
            return (contact(object_index=LEADER, activity=activity),)

        return contacts

    return make


def fly_in(
    craft: CraftView,
    contacts: t.Callable[[int], tuple[Contact, ...]] = lambda tick: (),
    ticks: int = 4000,
) -> list[tuple[int, Actions]]:
    """Ask about the craft from START_TICK as the engine would, until it has
    arrived: each answer's notebook, activity, interval and throttle go into
    the next view, and each answer must pass the engine's reader. Returns each
    think's ticks since the start and its actions."""
    steps = []
    tick = START_TICK
    while tick < START_TICK + ticks:
        question = Question(tick, (craft,), contacts(tick))
        answer = think(question)
        assert wire.read_answer(wire.answer_bytes(answer)[4:], question) == answer
        actions = answer.craft[0].actions
        steps.append((tick - START_TICK, actions))
        craft = dataclasses.replace(
            craft,
            notebook=answer.craft[0].notebook,
            activity=craft.activity if actions.activity is None else actions.activity,
            think_interval=actions.think_interval or craft.think_interval,
            throttle=craft.throttle if actions.throttle is None else actions.throttle,
        )
        if not arrival.is_arriving(craft):
            return steps
        tick += craft.think_interval
    raise AssertionError(f"still arriving after {ticks} ticks")


def test_jump_speed_steps_down_at_the_1997_seconds() -> None:
    second = arrival.GAME_SECOND
    assert [arrival.jump_speed(t) for t in (0, 6 * second - 1)] == [3600, 3600]
    assert [arrival.jump_speed(t) for t in (6 * second, 9 * second - 1)] == [1800] * 2
    assert [arrival.jump_speed(t) for t in (9 * second, 11 * second)] == [900] * 2


def test_a_jump_starts_marked_arriving_at_full_speed(arriving) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE)
    _, first = fly_in(craft)[0]
    assert first.arrival == Arrival(arriving=True, speed=3600)
    assert first.think_interval == arrival.ARRIVAL_INTERVAL
    assert first.activity is None and first.throttle is None


def test_a_leaders_jump_slows_in_steps_and_ends_after_11_game_seconds(
    arriving,
) -> None:
    steps = fly_in(arriving(Activity.ARRIVING_FROM_HYPERSPACE))
    *flying, (ended_at, last) = steps
    for ticks, actions in flying:
        assert actions.arrival == Arrival(True, arrival.jump_speed(ticks))
    assert {actions.arrival.speed for _, actions in flying} == {3600, 1800, 900}
    # The 1997 AI's arrival timer: 2,596 ticks, 11 game seconds.
    assert flying[-1][0] < 2596 <= ended_at
    assert ended_at - flying[-1][0] == arrival.ARRIVAL_INTERVAL
    assert last.arrival == Arrival(arriving=False, speed=arrival.ARRIVED_SPEED)
    assert last.activity == Activity.NONE
    assert last.think_interval == OWN_INTERVAL


def test_a_follower_ends_its_jump_with_its_leader(arriving, leader) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE, leader=LEADER)
    *flying, (ended_at, last) = fly_in(craft, leader(until=1500))
    assert flying[-1][0] < 1500 <= ended_at < 1500 + arrival.ARRIVAL_INTERVAL
    assert last.arrival == Arrival(arriving=False, speed=arrival.ARRIVED_SPEED)
    assert last.activity == Activity.NONE
    assert last.think_interval == OWN_INTERVAL


def test_a_follower_without_its_leader_ends_its_jump(arriving) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE, leader=LEADER)
    steps = fly_in(craft)
    assert [ticks for ticks, _ in steps] == [0, arrival.ARRIVAL_INTERVAL]
    assert steps[-1][1].arrival == Arrival(False, arrival.ARRIVED_SPEED)


def test_a_follower_ends_its_jump_on_time_at_the_latest(arriving, leader) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE, leader=LEADER)
    *flying, (ended_at, _) = fly_in(craft, leader(until=10_000))
    assert flying[-1][0] < 2596 <= ended_at


@pytest.mark.parametrize(
    ("order", "speed"),
    [
        (OrderCode.HOLD_STEADY, 0),
        (OrderCode.HOLD_STEADY_7, 0),
        (OrderCode.NULL, 0),
        (OrderCode.CIRCLE, arrival.ARRIVED_SPEED),
        (OrderCode.ESCORT, arrival.ARRIVED_SPEED),
    ],
)
def test_a_craft_whose_order_starts_stopped_ends_its_jump_stopped(
    arriving, order: OrderCode, speed: int
) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE, order_code=order)
    _, last = fly_in(craft)[-1]
    assert last.arrival == Arrival(arriving=False, speed=speed)


def test_a_hangar_exit_flies_its_order_from_its_first_think(arriving, contact) -> None:
    """No dash: the answer is the order's own, with the leaving mark ended."""
    foe = contact(object_index=FOE, team=2, x=0, y=0x3000, z=0)
    own = contact(object_index=4, team=1, x=0, y=0, z=0)
    leaving = arriving(Activity.LEAVING_HANGAR, enemy_mask=0b10, order_target_mask=0b10)
    flying = dataclasses.replace(leaving, activity=Activity.NONE)
    answer = think(Question(START_TICK, (leaving,), (own, foe))).craft[0]
    order = think(Question(START_TICK, (flying,), (own, foe))).craft[0]
    assert answer.actions == dataclasses.replace(order.actions, activity=Activity.NONE)
    assert answer.notebook == order.notebook
    assert notebook.decode(answer.notebook).behavior == BehaviorCode.ATTACK
    assert answer.actions.arrival is None


def test_a_hangar_exit_with_nothing_to_do_keeps_its_own_throttle(arriving) -> None:
    craft = arriving(Activity.LEAVING_HANGAR, order_code=OrderCode.RENDEZVOUS)
    answer = think(Question(START_TICK, (craft,), ())).craft[0]
    assert answer.actions == Actions(activity=Activity.NONE)


def test_an_arrived_craft_flies_its_order_with_its_dice(arriving) -> None:
    craft = arriving(Activity.ARRIVING_FROM_HYPERSPACE)
    tick = START_TICK
    question = Question(tick, (craft,), ())
    for _ in range(100):
        if not arrival.is_arriving(craft):
            break
        answer = think(question).craft[0]
        kept = notebook.decode(answer.notebook)
        activity = answer.actions.activity
        craft = dataclasses.replace(
            craft,
            notebook=answer.notebook,
            activity=craft.activity if activity is None else activity,
        )
        tick += arrival.ARRIVAL_INTERVAL
        question = Question(tick, (craft,), ())
    assert not arrival.is_arriving(craft)
    attacking = notebook.decode(think(question).craft[0].notebook)
    assert kept.behavior == BehaviorCode.ARRIVAL
    assert attacking.behavior == BehaviorCode.ATTACK
    assert attacking.dice == kept.dice


def test_only_a_jump_is_flown_by_arrival(view) -> None:
    jumping = {Activity.ARRIVING_FROM_HYPERSPACE}
    assert {a for a in Activity if arrival.is_arriving(view(activity=a))} == jumping
    leaving = {a for a in Activity if arrival.is_leaving_hangar(view(activity=a))}
    assert leaving == {Activity.LEAVING_HANGAR}


def test_arrival_says_when_it_starts_and_ends(arriving, caplog) -> None:
    caplog.set_level(logging.INFO, logger="jedimaster_ai")
    fly_in(arriving(Activity.ARRIVING_FROM_HYPERSPACE, order_code=OrderCode.CIRCLE))
    think(
        Question(
            START_TICK,
            (arriving(Activity.LEAVING_HANGAR, order_code=OrderCode.CIRCLE),),
            (),
        )
    )
    events = [r.getMessage().split()[0] for r in caplog.records]
    assert events == ["arrival.started", "arrival.finished", "arrival.finished"]


def test_an_arrival_faster_than_the_1997_jump_is_refused() -> None:
    with pytest.raises(ValueError, match="outside 0 to 3600"):
        Arrival(arriving=True, speed=3601)
