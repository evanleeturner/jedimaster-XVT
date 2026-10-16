"""Forming up: a flight out of a mothership's hangar gathers before its order."""

from __future__ import annotations

import dataclasses
import logging
import typing as t

import pytest

from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import arrival
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import formation
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import Books
from jedimaster_ai.world import Contact
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import CraftView
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

START_TICK = 1000
OWN_INTERVAL = 236
# The view's own object index, as conftest's VIEW has it.
OWN = 4
LEADER = 20
MATE = 21
DESTROYER = 30
FOE = 9
LAUNCH_YAW = 0x1000
LAUNCH_PITCH = 0x4000
# An Imperial Star Destroyer: retail size and hull box (``test_hull.py``'s
# source); level at yaw 0, its box_max side faces a craft south of it.
ISD = {
    "genus": 4,
    "size": 65396,
    "box_min": (-19140, -40940, -7132),
    "box_max": (19140, 24456, 12570),
}
PLACE = (-0x400, 0x300, 7)
# A flight's other craft sit this far off, clear of any steering around them.
AWAY = 0x40000
# The owner's ruling: a flight forms up for at most 5 game seconds.
LIMIT = 1180
# A small craft forming up thinks every 29 ticks, a bulky one every 59.
SMALL_INTERVAL = 29
BULKY_INTERVAL = 59


@pytest.fixture
def launching(view):
    """Return a craft leaving a hangar on its launch heading, no enemy shown."""

    def make(**changes) -> CraftView:
        mine = {
            "activity": Activity.LEAVING_HANGAR,
            "think_interval": OWN_INTERVAL,
            "order_code": OrderCode.ATTACK,
            "turret_slot_mask": 0,
            "enemy_mask": 0,
            "order_target_mask": 0,
            "leader": NO_OBJECT,
            "yaw": LAUNCH_YAW,
            "pitch": LAUNCH_PITCH,
            "roll": 0,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def follower(launching):
    """Return a craft leaving a hangar behind LEADER, with its place given."""

    def make(**changes) -> CraftView:
        mine = {"leader": LEADER, "books": Books.TARGETS_LEFT | Books.PLACE}
        return launching(**(mine | {"place": PLACE} | changes))

    return make


@pytest.fixture
def mate(contact):
    """Return a craft of the view's flight, forming up unless told."""

    def make(**changes) -> Contact:
        mine = {
            "object_index": MATE,
            "team": 1,
            "flight_group": 2,
            "target": NO_OBJECT,
            "activity": Activity.IN_FORMATION,
            "x": AWAY,
            "y": 0,
            "z": 0,
            "yaw": LAUNCH_YAW,
            "pitch": LAUNCH_PITCH,
            "speed": 200,
        }
        return contact(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    """Return the view's own contact, last hit by FOE, and FOE after it: put
    them at positions 0 and 1 and give the view enemy_mask 0b10."""

    def make() -> tuple[Contact, Contact]:
        own = contact(object_index=OWN, team=1, flight_group=2, last_attacker=FOE)
        enemy = contact(object_index=FOE, team=2, target=OWN, x=AWAY)
        return own, enemy

    return make


def form(
    craft: CraftView,
    contacts: t.Callable[[int], tuple[Contact, ...]],
    ticks: int = 4000,
) -> list[tuple[int, Actions]]:
    """Fly the craft's form-up from START_TICK as the engine would ask it:
    each answer's notebook, activity and interval go into the next view, and
    each answer must pass the engine's reader. Returns each think's ticks
    since the start and its actions, up to the think that ends it."""
    steps = []
    tick = START_TICK
    while tick < START_TICK + ticks:
        question = Question(tick, (craft,), contacts(tick))
        assert arrival.forms_up(craft, question)
        decision = arrival.form_up(craft, question)
        answer = Answer(
            tick,
            (CraftAnswer(craft.object_index, decision.notebook, decision.actions),),
        )
        assert wire.read_answer(wire.answer_bytes(answer)[4:], question) == answer
        actions = decision.actions
        steps.append((tick - START_TICK, actions))
        craft = dataclasses.replace(
            craft,
            notebook=decision.notebook,
            activity=craft.activity if actions.activity is None else actions.activity,
            think_interval=actions.think_interval,
        )
        if craft.activity == Activity.NONE:
            return steps
        tick += craft.think_interval
    raise AssertionError(f"still forming up after {ticks} ticks")


def forming(craft: CraftView, contacts: tuple[Contact, ...]) -> CraftView:
    """Return the craft after its first think forming up at START_TICK."""
    decision = arrival.form_up(craft, Question(START_TICK, (craft,), contacts))
    assert decision.actions.activity == Activity.IN_FORMATION
    return dataclasses.replace(
        craft, activity=Activity.IN_FORMATION, notebook=decision.notebook
    )


def test_a_leader_out_of_a_hangar_forms_up_on_its_launch_heading(
    launching, mate
) -> None:
    craft = launching()
    question = Question(START_TICK, (craft,), (mate(activity=Activity.LEAVING_HANGAR),))
    assert arrival.forms_up(craft, question)
    decision = arrival.form_up(craft, question)
    # 0xE000 is 7/8 of full throttle, the most a leader with followers cruises.
    assert decision.actions == Actions(
        steer=Steer(LAUNCH_YAW, LAUNCH_PITCH, 0),
        throttle=0xE000,
        activity=Activity.IN_FORMATION,
        think_interval=SMALL_INTERVAL,
    )
    book = notebook.decode(decision.notebook)
    assert (book.behavior, book.phase, book.phase_tick) == (
        BehaviorCode.ARRIVAL,
        arrival.FORMING_UP,
        START_TICK,
    )


def test_a_leaders_form_up_ends_on_5_game_seconds_never_past(launching, mate) -> None:
    *flying, (ended_at, last) = form(launching(), lambda tick: (mate(),))
    assert ended_at == LIMIT
    assert all(ticks < LIMIT for ticks, _ in flying)
    assert all(actions.think_interval <= SMALL_INTERVAL for _, actions in flying)
    assert [actions.activity for _, actions in flying] == [Activity.IN_FORMATION] + [
        None
    ] * (len(flying) - 1)
    assert last == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_a_leader_holds_its_launch_heading_while_it_turns_off_it(
    launching, mate
) -> None:
    craft = forming(launching(), (mate(),))
    turned = dataclasses.replace(craft, yaw=0x3000, pitch=0x3800)
    question = Question(START_TICK + SMALL_INTERVAL, (turned,), (mate(),))
    steer = arrival.form_up(turned, question).actions.steer
    assert steer == Steer(LAUNCH_YAW, LAUNCH_PITCH, 0)


@pytest.mark.parametrize("own", [708, 118, SMALL_INTERVAL])
def test_a_formed_up_craft_gets_its_own_interval_back(launching, mate, own) -> None:
    steps = form(launching(think_interval=own), lambda tick: (mate(),))
    assert steps[-1] == (LIMIT, Actions(activity=Activity.NONE, think_interval=own))
    assert {actions.think_interval for _, actions in steps[:-2]} == {SMALL_INTERVAL}


def test_a_bulky_leader_thinks_every_59_ticks(launching, mate) -> None:
    freighter = mate(object_index=OWN, genus=3, x=-5, y=6, z=7)
    steps = form(launching(), lambda tick: (freighter, mate()))
    assert {actions.think_interval for _, actions in steps[:-1]} == {BULKY_INTERVAL}
    assert steps[-1][0] == LIMIT


def test_a_follower_out_of_a_hangar_flies_to_its_place(follower, mate) -> None:
    craft = follower()
    leader = mate(object_index=LEADER, activity=Activity.LEAVING_HANGAR)
    question = Question(START_TICK, (craft,), (leader,))
    assert arrival.forms_up(craft, question)
    actions = arrival.form_up(craft, question).actions
    flown = formation.fly(craft, question, leader)
    assert actions == dataclasses.replace(flown, activity=Activity.IN_FORMATION)
    assert actions.steer != Steer(LAUNCH_YAW, LAUNCH_PITCH, 0)


def test_a_follower_ends_its_form_up_with_its_leader(follower, mate) -> None:
    def contacts(tick: int) -> tuple[Contact, ...]:
        formed = tick - START_TICK >= 600
        activity = Activity.NONE if formed else Activity.IN_FORMATION
        return (mate(object_index=LEADER, activity=activity),)

    *flying, (ended_at, last) = form(follower(), contacts)
    assert flying[-1][0] < 600 <= ended_at < 600 + SMALL_INTERVAL
    assert last == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_a_follower_ends_its_form_up_on_time_at_the_latest(follower, mate) -> None:
    steps = form(follower(), lambda tick: (mate(object_index=LEADER),))
    assert steps[-1][0] == LIMIT


def test_a_follower_whose_leader_is_gone_ends_its_form_up(follower, mate) -> None:
    leader = mate(object_index=LEADER)
    steps = form(follower(), lambda tick: (leader,) if tick == START_TICK else ())
    assert [ticks for ticks, _ in steps] == [0, SMALL_INTERVAL]
    assert steps[-1][1] == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_a_craft_named_leader_holds_the_heading_it_began_on(follower, mate) -> None:
    craft = forming(follower(), (mate(object_index=LEADER),))
    named = dataclasses.replace(craft, leader=NO_OBJECT, yaw=0x3000)
    question = Question(START_TICK + SMALL_INTERVAL, (named,), (mate(),))
    actions = arrival.form_up(named, question).actions
    assert actions.steer == Steer(LAUNCH_YAW, LAUNCH_PITCH, 0)
    assert actions.throttle == 0xE000


@pytest.mark.parametrize(
    "changes",
    [
        {"object_index": OWN},
        {"flight_group": 3},
        {"team": 2},
        {"flags": ContactFlags.PLAYER},
        {"activity": Activity.NONE},
    ],
    ids=["itself", "another_group", "another_team", "a_player", "flying_its_order"],
)
def test_a_leader_without_a_forming_flight_flies_its_order(
    launching, mate, changes
) -> None:
    craft = launching()
    assert not arrival.forms_up(
        craft, Question(START_TICK, (craft,), (mate(**changes),))
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"books": Books.TARGETS_LEFT},
        {"leader": MATE},
    ],
    ids=["no_place", "leader_not_shown"],
)
def test_a_follower_without_its_leader_or_place_flies_its_order(
    follower, mate, changes
) -> None:
    craft = follower(**changes)
    leader = mate(object_index=LEADER, activity=Activity.LEAVING_HANGAR)
    assert not arrival.forms_up(craft, Question(START_TICK, (craft,), (leader,)))


def test_a_follower_of_a_player_flies_its_order(follower, mate) -> None:
    craft = follower()
    leader = mate(object_index=LEADER, flags=ContactFlags.PLAYER)
    assert not arrival.forms_up(craft, Question(START_TICK, (craft,), (leader,)))


def test_a_leader_ends_its_form_up_when_its_flight_no_longer_forms_up(
    launching, mate
) -> None:
    def contacts(tick: int) -> tuple[Contact, ...]:
        formed = tick - START_TICK >= 600
        return (mate(activity=Activity.NONE if formed else Activity.IN_FORMATION),)

    *flying, (ended_at, last) = form(launching(), contacts)
    assert flying[-1][0] < 600 <= ended_at < 600 + SMALL_INTERVAL
    assert last == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_a_craft_with_an_enemy_after_it_flies_its_order_out_of_the_hangar(
    launching, mate, foe
) -> None:
    craft = launching(enemy_mask=0b10)
    own, enemy = foe()
    contacts = (own, enemy, mate())
    assert not arrival.forms_up(craft, Question(START_TICK, (craft,), contacts))
    assert arrival.forms_up(craft, Question(START_TICK, (craft,), (own, mate())))


def test_an_enemy_after_a_forming_craft_ends_its_form_up(launching, mate, foe) -> None:
    own, enemy = foe()

    def contacts(tick: int) -> tuple[Contact, ...]:
        if tick - START_TICK >= 600:
            return (own, enemy, mate())
        return (own, mate(object_index=FOE, target=NO_OBJECT), mate())

    *flying, (ended_at, last) = form(launching(enemy_mask=0b10), contacts)
    assert flying[-1][0] < 600 <= ended_at < 600 + SMALL_INTERVAL
    assert last == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_a_homing_warhead_coming_ends_a_form_up(launching, mate) -> None:
    craft = launching(threat_count=1)
    assert not arrival.forms_up(craft, Question(START_TICK, (craft,), (mate(),)))
    craft = dataclasses.replace(forming(launching(), (mate(),)), threat_count=1)
    question = Question(START_TICK + SMALL_INTERVAL, (craft,), (mate(),))
    actions = arrival.form_up(craft, question).actions
    assert actions == Actions(activity=Activity.NONE, think_interval=OWN_INTERVAL)


def test_only_an_in_formation_craft_is_forming_up(view) -> None:
    forming_up = {a for a in Activity if arrival.is_forming_up(view(activity=a))}
    assert forming_up == {Activity.IN_FORMATION}


def test_a_form_up_says_when_it_starts_and_why_it_ends(
    launching, follower, mate, foe, caplog
) -> None:
    caplog.set_level(logging.INFO, logger="jedimaster_ai")
    form(launching(), lambda tick: (mate(),))
    leader = mate(object_index=LEADER)
    form(follower(), lambda tick: (leader,) if tick == START_TICK else ())
    own, enemy = foe()
    form(
        launching(enemy_mask=0b10),
        lambda tick: (own, enemy, mate()) if tick > START_TICK else (own, mate()),
    )
    lines = [r.getMessage() for r in caplog.records]
    assert [line.split()[0] for line in lines] == [
        "arrival.forming_up",
        "arrival.formed_up",
    ] * 3
    assert (
        lines[0] == "arrival.forming_up object=4 leader=65535 ticks=1180 interval=236"
    )
    assert lines[1] == "arrival.formed_up object=4 ticks=1180 interval=236 why=0"
    assert lines[3].endswith(f"ticks={SMALL_INTERVAL} interval=236 why=1")
    assert lines[5].endswith(f"ticks={SMALL_INTERVAL} interval=236 why=2")


def test_only_a_forming_leader_logs_its_heading(
    launching, follower, mate, caplog
) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai.behaviors.arrival")
    craft = launching()
    arrival.form_up(craft, Question(START_TICK, (craft,), (mate(),)))
    craft = follower()
    arrival.form_up(craft, Question(START_TICK, (craft,), (mate(object_index=LEADER),)))
    leading = [
        r.getMessage() for r in caplog.records if "arrival.leading" in r.getMessage()
    ]
    assert leading == [
        f"arrival.leading object=4 yaw={LAUNCH_YAW} pitch={LAUNCH_PITCH} "
        f"throttle={0xE000} left={LIMIT}"
    ]


def test_think_flies_a_flight_leaving_a_hangar_by_its_form_up(
    launching, mate, caplog
) -> None:
    caplog.set_level(logging.INFO, logger="jedimaster_ai")
    craft = launching()
    question = Question(START_TICK, (craft,), (mate(activity=Activity.LEAVING_HANGAR),))
    answer = think(question).craft[0]
    book = notebook.decode(answer.notebook)
    assert (book.behavior, book.phase) == (BehaviorCode.ARRIVAL, arrival.FORMING_UP)
    assert answer.actions.activity == Activity.IN_FORMATION
    events = [r.getMessage().split()[0] for r in caplog.records]
    assert [event for event in events if event.startswith("arrival.")] == [
        "arrival.forming_up"
    ]


def test_after_its_form_up_a_craft_flies_its_order_with_its_dice(
    launching, mate
) -> None:
    craft = launching()
    tick = START_TICK
    while craft.activity != Activity.NONE:
        assert tick < START_TICK + 4000, "still forming up after 4000 ticks"
        answer = think(Question(tick, (craft,), (mate(),))).craft[0]
        kept = notebook.decode(answer.notebook)
        actions = answer.actions
        craft = dataclasses.replace(
            craft,
            notebook=answer.notebook,
            activity=craft.activity if actions.activity is None else actions.activity,
            think_interval=actions.think_interval or craft.think_interval,
        )
        tick += craft.think_interval
    order = notebook.decode(
        think(Question(tick, (craft,), (mate(),))).craft[0].notebook
    )
    assert kept.behavior == BehaviorCode.ARRIVAL
    assert order.behavior == BehaviorCode.ATTACK
    assert order.dice == kept.dice


def test_a_forming_leader_steers_clear_of_a_star_destroyer_ahead(
    launching, mate, contact
) -> None:
    """On yaw 0 the leader heads at the destroyer's hull 8,000 ahead, inside
    LOOK_AHEAD of the 4,096 kept off it."""
    craft = launching(x=0, y=0, z=0, yaw=0)
    own = contact(object_index=OWN, genus=0, x=0, y=0, z=0, target=NO_OBJECT)
    destroyer = contact(
        object_index=DESTROYER,
        x=0,
        y=ISD["box_max"][1] + 8000,
        z=0,
        yaw=0,
        pitch=LEVEL,
        roll=0,
        target=NO_OBJECT,
        **ISD,
    )
    question = Question(START_TICK, (craft,), (own, destroyer, mate()))
    assert arrival.form_up(craft, question).actions.steer == Steer(0, LAUNCH_PITCH, 0)
    actions = think(question).craft[0].actions
    assert actions.steer.yaw != 0
    assert actions.activity == Activity.IN_FORMATION
