"""Start the engine on one description and read what it wrote.

Purpose:
    The launcher's way to fly a single-player battle: write a description,
    run the engine with ``--launch``, wait, and give back the checked result.

Flow:
    ``fly`` builds and checks the description, writes it in a folder of its
    own, runs ``<engine> --launch FILE [extra arguments]`` with a time limit,
    then reads and checks the result file: the one the description names,
    or the one the engine writes beside the description when it refuses a
    description it could not read or check. ``fly_async`` does the same
    without blocking the event loop, and stops the engine when its task is
    cancelled: ``terminate``, then ``kill`` after ``STOP_SECONDS``.

Invariants:
    - A description that breaks its schema is never given to the engine.
    - A result that is missing, is not JSON or breaks its schema is an error,
      whatever the engine's exit code was.
    - A folder made for the run is removed at the end; a result path given by
      the caller is kept.
    - A result file an earlier run left at the result path is removed before
      the engine starts, so it is never read as this run's.

Call:
    ``fly(engine, description, extra=(), result=None, timeout=3600.0)`` or
    ``await fly_async(engine, description, extra=(), timeout=3600.0)``
"""

from __future__ import annotations

import asyncio
import json
import logging
import subprocess
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from .check import errors
from .schema import launch_schema
from .schema import result_schema

logger = logging.getLogger(__name__)

STOP_SECONDS = 10.0
"""How long a stopped engine may take to exit before it is killed."""


class LaunchError(RuntimeError):
    """A battle that could not be started or whose result cannot be trusted."""


def fly(
    engine: str,
    description: dict[str, Any],
    extra: Sequence[str] = (),
    result: str | None = None,
    timeout: float = 3600.0,
) -> tuple[dict[str, Any], int]:
    """Fly one battle and return (result document, the engine's exit code).

    ``result`` is where the engine writes the result file; without it a file
    in a temporary folder is used. Raises LaunchError when the description
    breaks its schema, the engine cannot be started or runs past ``timeout``
    seconds (it is killed), or the result file is missing or breaks its
    schema. A refusal by the engine is a result, not an error.
    """
    with tempfile.TemporaryDirectory(prefix="jedimaster-launch-") as folder:
        description_path, result_path = _prepare(description, folder, result)
        command = [engine, "--launch", str(description_path), *extra]
        logger.info("starting %s", command)
        try:
            done = subprocess.run(command, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as exc:
            raise LaunchError(
                f"the engine ran past {timeout:g} seconds and was stopped"
            ) from exc
        except OSError as exc:
            raise LaunchError(f"the engine could not be started: {exc}") from exc
        logger.info("engine exit code %s", done.returncode)
        written_path = _result_written(result_path, description_path)
        return _read_result(written_path), done.returncode


def _prepare(
    description: dict[str, Any], folder: str, result: str | None
) -> tuple[Path, Path]:
    """Check and write the description in ``folder``; return (its path, the result path).

    Raises LaunchError when the description breaks its schema. Removes a result
    file an earlier run left at the result path.
    """
    result_path = Path(result) if result else Path(folder) / "result.json"
    written = dict(description, result=str(result_path))
    problems = errors(written, launch_schema(), "the description")
    if problems:
        raise LaunchError("; ".join(problems))
    description_path = Path(folder) / "launch.json"
    description_path.write_text(json.dumps(written, indent=2) + "\n", encoding="utf-8")
    result_path.unlink(missing_ok=True)
    return description_path, result_path


async def _stop(process: asyncio.subprocess.Process) -> None:
    """Ask the engine to exit; kill it if it has not exited after ``STOP_SECONDS``."""
    if process.returncode is not None:
        return
    process.terminate()
    try:
        await asyncio.wait_for(process.wait(), STOP_SECONDS)
    except TimeoutError:
        logger.info("the engine did not exit in %g seconds: killed", STOP_SECONDS)
        process.kill()
        await process.wait()


async def fly_async(
    engine: str,
    description: dict[str, Any],
    extra: Sequence[str] = (),
    timeout: float = 3600.0,
) -> tuple[dict[str, Any], int]:
    """Fly one battle without blocking the event loop; return (result, exit code).

    Checks and runs as ``fly`` does, with the result in a temporary folder.
    Raises LaunchError as ``fly`` does. When the awaiting task is cancelled the
    engine is stopped (``terminate``, then ``kill`` after ``STOP_SECONDS``)
    and the cancellation passes on. Does not read a result after a stop.
    """
    with tempfile.TemporaryDirectory(prefix="jedimaster-launch-") as folder:
        description_path, result_path = _prepare(description, folder, None)
        command = [engine, "--launch", str(description_path), *extra]
        logger.info("starting %s", command)
        try:
            process = await asyncio.create_subprocess_exec(*command)
        except OSError as exc:
            raise LaunchError(f"the engine could not be started: {exc}") from exc
        try:
            await asyncio.wait_for(process.wait(), timeout)
        except TimeoutError as exc:
            await _stop(process)
            raise LaunchError(
                f"the engine ran past {timeout:g} seconds and was stopped"
            ) from exc
        except asyncio.CancelledError:
            logger.info("the battle was cancelled: stopping the engine")
            await asyncio.shield(_stop(process))
            raise
        logger.info("engine exit code %s", process.returncode)
        written_path = _result_written(result_path, description_path)
        return _read_result(written_path), int(process.returncode)


def _result_written(result_path: Path, description_path: Path) -> Path:
    """Return the file the engine wrote its result to.

    The engine writes to ``result_path``, except when it refuses a
    description it could not read or check: it cannot trust the path such a
    description names, so it writes ``launch.json.result.json`` beside the
    description. Returns ``result_path`` when neither file exists.
    """
    beside = description_path.with_name(description_path.name + ".result.json")
    if not result_path.exists() and beside.exists():
        logger.debug("the result is beside the description: %s", beside)
        return beside
    return result_path


def _read_result(path: Path) -> dict[str, Any]:
    """Return the checked result document at path; raise LaunchError when it cannot be trusted."""
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise LaunchError(f"the engine left no result file at {path}") from exc
    except ValueError as exc:
        raise LaunchError(f"the result file {path} is not JSON") from exc
    problems = errors(document, result_schema(), "the result")
    if problems:
        raise LaunchError("; ".join(problems))
    return document
