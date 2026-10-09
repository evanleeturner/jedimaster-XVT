"""Command line of the 3D models: ``python -m jedimaster models dump|export``.

Purpose:
    ``models dump <opt>`` prints the reader's view of one model file in
    the ``file`` sheet's form; ``models export <install> <out-folder>
    [--no-balance-of-power]`` writes ``models.json`` and one ``.glb`` per
    model file and switch index.

Flow:
    ``add_parser`` adds the ``models`` command to the package's parser;
    ``run`` dispatches to ``_dump`` or ``_export``; ``export_models``
    writes the files of one view.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - Exit status: 0 on success, 1 when the model to dump cannot be read
      or a file cannot be written, 2 when the model or the install is not
      found. An export lists a model it cannot read in ``models.json``
      (``not_loaded``, with a WARNING) and writes no ``.glb`` for it.

Call:
    ``python -m jedimaster models export <install> export/models``
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from ..install import find_install
from ..text.cli import find_file
from .draw import most_children
from .game import load_model
from .game import models_view
from .game import ModelsView
from .gltf import glb_bytes
from .gltf import glb_names
from .model import NODE_SWITCH
from .objects import objects_view
from .render import model_lines
from .to_json import glb_stem
from .to_json import models_to_json

logger = logging.getLogger(__name__)

JSON_NAME = "models.json"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``models`` command and its two subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    models = sub.add_parser("models", help="read the 3D model files (.opt)")
    models_sub = models.add_subparsers(dest="models_command", required=True)
    dump = models_sub.add_parser("dump", help="print one model as the reader reads it")
    dump.add_argument("opt", help="a .opt file, or a game path with --install")
    dump.add_argument("--install", help="install folder for a game path")
    export = models_sub.add_parser("export", help="write models.json and .glb files")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    export.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def _dump(args: argparse.Namespace) -> int:
    path = find_file(args.opt, args.install)
    if path is None:
        logger.error("model not found: %s", args.opt)
        return 2
    entry = load_model(args.opt, path, str(path))
    if entry.model is None:
        logger.error("model %s not loaded: %s", args.opt, entry.error)
        return 1
    sys.stdout.write("\n".join(model_lines(entry)) + "\n")
    return 0


def export_models(view: ModelsView, out: Path) -> int:
    """Write one ``.glb`` per loaded model and switch index into ``out``.

    Returns how many files were written. Raises ``OSError`` when a file
    cannot be written. Does not write ``models.json``.
    """
    written = 0
    for entry in view.models:
        if entry.model is None:
            continue
        stem = glb_stem(entry.name)
        names = glb_names(stem, most_children(entry.model, NODE_SWITCH))
        for switch, name in enumerate(names):
            (out / name).write_bytes(glb_bytes(entry.model, stem, switch))
            logger.debug("wrote %s", out / name)
            written += 1
    logger.info("wrote %d .glb files to %s", written, out)
    return written


def _export(args: argparse.Namespace) -> int:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
        return 2
    balance_of_power = not args.no_balance_of_power
    out = Path(args.out)
    try:
        view = models_view(install, balance_of_power)
        objects = objects_view(install, balance_of_power)
        out.mkdir(parents=True, exist_ok=True)
        written = export_models(view, out)
        text = json.dumps(models_to_json(view, objects), ensure_ascii=False, indent=1)
        (out / JSON_NAME).write_text(text + "\n", encoding="utf-8")
    except (OSError, ValueError) as exc:
        logger.error("cannot export to %s: %s", out, exc)
        return 1
    print(f"exported {written} .glb files and {JSON_NAME} to {out}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run a ``models`` subcommand; return its exit status.

    Returns 0 on success, 1 when the model to dump cannot be read or a
    file cannot be written, 2 when the model or the install is not found.
    Does not catch errors other than OS and value errors.
    """
    if args.models_command == "dump":
        return _dump(args)
    return _export(args)
