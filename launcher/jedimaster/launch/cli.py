"""Command lines of the launch command, ``python -m jedimaster launch``, and of ai-setup.

Purpose:
    ``launch --type T [--side S] --mission ID --pilot NAME [--engine PATH]``
    flies one single-player battle in the engine and prints one line saying
    how it ended. Without ``--engine`` the current build ``jedimaster update``
    installed is flown.

Flow:
    ``add_parser`` adds the command; ``run`` builds the description, flies it
    through ``run.fly`` and prints the line. ``--engine-arg`` hands one extra
    argument to the engine (for example ``--engine-arg=--game-data`` then
    ``--engine-arg=/path``), ``--result`` keeps the result file,
    ``--setting NAME=VALUE`` (repeatable) sets one game option of the
    description (see ``options``), and ``--ai-program FOLDER`` has the engine
    start the AI program in that folder with this Python; without it the AI
    program packaged beside the engine is used. Before the engine starts, the
    two must speak the same AI version (``ai_wire_check``).

Invariants:
    - ``print`` carries only the one line; diagnostics go to the logging
      module.
    - A battle never starts without an AI program that speaks the engine's
      AI version: the refusal sentence goes to the log and the status is 1.
    - Exit status: the engine's own (0 flown, 3 refused, 4 not reached, 5
      failed) when its result says so; 1 when the battle could not be
      started or its result cannot be trusted; 2 for arguments that do not
      make a description, an AI program folder without the program
      included, no ``--engine`` and no current build.

Call:
    ``python -m jedimaster launch --type training --side rebel --mission 1ta02bz.tie --pilot Ace --engine ./OpenXvT``
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
from typing import Any

from ..mission.names import CRAFTTYPE
from .ai_wire_check import check_pair
from .ai_wire_check import PairRefused
from .builds import current_engine
from .model import ai_program_of
from .model import build_description
from .model import GAME_TYPES
from .model import SIDES
from .options import parse_setting
from .run import fly
from .run import LaunchError
from .run import save_ai_program

logger = logging.getLogger(__name__)


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``launch`` command to ``sub``. Returns None; parses nothing."""
    launch = sub.add_parser("launch", help="fly one single-player battle in the engine")
    launch.add_argument("--type", required=True, choices=GAME_TYPES, dest="game_type")
    launch.add_argument(
        "--side",
        choices=SIDES,
        help="rebel or imperial for training, combat and campaign",
    )
    launch.add_argument(
        "--mission", required=True, help="the mission's file name in its game list"
    )
    launch.add_argument("--pilot", required=True, help="a pilot the game already has")
    launch.add_argument(
        "--engine",
        help="the engine program (default: the current build installed by jedimaster update)",
    )
    launch.add_argument(
        "--builds", help="the folder of installed builds (default: the user's own)"
    )
    launch.add_argument("--result", help="keep the result file here")
    launch.add_argument("--seed", type=int, help="the random seed of a sequence")
    launch.add_argument(
        "--timeout",
        type=float,
        default=3600.0,
        help="seconds before the engine is stopped",
    )
    launch.add_argument(
        "--engine-arg", action="append", default=[], help="one extra engine argument"
    )
    launch.add_argument(
        "--ai-program",
        metavar="FOLDER",
        help="the AI program that flies the computer craft (default: the one packaged beside the engine)",
    )
    launch.add_argument(
        "--setting",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="one game option, for example difficulty=hard (repeatable)",
    )
    ai_setup = sub.add_parser(
        "ai-setup",
        help="keep the AI program in the game's settings, for every flight not started by launch",
    )
    ai_setup.add_argument("--engine", required=True, help="the engine program")
    ai_setup.add_argument(
        "--ai-program",
        required=True,
        help="the AI program's package folder (ai/jedimaster_ai in the repository)",
    )


def _count_words(flown: int, lost: int, one: str, many: str) -> str:
    """Return how a rank's craft fared: ``one`` names a single craft, ``many`` plural ones."""
    if flown == 1:
        return f"the {one} {'was destroyed' if lost else 'lived'}"
    if lost == 0:
        return f"all {flown} {many} lived"
    if lost == flown:
        return f"all {flown} {many} were destroyed"
    return f"{flown - lost} of {flown} {many} lived"


def sith_clause(sith: dict[str, Any]) -> str:
    """Return the end line's clause on the Sith flight, or "" when the setting is none.

    Names the flight and its craft type, then how the Lord and the
    apprentices fared (destroyed, or lived: left by hyperspace or still
    flying at the end). A setting with no flight promoted says why instead.
    """
    if sith["setting"] == "none":
        return ""
    if sith["flight_group"] < 0:
        return f"; no Sith flight: {sith['why']}" if sith["why"] else "; no Sith flight"
    craft = CRAFTTYPE.get(sith["craft_type"], f"craft type {sith['craft_type']}")
    fates = []
    if sith["lord_flown"]:
        fates.append(
            _count_words(sith["lord_flown"], sith["lord_lost"], "Lord", "Lords")
        )
    if sith["apprentices_flown"]:
        fates.append(
            _count_words(
                sith["apprentices_flown"],
                sith["apprentices_lost"],
                "apprentice",
                "apprentices",
            )
        )
    fate = ", ".join(fates) if fates else "no Sith craft arrived"
    return f"; Sith flight {sith['flight_name']} ({craft}): {fate}"


def summary(result: dict[str, Any]) -> str:
    """Return the one line that says how a battle ended.

    A version 3 result with a Sith setting adds the Sith clause to a battle
    that was flown; a version 2 result has none.
    """
    if result["status"] == "flown":
        line = (
            f"flown: {result['outcome'].replace('_', ' ')}, score {result['score']}, "
            f"kills {result['kills']}, losses {result['losses']}"
        )
        if result["outcome"] == "completed":
            line += f", goals met at {result['goals_met_seconds']} s"
        if "sith" in result:
            line += sith_clause(result["sith"])
        return line
    return f"{result['status'].replace('_', ' ')}: {result['why']}"


def run(args: argparse.Namespace) -> int:
    """Run the ``launch`` command, or ``ai-setup`` (``run_ai_setup``); return its exit status.

    Returns the engine's exit code when the result agrees with it, 1 when the
    battle cannot be started or the result cannot be trusted, 2 when the
    arguments make no description.
    """
    if args.command == "ai-setup":
        return run_ai_setup(args)
    engine = args.engine
    if not engine:
        found = current_engine(Path(args.builds) if args.builds else None)
        if found is None:
            logger.error(
                'no game program: give --engine, or run "jedimaster update" first'
            )
            return 2
        engine = str(found)
    try:
        settings = dict(parse_setting(item) for item in args.setting)
        if args.ai_program:
            ai_program_of(args.ai_program)
        ai_program = check_pair(engine, args.ai_program).program
        description = build_description(
            args.game_type,
            args.side,
            args.mission,
            args.pilot,
            seed=args.seed,
            settings=settings,
            ai_program=ai_program,
        )
    except ValueError as exc:
        logger.error("%s", exc)
        return 2
    except PairRefused as exc:
        logger.error("%s", exc)
        return 1
    try:
        result, code = fly(
            engine, description, args.engine_arg, args.result, args.timeout
        )
    except LaunchError as exc:
        logger.error("%s", exc)
        return 1
    print(summary(result))
    if code != result["exit_code"]:
        logger.error(
            "the engine exited with %s but its result says %s",
            code,
            result["exit_code"],
        )
        return 1
    return code


def run_ai_setup(args: argparse.Namespace) -> int:
    """Run the ``ai-setup`` command; return its exit status.

    Returns the engine's exit code, or 1 when the folder holds no AI program
    or the engine cannot be run.
    """
    try:
        code = save_ai_program(args.engine, args.ai_program)
    except (ValueError, LaunchError) as exc:
        logger.error("%s", exc)
        return 1
    if code == 0:
        print("kept: every flight not started by launch runs this AI program")
    return code
