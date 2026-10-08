"""Command line of the text files: ``python -m jedimaster text dump|export``.

Purpose:
    ``text dump <kind> <file>`` prints one file's reading in the answer
    sheets' form; ``text export <install> <out-folder>
    [--no-balance-of-power]`` writes ``text.json`` with every table and
    list of the view.

Flow:
    ``add_parser`` adds the ``text`` command to the package's parser;
    ``run`` dispatches to ``_dump`` or ``_export``.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 on success, 1 when a file cannot be read, is refused
      (dump) or cannot be written, 2 when the file or the install is not
      found. A refused ``strings.txt`` does not fail an export: it is
      recorded in ``text.json`` as an error.

Call:
    ``python -m jedimaster text dump front FRONTTXT.TXT``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..install import find_install
from ..install import resolve_game_path
from .game import GAME_NAMES
from .game import read_kind
from .game import TEXT_FILES
from .game import text_view
from .render import render_text
from .strings import StringsFormatError
from .to_json import text_to_json

logger = logging.getLogger(__name__)

JSON_NAME = "text.json"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``text`` command and its two subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    text = sub.add_parser("text", help="read the game's text files")
    text_sub = text.add_subparsers(dest="text_command", required=True)
    dump = text_sub.add_parser("dump", help="print one text file as the game reads it")
    dump.add_argument("kind", choices=[kind for kind, _ in TEXT_FILES])
    dump.add_argument("file", help="a text file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    export = text_sub.add_parser("export", help="write text.json")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    export.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def find_file(given: str, install_folder: str | None) -> Path | None:
    """Return the file a command names: a path, or a game path in an install.

    ``given`` is used as it is when it names a file; otherwise it resolves
    as a game path in ``install_folder`` (or a found install). Returns None
    when neither gives a file, the install is not found or the game path
    is not relative. Does not read the file.
    """
    path = Path(given)
    if path.is_file():
        return path
    install = find_install(install_folder) if install_folder else find_install()
    try:
        resolved = resolve_game_path(install, given) if install else None
    except ValueError:
        resolved = None
    return resolved if resolved is not None and resolved.is_file() else None


def _dump(args: argparse.Namespace) -> int:
    path = find_file(args.file, args.install)
    if path is None:
        logger.error("text file not found: %s", args.file)
        return 2
    name = GAME_NAMES[args.kind]
    try:
        result = read_kind(args.kind, path)
    except StringsFormatError as exc:
        logger.error("refused: %s", exc)
        sys.stdout.write(render_text(args.kind, None, name, str(path)))
        return 1
    except OSError as exc:
        logger.error("cannot read %s: %s", path, exc)
        return 1
    sys.stdout.write(render_text(args.kind, result, name, str(path)))
    return 0


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    view = text_view(install, balance_of_power=not args.no_balance_of_power)
    out = Path(args.out)
    try:
        out.mkdir(parents=True, exist_ok=True)
        text = json.dumps(text_to_json(view), ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except OSError as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    read = sum(f.result is not None for f in view.files.values())
    print(f"exported {read} of {len(view.files)} text files to {out / JSON_NAME}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run a ``text`` subcommand; return its exit status.

    Returns 0 on success, 1 when a file cannot be read, is refused (dump)
    or cannot be written, 2 when the file or the install is not found. Does
    not catch errors other than refusals and OS errors.
    """
    if args.text_command == "dump":
        return _dump(args)
    return _export(args)
