"""Fence for board: the boarding orders find a craft to board, fly to its
board point and dock.

The craft flies at the origin facing +y (yaw 0), level, on a Give order
that asks one boarding; contact 0 is its own, contacts 1 and 2 the craft its
order names. The craft to board sits parked 32,768 units ahead, a model of
size 0, so its clearance is 4,096. Rulings C1 (Python flies the approach),
C2 (full throttle past 16,384), C6 (craft only), C8 (each order's looking
line) and C9 (the lower object index keeps a craft) each have a test; the
1997 search (``old:pai.c:802-939``) is fenced test by test. Round L2 adds
the dock: the board action at the point, nothing else sent while the engine
docks (ruling L2-1), the look after it (C7), the docked list on the command
path (ruling 102) and the first pair before the second (ruling 101).
"""

from __future__ import annotations

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import notebook
from jedimaster_ai import progress
from jedimaster_ai import wire
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import board
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
TARGET = 9
SIGNATURE = 11
OTHER = 10
FAR = 12
NAMED = 13
SOMEONE = 3
GIVE = 12
CAPTURE = 15
DESTROY = 16
PLATFORM = 5
FREIGHTER = 3
# Not active (0) or disabled (2): breaking up.
BREAKING_UP = 5
BOARD_POINT = Books.TARGETS_LEFT | Books.BOARD_POINT
AT_POINT = BOARD_POINT | Books.AT_BOARD_POINT


@pytest.fixture
def boarder(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "x": 0,
            "y": 0,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "roll": 0,
            "order_code": GIVE,
            "order_codes": (GIVE, 0, 0, 0),
            "order_target_mask": 0b110,
            "order_variable2": 1,
            "cannon_group_count": 2,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def craft(contact):
    """Return a craft to board: parked 32,768 units ahead."""

    def make(**changes):
        theirs = {
            "object_index": TARGET,
            "object_signature": SIGNATURE,
            "x": 0,
            "y": 0x8000,
            "z": 0,
            "flags": ContactFlags.PARKED,
            "genus": FREIGHTER,
            "size": 0,
            "speed": 0,
            "target": NO_OBJECT,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def ask(contact):
    """Return the craft's boarding decision, its own contact first, its
    actions checked against the engine's reader."""

    def decide(me, *others, tick=1000, statics=(), **mine):
        fields = {
            "object_index": me.object_index,
            "object_signature": me.object_signature,
            "x": me.x,
            "y": me.y,
            "z": me.z,
            "genus": 0,
            "flags": 0,
            "target": NO_OBJECT,
        }
        own = contact(**(fields | mine))
        question = Question(tick, (me,), (own, *others), statics)
        decision = board.think(me, question)
        wire.check_shown(decision.actions, me, question)
        return decision

    return decide


def holding(phase=board.APPROACH, slot=0, target=TARGET, signature=SIGNATURE):
    """Return a notebook that holds a target."""
    memory = board.Memory(slot, 1, target, signature)
    return notebook.Notebook(board.CODE, phase, 1, 0, memory.write()).encode()


def memory_of(decision) -> board.Memory:
    return board.Memory.read(notebook.decode(decision.notebook).scratch)


def phase_of(decision) -> int:
    return notebook.decode(decision.notebook).phase


def test_the_order_codes_are_the_eight_boarding_orders() -> None:
    assert (
        board.GIVE,
        board.TAKE,
        board.EXCHANGE,
        board.CAPTURE,
        board.DESTROY,
        board.PICK_UP,
        board.SS_BOARD,
        board.REPAIR,
    ) == (
        OrderCode.BOARD_AND_GIVE_CARGO,
        OrderCode.BOARD_AND_TAKE_CARGO,
        OrderCode.BOARD_AND_EXCHANGE_CARGO,
        OrderCode.BOARD_AND_CAPTURE_CARGO,
        OrderCode.BOARD_AND_DESTROY_CARGO,
        OrderCode.PICK_UP,
        OrderCode.SS_BOARD,
        OrderCode.BOARD_TO_REPAIR,
    )
    assert len(board.ORDERS) == 8
    assert (board.DONE, board.NO_TARGET_LEFT) == (
        progress.DONE,
        progress.NO_BOARDING_TARGET,
    )


def test_it_picks_a_parked_craft_its_order_names(boarder, craft, ask) -> None:
    decision = ask(boarder(), craft())
    assert decision.actions.target == TARGET
    assert decision.actions.activity == Activity.BOARDING
    assert decision.actions.throttle == 0xFFFF
    assert phase_of(decision) == board.APPROACH
    assert memory_of(decision) == board.Memory(0, 1, TARGET, SIGNATURE)


def test_with_nothing_to_board_it_stops_and_looks(boarder, craft, ask) -> None:
    decision = ask(boarder(), craft(flags=0))
    assert decision.actions.throttle == 0
    assert decision.actions.steer is None
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.activity == Activity.LOOKING_TO_BOARD
    assert phase_of(decision) == board.LOOKING
    assert memory_of(decision).held == 0


@pytest.mark.parametrize(
    ("flags", "genus", "activity", "strict", "boardable"),
    [
        (ContactFlags.PARKED, FREIGHTER, 0, False, True),
        (ContactFlags.DISABLED, FREIGHTER, 0, False, True),
        (0, PLATFORM, 0, False, True),
        (ContactFlags.AWAITING, FREIGHTER, 0, False, True),
        (0, FREIGHTER, Activity.LOOKING_TO_BOARD, False, True),
        (0, FREIGHTER, Activity.LOOKING_TO_CAPTURE, False, True),
        (0, FREIGHTER, Activity.LOOKING_TO_PICK_UP, False, True),
        (0, FREIGHTER, Activity.WAITING_FOR_BOARDING_CRAFT, False, True),
        (0, FREIGHTER, Activity.BOARDING, False, False),
        (0, FREIGHTER, 0, False, False),
        (ContactFlags.PARKED, FREIGHTER, 0, True, True),
        (ContactFlags.DISABLED, FREIGHTER, 0, True, True),
        (0, PLATFORM, 0, True, True),
        (ContactFlags.AWAITING, FREIGHTER, 0, True, False),
        (0, FREIGHTER, Activity.LOOKING_TO_BOARD, True, False),
        (0, FREIGHTER, Activity.WAITING_FOR_BOARDING_CRAFT, True, False),
        (ContactFlags.PLAYER | ContactFlags.DISABLED, FREIGHTER, 0, True, True),
    ],
    ids=[
        "parked", "disabled", "platform", "awaiting", "looking boarder",
        "looking capturer", "looking picker", "at a rendezvous",
        "docking boarder", "flying", "strict parked", "strict disabled",
        "strict platform", "strict awaiting", "strict looking boarder",
        "strict at a rendezvous", "disabled player",
    ],
)  # fmt: skip
def test_what_may_be_boarded_now(
    contact, flags, genus, activity, strict, boardable
) -> None:
    candidate = contact(flags=flags, genus=genus, activity=activity)
    assert board.may_board(candidate, strict) is boardable


@pytest.mark.parametrize("order", [CAPTURE, DESTROY])
def test_capture_and_destroy_pass_over_a_craft_waiting_to_be_boarded(
    boarder, craft, ask, order
) -> None:
    me = boarder(order_code=order, order_codes=(order, 0, 0, 0))
    assert ask(me, craft(flags=ContactFlags.AWAITING)).actions.target == NO_OBJECT
    assert ask(boarder(), craft(flags=ContactFlags.AWAITING)).actions.target == TARGET


def test_a_craft_its_order_does_not_name_is_passed_over(boarder, craft, ask) -> None:
    decision = ask(boarder(order_target_mask=0b100), craft())
    assert decision.actions.target == NO_OBJECT


def test_it_never_boards_itself(boarder, ask) -> None:
    decision = ask(boarder(order_target_mask=0b1), flags=ContactFlags.PARKED)
    assert decision.actions.target == NO_OBJECT


def test_the_nearest_is_by_1997s_rough_distance(boarder, craft, ask) -> None:
    corner = craft(x=3000, y=3000)
    beside = craft(object_index=OTHER, object_signature=12, x=4400, y=0)
    assert geometry.distance(3000, 3000, 0) < geometry.distance(4400, 0, 0)
    assert ask(boarder(), corner, beside).actions.target == OTHER


def test_between_two_as_near_the_earlier_contact_wins(boarder, craft, ask) -> None:
    first = craft(object_index=FAR, y=5000)
    second = craft(object_index=OTHER, x=5000, y=0)
    assert ask(boarder(), first, second).actions.target == FAR
    assert ask(boarder(), second, first).actions.target == OTHER


def test_there_is_no_range_limit(boarder, craft, ask) -> None:
    decision = ask(boarder(), craft(y=0x7FFF0000))
    assert decision.actions.target == TARGET


@pytest.mark.parametrize(
    ("boarder_index", "picked"),
    [(SOMEONE, NO_OBJECT), (ME, TARGET), (NO_OBJECT, TARGET)],
    ids=["another keeps it", "it keeps it", "nobody keeps it"],
)
def test_a_craft_another_boarder_keeps_is_passed_over(
    boarder, craft, ask, boarder_index, picked
) -> None:
    decision = ask(boarder(), craft(boarder=boarder_index))
    assert decision.actions.target == picked


def test_a_craft_already_boarded_is_passed_over(boarder, craft, ask) -> None:
    me = boarder(boarded=(5, SIGNATURE))
    assert ask(me, craft()).actions.target == NO_OBJECT
    assert ask(me, craft(object_signature=6)).actions.target == TARGET


def test_a_craft_breaking_up_is_passed_over(boarder, craft, ask) -> None:
    decision = ask(boarder(), craft(object_kind=BREAKING_UP))
    assert decision.actions.target == NO_OBJECT


def test_a_static_object_is_never_boarded(boarder, contact, ask) -> None:
    buoy = contact(
        object_index=40, flags=ContactFlags.STATIC | ContactFlags.PARKED, genus=9
    )
    me = boarder(order_target_mask=0, static_target_mask=0b1)
    assert ask(me, statics=(buoy,)).actions.target == NO_OBJECT


def test_a_command_target_comes_first(boarder, craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    commanded = craft(object_index=FAR, flags=0, y=0x20000)
    me = boarder(order_target_mask=0b10, command_target=FAR)
    decision = ask(me, craft(y=0x1000), commanded)
    assert decision.actions.target == FAR
    assert 'source="command"' in caplog.text


@pytest.mark.parametrize(
    "shown_command", [False, True], ids=["not shown", "another keeps it"]
)
def test_a_command_target_it_may_not_take_leaves_the_search(
    boarder, craft, ask, shown_command
) -> None:
    others = [craft(y=0x1000)]
    if shown_command:
        others.append(craft(object_index=FAR, boarder=SOMEONE))
    me = boarder(order_target_mask=0b10, command_target=FAR)
    assert ask(me, *others).actions.target == TARGET


def test_it_keeps_its_target_though_no_longer_boardable(boarder, craft, ask) -> None:
    nearer = craft(object_index=OTHER, object_signature=12, y=0x1000)
    decision = ask(boarder(notebook=holding()), craft(flags=0), nearer)
    assert decision.actions.target == TARGET


@pytest.mark.parametrize(
    ("held", "why"),
    [
        (None, "gone"),
        ({"object_signature": 99}, "replaced"),
        ({"object_kind": BREAKING_UP}, "wrecked"),
        ({"boarder": SOMEONE}, "taken"),
    ],
)
def test_it_looks_again_once_its_target_is_lost(
    boarder, craft, ask, caplog, held, why
) -> None:
    caplog.set_level("INFO")
    nearer = craft(object_index=OTHER, object_signature=12, y=0x1000)
    others = [nearer] if held is None else [craft(**held), nearer]
    decision = ask(boarder(notebook=holding()), *others)
    assert decision.actions.target == OTHER
    assert f'board.lost object={ME} target={TARGET} why="{why}"' in caplog.text


def test_a_notebook_from_another_order_slot_starts_looking(boarder, craft, ask) -> None:
    kept = ask(boarder(notebook=holding(slot=0)), craft(flags=0))
    assert kept.actions.target == TARGET
    moved = ask(boarder(notebook=holding(slot=1)), craft(flags=0))
    assert moved.actions.target == NO_OBJECT


@pytest.mark.parametrize(
    ("left", "throttle"),
    [
        (16385, 0xFFFF),
        (16384, 0x8000),
        (8193, 0x8000),
        (8192, 0x4000),
        (2049, 0x4000),
        (2048, 0),
        (0, 0),
    ],
)
def test_the_approach_throttle_by_distance_left(left, throttle) -> None:
    assert board.throttle_for(left) == throttle


def test_a_held_target_flies_to_its_board_point(boarder, craft, ask) -> None:
    me = boarder(notebook=holding(), books=BOARD_POINT, board_point=(0x10000, 0, 0))
    decision = ask(me, craft())
    assert decision.actions.steer == Steer(0x4000, LEVEL, 0)
    assert decision.actions.throttle == 0xFFFF


def test_a_new_target_is_flown_at_not_the_old_board_point(boarder, craft, ask) -> None:
    me = boarder(books=AT_POINT, board_point=(0x10000, 0, 0))
    decision = ask(me, craft(y=20000))
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.throttle == 0x8000
    assert phase_of(decision) == board.APPROACH


def test_without_a_board_point_it_stops_at_the_clearance(boarder, craft, ask) -> None:
    me = boarder(notebook=holding())
    assert ask(me, craft(y=4096 + 2049)).actions.throttle == 0x4000
    stopped = ask(me, craft(y=4096 + 2048))
    assert stopped.actions.throttle == 0
    assert stopped.actions.steer is None


def test_within_2048_of_the_board_point_it_stops(boarder, craft, ask) -> None:
    me = boarder(notebook=holding(), books=BOARD_POINT, board_point=(0, 2048, 0))
    decision = ask(me, craft())
    assert decision.actions.throttle == 0
    assert decision.actions.steer is None
    assert phase_of(decision) == board.APPROACH


def test_at_the_board_point_it_waits_stopped(boarder, craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    me = boarder(notebook=holding(), books=AT_POINT, board_point=(0, 0x3000, 0))
    decision = ask(me, craft())
    assert decision.actions.throttle == 0
    assert decision.actions.steer is None
    assert decision.actions.target == TARGET
    assert phase_of(decision) == board.AT_POINT
    assert f"board.at_point object={ME} target={TARGET}" in caplog.text
    caplog.clear()
    again = boarder(notebook=decision.notebook, books=AT_POINT, board_point=(0, 1, 0))
    ask(again, craft())
    assert "board.at_point" not in caplog.text


@pytest.mark.parametrize(
    ("order", "line"),
    [
        (OrderCode.BOARD_AND_GIVE_CARGO, Activity.LOOKING_TO_BOARD),
        (OrderCode.BOARD_AND_TAKE_CARGO, Activity.LOOKING_TO_BOARD),
        (OrderCode.BOARD_AND_EXCHANGE_CARGO, Activity.LOOKING_TO_BOARD),
        (OrderCode.BOARD_AND_CAPTURE_CARGO, Activity.LOOKING_TO_CAPTURE),
        (OrderCode.BOARD_AND_DESTROY_CARGO, Activity.LOOKING_TO_BOARD),
        (OrderCode.PICK_UP, Activity.LOOKING_TO_PICK_UP),
        (OrderCode.SS_BOARD, Activity.LOOKING_TO_BOARD),
        (OrderCode.BOARD_TO_REPAIR, Activity.LOOKING_TO_BOARD),
    ],
)
def test_each_order_shows_its_own_looking_line(boarder, ask, order, line) -> None:
    decision = ask(boarder(order_code=order, order_codes=(order, 0, 0, 0)))
    assert decision.actions.activity == line


def test_the_status_line_is_sent_only_when_it_changes(boarder, craft, ask) -> None:
    approaching = ask(boarder(activity=Activity.BOARDING), craft())
    assert approaching.actions.activity is None
    looking = ask(boarder(activity=Activity.LOOKING_TO_BOARD), craft(flags=0))
    assert looking.actions.activity is None


def test_its_cannons_never_fire(boarder, craft, ask) -> None:
    for decision in (ask(boarder(), craft()), ask(boarder(), craft(flags=0))):
        assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)


@pytest.mark.parametrize(
    ("variable2", "done", "books", "state"),
    [
        (1, 0, Books.TARGETS_LEFT, 0),
        (1, 1, Books.TARGETS_LEFT, 2),
        (2, 1, Books.TARGETS_LEFT, 0),
        (2, 3, Books.TARGETS_LEFT, 2),
        (2, 1, 0, 3),
        (1, 1, 0, 2),
        (0, 0, Books.TARGETS_LEFT, 2),
    ],
    ids=[
        "none yet", "one of one", "one of two", "past the count", "no target left",
        "done before no target", "none asked",
    ],
)  # fmt: skip
def test_the_goal(view, variable2, done, books, state) -> None:
    me = view(
        order_slot=1,
        order_variable2=variable2,
        books=books,
        order_progress=(5, done, 7, 8),
    )
    assert board.goal_state(me) == state


def test_a_new_target_is_logged(boarder, craft, ask, caplog) -> None:
    caplog.set_level("INFO")
    beside = craft(object_index=OTHER, x=4400, y=0)
    unboardable = craft(object_index=NAMED, flags=0)
    ask(boarder(order_target_mask=0b1110), craft(x=6000, y=0), beside, unboardable)
    assert (
        f'board.target object={ME} target={OTHER} source="search" distance=4400 '
        "order=12 named=3 boardable=2"
    ) in caplog.text


def test_the_first_look_is_logged(boarder, craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    first = ask(boarder(order_target_mask=0b110), craft(flags=0))
    assert f"board.looking object={ME} order=12 named=1 boardable=0" in caplog.text
    caplog.clear()
    ask(boarder(notebook=first.notebook), craft(flags=0))
    assert "board.looking" not in caplog.text


def test_the_approach_is_logged(boarder, craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    me = boarder(notebook=holding(), books=BOARD_POINT, board_point=(0, 0x3000, 0))
    ask(me, craft())
    assert (
        f"board.approach object={ME} target={TARGET} left=12288 throttle=32768 "
        "pointed=1 there=0"
    ) in caplog.text


@pytest.mark.parametrize(
    ("offset", "rough"),
    [
        ((3000, 3000, 0), 4500),
        ((4400, 0, 0), 4400),
        ((40, 100, 0), 120),
        ((100, 40, 50), 145),
        ((100, 40, 300), 360),
        ((-100, -40, -300), 360),
        ((100, 0, 100), 150),
    ],
)
def test_the_rough_distance(offset, rough) -> None:
    assert board.rough_distance(*offset) == rough


@pytest.mark.parametrize(
    ("command", "fields"),
    [(ME, {}), (FAR, {"object_kind": BREAKING_UP})],
    ids=["itself", "breaking up"],
)
def test_a_command_target_it_may_not_board_leaves_the_search(
    boarder, craft, ask, command, fields
) -> None:
    commanded = craft(object_index=FAR, **fields)
    me = boarder(order_target_mask=0b10, command_target=command)
    assert ask(me, craft(y=0x1000), commanded).actions.target == TARGET


def test_a_lost_target_is_forgotten(boarder, craft, ask) -> None:
    decision = ask(boarder(notebook=holding()))
    assert decision.actions.target == NO_OBJECT
    assert memory_of(decision).held == 0


def test_avoidance_leaves_its_target_alone_and_turns_it_round_others(
    boarder, craft, contact, ask
) -> None:
    big = craft(size=0x8000)
    me = boarder(notebook=holding(), books=BOARD_POINT, board_point=(0, 0x5000, 0))
    question = Question(1000, (me,), (contact(object_index=ME, flags=0), big))
    decision = board.think(me, question)
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert avoidance.steer_clear(me, question, decision) == decision
    freighter = craft(object_index=OTHER, y=0x3000, flags=0, size=0x2000)
    question = Question(
        1000, (me,), (contact(object_index=ME, flags=0), big, freighter)
    )
    turned = avoidance.steer_clear(me, question, board.think(me, question))
    assert turned.actions.steer.yaw != 0


# L2: the dock, and the look after it.


def test_at_the_board_point_it_asks_the_engine_to_dock(
    boarder, craft, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    me = boarder(notebook=holding(), books=AT_POINT, board_point=(0, 0x3000, 0))
    decision = ask(me, craft())
    assert decision.actions.board == TARGET
    assert decision.actions.target == TARGET
    assert f"board.dock object={ME} target={TARGET} order={GIVE}" in caplog.text


def test_short_of_its_board_point_it_asks_no_dock(boarder, craft, ask) -> None:
    near = boarder(notebook=holding(), books=BOARD_POINT, board_point=(0, 2048, 0))
    assert ask(near, craft()).actions.board is None
    # A target picked this think: the point at hand is the old target's.
    fresh = boarder(books=AT_POINT, board_point=(0, 1, 0))
    assert ask(fresh, craft()).actions.board is None


@pytest.mark.parametrize("phase", [1, 2, 3])
def test_while_the_engine_docks_it_sends_no_target_steer_or_throttle(
    boarder, craft, ask, phase
) -> None:
    me = boarder(
        notebook=holding(phase=board.AT_POINT),
        board_phase=phase,
        books=AT_POINT,
        board_point=(0, 1, 0),
    )
    decision = ask(me, craft())
    actions = decision.actions
    assert actions.target is None
    assert actions.steer is None
    assert actions.throttle is None
    assert actions.board is None
    assert actions.activity == Activity.BOARDING
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert phase_of(decision) == board.DOCKING
    assert memory_of(decision) == board.Memory(0, 1, TARGET, SIGNATURE)


def test_a_dock_is_flown_by_the_engine_whatever_the_craft_holds(
    boarder, craft, ask
) -> None:
    # A follower whose leader moved its order slot, its memory on slot 0.
    me = boarder(notebook=holding(), board_phase=2, order_slot=1, order_code=7)
    decision = ask(me, craft(object_index=OTHER, y=0x1000))
    assert decision.actions.target is None
    assert decision.actions.throttle is None
    assert memory_of(decision).target == TARGET


def test_a_dock_shows_docking_only_when_it_changes(boarder, craft, ask) -> None:
    me = boarder(notebook=holding(), board_phase=1, activity=Activity.BOARDING)
    assert ask(me, craft()).actions.activity is None


def test_a_dock_is_logged_each_think(boarder, craft, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    ask(boarder(notebook=holding(), board_phase=3), craft())
    assert f"board.docking object={ME} target={TARGET} phase=3" in caplog.text


def test_after_a_dock_that_boarded_it_looks_again_where_it_is(
    boarder, craft, ask, caplog
) -> None:
    caplog.set_level("INFO")
    me = boarder(
        notebook=holding(phase=board.DOCKING),
        y=0x7000,
        boarded=(SIGNATURE,),
        order_target_mask=0b1110,
    )
    left_behind = craft(object_index=OTHER, object_signature=12, y=0)
    beside = craft(object_index=FAR, object_signature=13, y=0x9000)
    decision = ask(me, craft(), left_behind, beside)
    assert decision.actions.target == FAR
    assert f"board.undocked object={ME} target={TARGET} boarded=1" in caplog.text
    assert "board.lost" not in caplog.text


def test_after_a_dock_the_engine_gave_up_it_keeps_its_target(
    boarder, craft, ask, caplog
) -> None:
    caplog.set_level("INFO")
    nearer = craft(object_index=OTHER, object_signature=12, y=0x1000)
    decision = ask(boarder(notebook=holding(phase=board.DOCKING)), craft(), nearer)
    assert decision.actions.target == TARGET
    assert f"board.undocked object={ME} target={TARGET} boarded=0" in caplog.text
    caplog.clear()
    ask(boarder(notebook=decision.notebook), craft(), nearer)
    assert "board.undocked" not in caplog.text


def test_a_command_target_already_boarded_leaves_the_search(
    boarder, craft, ask
) -> None:
    commanded = craft(object_index=FAR, object_signature=13, flags=0, y=0x20000)
    me = boarder(order_target_mask=0b10, command_target=FAR, boarded=(13,))
    assert ask(me, craft(y=0x1000), commanded).actions.target == TARGET


def test_the_first_pair_comes_before_a_nearer_craft_of_the_second(
    boarder, craft, ask
) -> None:
    second = craft(object_index=OTHER, object_signature=12, y=0x1000)
    me = boarder(order_target_mask=0b110, order_first_pair_mask=0b100)
    assert ask(me, second, craft()).actions.target == TARGET


def test_with_no_first_pair_craft_boardable_the_nearest_of_the_rest_is_picked(
    boarder, craft, ask
) -> None:
    first = craft(object_index=FAR, object_signature=13, y=0x800, flags=0)
    second = craft(object_index=OTHER, object_signature=12, y=0x1000)
    farther = craft(object_index=NAMED, object_signature=14, y=0x4000)
    me = boarder(order_target_mask=0b1110, order_first_pair_mask=0b10)
    assert ask(me, first, farther, second).actions.target == OTHER
