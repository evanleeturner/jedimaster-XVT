"""The battle that flies in the background: one at a time, started, ended, cancelled.

Purpose:
    Run the engine on one launch description without blocking the event loop
    the page and the bot share, tell every listener when the battle starts
    and when it ends, and stop it on request or at the time limit.

Flow:
    ``Battles.start`` takes the engine settings (``Flight``), the checked
    description and the data of the ``launch.started`` push; it makes a task
    that awaits ``fly_async`` and, when the engine is done, builds the
    ``launch.ended`` push and hands it to ``notify``. ``cancel`` cancels the
    task, which stops the engine (terminate, then kill after ten seconds).

Invariants:
    - At most one battle flies; ``start`` raises ``RuntimeError`` while one
      does, so the caller must check ``busy`` and answer in words.
    - Every started battle ends with exactly one ``launch.ended`` push, whose
      status is the engine's result status, ``cancelled`` or ``error``.
    - An engine whose exit code disagrees with its result is an ``error``,
      and a result that cannot be trusted is an ``error`` with the reason.
    - A failing listener is logged at WARNING and never stops the others.
    - The battle runs the engine given by the launcher's own command line;
      nothing here takes a path from a message.

Call:
    ``battles = Battles(notify); launch_id = battles.start(flight, description, data)``
"""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Awaitable
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..launch.cli import summary
from ..launch.run import fly_async
from ..launch.run import LaunchError
from .protocol import MAX_ID

logger = logging.getLogger(__name__)

Json = dict[str, Any]
Notify = Callable[[Json], Awaitable[None]]
DEFAULT_TIMEOUT = 3600.0


@dataclass(frozen=True)
class Flight:
    """The game program and what goes with it, from the launcher's command line."""

    engine: str
    args: tuple[str, ...] = ()
    pilot_folder: Path | None = None
    timeout: float = DEFAULT_TIMEOUT
    # The description's ``ai_program`` (``model.ai_program_of``); None starts none.
    ai_program: dict[str, str] | None = None


class Battles:
    """The one battle that may be flying, and the pushes about it."""

    def __init__(self, notify: Notify, first_id: int = 1) -> None:
        """Send every push to ``notify``; launch ids count from ``first_id``."""
        self._notify = notify
        self._next_id = first_id
        self._task: asyncio.Task | None = None
        self._current: Json | None = None
        self._entered = False
        self._early_cancel = False

    def busy(self) -> bool:
        """Say whether a battle is flying."""
        return self._current is not None

    def running_push(self) -> Json | None:
        """Return the ``launch.started`` push of the flying battle, or None."""
        if self._current is None:
            return None
        return {"event": "launch.started", "data": dict(self._current)}

    def start(self, flight: Flight, description: Json, data: Json) -> int:
        """Start the battle in the background; return its launch id.

        ``data`` is the push's data without the id. Raises ``RuntimeError``
        when a battle already flies or no event loop runs. Returns at once.
        """
        if self.busy():
            raise RuntimeError("a battle is already flying")
        loop = asyncio.get_running_loop()
        launch_id = self._next_id
        self._next_id = 1 if launch_id >= MAX_ID else launch_id + 1
        self._current = {"launch_id": launch_id, **data}
        self._entered = False
        self._early_cancel = False
        self._task = loop.create_task(self._run(launch_id, flight, description))
        logger.info("launch %d started: %s", launch_id, data)
        return launch_id

    def cancel(self, launch_id: int) -> bool:
        """Stop the battle ``launch_id``; return whether there was one to stop.

        Returns at once; the engine is stopped by the battle's own task, which
        then sends the ending push.
        """
        if self._current is None or self._current["launch_id"] != launch_id:
            logger.debug("cancel of launch %d: no such battle flying", launch_id)
            return False
        assert self._task is not None
        logger.info("launch %d cancelled", launch_id)
        if self._entered:
            self._task.cancel()
        else:
            # A task cancelled before its first step never runs, so it could
            # not send its ending; it is told instead.
            self._early_cancel = True
        return True

    async def _run(self, launch_id: int, flight: Flight, description: Json) -> None:
        self._entered = True
        if self._early_cancel:
            status, line = "cancelled", "cancelled"
        else:
            status, line = await self._fly(flight, description)
        self._current = None
        logger.info("launch %d ended: %s: %s", launch_id, status, line)
        await self._notify(
            {
                "event": "launch.ended",
                "data": {"launch_id": launch_id, "status": status, "line": line},
            }
        )

    async def _fly(self, flight: Flight, description: Json) -> tuple[str, str]:
        try:
            result, code = await fly_async(
                flight.engine, description, flight.args, flight.timeout
            )
        except asyncio.CancelledError:
            return "cancelled", "cancelled"
        except LaunchError as exc:
            return "error", str(exc)
        except Exception as exc:  # the battle must always end with a push
            logger.exception("the battle could not be run")
            return "error", f"the battle could not be run ({type(exc).__name__})"
        if code != result["exit_code"]:
            logger.error(
                "the engine exited with %s but its result says %s",
                code,
                result["exit_code"],
            )
            return "error", (
                f"the engine exited with {code} but its result says "
                f"{result['exit_code']}"
            )
        return result["status"], summary(result)
