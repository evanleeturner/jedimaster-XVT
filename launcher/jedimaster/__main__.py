"""Command line: ``python -m jedimaster dump|export``.

Purpose:
    Print one mission in the answer-sheet text format, or export every
    mission of an install as JSON files.

Flow:
    ``dump <mission>``: read the file (or, when no such file exists, resolve
    the argument as a game path in ``--install`` or a found install) and
    print ``render_mission``. ``export <install> <out-folder>``: list the
    install's missions by folder and write ``<out>/<folder>/<NAME>.json`` for
    each, then print one summary line.

Invariants:
    - ``print`` is used only for the command's output; diagnostics go to the
      logging module (stderr), WARNING by default, ``-v`` INFO, ``-vv`` DEBUG.
    - Exit status: 0 on success, 1 when a mission could not be read or
      written, 2 for a usage error or a mission/install that is not found.

Call:
    ``python -m jedimaster dump TRAIN/1TA01BF.TIE --install <folder>``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from .install import find_install
from .install import list_missions
from .install import resolve_game_path
from .mission import mission_to_json
from .mission import MissionFormatError
from .mission import read_mission
from .mission import render_mission

logger = logging.getLogger(__name__)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -m jedimaster")
    parser.add_argument("-v", "--verbose", action="count", default=0)
    sub = parser.add_subparsers(dest="command", required=True)
    dump = sub.add_parser("dump", help="print a mission in the answer-sheet format")
    dump.add_argument("mission", help="a .tie file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    export = sub.add_parser("export", help="write one JSON file per mission")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    return parser


def _dump(args: argparse.Namespace) -> int:
    path = Path(args.mission)
    if not path.is_file():
        install = find_install(args.install) if args.install else find_install()
        resolved = resolve_game_path(install, args.mission) if install else None
        if resolved is None or not resolved.is_file():
            logger.error("mission not found: %s", args.mission)
            return 2
        path = resolved
    try:
        mission = read_mission(path)
    except (MissionFormatError, OSError) as exc:
        logger.error("cannot read %s: %s", path, exc)
        return 1
    sys.stdout.write(render_mission(mission))
    return 0


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    out = Path(args.out)
    written = failed = 0
    for folder, files in list_missions(install).items():
        for path in files:
            target = out / folder / (path.stem + ".json")
            try:
                data = mission_to_json(read_mission(path))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(
                    json.dumps(data, ensure_ascii=False, indent=1) + "\n",
                    encoding="utf-8",
                )
            except (MissionFormatError, OSError) as exc:
                logger.error("cannot export %s: %s", path, exc)
                failed += 1
                continue
            logger.info("wrote %s", target)
            written += 1
    print(f"exported {written} missions to {out} ({failed} failed)")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    """Run the command line; return the exit status.

    Returns 0 on success, 1 when a mission fails to read or write, 2 for a
    missing mission or install (argparse itself exits 2 on bad usage). Does
    not catch errors other than mission-format and OS errors.
    """
    args = _parser().parse_args(argv)
    level = (logging.WARNING, logging.INFO, logging.DEBUG)[min(args.verbose, 2)]
    logging.basicConfig(level=level, format="%(levelname)s %(name)s: %(message)s")
    if args.command == "dump":
        return _dump(args)
    return _export(args)


if __name__ == "__main__":
    sys.exit(main())
