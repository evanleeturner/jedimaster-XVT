"""Fence for home and hold: a craft leaving the battle, and one holding steady.

Each way out once: by hyperspace to the home point or at once without one;
into the mothership through the slowing approach, the turn in, the entry at
the mothership's speed and the dock; with no way out, to the home point and
stop; a starship's order stopping for good or jumping. Then the craft already
jumping, the throttle for a speed, the route log, Hold Steady, and the
wave 2 bugs #211 and #213 as checks.
"""

from __future__ import annotations

import dataclasses
import logging

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import wire
from jedimaster_ai.behaviors import hold
from jedimaster_ai.behaviors import home
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

CARRIER = 9
# The craft sits at (-5, 6, 7) with full speed 300 (conftest's VIEW).
FAR = (200_000, 6, 7)


def toward(point, origin=(-5, 6, 7), roll=300) -> Steer:
    yaw, pitch = geometry.bearing(*(p - o for p, o in zip(point, origin, strict=True)))
    return Steer(yaw, pitch, roll)


def ask(view, contacts=()) -> Actions:
    """Return the home behavior's actions, checked against the engine's reader."""
    question = Question(1, (view,), tuple(contacts))
    actions = home.think(view, question).actions
    wire.check_shown(actions, view, question)
    return actions


@pytest.fixture
def carrier(contact):
    return contact(object_index=CARRIER, genus=4, speed=30, target=0xFFFF)


def hangar_view(view, outside, **changes):
    fields = {
        "order_code": OrderCode.GO_HOME,
        "books": Books.BY_MOTHERSHIP,
        "mothership": CARRIER,
        "outside_hangar": outside,
        "inside_hangar": (outside[0] + 100, outside[1], outside[2]),
    }
    return view(**(fields | changes))


def test_a_jump_flies_to_the_home_point_at_full_throttle(view) -> None:
    craft = view(books=Books.MAY_JUMP | Books.HOME_POINT, home_point=FAR)
    assert ask(craft) == Actions(
        steer=toward(FAR),
        throttle=home.FULL_THROTTLE,
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
    )


def test_a_jump_departs_within_range_or_at_once_with_no_home_point(view) -> None:
    near = (home.JUMP_RANGE - 10, 6, 7)
    departing = Actions(
        throttle=home.FULL_THROTTLE,
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
        depart=True,
    )
    assert ask(view(books=Books.MAY_JUMP | Books.HOME_POINT, home_point=near)) == (
        departing
    )
    assert ask(view(books=Books.MAY_JUMP, home_point=FAR)) == departing


def test_a_jump_comes_before_a_mothership(view, carrier) -> None:
    craft = hangar_view(view, (50_000, 0, 0), books=Books.MAY_JUMP)
    assert ask(craft, [carrier]).depart


@pytest.mark.parametrize(
    ("x", "speed"), [(70_000, None), (60_000, 150), (30_000, 100), (10_000, 75)]
)
def test_the_approach_slows_near_the_outside_hangar_point(
    view, carrier, x, speed
) -> None:
    outside = (x - 5, 6, 7)
    actions = ask(hangar_view(view, outside), [carrier])
    throttle = home.FULL_THROTTLE if speed is None else home.throttle_for(view(), speed)
    assert actions == Actions(
        steer=toward(outside),
        throttle=throttle,
        activity=Activity.GOING_HOME,
        target=CARRIER,
    )


@pytest.mark.parametrize(("speed", "enter"), [(30, 55), (24, 40), (0, 40)])
def test_the_turn_in_enters_at_the_motherships_speed(
    view, contact, speed, enter
) -> None:
    mothership = contact(object_index=CARRIER, genus=4, speed=speed)
    craft = hangar_view(view, (1_000, 6, 7))
    assert ask(craft, [mothership]) == Actions(
        steer=toward((1_100, 6, 7)),
        throttle=home.throttle_for(craft, enter),
        activity=Activity.ENTERING_HANGAR,
        think_interval=home.ENTER_INTERVAL,
        target=CARRIER,
    )


def test_a_craft_entering_keeps_entering_and_docks_at_the_hangar(view, carrier) -> None:
    entering = hangar_view(view, (5_000, 6, 7), activity=Activity.ENTERING_HANGAR)
    assert ask(entering, [carrier]).activity == Activity.ENTERING_HANGAR
    at = hangar_view(view, (5_000, 6, 7), books=Books.BY_MOTHERSHIP | Books.AT_HANGAR)
    assert ask(at, [carrier]) == Actions(dock=True)


def test_with_no_way_out_it_flies_to_the_home_point_and_stops(view) -> None:
    craft = view(order_code=OrderCode.GO_HOME, books=Books.HOME_POINT, home_point=FAR)
    assert ask(craft) == Actions(
        steer=toward(FAR),
        throttle=home.FULL_THROTTLE,
        activity=Activity.GOING_HOME,
        target=NO_OBJECT,
    )
    stopped = Actions(throttle=0, activity=Activity.GOING_HOME, target=NO_OBJECT)
    assert ask(view(books=Books.HOME_POINT, home_point=(100, 6, 7))) == stopped
    assert ask(view(books=Books.BY_MOTHERSHIP)) == stopped


@pytest.mark.parametrize("code", [OrderCode.SS_PATROL_LOOP, OrderCode.SS_GO_HOME])
def test_a_starship_order_stops_for_good_or_jumps(view, code) -> None:
    by_mothership = view(order_code=code, books=Books.BY_MOTHERSHIP)
    assert ask(by_mothership) == Actions(
        throttle=0, activity=Activity.NONE, target=NO_OBJECT
    )
    jumping = view(order_code=code, books=Books.MAY_JUMP)
    assert ask(jumping).depart
    no_drive = view(order_code=code, books=Books.HOME_POINT, home_point=FAR)
    assert ask(no_drive).activity == Activity.GOING_HOME


def test_a_craft_already_entering_hyperspace_is_sent_nothing(view, contact) -> None:
    craft = view(books=Books.MAY_JUMP, notebook=bytes(range(64)))
    own = contact(object_index=craft.object_index, object_kind=5)
    decision = home.think(craft, Question(1, (craft,), (own,)))
    assert decision.notebook == craft.notebook
    assert decision.actions == Actions()


@pytest.mark.parametrize(
    ("full", "speed", "throttle"),
    [(300, 150, 0x8000), (300, 300, 0xFFFF), (0, 0, 0xFFFF)],
)
def test_throttle_for_is_speed_over_full_speed(view, full, speed, throttle) -> None:
    assert home.throttle_for(view(full_speed=full), speed) == throttle


def test_a_new_route_is_logged_once(view, caplog) -> None:
    craft = view(books=Books.MAY_JUMP | Books.HOME_POINT, home_point=FAR)
    with caplog.at_level(logging.DEBUG, logger="jedimaster_ai.behaviors.home"):
        ask(craft)
        ask(dataclasses.replace(craft, activity=Activity.LEAVING_BY_HYPERSPACE))
    assert caplog.messages == [
        f'home.route object=4 route="jump" from=0 books={Books.MAY_JUMP | Books.HOME_POINT:d}'
    ]


def test_hold_steady_sets_throttle_0_and_keeps_the_notebook(view) -> None:
    craft = view(order_code=OrderCode.HOLD_STEADY, notebook=bytes(range(64)))
    decision = hold.think(craft, Question(1, (craft,)))
    assert (decision.notebook, decision.actions) == (
        craft.notebook,
        Actions(throttle=0),
    )


def test_bug_211_each_craft_docks_alone(view, carrier) -> None:
    docking = hangar_view(
        view, (5_000, 6, 7), books=Books.BY_MOTHERSHIP | Books.AT_HANGAR
    )
    follower = hangar_view(view, (30_000, 6, 7), object_index=5, leader=4)
    question = Question(1, (docking, follower), (carrier,))
    assert home.think(docking, question).actions.dock
    assert not home.think(follower, question).actions.dock


def test_bug_213_the_craft_flies_to_the_one_mothership_the_engine_chose(
    view, carrier
) -> None:
    craft = hangar_view(view, (70_000, 6, 7))
    actions = ask(craft, [carrier])
    assert (actions.target, actions.steer) == (CARRIER, toward((70_000, 6, 7)))
