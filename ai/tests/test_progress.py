"""Fence for progress: a craft's way through its orders, to the next or home.

Each rule of the 1997 leader plans' order conditions 38 to 40 once: the books
that send a craft home, an order's goal marking its slot done, home once the
group's orders are done or the craft skipped, the skip to slot 3, the move to
the next slot; a follower that never moves; and every move one the engine's
reader takes.
"""

from __future__ import annotations

import logging

import pytest

from jedimaster_ai import progress
from jedimaster_ai import wire
from jedimaster_ai.behaviors import board
from jedimaster_ai.behaviors import boarded
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import dropoff
from jedimaster_ai.behaviors import waiting
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Books
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Question

# An Attack leader whose targets are all gone, on slot 0 of a group with
# orders in slots 0 and 1.
DONE_ATTACK = {
    "order_code": OrderCode.ATTACK,
    "order_codes": (OrderCode.ATTACK, OrderCode.ATTACK, 0, 0),
    "books": 0,
}


@pytest.mark.parametrize(
    "books", [Books.ABORTED, Books.DEPARTURE_DUE, Books.ORDERS_DONE]
)
@pytest.mark.parametrize("leader", [0xFFFF, 3])
def test_the_home_books_send_any_craft_home(view, books, leader) -> None:
    craft = view(books=books | Books.TARGETS_LEFT, leader=leader)
    assert progress.decide(craft) == progress.Progress(home=True)


def test_an_order_with_targets_left_stays(view) -> None:
    assert progress.decide(view()) == progress.STAY


def test_a_done_order_moves_to_the_next_slot_marked_done(view) -> None:
    assert progress.decide(view(**DONE_ATTACK)) == progress.Progress(
        move=OrderMove(1, 2)
    )


def test_the_last_done_order_marks_its_slot_and_goes_home(view) -> None:
    craft = view(**DONE_ATTACK, order_slot=1, order_states=(2, 0, 0, 0))
    assert progress.decide(craft) == progress.Progress(home=True, move=OrderMove(1, 2))


def test_a_boarding_slot_with_no_target_counts_as_done(view) -> None:
    craft = view(**DONE_ATTACK, order_slot=1, order_states=(3, 0, 0, 0))
    assert progress.decide(craft).home


def test_a_slot_already_done_goes_home_without_marking_it_again(view) -> None:
    craft = view(**DONE_ATTACK, order_slot=1, order_states=(3, 2, 0, 0))
    assert progress.decide(craft) == progress.Progress(home=True)


def test_a_craft_that_cannot_move_moves_on_but_never_goes_home(view) -> None:
    assert progress.decide(view(**DONE_ATTACK, full_speed=0)).move == OrderMove(1, 2)
    last = view(**DONE_ATTACK | {"full_speed": 0, "order_codes": (7, 0, 0, 0)})
    assert progress.decide(last) == progress.Progress(move=OrderMove(0, 2))


def test_the_skip_moves_to_slot_3_after_the_done_check(view) -> None:
    skip = view(books=Books.TARGETS_LEFT | Books.SKIP_DUE)
    assert progress.decide(skip) == progress.Progress(move=OrderMove(3, 0))
    done = view(**DONE_ATTACK | {"books": Books.SKIP_DUE})
    assert progress.decide(done) == progress.Progress(move=OrderMove(3, 2))


def test_a_skipped_craft_goes_home_once_order_4_is_done(view) -> None:
    craft = view(
        order_slot=3,
        order_codes=(7, 7, 7, 7),
        books=Books.SKIPPED,
    )
    assert progress.decide(craft) == progress.Progress(home=True, move=OrderMove(3, 2))


def test_a_follower_never_moves_its_slot(view) -> None:
    assert progress.decide(view(**DONE_ATTACK, leader=3)) == progress.STAY


@pytest.mark.parametrize(
    "code",
    [
        OrderCode.PROTECT,
        OrderCode.ESCORT,
        OrderCode.DISABLE,
        OrderCode.SS_PROTECT,
        OrderCode.SS_PROTECT_2,
        OrderCode.SS_PATROL_AND_DISABLE,
    ],
)
def test_the_fighting_and_guarding_orders_are_done_with_no_target_left(
    view, code
) -> None:
    done = view(**DONE_ATTACK | {"order_code": code})
    assert progress.decide(done) == progress.Progress(move=OrderMove(1, 2))
    left = view(**DONE_ATTACK | {"order_code": code, "books": Books.TARGETS_LEFT})
    assert progress.decide(left) == progress.STAY


def test_ss_await_return_is_done_once_every_group_is_back(view) -> None:
    waiting_craft = {"order_code": OrderCode.SS_AWAIT_RETURN}
    back = view(**DONE_ATTACK | waiting_craft | {"books": Books.ALL_RETURNED})
    assert progress.decide(back) == progress.Progress(move=OrderMove(1, 2))
    assert progress.decide(view(**DONE_ATTACK | waiting_craft)) == progress.STAY


def test_ss_launch_is_done_once_every_group_is_out(view) -> None:
    launching = {"order_code": OrderCode.SS_LAUNCH}
    out = view(**DONE_ATTACK | launching | {"books": Books.ALL_LAUNCHED})
    assert progress.decide(out) == progress.Progress(move=OrderMove(1, 2))
    assert progress.decide(view(**DONE_ATTACK | launching)) == progress.STAY
    back = view(**DONE_ATTACK | launching | {"books": Books.ALL_RETURNED})
    assert progress.decide(back) == progress.STAY


def test_self_destruct_is_never_done(view) -> None:
    books = Books.ALL_RETURNED | Books.ALL_LAUNCHED
    craft = view(
        **DONE_ATTACK | {"order_code": OrderCode.SELF_DESTRUCT, "books": books}
    )
    assert progress.decide(craft) == progress.STAY


def test_wait_and_wait_2_end_when_the_waiting_behavior_says_so() -> None:
    assert progress.GOALS[OrderCode.WAIT] is waiting.done
    assert progress.GOALS[OrderCode.WAIT_2] is waiting.done


def test_drop_off_is_done_once_all_is_dropped(view) -> None:
    assert progress.GOALS[OrderCode.DROP_OFF] is dropoff.done
    dropping = {
        "order_code": OrderCode.DROP_OFF,
        "order_codes": (OrderCode.DROP_OFF, OrderCode.DROP_OFF, 0, 0),
    }
    dropped = view(**dropping | {"books": Books.ALL_DROPPED})
    assert progress.decide(dropped) == progress.Progress(move=OrderMove(1, 2))
    at_point = view(**dropping | {"books": Books.DROP_POINT | Books.AT_DROP_POINT})
    assert progress.decide(at_point) == progress.STAY


def test_disabled_and_await_boarding_end_when_the_boarded_behavior_says_so() -> None:
    assert progress.GOALS[OrderCode.DISABLED] is boarded.done
    assert progress.GOALS[OrderCode.AWAIT_BOARDING] is boarded.done


# An Await Boarding leader to be boarded twice, on slot 0 of a group with
# orders in slots 0 and 1.
AWAITING = {
    "order_code": OrderCode.AWAIT_BOARDING,
    "order_codes": (OrderCode.AWAIT_BOARDING, OrderCode.ATTACK, 0, 0),
    "order_variable1": 2,
    "books": 0,
}


def test_await_boarding_moves_on_once_boarded_variable1_times(view) -> None:
    craft = view(**AWAITING, order_progress=(2, 0, 0, 0))
    assert progress.decide(craft) == progress.Progress(move=OrderMove(1, 2))
    once = view(**AWAITING, order_progress=(1, 0, 0, 0))
    assert progress.decide(once) == progress.STAY


# A Give leader asked for one boarding, on slot 0 of a group with orders in
# slots 0 and 1.
GIVING = {
    "order_code": OrderCode.BOARD_AND_GIVE_CARGO,
    "order_codes": (OrderCode.BOARD_AND_GIVE_CARGO, OrderCode.ATTACK, 0, 0),
    "order_variable2": 1,
}


def test_a_boarding_order_is_done_once_its_boardings_reach_variable2(view) -> None:
    craft = view(**GIVING, order_progress=(1, 0, 0, 0))
    assert progress.decide(craft) == progress.Progress(move=OrderMove(1, 2))
    assert progress.decide(view(**GIVING)) == progress.STAY


@pytest.mark.parametrize("code", sorted(board.ORDERS))
def test_a_boarding_order_with_no_target_left_moves_on_marked_3(view, code) -> None:
    craft = view(**GIVING | {"order_code": code, "books": 0})
    assert progress.decide(craft) == progress.Progress(move=OrderMove(1, 3))


def test_the_last_boarding_order_with_no_target_left_goes_home(view) -> None:
    craft = view(**GIVING | {"order_codes": (12, 0, 0, 0), "books": 0})
    assert progress.decide(craft) == progress.Progress(home=True, move=OrderMove(0, 3))


def test_a_slot_already_marked_3_moves_on(view) -> None:
    craft = view(**GIVING, order_states=(3, 0, 0, 0))
    assert progress.decide(craft) == progress.Progress(move=OrderMove(1, 0))


def test_an_order_with_no_goal_test_is_never_done(view) -> None:
    craft = view(**DONE_ATTACK | {"order_code": OrderCode.RENDEZVOUS})
    assert progress.decide(craft) == progress.STAY


@pytest.mark.parametrize(
    "changes",
    [
        DONE_ATTACK,
        DONE_ATTACK | {"order_slot": 1, "order_states": (2, 0, 0, 0)},
        DONE_ATTACK | {"books": Books.SKIP_DUE},
        {"order_slot": 3, "order_codes": (7, 7, 7, 7), "books": Books.SKIPPED},
        DONE_ATTACK | {"full_speed": 0, "order_codes": (7, 0, 0, 0)},
        GIVING | {"books": 0},
        GIVING | {"order_codes": (12, 0, 0, 0), "books": 0},
    ],
)
def test_every_move_is_one_the_engines_reader_takes(view, changes) -> None:
    craft = view(**changes)
    move = progress.decide(craft).move
    assert move is not None
    wire.check_shown(Actions(order=move), craft, Question(1, (craft,)))


def test_with_move_adds_the_order_action_and_keeps_the_rest(view) -> None:
    decision = Decision(bytes(64), Actions(throttle=5))
    moved = progress.with_move(decision, OrderMove(1, 2))
    assert moved == Decision(bytes(64), Actions(throttle=5, order=OrderMove(1, 2)))
    assert progress.with_move(decision, None) is decision


def test_a_move_is_logged_at_debug(view, caplog) -> None:
    with caplog.at_level(logging.DEBUG, logger="jedimaster_ai.progress"):
        progress.decide(view(**DONE_ATTACK))
    assert caplog.messages == ['progress.move object=4 from=0 to=1 state=2 why="next"']
