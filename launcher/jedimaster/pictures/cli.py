"""Command line of the menu pictures: ``python -m jedimaster pictures dump|export``.

Purpose:
    ``pictures dump <bmp>`` prints one picture's line in the answer sheets'
    form; ``pictures export <install> <out-folder> [--no-balance-of-power]``
    writes ``pictures.json`` and one PNG per picture loaded.

Flow:
    ``add_parser`` adds the ``pictures`` command to the package's parser;
    ``run`` dispatches to ``_dump`` or ``_export``.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 on success, 1 when a picture cannot be read or a file
      cannot be written, 2 when the picture or the install is not found.

Call:
    ``python -m jedimaster pictures export <install> export/pictures``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..icons.png import write_png
from ..install import find_install
from ..text.cli import find_file
from .game import load_picture
from .game import pictures_view
from .render import picture_line
from .to_json import pictures_to_json
from .to_json import png_names

logger = logging.getLogger(__name__)

JSON_NAME = "pictures.json"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``pictures`` command and its two subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    pictures = sub.add_parser("pictures", help="read the menu screens' pictures")
    pictures_sub = pictures.add_subparsers(dest="pictures_command", required=True)
    dump = pictures_sub.add_parser("dump", help="print one picture's sheet line")
    dump.add_argument("bmp", help="a .bmp file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    export = pictures_sub.add_parser("export", help="write pictures.json and PNGs")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    export.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def _dump(args: argparse.Namespace) -> int:
    path = find_file(args.bmp, args.install)
    if path is None:
        logger.error("picture not found: %s", args.bmp)
        return 2
    picture = load_picture(args.bmp, path, str(path))
    if picture.bitmap is None:
        logger.error("picture %s not loaded: %s", args.bmp, picture.error)
        return 1
    sys.stdout.write(picture_line(picture) + "\n")
    return 0


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    view = pictures_view(install, balance_of_power=not args.no_balance_of_power)
    names = png_names(view)
    out = Path(args.out)
    try:
        out.mkdir(parents=True, exist_ok=True)
        for picture in view.pictures:
            if picture.bitmap is not None and picture.name in names:
                write_png(out / names[picture.name], picture.bitmap)
        text = json.dumps(pictures_to_json(view, names), ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except (OSError, ValueError) as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    print(f"exported {len(names)} pictures and {JSON_NAME} to {out}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run a ``pictures`` subcommand; return its exit status.

    Returns 0 on success, 1 when a picture cannot be read or a file cannot
    be written, 2 when the picture or the install is not found. Does not
    catch errors other than OS and value errors.
    """
    if args.pictures_command == "dump":
        return _dump(args)
    return _export(args)
