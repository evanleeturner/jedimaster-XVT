"""Render every stock mission and diff it against its answer sheet.

Purpose:
    The acceptance fence of the renderer: each of the missions listed in
    ``missions.txt`` must render to exactly the text of
    ``answers/<folder>/<NAME>.txt``.

Flow:
    Read the mission list (paths relative to the install), read each mission
    straight from ``<install>/<path>`` (no case folding, no BalanceOfPower
    lookup, so the listed file is the one compared), render it, compare it
    with the sheet, print a unified diff head for each difference, then one
    summary line.

Invariants:
    - Exit status 0 only when every listed mission renders identically and
      the list is not empty; 1 otherwise.
    - A mission that cannot be read counts as a difference, never a crash.

Call:
    ``python tools/compare_answers.py --answers DIR --missions FILE
    [--install DIR] [--context N]``; the answer sheets and the mission list
    hold game data, so they live on the machine that runs the check, never
    in the repository
"""

from __future__ import annotations

import argparse
import difflib
import logging
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.install import find_install  # noqa: E402
from jedimaster.mission import MissionFormatError  # noqa: E402
from jedimaster.mission import read_mission  # noqa: E402
from jedimaster.mission import render_mission  # noqa: E402

logger = logging.getLogger("compare_answers")


def _sheet_for(answers: Path, rel: str) -> Path:
    return answers / Path(rel).with_suffix(".txt")


def compare(install: Path, answers: Path, missions: list[str], context: int) -> int:
    """Compare each mission's rendering with its sheet; return the count same.

    Prints a diff head (``context`` lines) for each mission that differs.
    Does not stop at the first difference.
    """
    same = 0
    for rel in missions:
        sheet = _sheet_for(answers, rel)
        try:
            text = render_mission(read_mission(install / rel))
            expected = sheet.read_text(encoding="iso-8859-1")
        except (MissionFormatError, OSError) as exc:
            print(f"ERROR {rel}: {exc}")
            continue
        if text == expected:
            same += 1
            logger.info("same: %s", rel)
            continue
        diff = list(
            difflib.unified_diff(
                expected.splitlines(),
                text.splitlines(),
                str(sheet),
                rel + " (rendered)",
                n=0,
                lineterm="",
            )
        )
        print(f"DIFF {rel} ({len(diff)} diff lines)")
        print("\n".join(diff[:context]))
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when all listed missions are identical."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--install", help="install folder (default: found)")
    parser.add_argument(
        "--answers", required=True, help="the answer sheets' folder (local only)"
    )
    parser.add_argument(
        "--missions", required=True, help="the mission list file (local only)"
    )
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)
    install = find_install(args.install) if args.install else find_install()
    if install is None:
        print("no install found")
        return 1
    missions = Path(args.missions).read_text(encoding="utf-8").split("\n")
    missions = [m.strip() for m in missions if m.strip()]
    same = compare(install, Path(args.answers), missions, args.context)
    print(
        f"{same} of {len(missions)} missions render identically to their answer sheets"
    )
    return 0 if missions and same == len(missions) else 1


if __name__ == "__main__":
    sys.exit(main())
