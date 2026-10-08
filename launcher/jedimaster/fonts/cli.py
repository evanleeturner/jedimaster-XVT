"""Command line of the menu fonts: ``python -m jedimaster fonts dump|export``.

Purpose:
    ``fonts dump <file> [--text S ...]`` prints one font's reading in the
    answer sheets' form, each ``--text`` string (written as the sheets
    quote, ``\\x02`` and the like allowed) drawn after the glyphs;
    ``fonts export <install> <out-folder> [--no-balance-of-power]`` writes
    ``fonts.json`` and one atlas PNG per font.

Flow:
    ``add_parser`` adds the ``fonts`` command to the package's parser;
    ``run`` dispatches to ``_dump`` or ``_export``.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 on success, 1 when a font cannot be read or written
      (or a ``--text`` cannot be unquoted), 2 when the font or the install
      is not found.

Call:
    ``python -m jedimaster fonts dump TIMES10.ABP --text 'a\\x02b'``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..install import find_install
from ..text.cli import find_file
from ..text.sheet import unquote
from .game import fonts_view
from .reader import FontFormatError
from .reader import read_font
from .render import render_font
from .to_json import atlas_name
from .to_json import fonts_to_json
from .to_json import write_atlas

logger = logging.getLogger(__name__)

JSON_NAME = "fonts.json"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``fonts`` command and its two subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    fonts = sub.add_parser("fonts", help="read the game's menu fonts")
    fonts_sub = fonts.add_subparsers(dest="fonts_command", required=True)
    dump = fonts_sub.add_parser("dump", help="print one font as the game reads it")
    dump.add_argument("file", help="a font file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    dump.add_argument(
        "--text", action="append", default=[], help="a string to draw (repeatable)"
    )
    export = fonts_sub.add_parser("export", help="write fonts.json and the atlases")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    export.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def _dump(args: argparse.Namespace) -> int:
    path = find_file(args.file, args.install)
    if path is None:
        logger.error("font not found: %s", args.file)
        return 2
    try:
        samples = [unquote(text) for text in args.text]
        font = read_font(path)
    except (FontFormatError, ValueError, OSError) as exc:
        logger.error("cannot read %s: %s", path, exc)
        return 1
    sys.stdout.write(render_font(font, path.name.lower(), str(path), samples))
    return 0


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    view = fonts_view(install, balance_of_power=not args.no_balance_of_power)
    out = Path(args.out)
    written = 0
    try:
        out.mkdir(parents=True, exist_ok=True)
        for font_file in view.fonts.values():
            if font_file.font is None:
                continue
            target = out / atlas_name(font_file.name)
            write_atlas(target, font_file.font)
            logger.info("wrote %s", target)
            written += 1
        text = json.dumps(fonts_to_json(view), ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except (OSError, ValueError) as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    print(f"exported {written} font atlases and {JSON_NAME} to {out}")
    return 0 if written == len(view.fonts) else 1


def run(args: argparse.Namespace) -> int:
    """Run a ``fonts`` subcommand; return its exit status.

    Returns 0 on success, 1 when a font cannot be read or written, 2 when
    the font or the install is not found. Does not catch errors other than
    font-format, value and OS errors.
    """
    if args.fonts_command == "dump":
        return _dump(args)
    return _export(args)
