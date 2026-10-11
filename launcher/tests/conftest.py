"""Settle the battles a test leaves flying, so a failed test cannot hang the run.

Purpose:
    A test that fails while a stand-in battle still flies would leave the
    battle's task for the event loop's teardown, which then waits on the
    engine. After each test, any battle started through ``solodata`` is
    cancelled and given a moment to end.

Flow:
    The autouse fixture runs after the test: for each control ``solodata``
    made, ask the battle that flies to stop, and forget the controls.

Invariants:
    - Touches only controls made by ``solodata.make_control``.

Call:
    pytest picks this file up by itself.
"""

from __future__ import annotations

import asyncio
import logging

import pytest
import solodata

logger = logging.getLogger(__name__)


@pytest.fixture(autouse=True)
async def settle_battles():
    """After the test, cancel every battle still flying and wait for its end."""
    yield
    for control in solodata.CONTROLS:
        running = control.battles.running_push()
        if running is not None:
            control.battles.cancel(running["data"]["launch_id"])
        for _ in range(100):
            if not control.battles.busy():
                break
            await asyncio.sleep(0.05)
    solodata.CONTROLS.clear()
