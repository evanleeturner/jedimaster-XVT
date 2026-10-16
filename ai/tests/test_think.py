"""Fence for think: answers in order, the same every time, the same after a rewind.

``wander`` stands in for a written behavior: it uses the notebook, the dice
and every action kind, so the fences cover what later behaviors will do.
"""

from __future__ import annotations

import dataclasses
import hashlib
import logging

import pytest

from jedimaster_ai import notebook
from jedimaster_ai import think as think_module
from jedimaster_ai import wire
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.dice import roll
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import Books
from jedimaster_ai.world import Commands
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import NO_ACTIONS
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Turret

WANDER = 200


def wander(view, question: Question) -> Decision:
    """Turn to a random heading each think; every third, change phase."""
    tick = question.tick
    book = notebook.open_for(view, WANDER, tick)
    heading, state = roll(book.dice, 0x10000)
    book = dataclasses.replace(book, dice=state)
    if tick - book.phase_tick >= 24:
        book = book.in_phase((book.phase + 1) % 3, tick)
    actions = Actions(
        Steer(heading, view.pitch, view.roll),
        throttle=0x4000 * (book.phase + 1),
        activity=Activity.WAITING,
        think_interval=8,
    )
    return Decision(book.encode(), actions)


WANDERING = {code: wander for code in OrderCode}


def first_question(view) -> Question:
    craft = tuple(
        view(object_index=index, random_seed=seed) for index, seed in ((3, 11), (5, -7))
    )
    return Question(1000, craft)


def fly(question: Question, steps: int) -> list[bytes]:
    """Return each step's answer bytes, each notebook carried to the next."""
    answers = []
    for _ in range(steps):
        answer = think(question, WANDERING)
        answers.append(wire.answer_bytes(answer))
        craft = tuple(
            dataclasses.replace(view, notebook=said.notebook)
            for view, said in zip(question.craft, answer.craft, strict=True)
        )
        question = Question(question.tick + 8, craft)
    return answers


def digest(answers: list[bytes]) -> str:
    return hashlib.sha256(b"".join(answers)).hexdigest()


def test_answers_follow_the_questions_order(view) -> None:
    question = Question(5, tuple(view(object_index=index) for index in (9, 2, 7)))
    answer = think(question)
    assert answer.tick == 5
    assert [craft.object_index for craft in answer.craft] == [9, 2, 7]


def test_a_craft_whose_order_has_no_behavior_drifts(view) -> None:
    unwritten = {"order_code": OrderCode.NULL, "turret_slot_mask": 0}
    craft = (
        view(object_index=1, **unwritten),
        view(object_index=2, notebook=bytes(range(64)), **unwritten),
    )
    for view_, said in zip(craft, think(Question(5, craft)).craft, strict=True):
        assert (said.notebook, said.actions) == (view_.notebook, NO_ACTIONS)


def test_two_flights_from_one_start_answer_alike(view) -> None:
    assert digest(fly(first_question(view), 20)) == digest(
        fly(first_question(view), 20)
    )


def test_a_replay_from_a_saved_step_answers_alike(view) -> None:
    whole = fly(first_question(view), 20)
    # The question at step 10, rebuilt from the notebooks step 9 answered.
    question = first_question(view)
    said = wire.read_answer(whole[9][4:], dataclasses.replace(question, tick=1072))
    saved = Question(
        1080,
        tuple(
            dataclasses.replace(craft, notebook=answer.notebook)
            for craft, answer in zip(question.craft, said.craft, strict=True)
        ),
    )
    assert fly(saved, 10) == whole[10:]


def test_a_craft_answers_alike_whoever_shares_its_question(view) -> None:
    pair = first_question(view)
    alone = think(Question(pair.tick, pair.craft[1:]), WANDERING)
    swapped = think(Question(pair.tick, pair.craft[::-1]), WANDERING)
    assert alone.craft[0] == swapped.craft[0] == think(pair, WANDERING).craft[1]


def test_the_wanderer_changes_phase_and_dice(view) -> None:
    answers = fly(first_question(view), 4)
    phases = {notebook.decode(answers[step][22:86]).phase for step in range(4)}
    assert phases == {0, 1}


def test_debug_records_each_craft_in_and_out(view, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    think(first_question(view))
    assert caplog.text.count("think.view object=") == 2
    assert caplog.text.count("think.answer object=") == 2
    assert "think.question tick=1000 craft=2" in caplog.text


def test_debug_records_the_formation_place(view, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    placed = view(leader=4, books=Books.PLACE, place=(-1200, 3400, 56))
    think(Question(1000, (placed,)))
    assert " place_x=-1200 place_y=3400 place_z=56 " in caplog.text


def test_without_debug_nothing_is_recorded(view, caplog) -> None:
    caplog.set_level(logging.INFO, logger="jedimaster_ai")
    think(first_question(view))
    assert caplog.text == ""


def answered(question: Question) -> Answer:
    """Return the answer as the engine's reader takes it, through its bytes."""
    answer = think(question)
    return wire.read_answer(wire.answer_bytes(answer)[4:], question)


def test_books_send_a_craft_home_whatever_its_order(view) -> None:
    craft = view(order_code=OrderCode.ESCORT, books=Books.ABORTED | Books.MAY_JUMP)
    said = answered(Question(1, (craft,))).craft[0].actions
    assert (said.activity, said.depart) == (Activity.LEAVING_BY_HYPERSPACE, True)


def test_a_real_attack_flight_finishes_both_orders_and_docks(view, contact) -> None:
    carrier = contact(object_index=9, genus=4, speed=20, target=0xFFFF)
    flight = {
        "order_codes": (7, 7, 0, 0),
        "books": Books.TARGETS_LEFT | Books.BY_MOTHERSHIP,
        "mothership": 9,
        "outside_hangar": (90_000, 6, 7),
        "inside_hangar": (90_100, 6, 7),
        "turret_slot_mask": 0,
    }

    def step(**changes):
        craft = view(**(flight | changes))
        return answered(Question(1, (craft,), (carrier,))).craft[0].actions

    assert step().order is None
    assert step(books=Books.BY_MOTHERSHIP).order == OrderMove(1, 2)
    assert step(order_slot=1, order_states=(2, 0, 0, 0)).order is None
    last = step(order_slot=1, order_states=(2, 0, 0, 0), books=Books.BY_MOTHERSHIP)
    assert (last.order, last.activity, last.target) == (
        OrderMove(1, 2),
        Activity.GOING_HOME,
        9,
    )
    home_books = Books.BY_MOTHERSHIP | Books.ORDERS_DONE
    going = step(order_slot=1, order_states=(2, 2, 0, 0), books=home_books, activity=9)
    assert (going.order, going.activity) == (None, Activity.GOING_HOME)
    turning = step(
        order_slot=1,
        order_states=(2, 2, 0, 0),
        books=home_books,
        activity=9,
        x=89_000,
    )
    assert turning.activity == Activity.ENTERING_HANGAR
    docking = step(
        order_slot=1,
        order_states=(2, 2, 0, 0),
        books=home_books | Books.AT_HANGAR,
        activity=8,
        x=90_090,
    )
    assert docking.dock


def test_a_waiting_craft_waits_whatever_its_books_say(view) -> None:
    """Books that would send it home move nothing while the wait stands."""
    craft = view(
        order_code=OrderCode.ATTACK,
        commands=Commands.WAIT,
        books=Books.ORDERS_DONE | Books.MAY_JUMP,
    )
    said = answered(Question(1, (craft,))).craft[0].actions
    assert (said.throttle, said.activity) == (0, Activity.WAITING_FOR_GO)
    assert (said.order, said.depart) == (None, False)


def test_released_a_wingman_attacks_the_craft_its_player_named(view, contact) -> None:
    """A wingman of a player's flight (leader 0, the player's craft) told to
    wait, then given a target by the attack order, which ends the wait."""
    own = contact(object_index=4, team=1, x=0, y=0, z=0)
    named = contact(object_index=15, team=2, x=0, y=0x6000, z=0, target=0xFFFF)
    craft = view(
        order_code=OrderCode.ATTACK,
        activity=Activity.WAITING_FOR_GO,
        leader=0,
        command_target=15,
        enemy_mask=0b01,
        books=Books.TARGETS_LEFT,
        turret_slot_mask=0,
    )
    said = answered(Question(1, (craft,), (named, own))).craft[0].actions
    assert (said.target, said.activity) == (15, Activity.NONE)


def test_a_craft_past_its_mission_wait_stops_showing_it(view) -> None:
    """A craft that still shows WAITING from a Wait order it left without
    its done think (a skip) clears it when its new order names no activity."""
    craft = view(order_code=OrderCode.HOLD_STEADY, activity=Activity.WAITING)
    said = answered(Question(1, (craft,))).craft[0].actions
    assert said.activity == Activity.NONE


def test_debug_records_the_maneuver_timer(view, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    think(Question(1000, (view(maneuver_timer=1180),)))
    assert " timer=1180 " in caplog.text


def test_debug_records_who_last_shot_a_contact(view, contact, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    think(Question(1000, (view(),), (contact(hit_by=17), contact(hit_by=65535))))
    lines = [line for line in caplog.text.splitlines() if "think.contact" in line]
    assert [line.split(" hit_by=")[1].split(" ")[0] for line in lines] == [
        "17",
        "65535",
    ]


def test_debug_records_a_contacts_model_box_and_roll(view, contact, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    boxed = contact(box_min=(-3, -4, -5), box_max=(6, 7, 8), roll=0xC000, boarder=12)
    think(Question(1000, (view(),), (boxed,)))
    (line,) = [line for line in caplog.text.splitlines() if "think.contact" in line]
    assert line.endswith(
        "min_x=-3 min_y=-4 min_z=-5 max_x=6 max_y=7 max_z=8 roll=49152 boarder=12"
    )


def test_debug_records_the_timer_evaded_and_ion_actions(caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    actions = Actions(
        evaded=True,
        maneuver_timer=1180,
        self_destruct=3,
        board=12,
        turrets=(Turret(3, 9, True), Turret(5, 9)),
    )
    think_module._log_answer(CraftAnswer(4, bytes(NOTEBOOK_BYTES), actions))
    # Bit n of the actions is action kind n: turrets 8, evaded 14, timer 15,
    # self destruct 16, board 17.
    assert "think.answer object=4 actions=246016 " in caplog.text
    assert "think.turret object=4 slot=3 target=9 ion=1" in caplog.text
    assert "think.turret object=4 slot=5 target=9 ion=0" in caplog.text


def test_debug_records_the_new_view_values_and_the_statics(
    view, contact, caplog
) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    static = contact(object_index=40, flags=8)
    craft = view(
        escorted=9,
        station=(11, 12, 13),
        start_point=(21, 22, 23),
        targets_now=5,
        static_target_mask=1,
        board_point=(31, 32, 33),
        order_progress=(1, 2, 3, 4),
        boarded=(7, 8),
        board_phase=2,
        lifetime_timer=3540,
        order_first_pair_mask=6,
    )
    think(Question(1000, (craft,), (contact(),), (static,)))
    assert "think.question tick=1000 craft=1 contacts=1 statics=1" in caplog.text
    assert "think.contact object=40 " in caplog.text
    assert (
        " escorted=9 station_x=11 station_y=12 station_z=13 start_x=21 "
        "start_y=22 start_z=23 targets_now=5 static_targets=1 "
        'board_x=31 board_y=32 board_z=33 progress="01020304" boarded="7,8" '
        "board_phase=2 lifetime_timer=3540 first_pair=6 " in caplog.text
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"books": Books.ORDERS_DONE | Books.MAY_JUMP},
        {"commands": Commands.WAIT, "books": Books.TARGETS_LEFT},
        {"order_variable2": 1, "order_progress": (1, 0, 0, 0)},
    ],
    ids=["books send it home", "a player's wait", "its goal met"],
)
def test_a_docking_craft_is_left_to_the_engine(view, changes) -> None:
    """While the engine docks it, nothing a dock reads is sent, nor an order
    move (ruling L2-1)."""
    give = OrderCode.BOARD_AND_GIVE_CARGO
    fields = {"order_code": give, "order_codes": (give, give, 0, 0), "board_phase": 3}
    said = answered(Question(1, (view(**(fields | changes)),))).craft[0].actions
    assert (said.target, said.steer, said.throttle) == (None, None, None)
    assert (said.order, said.depart, said.activity) == (None, False, Activity.BOARDING)


def test_a_captured_craft_drops_its_order(view) -> None:
    """Its Attack goal met, it would move to its next order; captured and
    unable to move, it holds instead."""
    attack = OrderCode.ATTACK
    craft = view(
        order_code=attack,
        order_codes=(attack, attack, 0, 0),
        full_speed=0,
        books=Books.CAPTURED,
        turret_slot_mask=0,
    )
    said = answered(Question(1, (craft,))).craft[0].actions
    assert (said.order, said.throttle, said.target) == (None, 0, 0xFFFF)


def test_a_captured_craft_goes_home_and_a_players_wait_still_holds_it(view) -> None:
    hold = OrderCode.HOLD_STEADY
    craft = view(order_code=hold, books=Books.CAPTURED | Books.MAY_JUMP)
    said = answered(Question(1, (craft,))).craft[0].actions
    assert (said.activity, said.depart) == (Activity.LEAVING_BY_HYPERSPACE, True)
    held = dataclasses.replace(craft, commands=Commands.WAIT)
    said = answered(Question(1, (held,))).craft[0].actions
    assert (said.activity, said.depart) == (Activity.WAITING_FOR_GO, False)
