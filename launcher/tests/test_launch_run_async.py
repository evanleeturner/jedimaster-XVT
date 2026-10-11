"""Flying a battle without blocking: the result, the time limit, the stop.

Purpose:
    Prove ``fly_async`` returns the checked result and exit code, raises
    ``LaunchError`` for a bad description, a missing result, a program that
    cannot start or one that outlasts its limit, and stops the engine when
    its task is cancelled: ``terminate``, then ``kill`` when it will not exit.

Flow:
    The stand-in engine of ``solodata`` with each behaviour; the stop time
    is shortened for the stubborn engine.

Invariants:
    - No game data; no real engine.

Call:
    ``pytest tests/test_launch_run_async.py``
"""

from __future__ import annotations

import asyncio
import logging
import os
import time

import pytest
from solodata import make_engine

from jedimaster.launch import run
from jedimaster.launch.model import build_description
from jedimaster.launch.run import fly_async
from jedimaster.launch.run import LaunchError

logger = logging.getLogger(__name__)

DESCRIPTION = build_description("melee", "none", "delta.tie", "Ace")


@pytest.fixture
def engine(tmp_path):
    return str(make_engine(tmp_path))


async def test_a_flown_battle_returns_its_result_and_exit_code(engine):
    result, code = await fly_async(engine, DESCRIPTION, ["done"])
    assert code == 0
    assert (result["status"], result["score"], result["kills"]) == ("flown", 10, 2)


async def test_a_refusal_is_a_result_not_an_error(engine):
    result, code = await fly_async(engine, DESCRIPTION, ["refuse"])
    assert (code, result["status"]) == (3, "refused")


async def test_the_description_given_to_the_engine_is_the_one_asked_for(
    engine, tmp_path
):
    copy = tmp_path / "copy.json"
    await fly_async(engine, DESCRIPTION, ["done", f"--copy={copy}"])
    import json

    seen = json.loads(copy.read_text())
    assert {k: v for k, v in seen.items() if k != "result"} == DESCRIPTION


async def test_the_event_loop_keeps_running_while_the_engine_flies(engine):
    ticks = 0

    async def tick():
        nonlocal ticks
        while True:
            await asyncio.sleep(0.01)
            ticks += 1

    ticker = asyncio.ensure_future(tick())
    task = asyncio.ensure_future(fly_async(engine, DESCRIPTION, ["hang"], timeout=0.5))
    with pytest.raises(LaunchError):
        await task
    ticker.cancel()
    assert ticks >= 10


async def test_a_description_that_breaks_its_schema_is_never_run(engine):
    with pytest.raises(LaunchError, match="description"):
        await fly_async(engine, {**DESCRIPTION, "pilot": ""}, ["done"])


async def test_an_engine_that_cannot_start_is_an_error(tmp_path):
    with pytest.raises(LaunchError, match="could not be started"):
        await fly_async(str(tmp_path / "nothing"), DESCRIPTION)


async def test_a_missing_result_is_an_error_whatever_the_exit_code(engine):
    with pytest.raises(LaunchError, match="no result"):
        await fly_async(engine, DESCRIPTION, ["silent"])


async def test_an_engine_past_its_limit_is_stopped_and_named(engine):
    started = time.monotonic()
    with pytest.raises(LaunchError, match="ran past 0.5 seconds"):
        await fly_async(engine, DESCRIPTION, ["hang"], timeout=0.5)
    assert time.monotonic() - started < 5


async def test_cancelling_the_task_stops_the_engine(engine, tmp_path):
    task = asyncio.ensure_future(fly_async(engine, DESCRIPTION, ["hang"]))
    await asyncio.sleep(0.5)
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task


async def test_an_engine_that_ignores_terminate_is_killed(engine, monkeypatch):
    monkeypatch.setattr(run, "STOP_SECONDS", 0.5)
    task = asyncio.ensure_future(fly_async(engine, DESCRIPTION, ["stubborn"]))
    await asyncio.sleep(1.0)
    started = time.monotonic()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    waited = time.monotonic() - started
    assert 0.4 < waited < 5, "terminated, waited the stop time, then killed"


async def test_no_engine_process_is_left_after_a_cancel(engine, tmp_path):
    task = asyncio.ensure_future(fly_async(engine, DESCRIPTION, ["hang"]))
    await asyncio.sleep(0.5)
    children = _children()
    assert children, "the engine should be running"
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    for pid in children:
        assert not _alive(pid)


def _children() -> list[int]:
    me = os.getpid()
    found = []
    for entry in os.listdir("/proc"):
        if entry.isdigit():
            try:
                stat = open(f"/proc/{entry}/stat").read().rsplit(")", 1)[1].split()
            except OSError:
                continue
            if int(stat[1]) == me:
                found.append(int(entry))
    return found


def _alive(pid: int) -> bool:
    try:
        stat = open(f"/proc/{pid}/stat").read().rsplit(")", 1)[1].split()
    except OSError:
        return False
    return stat[0] != "Z"
