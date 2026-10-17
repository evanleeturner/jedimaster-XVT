"""Fence for Rendezvous: a craft flies to its group's rendezvous point,
waits there for good on the order slot, is done once boarded variable1
times, defends itself on the way and at the point, and gives its status
line back when another behavior flies it.

The craft flies at the origin facing +y, level, at full skill, on a
Rendezvous in slot 0 that needs 2 boardings; its point is 100,000 units
ahead; contact 0 is its own, a starfighter.
"""

from __future__ import annotations

import dataclasses

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import formation
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.behaviors import rendezvous
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.tactics import GENUS_STARSHIP
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
WINGMAN = 6
LEADER = 8
CHASER = 12
POINT = (0, 100_000, 0)
THROTTLE = 0x8000
FLYING = Activity.FLYING_TO_RENDEZVOUS
WAITING = Activity.WAITING_FOR_BOARDING_CRAFT
HOLD = (HOLD_FIRE, HOLD_FIRE)
STOPPED = Actions(throttle=0, activity=WAITING, target=NO_OBJECT, lasers=HOLD)


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
            "enemy_mask": 0xFFFFFFFE,
            "cannon_group_count": 1,
            "order_code": OrderCode.RENDEZVOUS,
            "order_codes": (4, 0, 0, 0),
            "order_slot": 0,
            "order_variable1": 2,
            "order_throttle": THROTTLE,
            "books": Books.RENDEZVOUS_POINT,
            "rendezvous_point": POINT,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def own(contact):
    def make(**changes):
        mine = {"object_index": ME, "team": 1, "x": 0, "y": 0, "z": 0, "genus": 0}
        return contact(**(mine | changes))

    return make


@pytest.fixture
def chaser(contact):
    """An enemy fighter on the craft's tail, targeting it."""
    return contact(object_index=CHASER, team=2, x=0, y=-0x2000, z=0, target=ME, genus=0)


@pytest.fixture
def ask(own):
    def decide(me, *others, tick=1000, mine=None):
        shown = own(x=me.x, y=me.y, z=me.z) if mine is None else mine
        return rendezvous.think(me, Question(tick, (me,), (shown, *others)))

    return decide


def flying(me, throttle=THROTTLE):
    yaw, pitch = geometry.bearing(
        *(p - q for p, q in zip(POINT, (me.x, me.y, me.z), strict=True))
    )
    return Actions(
        steer=Steer(yaw, pitch, me.roll),
        throttle=throttle,
        activity=FLYING,
        target=NO_OBJECT,
        lasers=HOLD,
    )


def test_the_codes_are_rendezvous() -> None:
    assert rendezvous.RENDEZVOUS == OrderCode.RENDEZVOUS
    assert rendezvous.CODE == BehaviorCode.RENDEZVOUS


def test_far_from_the_point_it_steers_at_it_at_the_orders_throttle(craft, ask) -> None:
    me = craft()
    actions = ask(me).actions
    assert actions == flying(me)
    assert actions.steer.yaw == 0


def test_it_steers_at_a_point_off_its_nose(craft, ask) -> None:
    me = craft(x=30_000, y=-40_000, z=5_000, yaw=0x4000)
    actions = ask(me).actions
    assert actions == flying(me)
    assert actions.steer.yaw != me.yaw


def test_the_throttle_is_the_cruise_throttle_patrol_flies(craft, ask) -> None:
    me = craft(order_throttle=patrol.FULL_THROTTLE, order_speed=20, full_speed=300)
    assert ask(me).actions.throttle == patrol.cruise_throttle(me)
    assert patrol.cruise_throttle(me) != patrol.FULL_THROTTLE


def test_an_order_throttle_of_0_stays_0_as_1997s_cruise(craft, ask) -> None:
    assert ask(craft(order_throttle=0)).actions.throttle == 0


def test_leading_a_wingman_it_eases_its_throttle(craft, ask, contact) -> None:
    me = craft(order_throttle=patrol.FULL_THROTTLE, flight_group=2)
    wingman = contact(object_index=WINGMAN, team=1, flight_group=2, x=500, y=0, z=0)
    assert ask(me).actions.throttle == patrol.FULL_THROTTLE
    assert ask(me, wingman).actions.throttle == formation.EASED_THROTTLE


@pytest.mark.parametrize(
    ("yaw", "moves"),
    [(patrol.STARSHIP_TURN - 1, True), (patrol.STARSHIP_TURN, False)],
)
def test_a_starship_off_course_stops_to_turn(craft, ask, own, yaw, moves) -> None:
    me = craft(yaw=-yaw & 0xFFFF)
    actions = ask(me, mine=own(genus=GENUS_STARSHIP)).actions
    assert actions.throttle == (THROTTLE if moves else 0)
    assert actions.steer is not None


def test_the_flight_is_logged_with_the_distance_left(craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    ask(craft())
    assert f"rendezvous.fly object={ME} left=100000 throttle={THROTTLE}" in caplog.text


@pytest.mark.parametrize(
    ("left", "waits"), [(rendezvous.NEAR - 1, True), (rendezvous.NEAR, False)]
)
def test_within_near_of_the_point_it_stops_and_waits(craft, ask, left, waits) -> None:
    me = craft(y=POINT[1] - left)
    actions = ask(me).actions
    assert (actions == STOPPED) is waits
    assert rendezvous.NEAR == 16_384


def test_the_wait_is_logged_once(craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    first = ask(craft(y=POINT[1] - 1000))
    assert f"rendezvous.waiting object={ME} left=1000 boarded=0 needed=2" in caplog.text
    caplog.clear()
    again = craft(y=POINT[1] - 1000, notebook=first.notebook, activity=WAITING)
    assert ask(again, tick=1016).actions == STOPPED
    assert "rendezvous.waiting" not in caplog.text


def test_once_it_waits_it_never_flies_to_the_point_again(craft, ask) -> None:
    book = ask(craft(y=POINT[1] - 1000)).notebook
    assert notebook.decode(book).phase == rendezvous.WAITING
    pushed = craft(y=-50_000, notebook=book)
    assert ask(pushed, tick=1016).actions == STOPPED


def test_its_line_alone_says_it_waits(craft, ask) -> None:
    """While attack's notebook stands in for a fight, the line is the only
    mark that the craft reached the point."""
    assert ask(craft(y=-50_000, activity=WAITING)).actions == STOPPED


def test_a_wait_on_another_slot_is_not_this_one(craft, ask) -> None:
    book = ask(craft(y=POINT[1] - 1000)).notebook
    me = craft(order_slot=1, order_codes=(4, 4, 0, 0), notebook=book)
    assert ask(me, tick=1016).actions == flying(me)


def test_with_no_point_named_it_holds_still_and_warns_once(craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    me = craft(books=0)
    first = ask(me)
    assert first.actions == Actions(throttle=0, target=NO_OBJECT, lasers=HOLD)
    assert f"rendezvous.no_point object={ME} books=0" in caplog.text
    caplog.clear()
    assert (
        ask(craft(books=0, notebook=first.notebook), tick=1016).actions == first.actions
    )
    assert "rendezvous.no_point" not in caplog.text


def test_a_point_not_named_is_never_reached(craft, ask) -> None:
    """Without the book the point reads (0, 0, 0), where the craft sits."""
    assert ask(craft(books=0, rendezvous_point=(0, 0, 0))).actions.activity is None
    assert not rendezvous.done(
        craft(books=0, rendezvous_point=(0, 0, 0), order_variable1=0)
    )


@pytest.fixture
def leader(contact):
    """The leader, on the craft's team, 20,000 units ahead."""
    return contact(
        object_index=LEADER, team=1, flight_group=2, x=0, y=20_000, z=0, genus=0
    )


def test_a_follower_flies_to_its_place_under_the_line(craft, ask, own, leader) -> None:
    me = craft(
        leader=LEADER,
        books=Books.RENDEZVOUS_POINT | Books.PLACE,
        place=(500, 19_000, 0),
    )
    question = Question(1000, (me,), (own(), leader))
    expected = dataclasses.replace(
        formation.fly(me, question, leader),
        activity=FLYING,
        target=NO_OBJECT,
        lasers=HOLD,
    )
    actions = ask(me, leader).actions
    assert actions == expected
    assert actions.steer != flying(me).steer


def test_a_follower_with_no_leader_shown_flies_to_the_point_itself(craft, ask) -> None:
    me = craft(
        leader=LEADER,
        books=Books.RENDEZVOUS_POINT | Books.PLACE,
        place=(500, 19_000, 0),
    )
    assert ask(me).actions == flying(me)


def test_a_follower_near_the_point_waits(craft, ask, leader) -> None:
    me = craft(
        y=POINT[1] - 900,
        leader=LEADER,
        books=Books.RENDEZVOUS_POINT | Books.PLACE,
        place=(500, POINT[1] - 900, 0),
    )
    assert ask(me, dataclasses.replace(leader, y=POINT[1])).actions == STOPPED


def test_done_once_it_waits_and_was_boarded_variable1_times(craft) -> None:
    near = POINT[1] - 1000
    assert not rendezvous.done(craft(order_progress=(2, 0, 0, 0)))
    assert not rendezvous.done(craft(y=near, order_progress=(1, 0, 0, 0)))
    assert rendezvous.done(craft(y=near, order_progress=(2, 0, 0, 0)))
    assert rendezvous.done(craft(y=near, order_progress=(3, 0, 0, 0)))
    assert rendezvous.done(craft(activity=WAITING, order_progress=(2, 0, 0, 0)))


def test_a_variable1_of_0_is_done_on_arrival(craft) -> None:
    assert not rendezvous.done(craft(order_variable1=0))
    assert rendezvous.done(craft(y=POINT[1] - 1000, order_variable1=0))


def test_done_reads_its_own_slot(craft, ask) -> None:
    book = ask(craft(y=POINT[1] - 1000)).notebook
    me = craft(notebook=book, order_slot=1, order_codes=(4, 4, 0, 0))
    assert not rendezvous.done(dataclasses.replace(me, order_progress=(0, 2, 0, 0)))
    assert not rendezvous.done(dataclasses.replace(me, order_progress=(2, 0, 0, 0)))
    assert rendezvous.done(
        dataclasses.replace(me, order_progress=(0, 2, 0, 0), activity=WAITING)
    )
    assert rendezvous.done(craft(notebook=book, y=-50_000, order_progress=(2, 0, 0, 0)))


@pytest.mark.parametrize(("y", "line"), [(0, FLYING), (POINT[1] - 1000, WAITING)])
def test_it_fights_the_enemy_after_it_under_its_line(
    craft, ask, chaser, y, line
) -> None:
    me = craft(y=y)
    actions = ask(me, dataclasses.replace(chaser, y=y - 0x2000)).actions
    assert actions.target == CHASER
    assert actions.steer is not None and actions.throttle != 0
    assert actions.activity == line


def test_through_a_fight_at_the_point_it_still_waits(craft, ask, chaser) -> None:
    y = POINT[1] - 1000
    fought = ask(
        craft(y=y, activity=WAITING), dataclasses.replace(chaser, y=y - 0x2000)
    )
    assert notebook.decode(fought.notebook).behavior == attack.CODE
    clear = ask(craft(y=-50_000, activity=WAITING, notebook=fought.notebook), tick=1016)
    assert clear.actions == STOPPED


def test_with_no_cannons_it_weaves_off_a_chaser(craft, ask, chaser) -> None:
    actions = ask(craft(cannon_group_count=0), chaser).actions
    assert actions.target != CHASER
    assert actions.steer is not None and actions.steer != flying(craft()).steer
    assert actions.activity == FLYING


def test_it_sends_its_line_every_think(craft, ask) -> None:
    """``ended`` takes a line no answer names for the end of the order."""
    assert ask(craft(activity=FLYING)).actions.activity == FLYING
    assert ask(craft(activity=WAITING)).actions.activity == WAITING


def test_flown_by_another_behavior_it_gets_activity_none(craft) -> None:
    plain = Decision(b"", Actions(throttle=5))
    for line in (FLYING, WAITING):
        assert rendezvous.ended(craft(activity=line), plain).actions == Actions(
            throttle=5, activity=Activity.NONE
        )
    going = Decision(b"", Actions(activity=Activity.GOING_HOME))
    assert rendezvous.ended(craft(activity=WAITING), going) == going
    assert rendezvous.ended(craft(activity=Activity.WAITING), plain) == plain
