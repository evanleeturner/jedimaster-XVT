"""Fence for think: answers in order, the same every time, the same after a rewind.

``wander`` stands in for a written behavior: it uses the notebook, the dice
and every action kind, so the fences cover what later behaviors will do.
"""

from __future__ import annotations

import dataclasses
import hashlib
import logging

from jedimaster_ai import notebook
from jedimaster_ai import wire
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.dice import roll
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import NO_ACTIONS
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

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
    hold = {"order_code": OrderCode.HOLD_STEADY, "turret_slot_mask": 0}
    craft = (
        view(object_index=1, **hold),
        view(object_index=2, notebook=bytes(range(64)), **hold),
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


def test_without_debug_nothing_is_recorded(view, caplog) -> None:
    caplog.set_level(logging.INFO, logger="jedimaster_ai")
    think(first_question(view))
    assert caplog.text == ""
