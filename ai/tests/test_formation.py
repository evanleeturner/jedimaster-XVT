"""Fence for formation: a follower flying to its place beside its leader.

Each rule once: the leader a craft follows (only one the engine names, the
question shows and no player flies, with its place given, flying the same
way); the far case, measured from its place, and a starship's farther
reach; a place far behind its leader held, one far behind the craft never
turned back to; at its place it
flies the leader's heading at the leader's speed; a place ahead speeds it
up, one behind slows it to half the leader's speed and is never turned back
to; a place aside turns it that way; a bulky follower's think interval; the
leader with followers eased to seven eighths (ruling 48); the jump roll
(ruling 35). Then the two behaviors that call it: patrol, and going home by
a jump (the follower departing after a leader that has begun its jump), to
a hangar and with no way out. The log lines. Then a real case: a
follower 20,000 units behind its place flown through think() until it holds
it.
"""

from __future__ import annotations

import dataclasses
import logging
import math

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import tactics
from jedimaster_ai import wire
from jedimaster_ai.behaviors import formation
from jedimaster_ai.behaviors import home
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.think import think
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

ME = 4
LEADER = 9
MATE = 5
CARRIER = 20
# The craft sits at (-5, 6, 7) (conftest's VIEW); its leader flies +x, level.
HERE = (-5, 6, 7)
EAST = 0x4000
LEVEL = 0x4000
LEADER_SPEED = 100
CIRCLE = 2


@pytest.fixture
def follower(view):
    """Return a maker of a follower of LEADER whose place is where it sits,
    the named fields changed."""

    def make(**changes):
        fields = {
            "object_index": ME,
            "leader": LEADER,
            "books": Books.PLACE,
            "place": HERE,
            "full_speed": 300,
        }
        return view(**(fields | changes))

    return make


@pytest.fixture
def own(contact):
    """Return a maker of the craft's own contact, a fighter at HERE."""

    def make(**changes):
        fields = {"object_index": ME, "team": 1, "flight_group": 2, "genus": 0}
        return contact(**(fields | dict(zip("xyz", HERE, strict=True)) | changes))

    return make


@pytest.fixture
def leader(contact):
    """Return a maker of the leader's contact: 1,000 units east of HERE,
    flying east, level, at LEADER_SPEED."""

    def make(**changes):
        fields = {
            "object_index": LEADER,
            "team": 1,
            "flight_group": 2,
            "genus": 0,
            "x": 995,
            "y": 6,
            "z": 7,
            "yaw": EAST,
            "pitch": LEVEL,
            "speed": LEADER_SPEED,
            "activity": Activity.NONE,
            "target": NO_OBJECT,
        }
        return contact(**(fields | changes))

    return make


def flown(craft, contacts, tick=1000):
    """Return formation's actions for the craft's leader among contacts."""
    question = Question(tick, (craft,), tuple(contacts))
    lead = formation.leader_for(craft, question, formation.on_order)
    assert lead is not None
    actions = formation.fly(craft, question, lead)
    wire.check_shown(actions, craft, question)
    return actions


def test_the_leader_is_the_engines_shown_and_flown_by_no_player(
    follower, own, leader
) -> None:
    craft = follower()

    def found(view, contacts, flies=formation.on_order):
        return formation.leader_for(view, Question(1, (view,), tuple(contacts)), flies)

    assert found(craft, [own(), leader()]) == leader()
    assert found(dataclasses.replace(craft, books=0), [own(), leader()]) is None
    assert (
        found(dataclasses.replace(craft, leader=NO_OBJECT), [own(), leader()]) is None
    )
    assert found(craft, [own()]) is None
    assert found(craft, [own(), leader(flags=1)]) is None
    assert found(craft, [own(), leader(activity=Activity.GOING_HOME)]) is None
    assert found(craft, [own(), leader()], lambda contact: False) is None


@pytest.mark.parametrize(
    "activity, flies",
    [
        (Activity.NONE, True),
        (Activity.WAITING, True),
        (Activity.GOING_HOME, False),
        (Activity.GOING_HOME_EVADING, False),
        (Activity.LEAVING_BY_HYPERSPACE, False),
        (Activity.ENTERING_HANGAR, False),
    ],
)
def test_a_leader_flies_its_order_until_it_is_on_its_way_out(
    leader, activity, flies
) -> None:
    assert formation.on_order(leader(activity=activity)) is flies


def test_a_place_beyond_the_far_reach_not_behind_it_is_flown_at_full_throttle(
    follower, own, leader
) -> None:
    ahead = flown(
        follower(place=(-5 + formation.FAR + 1000, 6 - 500, 7)), [own(), leader()]
    )
    assert (ahead.steer.yaw, ahead.steer.pitch) == geometry.bearing(
        formation.FAR + 1000, -500, 0
    )
    assert ahead.throttle == formation.FULL_THROTTLE
    aside = flown(follower(place=(-5, 6 - formation.FAR - 1000, 7)), [own(), leader()])
    assert aside.steer.yaw == geometry.bearing(0, -formation.FAR - 1000, 0)[0]
    assert aside.throttle == formation.FULL_THROTTLE


def test_a_place_far_behind_its_leader_is_held_not_chased(
    follower, own, leader
) -> None:
    # A back row the mission sets farther than FAR behind the leader: at its
    # place the craft flies the leader's heading at the leader's speed.
    craft = follower()
    actions = flown(craft, [own(), leader(x=2 * formation.FAR)])
    assert actions.steer.yaw == EAST
    assert actions.throttle == tactics.throttle_for(craft, LEADER_SPEED)


def test_a_place_beyond_the_far_reach_behind_it_slows_it_never_turning_back(
    follower, own, leader
) -> None:
    craft = follower(place=(-5 - formation.FAR - 1000, 6, 7))
    actions = flown(craft, [own(), leader()])
    assert actions.steer.yaw == EAST
    slowest = LEADER_SPEED * formation.SLOWEST // 4
    assert actions.throttle == tactics.throttle_for(craft, slowest)


def test_a_starship_flies_in_formation_out_to_its_own_far_reach(
    follower, own, leader
) -> None:
    # Its place lies FAR + 1,000 ahead and 3,000 to its right: a starship
    # flies to it in formation, aiming LOOK_BULKY past it; a fighter as far
    # from its place flies straight at it, as a starship does past its own
    # far reach.
    craft = follower(place=(-5 + formation.FAR + 1000, 6 - 3000, 7))
    starship = own(genus=tactics.GENUS_STARSHIP, size=20_000)
    actions = flown(craft, [starship, leader()])
    assert (
        actions.steer.yaw
        == geometry.bearing(formation.FAR + 1000 + formation.LOOK_BULKY, -3000, 0)[0]
    )
    assert actions.think_interval == formation.INTERVAL_BULKY
    farther = follower(place=(-5 + formation.FAR_STARSHIP + 1000, 6 - 3000, 7))
    beyond = flown(farther, [starship, leader()])
    assert (
        beyond.steer.yaw == geometry.bearing(formation.FAR_STARSHIP + 1000, -3000, 0)[0]
    )
    assert beyond.throttle == formation.FULL_THROTTLE
    fighter = flown(craft, [own(), leader()])
    assert fighter.think_interval == formation.INTERVAL
    assert fighter.steer.yaw == geometry.bearing(formation.FAR + 1000, -3000, 0)[0]
    assert fighter.throttle == formation.FULL_THROTTLE


def test_at_its_place_it_flies_the_leaders_heading_at_the_leaders_speed(
    follower, own, leader
) -> None:
    actions = flown(follower(), [own(), leader()])
    assert (actions.steer.yaw, actions.steer.pitch, actions.steer.roll) == (
        EAST,
        LEVEL,
        300,
    )
    assert actions.throttle == tactics.throttle_for(follower(), LEADER_SPEED)
    assert actions.think_interval == formation.INTERVAL


def test_a_place_ahead_speeds_it_up_by_one_for_each_close_units(
    follower, own, leader
) -> None:
    ahead = 100 * formation.CLOSE
    actions = flown(follower(place=(-5 + ahead, 6, 7)), [own(), leader()])
    assert actions.steer.yaw == EAST
    assert actions.throttle == tactics.throttle_for(follower(), LEADER_SPEED + 100)


def test_a_place_behind_slows_it_to_half_the_leaders_speed_never_turning_back(
    follower, own, leader
) -> None:
    actions = flown(follower(place=(-20_005, 6, 7)), [own(), leader()])
    assert actions.steer.yaw == EAST
    assert actions.throttle == tactics.throttle_for(follower(), LEADER_SPEED // 2)
    slower = flown(follower(place=(-5 - 20 * formation.CLOSE, 6, 7)), [own(), leader()])
    assert slower.throttle == tactics.throttle_for(follower(), LEADER_SPEED - 20)


def test_a_place_aside_turns_it_that_way_while_ahead_along_the_heading(
    follower, own, leader
) -> None:
    # Flying east, a place 3,000 units toward -y lies to the right, a yaw
    # past east; to the left (+y), one short of it.
    right = flown(follower(place=(-5, 6 - 3000, 7)), [own(), leader()])
    left = flown(follower(place=(-5, 6 + 3000, 7)), [own(), leader()])
    assert right.steer.yaw == geometry.bearing(formation.LOOK, -3000, 0)[0]
    assert EAST < right.steer.yaw < EAST + geometry.QUARTER_TURN
    assert EAST - geometry.QUARTER_TURN < left.steer.yaw < EAST
    below = flown(follower(place=(-5, 6, 7 - 3000)), [own(), leader()])
    assert below.steer.pitch > LEVEL


def test_at_its_place_behind_a_climbing_leader_it_climbs_with_it(
    follower, own, leader
) -> None:
    # Pitch 0x3000 is nose up, an eighth of a turn above level.
    actions = flown(follower(), [own(), leader(pitch=0x3000)])
    assert abs(geometry.signed(actions.steer.pitch - 0x3000)) <= 8
    assert actions.steer.yaw == EAST


def test_a_bulky_follower_aims_farther_ahead(follower, own, leader) -> None:
    craft = follower(place=(-5, 6 - 3000, 7))
    freighter = own(genus=tactics.GENUS_FREIGHTER, size=4960)
    actions = flown(craft, [freighter, leader(genus=tactics.GENUS_FREIGHTER)])
    assert actions.steer.yaw == geometry.bearing(formation.LOOK_BULKY, -3000, 0)[0]
    assert actions.think_interval == formation.INTERVAL_BULKY


def test_a_leader_with_a_follower_in_the_battle_cruises_at_seven_eighths(
    view, own, contact
) -> None:
    lead = view(object_index=ME, leader=NO_OBJECT)
    mate = contact(object_index=MATE, team=1, flight_group=2)

    def eased(craft, contacts, throttle=formation.FULL_THROTTLE):
        return formation.eased(craft, Question(1, (craft,), tuple(contacts)), throttle)

    assert eased(lead, [own(), mate]) == formation.EASED_THROTTLE == 0xE000
    assert eased(lead, [own(), mate], 0x8000) == 0x8000
    assert eased(lead, [own()]) == formation.FULL_THROTTLE
    assert eased(lead, [own(), dataclasses.replace(mate, team=2)]) == 0xFFFF
    assert eased(lead, [own(), dataclasses.replace(mate, flight_group=3)]) == 0xFFFF
    assert eased(lead, [own(), dataclasses.replace(mate, flags=1)]) == 0xFFFF
    assert eased(dataclasses.replace(lead, leader=LEADER), [own(), mate]) == 0xFFFF


def test_the_jump_roll_is_one_in_eight_and_each_crafts_own(follower) -> None:
    craft = [
        follower(random_seed=seed, object_index=index)
        for seed in (1, 2)
        for index in (ME, MATE)
    ]
    rolls = [tuple(formation.jumps_now(c, tick) for tick in range(8000)) for c in craft]
    assert all(900 < sum(rolled) < 1100 for rolled in rolls)
    for rolled in rolls:
        ticks = [tick for tick, now in enumerate(rolled) if now]
        assert (
            len(
                {
                    later - earlier
                    for earlier, later in zip(ticks, ticks[1:], strict=False)
                }
            )
            > 20
        )
    together = sum(first and second for first, second in zip(*rolls[:2], strict=True))
    assert 60 < together < 200
    assert rolls[0] == tuple(
        formation.jumps_now(craft[0], tick) for tick in range(8000)
    )
    assert len(set(rolls)) == len(rolls)


def patrol_view(follower, **changes):
    fields = {
        "order_code": CIRCLE,
        "order_throttle": 0xFFFF,
        "order_variable1": 5,
        "waypoint_mask": 1,
        "waypoint_x": (0,) * 8,
        "waypoint_y": (100_000,) + (0,) * 7,
        "waypoint_z": (0,) * 8,
    }
    return follower(**(fields | changes))


def test_a_patrol_follower_flies_formation_while_its_leader_flies_the_order(
    follower, own, leader
) -> None:
    craft = patrol_view(follower, place=(-5, 6 - 3000, 7))
    contacts = (own(), leader())
    question = Question(1000, (craft,), contacts)
    actions = patrol.think(craft, question).actions
    assert actions == flown(craft, contacts)
    going = (own(), leader(activity=Activity.GOING_HOME))
    alone = patrol.think(craft, Question(1000, (craft,), going)).actions
    assert alone.steer.yaw == geometry.bearing(5, 100_000 - 6, -7)[0]
    assert alone.throttle == formation.FULL_THROTTLE


def test_a_patrol_leader_with_a_follower_is_eased(follower, own, contact) -> None:
    lead = patrol_view(follower, leader=NO_OBJECT, books=0)
    mate = contact(object_index=MATE, team=1, flight_group=2)
    question = Question(1000, (lead,), (own(), mate))
    assert patrol.think(lead, question).actions.throttle == formation.EASED_THROTTLE


def home_view(follower, **changes):
    fields = {
        "order_code": CIRCLE,
        "books": Books.PLACE | Books.ORDERS_DONE | Books.MAY_JUMP | Books.HOME_POINT,
        "home_point": (200_000, 6, 7),
        "enemy_mask": 0,
    }
    return follower(**(fields | changes))


def home_actions(craft, contacts, tick=1000):
    question = Question(tick, (craft,), tuple(contacts))
    actions = home.think(craft, question).actions
    wire.check_shown(actions, craft, question)
    return actions


def test_a_jumping_follower_flies_formation_and_departs_on_its_roll(
    follower, own, leader
) -> None:
    craft = home_view(follower, place=(-5, 6 - 3000, 7))
    jumping = leader(activity=Activity.LEAVING_BY_HYPERSPACE)
    actions = home_actions(craft, [own(), jumping])
    formed = flown(craft, [own(), dataclasses.replace(jumping, activity=0)])
    assert actions == dataclasses.replace(
        formed, activity=Activity.LEAVING_BY_HYPERSPACE, target=NO_OBJECT
    )
    near = dataclasses.replace(craft, home_point=(1000, 6, 7))
    ticks = range(1000, 1200)
    yes = next(tick for tick in ticks if formation.jumps_now(near, tick))
    no = next(tick for tick in ticks if not formation.jumps_now(near, tick))
    assert home_actions(near, [own(), jumping], yes).depart
    stays = home_actions(near, [own(), jumping], no)
    assert not stays.depart
    assert stays.steer is not None
    assert home_actions(near, [own(), leader()], no).depart


def test_after_its_leader_has_begun_its_jump_a_follower_departs_where_it_is(
    follower, own, leader, caplog
) -> None:
    # Its home point lies 200,000 away: it departs on its roll all the same,
    # flies formation between rolls, and departs surely from LAST_CALL.
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    craft = home_view(follower, place=(-5, 6 - 3000, 7))
    gone = leader(
        activity=Activity.LEAVING_BY_HYPERSPACE,
        object_kind=home.OBJECT_KIND_ENTERING_HYPERSPACE,
        speed=home.LAST_CALL - 1,
    )
    ticks = range(1000, 1200)
    yes = next(tick for tick in ticks if formation.jumps_now(craft, tick))
    no = next(tick for tick in ticks if not formation.jumps_now(craft, tick))
    assert home_actions(craft, [own(), gone], yes).depart
    waits = home_actions(craft, [own(), gone], no)
    assert not waits.depart
    assert waits == dataclasses.replace(
        flown(craft, [own(), dataclasses.replace(gone, activity=0)], no),
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
    )
    last = dataclasses.replace(gone, speed=home.LAST_CALL)
    assert home_actions(craft, [own(), last], no).depart
    still = leader(activity=Activity.LEAVING_BY_HYPERSPACE, speed=home.LAST_CALL)
    assert not home_actions(craft, [own(), still], yes).depart
    lines = [r.getMessage() for r in caplog.records if "after_leader" in r.getMessage()]
    assert lines == [
        f"home.after_leader object=4 leader=9 speed={home.LAST_CALL - 1} sure=0",
        f"home.after_leader object=4 leader=9 speed={home.LAST_CALL} sure=1",
    ]


def test_a_jumping_leader_with_a_follower_is_eased_and_departs_at_once(
    follower, own, contact
) -> None:
    lead = home_view(follower, leader=NO_OBJECT)
    mate = contact(object_index=MATE, team=1, flight_group=2, target=NO_OBJECT)
    assert home_actions(lead, [own(), mate]).throttle == formation.EASED_THROTTLE
    there = dataclasses.replace(lead, home_point=(1000, 6, 7))
    assert home_actions(there, [own(), mate]).depart


def test_a_follower_going_to_its_hangar_flies_formation_until_the_approach(
    follower, own, leader, contact
) -> None:
    far_door = (300_000, 6, 7)
    craft = home_view(
        follower,
        books=Books.PLACE | Books.ORDERS_DONE,
        mothership=CARRIER,
        outside_hangar=far_door,
        inside_hangar=(300_000, 1006, 7),
    )
    carrier = contact(object_index=CARRIER, team=1, flight_group=7, genus=4, x=300_000)
    going = leader(activity=Activity.GOING_HOME, target=CARRIER)
    contacts = [own(), going, carrier]
    actions = home_actions(craft, contacts)
    formed = flown(craft, [own(), dataclasses.replace(going, activity=0)])
    assert actions == dataclasses.replace(
        formed, activity=Activity.GOING_HOME, target=CARRIER
    )
    elsewhere = home_actions(
        craft, [own(), dataclasses.replace(going, target=21), carrier]
    )
    assert elsewhere.steer.yaw == EAST
    assert elsewhere.throttle == formation.FULL_THROTTLE
    close = dataclasses.replace(craft, outside_hangar=(60_000, 6, 7))
    approach = home_actions(close, contacts)
    assert approach.throttle == tactics.throttle_for(close, 150)
    assert approach.think_interval is None


def test_with_no_way_out_a_follower_stops_beside_its_leader(
    follower, own, leader
) -> None:
    craft = home_view(
        follower, books=Books.PLACE | Books.ORDERS_DONE | Books.HOME_POINT
    )
    waiting = leader(activity=Activity.GOING_HOME, speed=0)
    actions = home_actions(craft, [own(), waiting])
    assert actions.activity == Activity.GOING_HOME
    assert actions.target == NO_OBJECT
    assert actions.throttle == 0


def test_debug_records_where_the_place_lies_and_the_far_case(
    follower, own, leader, caplog
) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    flown(follower(place=(-5 + 3200, 6 - 400, 7)), [own(), leader()])
    flown(follower(place=(-5 + formation.FAR + 1000, 6, 7)), [own(), leader()])
    lines = [r.getMessage() for r in caplog.records if r.name.endswith("formation")]
    assert lines == [
        "formation.fly object=4 leader=9 ahead=3200 aside=400 speed=200 throttle=43690",
        f"formation.far object=4 leader=9 distance={formation.FAR + 1000}",
    ]


def test_a_follower_twenty_thousand_behind_flies_into_its_place(
    follower, own, leader
) -> None:
    """A real case: leader and follower fighters (top speed 150, a turn of
    47 yaw units a tick, 60 speed a simulated second) east at the eased
    throttle, the place 1,500 to the leader's right and behind; the
    follower starts 20,000 behind it and thinks through think()."""
    lead = {"x": 0.0, "y": 0.0, "yaw": EAST, "speed": 131}
    me = {"x": -21_500.0, "y": -1500.0, "yaw": EAST, "speed": 131, "throttle": 0xE000}
    errors, book, steer = [], bytes(64), EAST
    for tick in range(24_000):
        place = (round(lead["x"]) - 1500, round(lead["y"]) - 1500, 0)
        if tick % formation.INTERVAL == 0:
            craft = follower(
                x=round(me["x"]),
                y=round(me["y"]),
                z=0,
                yaw=me["yaw"],
                pitch=LEVEL,
                roll=0,
                speed=round(me["speed"]),
                full_speed=150,
                place=place,
                order_code=CIRCLE,
                waypoint_mask=0,
                notebook=book,
            )
            contacts = (
                own(x=craft.x, y=craft.y, z=0, yaw=craft.yaw, speed=craft.speed),
                leader(x=round(lead["x"]), y=round(lead["y"]), z=0, speed=131),
            )
            answer = think(Question(tick, (craft,), contacts)).craft[0]
            book, actions = answer.notebook, answer.actions
            steer, me["throttle"] = actions.steer.yaw, actions.throttle
        turn = max(-47, min(47, geometry.signed(steer - me["yaw"])))
        me["yaw"] = (me["yaw"] + turn) % geometry.TURN
        goal = me["throttle"] * 150 >> 16
        me["speed"] += max(-60 / 236, min(60 / 236, goal - me["speed"]))
        for craft_state in (lead, me):
            step = (4660 * round(craft_state["speed"]) + 128 >> 8) / 236
            angle = craft_state["yaw"] * 2 * math.pi / geometry.TURN
            craft_state["x"] += step * math.sin(angle)
            craft_state["y"] += step * math.cos(angle)
        errors.append(math.dist((me["x"], me["y"]), place[:2]))
    assert errors[0] > 19_900
    assert max(errors[-2000:]) < 300
    assert all(
        later <= earlier + 50
        for earlier, later in zip(errors, errors[100:], strict=False)
    )
