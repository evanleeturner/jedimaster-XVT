"""Command line of the briefing bundle: ``python -m jedimaster briefing export``.

Purpose:
    ``briefing export --install DIR --file PATH --out FILE`` writes the
    bundle of one mission as JSON. Both paths are required: game data stays
    on the machine that runs the command.

Flow:
    ``add_parser`` adds the ``briefing`` command; ``run`` finds the install,
    reads the mission, builds the bundle and writes it.

Invariants:
    - ``print`` carries only the command's one summary line; diagnostics go
      to the logging module.
    - Exit status: 0 on success, 1 when the mission cannot be read, the
      bundle cannot be built or the file cannot be written, 2 when the
      install or the mission file is not found.

Call:
    ``python -m jedimaster briefing export --install DIR --file M.tie --out M.json``
"""

from __future__ import annotations

import argparse
import json
import logging
from pathlib import Path

from ..install import find_install
from ..lists import ListFormatError
from ..mission import MissionFormatError
from ..mission import read_mission
from .build import BriefingBuildError
from .build import build_bundle

logger = logging.getLogger(__name__)


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``briefing`` command and its one subcommand to ``sub``.

    Returns None. Does not parse anything.
    """
    briefing = sub.add_parser("briefing", help="build a mission's briefing bundle")
    briefing_sub = briefing.add_subparsers(dest="briefing_command", required=True)
    export = briefing_sub.add_parser("export", help="write one mission's bundle")
    export.add_argument("--install", required=True, help="the install folder")
    export.add_argument("--file", required=True, help="the mission file (.tie)")
    export.add_argument("--out", required=True, help="the JSON file to write")


def run(args: argparse.Namespace) -> int:
    """Run the ``briefing`` command; return its exit status.

    Returns 0 on success, 1 when a file cannot be read, built or written,
    2 when the install or the mission file is not found. Does not catch
    errors other than the readers' format errors and OS errors.
    """
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    source = Path(args.file)
    if not source.is_file():
        logger.error("mission not found: %s", source)
        return 2
    try:
        bundle = build_bundle(install, read_mission(source))
        text = json.dumps(bundle, ensure_ascii=False, separators=(",", ":"))
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    except (MissionFormatError, BriefingBuildError, ListFormatError, OSError) as exc:
        logger.error("cannot export %s: %s", source, exc)
        return 1
    print(f"wrote the briefing of {source.name} to {args.out}")
    return 0
