"""Fence for home and hold: a craft leaving the battle, and one holding steady.

Each way out once: by hyperspace to the home point or at once without one;
into the mothership through the slowing approach, the turn in, the entry at
the mothership's speed and the dock; with no way out, to the home point and
stop; a starship's order stopping for good or jumping. Then the craft already
jumping, the throttle for a speed, the route log, Hold Steady, and the
wave 2 bugs #211 and #213 as checks.

Then rulings 31, 33 and 34: with no way out, it fights any enemy; attacked
on the way, it fights back at three quarters of its hull or more, else flies
home weaving about its way home, and goes home again once no enemy is after
it; at the door it goes on; a starship's order is never turned round; and no
departure without the jump the books allow (the engine's side is
``test_ai_books.c``: a model with no hyperdrive gets no MAY_JUMP).
"""

from __future__ import annotations

import dataclasses
import logging

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import tactics
from jedimaster_ai import weave
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
    throttle = (
        home.FULL_THROTTLE if speed is None else tactics.throttle_for(view(), speed)
    )
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
        throttle=tactics.throttle_for(craft, enter),
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
    assert tactics.throttle_for(view(full_speed=full), speed) == throttle


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


# Rulings 31, 33 and 34. The craft flies to its jump at FAR, its nose on the
# way home; contact 0 is its own, contact 1 the enemy (VIEW's enemy mask).
ME = 4
CHASER = 12
HOME_YAW = toward(FAR).yaw
JUMPING = {
    "books": Books.MAY_JUMP | Books.HOME_POINT,
    "home_point": FAR,
    "yaw": HOME_YAW,
    "pitch": geometry.LEVEL,
    "skill": 0xFFFF,
}
# A whole hull, and 299 of 400 left: under three quarters.
WHOLE = {"hull_strength": 400, "hull_damage": 0}
HURT = {"hull_strength": 400, "hull_damage": 101}


@pytest.fixture
def mine(contact):
    """Return the craft's own contact, its fields changed as named."""

    def make(craft, **changes):
        fields = {"object_index": ME, "team": 1, "x": craft.x, "y": craft.y}
        fields |= {"z": craft.z, "target": NO_OBJECT} | WHOLE
        return contact(**(fields | changes))

    return make


@pytest.fixture
def enemy(contact):
    """Return a small enemy craft after the craft, 0x2000 behind it unless
    placed elsewhere."""

    def make(**changes):
        fields = {"object_index": CHASER, "team": 2, "genus": 0, "target": ME}
        fields |= {"x": -5 - 0x2000, "y": 6, "z": 7, "speed": 0}
        return contact(**(fields | changes))

    return make


def decide(craft, contacts, tick=1000):
    """Return the home behavior's decision, its actions checked against the
    engine's reader."""
    question = Question(tick, (craft,), tuple(contacts))
    decision = home.think(craft, question)
    wire.check_shown(decision.actions, craft, question)
    return decision


AHEAD_X = -5 + 0x2000


def test_attacked_on_the_way_a_whole_craft_turns_to_fight(view, mine, enemy) -> None:
    craft = view(**JUMPING)
    decision = decide(craft, [mine(craft, last_attacker=CHASER), enemy(x=AHEAD_X)])
    assert decision.actions.target == CHASER
    assert decision.actions.activity == Activity.NONE
    assert notebook.decode(decision.notebook).behavior == 1  # attack's


def test_attacked_below_three_quarters_it_weaves_about_its_way_home(
    view, mine, enemy
) -> None:
    craft = view(**JUMPING | {"yaw": (HOME_YAW + 0x1000) % geometry.TURN})
    decision = decide(craft, [mine(craft, **HURT), enemy()])
    flown = weave.Weave.read(notebook.decode(decision.notebook).scratch)
    assert notebook.decode(decision.notebook).behavior == 3  # home's
    assert flown.flying and flown.course == HOME_YAW
    actions = decision.actions
    assert actions.steer.yaw in {
        (HOME_YAW + d) % geometry.TURN for d in (0x3000, -0x3000)
    }
    assert actions.throttle == home.FULL_THROTTLE
    assert actions.activity == Activity.GOING_HOME_EVADING
    assert actions.target == NO_OBJECT


def test_evading_with_the_enemy_off_its_tail_it_flies_straight_home(
    view, mine, enemy
) -> None:
    craft = view(**JUMPING)
    hit = mine(craft, last_attacker=CHASER, **HURT)
    actions = decide(craft, [hit, enemy(x=AHEAD_X)]).actions
    assert actions.steer == toward(FAR, roll=craft.roll)
    assert actions.activity == Activity.GOING_HOME_EVADING


def test_once_no_enemy_is_after_it_it_goes_home_ending_its_weave(
    view, mine, enemy, caplog
) -> None:
    craft = view(**JUMPING)
    evading = decide(craft, [mine(craft, **HURT), enemy()])
    caplog.set_level("DEBUG")
    later = view(**JUMPING | {"notebook": evading.notebook})
    gone = decide(later, [mine(later, **HURT), enemy(target=15)], tick=1016)
    assert gone.actions.steer == toward(FAR, roll=craft.roll)
    assert gone.actions.activity == Activity.LEAVING_BY_HYPERSPACE
    assert not weave.Weave.read(notebook.decode(gone.notebook).scratch).flying
    assert f'weave.end object={ME} reason="clear" swerves=1 rest_until=0' in (
        caplog.text
    )


def test_at_the_door_it_goes_on(view, mine, enemy, carrier) -> None:
    near = view(**JUMPING | {"home_point": (home.JUMP_RANGE - 10, 6, 7)})
    assert decide(near, [mine(near, last_attacker=CHASER), enemy()]).actions.depart
    entering = hangar_view(
        view, (5_000, 6, 7), activity=Activity.ENTERING_HANGAR, skill=0xFFFF
    )
    contacts = [mine(entering, last_attacker=CHASER), enemy(), carrier]
    actions = decide(entering, contacts).actions
    assert actions.activity == Activity.ENTERING_HANGAR
    at = hangar_view(view, (5_000, 6, 7), books=Books.BY_MOTHERSHIP | Books.AT_HANGAR)
    assert decide(at, [mine(at), enemy(), carrier]).actions == Actions(dock=True)


def test_attacked_on_the_hangar_approach_it_fights_too(
    view, mine, enemy, carrier
) -> None:
    craft = hangar_view(view, (70_000, 6, 7))
    contacts = [mine(craft, last_attacker=CHASER), enemy(), carrier]
    actions = decide(craft, contacts).actions
    assert actions.target == CHASER and actions.activity == Activity.NONE


def test_with_no_way_out_it_fights_any_enemy(view, mine, enemy, caplog) -> None:
    caplog.set_level("DEBUG")
    craft = view(
        order_code=OrderCode.GO_HOME,
        order_target_mask=0,
        books=Books.HOME_POINT,
        home_point=FAR,
        activity=Activity.GOING_HOME,
    )
    foe = enemy(x=AHEAD_X, target=NO_OBJECT)
    decision = decide(craft, [mine(craft, **HURT), foe])
    assert decision.actions.target == CHASER
    assert decision.actions.activity == Activity.NONE
    assert f'home.route object={ME} route="stranded" from=9 ' in caplog.text


@pytest.mark.parametrize("changes", [{"cannon_group_count": 0}, {"full_speed": 0}])
def test_with_no_way_out_and_no_dogfight_in_it_it_waits(
    view, mine, enemy, changes
) -> None:
    craft = view(
        order_code=OrderCode.GO_HOME,
        **{"books": Books.HOME_POINT, "home_point": FAR} | changes,
    )
    actions = decide(craft, [mine(craft), enemy(x=AHEAD_X)]).actions
    assert actions.activity == Activity.GOING_HOME and actions.target == NO_OBJECT


def test_a_mothership_arriving_takes_a_stranded_craft_home(
    view, mine, enemy, carrier
) -> None:
    craft = hangar_view(view, (70_000, 6, 7))
    foe = enemy(x=AHEAD_X, target=NO_OBJECT)
    actions = decide(craft, [mine(craft), foe, carrier]).actions
    assert actions.activity == Activity.GOING_HOME and actions.target == CARRIER


@pytest.mark.parametrize("code", [OrderCode.SS_PATROL_LOOP, OrderCode.SS_GO_HOME])
def test_a_starship_order_is_never_turned_round(view, mine, enemy, code) -> None:
    craft = view(**JUMPING | {"order_code": code})
    actions = decide(craft, [mine(craft, last_attacker=CHASER), enemy()]).actions
    assert actions.activity == Activity.LEAVING_BY_HYPERSPACE
    assert actions.target == NO_OBJECT


def test_no_order_departs_without_the_jump_its_books_allow(view) -> None:
    near = (home.JUMP_RANGE - 10, 6, 7)
    for code in OrderCode:
        for books in (Books.HOME_POINT, Books.HOME_POINT | Books.BY_MOTHERSHIP):
            craft = view(order_code=code, books=books, home_point=near)
            assert not home.think(craft, Question(1, (craft,), ())).actions.depart


def test_a_whole_craft_fights_then_runs_when_hurt_then_goes_home(
    view, mine, enemy, caplog
) -> None:
    """A real case: a craft heading for its jump is hit, turns and fights; its
    hull falls under three quarters and it runs for home weaving; its attacker
    turns away and it flies on to its jump."""
    caplog.set_level("DEBUG", logger="jedimaster_ai.behaviors.home")
    craft = view(**JUMPING | {"activity": Activity.LEAVING_BY_HYPERSPACE})
    fought = decide(craft, [mine(craft, last_attacker=CHASER), enemy(x=AHEAD_X)])
    craft = view(**JUMPING | {"activity": Activity.NONE, "notebook": fought.notebook})
    hurt = mine(craft, last_attacker=CHASER, **HURT)
    ran = decide(craft, [hurt, enemy()], tick=1016)
    state = {"activity": Activity.GOING_HOME_EVADING, "notebook": ran.notebook}
    craft = view(**JUMPING | state)
    left = decide(craft, [hurt, enemy(target=15)], tick=1032)
    activities = [d.actions.activity for d in (fought, ran, left)]
    assert activities == [
        Activity.NONE,
        Activity.GOING_HOME_EVADING,
        Activity.LEAVING_BY_HYPERSPACE,
    ]
    routes = [m.split('"')[1] for m in caplog.messages if m.startswith("home.route")]
    assert routes == ["fight", "evade", "jump"]


def weave_home(craft, contacts, ticks):
    """Return each decision of a craft evading home at the ticks, its notebook
    carried from one to the next."""
    decisions = []
    for tick in ticks:
        if decisions:
            craft = dataclasses.replace(craft, notebook=decisions[-1].notebook)
        decisions.append(decide(craft, contacts(craft), tick))
    return decisions


def test_a_weave_home_ends_when_the_tail_clears_though_the_enemy_is_after_it(
    view, mine, enemy
) -> None:
    craft = view(**JUMPING)
    hit = {"last_attacker": CHASER} | HURT
    (first,) = weave_home(craft, lambda c: [mine(c, **hit), enemy()], [1000])
    later = view(**JUMPING | {"notebook": first.notebook})
    ahead = decide(later, [mine(later, **hit), enemy(x=AHEAD_X)], tick=1016)
    assert ahead.actions.steer == toward(FAR, roll=craft.roll)
    assert ahead.actions.activity == Activity.GOING_HOME_EVADING
    assert not weave.Weave.read(notebook.decode(ahead.notebook).scratch).flying


def test_weaving_home_the_dice_move_on_at_each_swerve(view, mine, enemy) -> None:
    craft = view(**JUMPING)
    decisions = weave_home(
        craft, lambda c: [mine(c, **HURT), enemy()], range(1000, 2400, 16)
    )
    swerves = 0
    for before, after in zip(decisions, decisions[1:], strict=False):
        dice = (notebook.decode(d.notebook).dice for d in (before, after))
        if after.actions.steer.yaw != before.actions.steer.yaw:
            swerves += 1
            assert len(set(dice)) == 2
    assert swerves == 2


def test_weaving_home_a_top_pilot_drops_flares_as_the_weave_says(
    view, mine, enemy
) -> None:
    """At its second swerve, one weave in four drops a flare: over 40 dice
    seeds some do, and none whose view shows no flare ready."""
    ready = {"countermeasure": 2, "countermeasures_left": 4}
    ready |= {"countermeasure_working": 1}
    dropped = {True: 0, False: 0}
    for seed in range(1, 41):
        for flare in (True, False):
            craft = view(**JUMPING | {"random_seed": seed} | (ready if flare else {}))
            decisions = weave_home(
                craft, lambda c: [mine(c, **HURT), enemy()], range(1000, 1520, 16)
            )
            dropped[flare] += any(d.actions.countermeasure for d in decisions)
    assert 4 <= dropped[True] <= 20 and dropped[False] == 0


def test_weaving_on_the_hangar_approach_is_at_full_throttle(
    view, mine, enemy, carrier
) -> None:
    craft = hangar_view(view, (60_000, 6, 7), yaw=HOME_YAW, skill=0xFFFF)
    decision = decide(craft, [mine(craft, **HURT), enemy(), carrier])
    assert decision.actions.activity == Activity.GOING_HOME_EVADING
    assert decision.actions.throttle == home.FULL_THROTTLE
    assert decision.actions.target == CARRIER
