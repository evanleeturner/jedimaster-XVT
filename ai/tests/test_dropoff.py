"""Fence for dropoff: a carrier on Drop Off flies to its drop point, asks the
engine for the drop, and stays out of the engine's way while it runs.

The carrier leads its group on slot 1 of two Drop Off orders, level and
facing +y, at (1000, -2000, 300); its order names group 6, of which slot 1
has delivered 3 (slot 0's count is 9, so a read of the wrong slot shows),
at throttle 0x9000. The engine names a drop point 65,536 units ahead and
932 up unless a test moves it. Two rules (Python flies the approach, the
engine drops; nothing to drop is done) each have a test; 1997's
approach (``old:paiorder.c:624-667``) is fenced number by number.
"""

from __future__ import annotations

import dataclasses
import logging

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import BehaviorCode
from jedimaster_ai.behaviors import dropoff
from jedimaster_ai.behaviors import formation
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_ACTIONS
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
LEADER = 8
GROUP = 6
DELIVERED = 3
ORDER_THROTTLE = 0x9000
ROLL = 77
HERE = (1000, -2000, 300)
POINT = (1000, -2000 + 0x10000, 300 + 932)
HOLD = (HOLD_FIRE, HOLD_FIRE)
AT_POINT = Books.DROP_POINT | Books.AT_DROP_POINT
DEPLOYING = Activity.DEPLOYING_CRAFT


@pytest.fixture
def carrier(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "x": HERE[0],
            "y": HERE[1],
            "z": HERE[2],
            "yaw": 0,
            "pitch": LEVEL,
            "roll": ROLL,
            "order_code": OrderCode.DROP_OFF,
            "order_codes": (OrderCode.DROP_OFF, OrderCode.DROP_OFF, 0, 0),
            "order_slot": 1,
            "order_variable2": GROUP,
            "order_progress": (9, DELIVERED, 0, 0),
            "order_throttle": ORDER_THROTTLE,
            "books": Books.DROP_POINT,
            "drop_point": POINT,
            "cannon_group_count": 2,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def ask(contact):
    """Return the carrier's decision, its own contact first, its actions
    checked against the engine's reader."""

    def decide(me, *others, tick=1000):
        own = contact(
            object_index=me.object_index,
            object_signature=me.object_signature,
            x=me.x,
            y=me.y,
            z=me.z,
            target=NO_OBJECT,
        )
        question = Question(tick, (me,), (own, *others))
        decision = dropoff.think(me, question)
        wire.check_shown(decision.actions, me, question)
        return decision

    return decide


@pytest.fixture
def lines(caplog):
    """Return a reader of the dropoff lines logged since the last read, at
    every level."""
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    caplog.clear()

    def read():
        found = [
            (record.levelname, record.getMessage())
            for record in caplog.records
            if record.name == dropoff.__name__
        ]
        caplog.clear()
        return found

    return read


def in_phase(phase, slot=1, tick=0):
    """Return a notebook dropoff wrote in a phase for an order slot."""
    scratch = dropoff.SLOT.pack(slot) + bytes(
        notebook.SCRATCH_BYTES - dropoff.SLOT.size
    )
    return notebook.Notebook(dropoff.CODE, phase, 1, tick, scratch).encode()


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


def test_the_order_and_behavior_codes_are_drop_offs() -> None:
    assert dropoff.DROP_OFF == OrderCode.DROP_OFF == 18
    assert dropoff.CODE == BehaviorCode.DROP_OFF


def test_far_from_the_point_it_steers_at_it_at_the_orders_throttle(
    carrier, ask
) -> None:
    decision = ask(carrier())
    yaw, pitch = geometry.bearing(0, 0x10000, 932)
    assert decision.actions == Actions(
        steer=Steer(yaw, pitch, ROLL),
        throttle=ORDER_THROTTLE,
        activity=DEPLOYING,
        target=NO_OBJECT,
        lasers=HOLD,
    )
    assert phase_of(decision) == dropoff.APPROACH


def test_an_order_throttle_of_0_is_full(carrier, ask) -> None:
    assert ask(carrier(order_throttle=0)).actions.throttle == 0xFFFF


def test_a_craft_showing_its_line_is_not_sent_it_again(carrier, ask) -> None:
    assert ask(carrier(activity=DEPLOYING)).actions.activity is None


@pytest.mark.parametrize(
    ("left", "order_throttle", "throttle"),
    [
        (0x4000, ORDER_THROTTLE, ORDER_THROTTLE),
        (0x3FFF, ORDER_THROTTLE, 0xC000),
        (0x3FFF, 0x2000, 0xC000),
        (4096, ORDER_THROTTLE, 0xC000),
        (4095, ORDER_THROTTLE, 0x6000),
        (0, ORDER_THROTTLE, 0x6000),
        (0x4000, 0, 0xFFFF),
    ],
)
def test_the_throttle_by_the_distance_left_is_1997s(
    carrier, left, order_throttle, throttle
) -> None:
    craft = carrier(order_throttle=order_throttle)
    assert dropoff.throttle_for(craft, left) == throttle


@pytest.mark.parametrize(
    ("offset", "throttle"),
    [
        # 4,000 straight: a rough distance (4,400) or a sum (5,600) is not
        # nearer than 4,096.
        ((2400, 3200, 0), 0x6000),
        # 4,242 straight: the height counts.
        ((0, 3000, 3000), 0xC000),
    ],
)
def test_the_distance_left_is_the_straight_line_to_the_point(
    carrier, ask, offset, throttle
) -> None:
    point = tuple(here + step for here, step in zip(HERE, offset, strict=True))
    assert ask(carrier(drop_point=point)).actions.throttle == throttle


def test_the_approach_logs_the_distance_and_throttle(carrier, ask, lines) -> None:
    point = (HERE[0] + 2400, HERE[1] + 3200, HERE[2])
    ask(carrier(drop_point=point))
    assert lines() == [
        ("DEBUG", "dropoff.approach object=4 group=6 left=4000 throttle=24576")
    ]


def test_at_the_point_it_stops_and_asks_for_the_drop(carrier, ask) -> None:
    decision = ask(carrier(books=AT_POINT))
    assert decision.actions == Actions(
        throttle=0, activity=DEPLOYING, target=NO_OBJECT, lasers=HOLD, drop_off=1
    )
    assert phase_of(decision) == dropoff.AT_POINT


def test_the_ask_is_logged_once(carrier, ask, lines) -> None:
    first = ask(carrier(books=AT_POINT))
    assert lines() == [("INFO", "dropoff.at_point object=4 group=6")]
    again = ask(carrier(books=AT_POINT, notebook=first.notebook))
    assert again.actions.drop_off == 1
    assert lines() == []


@pytest.mark.parametrize("phase", [1, 2])
def test_while_the_engine_drops_it_sends_only_its_line(carrier, ask, phase) -> None:
    craft = carrier(books=AT_POINT, drop_phase=phase)
    decision = ask(craft)
    assert decision.actions == Actions(activity=DEPLOYING, lasers=HOLD)
    assert phase_of(decision) == dropoff.DROPPING
    shown = dataclasses.replace(craft, activity=DEPLOYING)
    assert ask(shown).actions == Actions(lasers=HOLD)


def test_the_drop_comes_first_whatever_the_order_and_books(carrier, ask) -> None:
    craft = carrier(
        order_code=OrderCode.SS_GO_HOME,
        books=Books.ALL_DROPPED | Books.ORDERS_DONE | Books.SKIP_DUE,
        drop_phase=2,
    )
    assert ask(craft).actions == Actions(activity=DEPLOYING, lasers=HOLD)


def test_the_drop_is_logged_when_it_starts_and_each_think(carrier, ask, lines) -> None:
    first = ask(carrier(drop_phase=1, notebook=in_phase(dropoff.AT_POINT)))
    assert lines() == [
        ("INFO", "dropoff.dropping object=4 group=6"),
        ("DEBUG", "dropoff.drop object=4 phase=1 delivered=3"),
    ]
    ask(carrier(drop_phase=2, notebook=first.notebook))
    assert lines() == [("DEBUG", "dropoff.drop object=4 phase=2 delivered=3")]


@pytest.mark.parametrize("books", [Books.ABORTED, Books.DEPARTURE_DUE])
def test_sent_home_while_it_drops_it_stops_the_drop(carrier, ask, lines, books) -> None:
    decision = ask(carrier(books=books, drop_phase=2))
    assert decision.actions == Actions(lasers=HOLD, drop_off=0)
    assert lines() == [
        ("INFO", f"dropoff.stopped object=4 delivered=3 books={int(books)}")
    ]


def test_a_sith_lords_abort_never_stops_its_drop(carrier, ask, lines) -> None:
    decision = ask(carrier(books=Books.ABORTED, drop_phase=2, skill_level=7))
    assert decision.actions.drop_off is None
    assert lines() == [
        ("INFO", "dropoff.dropping object=4 group=6"),
        ("DEBUG", "dropoff.drop object=4 phase=2 delivered=3"),
    ]


def test_a_sith_lords_departure_still_stops_its_drop(carrier, ask) -> None:
    books = Books.ABORTED | Books.DEPARTURE_DUE
    decision = ask(carrier(books=books, drop_phase=2, skill_level=7))
    assert decision.actions == Actions(lasers=HOLD, drop_off=0)


@pytest.mark.parametrize(
    "books",
    [Books.ORDERS_DONE, Books.SKIP_DUE, Books.SKIPPED, Books.ALL_DROPPED],
)
def test_no_other_book_stops_a_drop(carrier, ask, books) -> None:
    assert ask(carrier(books=books, drop_phase=1)).actions.drop_off is None


def test_done_is_all_dropped(carrier) -> None:
    assert dropoff.done(carrier(books=Books.ALL_DROPPED))
    others = Books(sum(Books) & ~Books.ALL_DROPPED)
    assert not dropoff.done(carrier(books=others))


def test_done_it_ends_its_line_and_sends_nothing_more(carrier, ask) -> None:
    decision = ask(carrier(books=Books.ALL_DROPPED, activity=DEPLOYING))
    assert decision.actions == Actions(activity=Activity.NONE)
    assert phase_of(decision) == dropoff.DONE
    other = carrier(books=Books.ALL_DROPPED, activity=Activity.GOING_HOME)
    assert ask(other).actions == NO_ACTIONS


def test_done_is_logged_once_per_order_slot(carrier, ask, lines) -> None:
    first = ask(carrier(books=Books.ALL_DROPPED))
    assert lines() == [("INFO", "dropoff.done object=4 group=6 delivered=3")]
    ask(carrier(books=Books.ALL_DROPPED, notebook=first.notebook))
    assert lines() == []
    ask(carrier(books=Books.ALL_DROPPED, notebook=in_phase(dropoff.DONE, slot=0)))
    assert lines() == [("INFO", "dropoff.done object=4 group=6 delivered=3")]


@pytest.fixture
def leader(contact):
    """Return the leader: on the carrier's team, 20,000 units ahead,
    dropping."""
    return contact(
        object_index=LEADER,
        team=1,
        x=HERE[0],
        y=HERE[1] + 20_000,
        z=HERE[2],
        yaw=0,
        pitch=LEVEL,
        speed=0,
        activity=DEPLOYING,
    )


def test_a_follower_flies_to_its_place_and_never_drops(
    carrier, contact, leader
) -> None:
    craft = carrier(
        leader=LEADER,
        books=Books.PLACE | AT_POINT,
        place=(HERE[0] + 500, HERE[1] + 19_000, HERE[2]),
    )
    own = contact(object_index=ME, x=HERE[0], y=HERE[1], z=HERE[2], genus=0)
    question = Question(1000, (craft,), (own, leader))
    decision = dropoff.think(craft, question)
    wire.check_shown(decision.actions, craft, question)
    flown = formation.fly(craft, question, leader)
    assert decision.actions == dataclasses.replace(flown, lasers=HOLD)
    assert flown.steer is not None and flown.throttle
    assert phase_of(decision) == dropoff.FOLLOWING


@pytest.mark.parametrize("shown", ["gone", "leaving", "no place"])
def test_a_follower_with_no_leader_to_follow_holds_still(
    carrier, ask, leader, lines, shown
) -> None:
    books = 0 if shown == "no place" else Books.PLACE
    craft = carrier(leader=LEADER, books=books, place=(HERE[0], HERE[1] + 19_000, 0))
    others = ()
    if shown != "gone":
        activity = Activity.LEAVING_BY_HYPERSPACE if shown == "leaving" else DEPLOYING
        others = (dataclasses.replace(leader, activity=activity),)
    decision = ask(craft, *others)
    assert decision.actions == Actions(throttle=0, lasers=HOLD)
    assert phase_of(decision) == dropoff.NO_LEADER
    assert lines() == [("DEBUG", "dropoff.no_leader object=4 leader=8")]
    ask(dataclasses.replace(craft, notebook=decision.notebook), *others)
    assert lines() == []


def test_with_no_point_and_not_done_it_holds_and_warns_once(
    carrier, ask, lines
) -> None:
    craft = carrier(books=Books.TARGETS_LEFT)
    decision = ask(craft)
    assert decision.actions == Actions(throttle=0, lasers=HOLD)
    assert phase_of(decision) == dropoff.NO_POINT
    assert lines() == [("WARNING", "dropoff.no_point object=4 group=6 books=512")]
    ask(dataclasses.replace(craft, notebook=decision.notebook))
    assert lines() == []


def test_the_notebook_holds_the_slot_and_a_phase_keeps_its_first_tick(
    carrier, ask
) -> None:
    first = ask(carrier(), tick=1000)
    written = notebook.decode(first.notebook)
    assert written.behavior == BehaviorCode.DROP_OFF
    assert dropoff.SLOT.unpack_from(written.scratch) == (1,)
    assert written.phase_tick == 1000
    later = ask(carrier(notebook=first.notebook), tick=1500)
    assert notebook.decode(later.notebook).phase_tick == 1000
    moved = ask(carrier(books=AT_POINT, notebook=first.notebook), tick=1700)
    assert notebook.decode(moved.notebook).phase_tick == 1700
