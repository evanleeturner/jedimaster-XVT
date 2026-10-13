"""The battle in the background: one at a time, started, ended, cancelled.

Purpose:
    Prove ``Battles`` flies one battle at a time without blocking, sends
    exactly one ending push whatever happened (flown, refused, cancelled,
    timed out, engine and result disagreeing, a crash), gives the line the
    summary makes, and keeps going when a listener fails.

Flow:
    A ``Control`` with the stand-in engine of ``solodata``; battles are
    started through ``launch.solo`` and the pushes collected by a listener.

Invariants:
    - No game data; every wait is bounded.

Call:
    ``pytest tests/test_page_battles.py``
"""

from __future__ import annotations

import asyncio
import logging

import pytest
from pagedata import request
from solodata import make_control
from solodata import result_of
from solodata import solo_args

from jedimaster.launch import run

logger = logging.getLogger(__name__)


class Pushes:
    """A listener that records the pushes it is given."""

    def __init__(self) -> None:
        self.seen: list[dict] = []
        self.ended = asyncio.Event()

    async def __call__(self, push: dict) -> None:
        self.seen.append(push)
        if push["event"] == "launch.ended":
            self.ended.set()


async def fly(tmp_path, behave="done", *extra, timeout=30.0):
    control, _ = make_control(tmp_path, behave, *extra, timeout=timeout)
    pushes = Pushes()
    control.subscribe(pushes)
    outcome = control.handle(request(1, "launch.solo", solo_args()))
    return control, pushes, outcome


async def ending(pushes: Pushes) -> dict:
    await asyncio.wait_for(pushes.ended.wait(), 15)
    return pushes.seen[-1]["data"]


async def test_a_flown_battle_ends_with_the_summary_line(tmp_path):
    _, pushes, outcome = await fly(tmp_path)
    assert result_of(outcome.reply)["started"] is True
    data = await ending(pushes)
    assert data["status"] == "flown"
    assert data["line"] == "flown: completed, score 10, kills 2, losses 0, goals met at 61 s"


async def test_a_started_push_comes_in_the_outcome_and_the_ending_through_the_listener(
    tmp_path,
):
    _, pushes, outcome = await fly(tmp_path)
    (started,) = outcome.pushes
    assert started["event"] == "launch.started"
    assert started["data"]["launch_id"] == result_of(outcome.reply)["launch_id"]
    await ending(pushes)
    assert [p["event"] for p in pushes.seen] == ["launch.ended"]
    assert pushes.seen[0]["data"]["launch_id"] == started["data"]["launch_id"]


async def test_the_answer_comes_before_the_battle_ends(tmp_path):
    control, pushes, outcome = await fly(tmp_path, "hang")
    assert result_of(outcome.reply)["started"] is True
    assert control.battles.busy() is True
    assert pushes.seen == []
    control.battles.cancel(result_of(outcome.reply)["launch_id"])
    await ending(pushes)


async def test_a_refused_battle_ends_with_the_engines_reason(tmp_path):
    _, pushes, _ = await fly(tmp_path, "refuse")
    data = await ending(pushes)
    assert data["status"] == "refused"
    assert data["line"] == "refused: The mission is not open."


async def test_cancel_stops_the_engine_and_the_line_says_cancelled(tmp_path):
    control, pushes, outcome = await fly(tmp_path, "hang")
    await asyncio.sleep(0.5)
    launch_id = result_of(outcome.reply)["launch_id"]
    reply = control.handle(request(2, "launch.cancel", {"launch_id": launch_id})).reply
    assert result_of(reply) == {"cancelled": True, "message": "Cancelling the battle."}
    data = await ending(pushes)
    assert (data["status"], data["line"]) == ("cancelled", "cancelled")
    assert control.battles.busy() is False


async def test_cancelling_a_battle_that_is_not_flying_says_so(tmp_path):
    control, pushes, outcome = await fly(tmp_path)
    await ending(pushes)
    reply = control.handle(request(2, "launch.cancel", {"launch_id": 99})).reply
    assert result_of(reply)["cancelled"] is False
    launch_id = result_of(outcome.reply)["launch_id"]
    again = control.handle(request(3, "launch.cancel", {"launch_id": launch_id}))
    assert result_of(again.reply)["cancelled"] is False


async def test_the_time_limit_ends_the_battle_with_the_reason(tmp_path):
    _, pushes, _ = await fly(tmp_path, "hang", timeout=0.5)
    data = await ending(pushes)
    assert data["status"] == "error"
    assert data["line"] == "the engine ran past 0.5 seconds and was stopped"


async def test_an_engine_that_will_not_stop_is_killed_on_cancel(tmp_path, monkeypatch):
    monkeypatch.setattr(run, "STOP_SECONDS", 0.5)
    control, pushes, outcome = await fly(tmp_path, "stubborn")
    await asyncio.sleep(1.0)
    control.battles.cancel(result_of(outcome.reply)["launch_id"])
    assert (await ending(pushes))["status"] == "cancelled"


async def test_an_exit_code_that_disagrees_with_the_result_is_an_error(tmp_path):
    _, pushes, _ = await fly(tmp_path, "mismatch")
    data = await ending(pushes)
    assert data["status"] == "error"
    assert data["line"] == "the engine exited with 5 but its result says 0"


async def test_a_crash_with_no_result_is_an_error_with_the_reason(tmp_path):
    _, pushes, _ = await fly(tmp_path, "crash")
    data = await ending(pushes)
    assert data["status"] == "error"
    assert "left no result file" in data["line"]


async def test_only_one_battle_flies_at_a_time(tmp_path):
    control, pushes, outcome = await fly(tmp_path, "hang")
    second = control.handle(request(2, "launch.solo", solo_args(pilot="Cee")))
    result = result_of(second.reply)
    assert result["started"] is False and result["launch_id"] == 0
    assert "already flying" in result["message"]
    assert second.pushes == []
    control.battles.cancel(result_of(outcome.reply)["launch_id"])
    await ending(pushes)


async def test_a_new_battle_may_start_once_the_last_has_ended(tmp_path):
    control, pushes, first = await fly(tmp_path)
    await ending(pushes)
    second = control.handle(request(2, "launch.solo", solo_args()))
    assert (
        result_of(second.reply)["launch_id"] == result_of(first.reply)["launch_id"] + 1
    )
    pushes.ended.clear()
    await ending(pushes)


async def test_a_failing_listener_does_not_stop_the_others_and_is_a_warning(
    tmp_path, caplog
):
    control, _ = make_control(tmp_path)

    async def broken(push):
        raise RuntimeError("no page")

    pushes = Pushes()
    control.subscribe(broken)
    control.subscribe(pushes)
    with caplog.at_level(logging.WARNING):
        control.handle(request(1, "launch.solo", solo_args()))
        await ending(pushes)
    assert any(
        r.levelno == logging.WARNING and "listener" in r.message for r in caplog.records
    )


async def test_a_page_that_connects_late_is_told_of_the_battle_that_flies(tmp_path):
    control, pushes, outcome = await fly(tmp_path, "hang")
    (started,) = control.connect_pushes()
    assert started["data"]["launch_id"] == result_of(outcome.reply)["launch_id"]
    assert started["data"]["pilot"] == "Ace"
    control.battles.cancel(started["data"]["launch_id"])
    await ending(pushes)
    assert control.connect_pushes() == []


def test_starting_without_an_event_loop_is_refused_in_words_not_raised(tmp_path):
    control, _ = make_control(tmp_path)
    reply = control.handle(request(1, "launch.solo", solo_args())).reply
    result = result_of(reply)
    assert result["started"] is False
    assert "could not start" in result["message"]
    assert control.battles.busy() is False, "a start that failed leaves nothing flying"
    assert control.connect_pushes() == []


@pytest.mark.parametrize("behave", ["done", "refuse", "mismatch", "crash"])
async def test_every_battle_ends_with_exactly_one_ending_push(tmp_path, behave):
    _, pushes, _ = await fly(tmp_path, behave)
    await ending(pushes)
    await asyncio.sleep(0.2)
    assert [p["event"] for p in pushes.seen] == ["launch.ended"]


async def test_start_refuses_a_second_battle_by_raising(tmp_path):
    control, flight = make_control(tmp_path, "hang")
    pushes = Pushes()
    control.subscribe(pushes)
    launch_id = control.battles.start(flight, {"x": 1}, {"title": "t"})
    with pytest.raises(RuntimeError, match="already flying"):
        control.battles.start(flight, {"x": 1}, {"title": "t"})
    control.battles.cancel(launch_id)
    await ending(pushes)


async def test_cancelling_another_launch_number_leaves_the_battle_flying(tmp_path):
    control, pushes, outcome = await fly(tmp_path, "hang")
    launch_id = result_of(outcome.reply)["launch_id"]
    await asyncio.sleep(0.3)
    assert control.battles.cancel(launch_id + 1) is False
    assert control.battles.busy() is True and pushes.seen == []
    control.battles.cancel(launch_id)
    await ending(pushes)


async def test_any_other_failure_still_ends_the_battle_with_a_push(
    tmp_path, monkeypatch, caplog
):
    from jedimaster.page import battles

    async def broken(*args, **kwargs):
        raise OSError("disk full")

    monkeypatch.setattr(battles, "fly_async", broken)
    _, pushes, _ = await fly(tmp_path)
    with caplog.at_level(logging.ERROR):
        data = await ending(pushes)
    assert data["status"] == "error"
    assert data["line"] == "the battle could not be run (OSError)"
    assert any(r.levelno == logging.ERROR for r in caplog.records)
