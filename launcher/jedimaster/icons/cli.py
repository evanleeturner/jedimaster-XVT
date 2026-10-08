"""Command line of the icons: ``python -m jedimaster icons dump|export``.

Purpose:
    ``icons dump <bmp>`` prints the bitmap reader's view of one file (header
    fields and what the decoding met, not the pixels); ``icons export
    <install> <out-folder> [--no-balance-of-power]`` writes ``icons.json``
    and one PNG file per icon sheet.

Flow:
    ``add_parser`` adds the ``icons`` command to the package's parser;
    ``run`` dispatches to ``_dump`` or ``_export``.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 on success, 1 when a file cannot be read or written,
      2 when the bitmap, the install or its icon list is not found.

Call:
    ``python -m jedimaster icons export <install> export/``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..install import find_install
from ..install import resolve_game_path
from ..lists import ListFormatError
from .bmp import BmpFormatError
from .bmp import read_bmp
from .game import icons_view
from .png import write_png
from .render import render_bmp
from .to_json import icons_to_json
from .to_json import picture_name

logger = logging.getLogger(__name__)

JSON_NAME = "icons.json"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``icons`` command and its two subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    icons = sub.add_parser("icons", help="read the briefing map's icon sheets")
    icons_sub = icons.add_subparsers(dest="icons_command", required=True)
    dump = icons_sub.add_parser("dump", help="print one bitmap as the reader reads it")
    dump.add_argument("bmp", help="a .bmp file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    export = icons_sub.add_parser("export", help="write icons.json and the pictures")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    export.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def _dump(args: argparse.Namespace) -> int:
    path = Path(args.bmp)
    if not path.is_file():
        install = find_install(args.install) if args.install else find_install()
        resolved = resolve_game_path(install, args.bmp) if install else None
        if resolved is None or not resolved.is_file():
            logger.error("bitmap not found: %s", args.bmp)
            return 2
        path = resolved
    try:
        bmp = read_bmp(path)
    except (BmpFormatError, OSError) as exc:
        logger.error("cannot read bitmap %s: %s", path, exc)
        return 1
    sys.stdout.write(render_bmp(bmp))
    return 0


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    try:
        view = icons_view(install, balance_of_power=not args.no_balance_of_power)
    except FileNotFoundError as exc:
        logger.error("no icon list: %s", exc)
        return 2
    except (ListFormatError, OSError) as exc:
        logger.error("cannot read the icon list: %s", exc)
        return 1
    out = Path(args.out)
    try:
        out.mkdir(parents=True, exist_ok=True)
        for sheet in view.sheets.values():
            target = out / picture_name(sheet.name)
            write_png(target, sheet.bitmap)
            logger.info("wrote %s", target)
        text = json.dumps(icons_to_json(view), ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except (OSError, ValueError) as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    print(f"exported {len(view.sheets)} icon sheets and {JSON_NAME} to {out}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run an ``icons`` subcommand; return its exit status.

    Returns 0 on success, 1 when a file cannot be read or written, 2 when
    the bitmap, the install or its icon list is not found. Does not catch
    errors other than bitmap-format, list-format and OS errors.
    """
    if args.icons_command == "dump":
        return _dump(args)
    return _export(args)
