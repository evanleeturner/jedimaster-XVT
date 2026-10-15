#!/usr/bin/env python3
"""Write the AI program the way the game's package carries it.

Purpose:
    A game build and its AI program must come from the same commit, so every
    game package carries the program. This tool writes the one folder the
    packaging steps hand to the engine's build (``XVT_AI_PROGRAM_DIR``);
    nothing else copies the program.

Flow:
    1. remove an earlier ``jedimaster_ai`` and license file in the folder
    2. copy ``jedimaster_ai/`` without ``__pycache__`` and compiled files
    3. write ``licenses/jedimaster_ai.txt``, the MIT text with the project's
       copyright line

Invariants:
    - only the two paths above are written or removed in the folder
    - the staged program is a byte copy of the source files
    - stdlib only

Call: stage_package.py FOLDER
  -> exit 0 staged, 2 the program or the license text is missing
"""

from __future__ import annotations

import argparse
import logging
import shutil
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

ROOT = Path(__file__).resolve().parents[2]
PROGRAM = ROOT / "ai" / "jedimaster_ai"
LICENSE_TEXT = ROOT / "LICENSES" / "MIT.txt"
COPYRIGHT = "Copyright (c) 2026 Evan Lee Turner"
PLACEHOLDER = "Copyright (c) <year> <copyright holders>"
JUNK = shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo")


def stage(
    folder: Path, program: Path = PROGRAM, license_text: Path = LICENSE_TEXT
) -> None:
    """Write ``jedimaster_ai/`` and ``licenses/jedimaster_ai.txt`` into ``folder``.

    Raises FileNotFoundError when ``program`` has no ``__main__.py`` or
    ``license_text`` is missing. Replaces what an earlier run wrote.
    """
    if not (program / "__main__.py").is_file():
        raise FileNotFoundError(f"no AI program in {program}")
    text = license_text.read_text(encoding="utf-8")
    target = folder / "jedimaster_ai"
    license_file = folder / "licenses" / "jedimaster_ai.txt"
    shutil.rmtree(target, ignore_errors=True)
    license_file.unlink(missing_ok=True)
    shutil.copytree(program, target, ignore=JUNK)
    license_file.parent.mkdir(parents=True, exist_ok=True)
    license_file.write_text(text.replace(PLACEHOLDER, COPYRIGHT), encoding="utf-8")
    logger.info("staged %s into %s", program, folder)


def main(argv: list[str] | None = None) -> int:
    """Run the command line; return the exit status."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path, help="the folder to write")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    try:
        stage(args.folder)
    except FileNotFoundError as exc:
        logger.error("%s", exc)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
