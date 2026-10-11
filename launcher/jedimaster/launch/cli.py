"""Command line of the launch command: ``python -m jedimaster launch``.

Purpose:
    ``launch --type T [--side S] --mission ID --pilot NAME --engine PATH``
    flies one single-player battle in the engine and prints one line saying
    how it ended.

Flow:
    ``add_parser`` adds the command; ``run`` builds the description, flies it
    through ``run.fly`` and prints the line. ``--engine-arg`` hands one extra
    argument to the engine (for example ``--engine-arg=--game-data`` then
    ``--engine-arg=/path``), ``--result`` keeps the result file, and
    ``--setting NAME=VALUE`` (repeatable) sets one game option of the
    description (see ``options``).

Invariants:
    - ``print`` carries only the one line; diagnostics go to the logging
      module.
    - Exit status: the engine's own (0 flown, 3 refused, 4 not reached, 5
      failed) when its result says so; 1 when the battle could not be
      started or its result cannot be trusted; 2 for arguments that do not
      make a description.

Call:
    ``python -m jedimaster launch --type training --side rebel --mission 1ta02bz.tie --pilot Ace --engine ./OpenXvT``
"""

from __future__ import annotations

import argparse
import logging
from typing import Any

from .model import build_description
from .model import GAME_TYPES
from .model import SIDES
from .options import parse_setting
from .run import fly
from .run import LaunchError

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
    launch.add_argument("--engine", required=True, help="the engine program")
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
        "--setting",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="one game option, for example difficulty=hard (repeatable)",
    )


def summary(result: dict[str, Any]) -> str:
    """Return the one line that says how a battle ended."""
    if result["status"] == "flown":
        return (
            f"flown: {result['outcome'].replace('_', ' ')}, score {result['score']}, "
            f"kills {result['kills']}, losses {result['losses']}, {result['flight_seconds']} s"
        )
    return f"{result['status'].replace('_', ' ')}: {result['why']}"


def run(args: argparse.Namespace) -> int:
    """Run the ``launch`` command; return its exit status.

    Returns the engine's exit code when the result agrees with it, 1 when the
    battle cannot be started or the result cannot be trusted, 2 when the
    arguments make no description.
    """
    try:
        settings = dict(parse_setting(item) for item in args.setting)
        description = build_description(
            args.game_type,
            args.side,
            args.mission,
            args.pilot,
            seed=args.seed,
            settings=settings,
        )
    except ValueError as exc:
        logger.error("%s", exc)
        return 2
    try:
        result, code = fly(
            args.engine, description, args.engine_arg, args.result, args.timeout
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
