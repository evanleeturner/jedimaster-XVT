"""Build the pictures view of an install, render it, and diff it against its sheets.

Purpose:
    The acceptance fence of the pictures' list and reading: for both views
    (``base`` reads the install as if Balance of Power were absent, ``bop``
    as it is) render the whole list and diff it against
    ``<answers>/<view>/pictures.txt``; check the game's
    ``image.registered`` log lines (resource, width, height, pixel bytes,
    in order) against the pictures loaded.

Flow:
    1. For each view, build ``pictures_view(install, view == "bop")``.
    2. Render, diff, check the log.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - A sheet that cannot be read counts as a difference.
    - Exit status 0 only when both sheets match and every log check passes.

Call:
    ``python tools/compare_pictures.py --answers DIR --install DIR [--context N]``
"""

from __future__ import annotations

import argparse
import difflib
import logging
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from compare_lists import log_fields  # noqa: E402

from jedimaster.pictures import pictures_view  # noqa: E402
from jedimaster.pictures import PicturesView  # noqa: E402
from jedimaster.pictures import render_pictures  # noqa: E402

logger = logging.getLogger("compare_pictures")

VIEWS = ("base", "bop")
KIND = "pictures"


def registered(view: PicturesView) -> list[tuple[str, str, str, str]]:
    """Return (name, width, height, bytes) of each loaded picture, list order.

    ``bytes`` is width times height: one byte per pixel. Does not include
    missing pictures.
    """
    return [
        (p.name, str(p.bitmap.width), str(p.bitmap.height), str(len(p.bitmap.pixels)))
        for p in view.pictures
        if p.bitmap is not None
    ]


def log_problems(view: PicturesView, log: str) -> list[str]:
    """Return the differences between ``image.registered`` lines and the view.

    Returns ``[]`` when the log's (resource, width, height, bytes) equal
    ``registered(view)`` in order, else one message naming the first
    difference. Does not check the log's other fields or lines.
    """
    theirs = [
        (f.get("resource"), f.get("width"), f.get("height"), f.get("bytes"))
        for f in log_fields(log, "image.registered")
    ]
    mine = registered(view)
    if mine == theirs:
        return []
    for i, (a, b) in enumerate(zip(mine, theirs, strict=False)):
        if a != b:
            return [f"log image.registered differs at {i}: view {a} log {b}"]
    return [f"log image.registered: view has {len(mine)}, log has {len(theirs)}"]


def check(answers: Path, view_name: str, view: PicturesView, context: int) -> bool:
    """Diff one pictures sheet and check its log; print what differs.

    Returns True when both agree. A sheet that cannot be read prints an
    ERROR line and returns False.
    """
    path = answers / view_name / f"{KIND}.txt"
    try:
        expected = path.read_text(encoding="latin-1")
        log_path = path.with_suffix(".log")
        log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
    except OSError as exc:
        print(f"ERROR {view_name}/{KIND}: {exc}")
        return False
    diff = list(
        difflib.unified_diff(
            expected.splitlines(), render_pictures(view).splitlines(), lineterm="", n=0
        )
    )
    problems = log_problems(view, log)
    if not diff and not problems:
        logger.info("same: %s/%s", view_name, KIND)
        return True
    print(f"DIFF {view_name}/{KIND} ({len(diff)} diff lines, {len(problems)} problems)")
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(diff[:context]))
    return False


def compare(answers: Path, install: Path, context: int) -> int:
    """Check the two sheets; print each difference; return how many match.

    Does not check ``sheets.md``.
    """
    same = 0
    for view_name in VIEWS:
        view = pictures_view(install, balance_of_power=view_name == "bop")
        loaded = sum(p.bitmap is not None for p in view.pictures)
        print(f"view {view_name}: {loaded} of {len(view.pictures)} pictures loaded")
        same += check(answers, view_name, view, context)
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when both sheets match."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    same = compare(Path(args.answers), Path(args.install), args.context)
    print(f"{same} of {len(VIEWS)} sheets match")
    return 0 if same == len(VIEWS) else 1


if __name__ == "__main__":
    sys.exit(main())
