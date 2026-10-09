"""Command line: ``python -m jedimaster <command>``, one command per kind of file.

Purpose:
    The commands are ``dump``, ``export``, ``lists``, ``icons``, ``text``,
    ``fonts``, ``pictures``, ``pilot``, ``models`` and ``movies``. Print one mission in
    the answer-sheet text format, or export every mission of an install as
    JSON files; ``lists`` does the same for the game's text lists;
    ``icons`` reads the briefing map's icon sheets; ``text``, ``fonts`` and
    ``pictures`` read the menus' text files, fonts and pictures; ``pilot``
    reads a pilot's files and loads a pilot; ``models`` reads the 3D model
    files; ``movies`` reads the movies and their subtitle files.

Flow:
    ``dump <mission>``: read the file (or, when no such file exists, resolve
    the argument as a game path in ``--install`` or a found install) and
    print ``render_mission``. ``export <install> <out-folder>``: list the
    install's missions by folder and write ``<out>/<folder>/<NAME>.json`` for
    each, then print one summary line. ``lists dump <file>`` prints the
    reader's own view of one list (``render_raw``), its kind taken from its
    name unless ``--kind`` gives it; ``lists export <install> <out-folder>``
    writes ``<out>/<path in the install>.json`` for every list of
    ``list_files``, then one summary line. ``icons dump <bmp>`` and ``icons
    export <install> <out-folder>`` are ``jedimaster.icons.cli``'s; ``text
    dump <kind> <file>``, ``fonts dump <file>``, ``pictures dump <bmp>``,
    ``models dump <opt>`` and their ``export <install> <out-folder>`` are
    the ``cli`` modules' of ``jedimaster.text``, ``jedimaster.fonts``,
    ``jedimaster.pictures`` and ``jedimaster.models``; ``pilot
    dump|load|export`` is ``jedimaster.pilot.cli``'s.

Invariants:
    - ``print`` is used only for the command's output; diagnostics go to the
      logging module (stderr), WARNING by default, ``-v`` INFO, ``-vv`` DEBUG.
    - Exit status: 0 on success, 1 when a mission or list could not be read
      or written, 2 for a usage error, a mission/list/install that is not
      found, or a list whose kind cannot be told.

Call:
    ``python -m jedimaster dump TRAIN/1TA01BF.TIE --install <folder>``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from .fonts import cli as fonts_cli
from .icons import cli as icons_cli
from .install import find_install
from .install import list_missions
from .install import resolve_game_path
from .lists import kind_of
from .lists import list_files
from .lists import list_to_json
from .lists import ListFormatError
from .lists import read_list
from .lists import render_raw
from .lists.files import KINDS
from .mission import mission_to_json
from .mission import MissionFormatError
from .mission import read_mission
from .mission import render_mission
from .models import cli as models_cli
from .movies import cli as movies_cli
from .pictures import cli as pictures_cli
from .pilot import cli as pilot_cli
from .text import cli as text_cli

logger = logging.getLogger(__name__)

SUBCOMMANDS = {
    "text": text_cli,
    "fonts": fonts_cli,
    "pictures": pictures_cli,
    "pilot": pilot_cli,
    "models": models_cli,
    "movies": movies_cli,
}
"""The commands whose modules parse and run their own subcommands."""


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
    lists = sub.add_parser("lists", help="read the game's text lists")
    lists_sub = lists.add_subparsers(dest="lists_command", required=True)
    ldump = lists_sub.add_parser("dump", help="print one list as the reader reads it")
    ldump.add_argument("list", help="a list file, or a game path with --install")
    ldump.add_argument("--install", help="install folder for a game path")
    ldump.add_argument(
        "--kind", choices=KINDS, help="the list's kind (default: by name)"
    )
    lexport = lists_sub.add_parser("export", help="write one JSON file per list")
    lexport.add_argument("install", help="the install folder")
    lexport.add_argument("out", help="the output folder")
    icons_cli.add_parser(sub)
    text_cli.add_parser(sub)
    fonts_cli.add_parser(sub)
    pictures_cli.add_parser(sub)
    pilot_cli.add_parser(sub)
    models_cli.add_parser(sub)
    movies_cli.add_parser(sub)
    return parser


def _lists_dump(args: argparse.Namespace) -> int:
    path = Path(args.list)
    if not path.is_file():
        install = find_install(args.install) if args.install else find_install()
        resolved = resolve_game_path(install, args.list) if install else None
        if resolved is None or not resolved.is_file():
            logger.error("list not found: %s", args.list)
            return 2
        path = resolved
    kind = args.kind or kind_of(path)
    if kind is None:
        logger.error("cannot tell the kind of %s: give --kind", path)
        return 2
    try:
        result = read_list(kind, path)
    except (ListFormatError, OSError) as exc:
        logger.error("cannot read list %s: %s", path, exc)
        return 1
    sys.stdout.write(render_raw(result))
    return 0


def _lists_export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install, no lists: %s", args.install)
        return 2
    out = Path(args.out)
    written = failed = 0
    for kind, paths in list_files(install).items():
        for path in paths:
            target = out / path.relative_to(install).with_suffix(".json")
            try:
                data = list_to_json(read_list(kind, path))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(
                    json.dumps(data, ensure_ascii=False, indent=1) + "\n",
                    encoding="utf-8",
                )
            except (ListFormatError, OSError) as exc:
                logger.error("cannot export %s: %s", path, exc)
                failed += 1
                continue
            logger.info("wrote %s", target)
            written += 1
    print(f"exported {written} lists to {out} ({failed} failed)")
    return 1 if failed else 0


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

    Returns 0 on success, 1 when a mission, list, bitmap, text file, font,
    picture, pilot or model fails to read or write, 2 for a missing one or
    install
    or a list of unknown kind (argparse itself exits 2 on bad usage). Does not
    catch errors other than the readers' format errors and OS errors.
    """
    args = _parser().parse_args(argv)
    level = (logging.WARNING, logging.INFO, logging.DEBUG)[min(args.verbose, 2)]
    logging.basicConfig(level=level, format="%(levelname)s %(name)s: %(message)s")
    if args.command == "dump":
        return _dump(args)
    if args.command == "icons":
        return icons_cli.run(args)
    if args.command in SUBCOMMANDS:
        return SUBCOMMANDS[args.command].run(args)
    if args.command == "lists":
        return (
            _lists_dump(args) if args.lists_command == "dump" else _lists_export(args)
        )
    return _export(args)


if __name__ == "__main__":
    sys.exit(main())
