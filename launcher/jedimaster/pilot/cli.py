"""Command line of the pilot files: ``python -m jedimaster pilot dump|load|export``.

Purpose:
    ``pilot dump <file>`` prints one ``.pl2`` or ``.plt`` read whole as its
    record; ``pilot load <folder> <name> <install>`` prints the record the
    game holds after loading the pilot from a folder; ``pilot export``
    writes that record as ``pilot.json``.

Flow:
    ``add_parser`` adds the ``pilot`` command to the package's parser;
    ``run`` dispatches to ``_dump``, ``_load`` or ``_export``; the last two
    share ``loaded``: find the install, read its defaults, load.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 when the sheet or the JSON is written (a load whose
      result is 0 included), 1 when a file cannot be read, is refused or
      cannot be written, 2 when the file, folder or install is not found.
    - ``--no-balance-of-power`` reads the install as if Balance of Power
      were absent.

Call:
    ``python -m jedimaster pilot load <folder> Host0.plt <install>``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..install import find_install
from ..lists import ListFormatError
from .defaults import install_defaults
from .load import load_pilot
from .load import PilotLoad
from .record import PilotFormatError
from .record import read_pilot_file
from .render import render_load
from .render import render_raw
from .to_json import pilot_to_json

logger = logging.getLogger(__name__)

JSON_NAME = "pilot.json"


def _load_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("folder", help="the player's folder holding the pilot")
    parser.add_argument("name", help="the pilot's .plt name, e.g. Host0.plt")
    parser.add_argument("install", help="the install folder")
    parser.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``pilot`` command and its three subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    pilot = sub.add_parser("pilot", help="read the pilot files")
    pilot_sub = pilot.add_subparsers(dest="pilot_command", required=True)
    dump = pilot_sub.add_parser("dump", help="print one .pl2 or .plt as its record")
    dump.add_argument("file", help="a .pl2 or .plt file")
    load = pilot_sub.add_parser("load", help="print the pilot as the game loads it")
    _load_arguments(load)
    export = pilot_sub.add_parser("export", help="write pilot.json")
    _load_arguments(export)
    export.add_argument("out", help="the output folder")


def _dump(args: argparse.Namespace) -> int:
    path = Path(args.file)
    if not path.is_file():
        logger.error("pilot file not found: %s", path)
        return 2
    try:
        name, record = read_pilot_file(path)
    except (PilotFormatError, OSError) as exc:
        logger.error("cannot read %s: %s", path, exc)
        return 1
    sys.stdout.write(render_raw(name, record, path.stat().st_size))
    return 0


def loaded(args: argparse.Namespace) -> PilotLoad | int:
    """Return the load a ``load`` or ``export`` command asks for, or a status.

    Returns 2 when the folder or the install is not found, 1 when the
    install's files or the pilot's cannot be read; else the ``PilotLoad``.
    Does not print.
    """
    folder = Path(args.folder)
    if not folder.is_dir():
        logger.error("not a folder: %s", folder)
        return 2
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    try:
        defaults = install_defaults(install, not args.no_balance_of_power)
        return load_pilot(folder, args.name, defaults)
    except (ListFormatError, OSError) as exc:
        logger.error("cannot load %s: %s", args.name, exc)
        return 1


def _load(args: argparse.Namespace) -> int:
    load = loaded(args)
    if isinstance(load, int):
        return load
    sys.stdout.write(render_load(load))
    return 0


def _export(args: argparse.Namespace) -> int:
    load = loaded(args)
    if isinstance(load, int):
        return load
    out = Path(args.out)
    data = pilot_to_json(load, balance_of_power=not args.no_balance_of_power)
    try:
        out.mkdir(parents=True, exist_ok=True)
        text = json.dumps(data, ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except OSError as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    print(f"exported {load.name} (result {load.result}) to {out / JSON_NAME}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run a ``pilot`` subcommand; return its exit status.

    Returns 0 on success, 1 when a file cannot be read, is refused or
    cannot be written, 2 when the file, folder or install is not found.
    Does not catch errors other than refusals and OS errors.
    """
    if args.pilot_command == "dump":
        return _dump(args)
    if args.pilot_command == "load":
        return _load(args)
    return _export(args)
