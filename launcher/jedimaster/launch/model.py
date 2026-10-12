"""The words of the launch description and the result, and a description builder.

Purpose:
    One place for every name and number the two launch documents share with
    the engine, so the schemas, the checker and the command agree with the
    engine's reading (``engine/src/xvt_runtime/runtime/launch_description.c``).

Flow:
    ``build_description`` turns the command's arguments into the description
    dict the engine reads; ``folder_of`` gives a game type's mission folder;
    ``ai_program_of`` turns the AI program's folder into the description's
    ``ai_program``, run by this launcher's own Python.

Invariants:
    - The description names settings by the lobby setup document's names and
      values (``schema/setup.schema.json``), those of the game's solo screen.
    - A game type that takes a side is given ``rebel`` or ``imperial``; the
      others are given ``none``.

Call:
    ``build_description("training", "rebel", "1ta02bz.tie", "Ace")``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

SCHEMA_VERSION = 1
MODE = "single_player"

GAME_TYPES = ("training", "melee", "tournament", "combat", "battle", "campaign")
FOLDERS = {
    "training": "train",
    "melee": "melee",
    "tournament": "tourn",
    "combat": "combat",
    "battle": "battle",
    "campaign": "campaign",
}
SIDED_TYPES = ("training", "combat", "campaign")
SIDES = ("rebel", "imperial", "none")

DIFFICULTIES = ("easy", "medium", "hard", "easy_cheat")
SEQUENCE_CHOICES = ("restart", "continue")
BATTLE_LENGTHS = (2, 3, 4)
TIME_LIMIT_WORDS = ("none", "default")
TIME_LIMIT_MINUTES = (1, 20)
BOOLEAN_SETTINGS = ("collisions", "craft_jumping", "random_setup")

MISSION_PATTERN = r"^[A-Za-z]+/[A-Za-z0-9_.]{1,63}$"
SEED_MAXIMUM = 4294967295

STATUSES = ("flown", "refused", "not_reached", "failed")
OUTCOMES = ("none", "completed", "not_completed")
EXIT_CODES = {"flown": 0, "refused": 3, "not_reached": 4, "failed": 5}


def folder_of(game_type: str) -> str:
    """Return the mission folder of ``game_type``; raise ValueError for an unknown type."""
    if game_type not in FOLDERS:
        raise ValueError(f"unknown game type {game_type!r}")
    return FOLDERS[game_type]


def ai_program_of(folder: str | Path) -> dict[str, str]:
    """Return the description's ``ai_program`` for the AI program in ``folder``.

    ``folder`` is the program's package folder, ``ai/jedimaster_ai`` in the
    repository; the engine runs it with the Python running this launcher, and
    the path is made absolute because the engine starts it from its own
    working folder. Raises ValueError when ``folder`` holds no
    ``__main__.py``.
    """
    path = Path(folder).resolve()
    if not (path / "__main__.py").is_file():
        raise ValueError(f"no AI program in {folder}: the folder holds no __main__.py")
    program = {"python": sys.executable, "path": str(path)}
    logger.debug("ai program %s", program)
    return program


def build_description(
    game_type: str,
    side: str | None,
    mission: str,
    pilot: str,
    result: str | None = None,
    seed: int | None = None,
    settings: dict[str, Any] | None = None,
    ai_program: dict[str, str] | None = None,
) -> dict[str, Any]:
    """Return the description of one single-player battle.

    ``mission`` is the mission's file name in its game list, or
    ``folder/file``. A game type that takes a side needs ``rebel`` or
    ``imperial``; the others take none and get ``none``. Raises ValueError
    for an unknown game type, a side that does not fit the type, or a mission
    named for another folder. Does not check names against the engine's
    rules; the schema checker does.
    """
    folder = folder_of(game_type)
    if game_type in SIDED_TYPES:
        if side not in ("rebel", "imperial"):
            raise ValueError(f"a {game_type} battle needs a side, rebel or imperial")
    elif side not in (None, "none"):
        raise ValueError(f"a {game_type} battle takes no side")
    path = mission if "/" in mission else f"{folder}/{mission}"
    if path.split("/", 1)[0].lower() != folder:
        raise ValueError(f"the mission {mission!r} is not in the {folder} folder")
    description: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "mode": MODE,
        "game_type": game_type,
        "side": side if game_type in SIDED_TYPES else "none",
        "mission": path,
        "pilot": pilot,
    }
    if result is not None:
        description["result"] = result
    if seed is not None:
        description["seed"] = seed
    if settings:
        description["settings"] = dict(settings)
    if ai_program is not None:
        description["ai_program"] = dict(ai_program)
    logger.debug("description %s", description)
    return description
