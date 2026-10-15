"""Check that a game build and its AI program speak the same AI version.

Purpose:
    The engine and the AI program talk in one message format; each carries a
    version number for it. A pair whose numbers differ cannot fly computer
    craft: the AI program refuses the first question and the battle ends at
    once. This module reads both numbers before a battle, so the player is
    told the cause in plain words instead.

Flow:
    ``check_pair`` finds the AI program (the folder the player named, else the
    one packaged beside the engine), asks the engine ``--version`` and the
    AI program ``--version`` (each limited to ``ASK_SECONDS``), and returns the
    pair when the two numbers are equal. Otherwise it raises ``PairRefused``
    whose text is the one sentence for the player. Nothing is remembered:
    every call asks again, so a build swapped between battles is followed.

Invariants:
    - A pair is returned only when the AI program exists and both numbers
      were read and are equal.
    - Each refusal is one of five sentences, worded as the constants below.
    - An engine that could not be started or did not answer in time is
      ``ENGINE_SILENT``; ``ENGINE_TOO_OLD`` is only for an engine that ran and
      gave no ``ai_wire=`` line.
    - The engine is run with ``--version`` and no other argument: it opens no
      window and writes no file.
    - The AI program is run by this launcher's own Python, as a battle runs it.

Call:
    ``pair = check_pair("/path/OpenXvT", None)``; ``pair.program`` is the
    description's ``ai_program``.
"""

from __future__ import annotations

import logging
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from .model import ai_program_of

logger = logging.getLogger(__name__)

ASK_SECONDS = 10.0
"""How long the engine or the AI program may take to say its version."""

MISMATCH = (
    "This game build speaks AI version {engine} and its AI program speaks AI "
    'version {program}, so computer craft could not fly. Run "jedimaster update" '
    "for a matching pair."
)
ENGINE_TOO_OLD = (
    "This game build is too old to say which AI version it speaks. "
    'Run "jedimaster update" for a current one.'
)
ENGINE_SILENT = "This game build could not be asked which AI version it speaks: {why}."
NO_PROGRAM = (
    "No AI program was found beside this game build, so computer craft could "
    'not fly. Run "jedimaster update", or name one with --ai-program.'
)
PROGRAM_SILENT = (
    "The AI program in {folder} did not say which AI version it speaks: {why}."
)

WIRE_LINE = re.compile(r"ai_wire=(\d+)")


class PairRefused(Exception):
    """The pair cannot fly computer craft; the text is the sentence for the player."""


@dataclass(frozen=True)
class Pair:
    """A matching pair: the shared AI version and the description's ``ai_program``."""

    version: int
    program: dict[str, str]


def packaged_ai_program(engine: str | Path) -> Path | None:
    """Return the AI program folder packaged beside ``engine``, or None.

    Looks at ``<engine's folder>/ai/jedimaster_ai`` and, for the macOS
    application, ``<engine's folder>/../Resources/ai/jedimaster_ai``; a folder
    counts when it holds ``__main__.py``.
    """
    folder = Path(engine).parent
    for candidate in (
        folder / "ai" / "jedimaster_ai",
        folder.parent / "Resources" / "ai" / "jedimaster_ai",
    ):
        if (candidate / "__main__.py").is_file():
            return candidate
    return None


def _ask(command: list[str]) -> tuple[list[str], str | None, bool]:
    """Run ``command``; return (its standard output lines, why none or None, ran).

    ``ran`` is False when the command could not be started or timed out, True
    when it ran to an end (even one with a bad exit status).
    """
    try:
        done = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=ASK_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return [], f"it did not answer within {ASK_SECONDS:g} seconds", False
    except OSError as exc:
        why = f"it could not be started ({exc.strerror or type(exc).__name__})"
        return [], why, False
    if done.returncode != 0:
        detail = (done.stderr or "").strip().splitlines()
        reason = f"it exited with status {done.returncode}"
        return [], f"{reason}: {detail[-1].rstrip('.')}" if detail else reason, True
    return done.stdout.splitlines(), None, True


def _wire_of(lines: list[str]) -> int | None:
    """Return the number of the first ``ai_wire=N`` line in ``lines``, or None."""
    for line in lines:
        found = WIRE_LINE.fullmatch(line.strip())
        if found:
            return int(found.group(1))
    return None


def engine_says(engine: str | Path) -> tuple[str | None, int | None]:
    """Return (the engine's version text, its AI version) from ``<engine> --version``.

    Both are None when the engine gives no answer (an older build rejects the
    option). The version text is the first line without the ``OpenXvT `` word.
    """
    lines, why, _ = _ask([str(engine), "--version"])
    logger.debug("engine %s --version: %s %s", engine, lines, why)
    if why is not None or not lines:
        return None, None
    first = lines[0].strip()
    text = first[len("OpenXvT ") :] if first.startswith("OpenXvT ") else None
    return text, _wire_of(lines)


def check_pair(engine: str | Path, ai_program: str | Path | None) -> Pair:
    """Return the pair of ``engine`` and its AI program when their AI versions match.

    ``ai_program`` is the folder the player named, or None for the one
    packaged beside the engine. Raises ``PairRefused`` with the sentence for
    the player when there is no AI program, the engine or the AI program says
    no version, or the two differ.
    """
    folder = Path(ai_program) if ai_program else packaged_ai_program(engine)
    if folder is None:
        raise PairRefused(NO_PROGRAM)
    lines, why, ran = _ask([str(engine), "--version"])
    logger.debug("engine %s --version: %s %s", engine, lines, why)
    if not ran:
        raise PairRefused(ENGINE_SILENT.format(why=why))
    engine_wire = _wire_of(lines)
    if engine_wire is None:
        raise PairRefused(ENGINE_TOO_OLD)
    try:
        program = ai_program_of(folder)
    except ValueError:
        raise PairRefused(
            PROGRAM_SILENT.format(folder=folder, why="the folder holds no __main__.py")
        ) from None
    lines, why, _ = _ask([sys.executable, program["path"], "--version"])
    if why is None:
        program_wire = _wire_of(lines)
        why = None if program_wire is not None else "it printed no ai_wire= line"
    if why is not None:
        raise PairRefused(PROGRAM_SILENT.format(folder=folder, why=why))
    if engine_wire != program_wire:
        raise PairRefused(MISMATCH.format(engine=engine_wire, program=program_wire))
    logger.debug("pair ok: AI version %d, program %s", engine_wire, program)
    return Pair(engine_wire, program)
