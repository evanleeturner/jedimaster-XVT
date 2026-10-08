"""Build the icon view of an install, render it, and diff it against its sheets.

Purpose:
    The acceptance fence of the icon reader: for both views (``base`` reads
    the install as if Balance of Power were absent, ``bop`` as it is) and
    all three kinds (``sheets``, ``tables``, ``draws``), render the view and
    diff it against ``<answers>/<view>/<kind>.txt``. For ``sheets`` and
    ``draws`` also check the log's ``image.registered`` lines (resource,
    ``file=``, ``rle=``, in order) against the view's registered images.

Flow:
    1. For each view, build ``icons_view(install, view == "bop")`` once.
    2. For each kind, render, diff, and check the log where asked.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - A sheet that cannot be read or rendered counts as a difference.
    - Exit status 0 only when all six sheets match and every log check
      passes.

Call:
    ``python tools/compare_icons.py --answers DIR --install DIR [--context N]``
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

from jedimaster.icons import icons_view  # noqa: E402
from jedimaster.icons import render_draws  # noqa: E402
from jedimaster.icons import render_sheets  # noqa: E402
from jedimaster.icons import render_tables  # noqa: E402
from jedimaster.icons.bmp import BmpFormatError  # noqa: E402
from jedimaster.icons.game import IconView  # noqa: E402
from jedimaster.lists import ListFormatError  # noqa: E402

logger = logging.getLogger("compare_icons")

VIEWS = ("base", "bop")
KINDS = ("sheets", "tables", "draws")
LOGGED_KINDS = ("sheets", "draws")


def registered(view: IconView) -> list[tuple[str, str, str]]:
    """Return (name, bitmap word, flag) of each registered image, file order.

    A registered group missing from the table (never, for a list the view
    read) is left out. Does not check that the bitmaps exist.
    """
    by_group = {image.group: image for image in view.images.images}
    return [
        (by_group[g].name, by_group[g].bitmap, str(by_group[g].flag))
        for g in view.images.registered
        if g in by_group
    ]


def log_problems(view: IconView, log: str) -> list[str]:
    """Return the differences between the log's registrations and the view.

    Returns ``[]`` when the log's (resource, file, rle) triples equal
    ``registered(view)`` in order, else one message naming the first
    difference. Does not check the log's other fields or lines.
    """
    theirs = [
        (r.get("resource"), r.get("file"), r.get("rle"))
        for r in log_fields(log, "image.registered")
    ]
    mine = registered(view)
    if mine == theirs:
        return []
    for i, (a, b) in enumerate(zip(mine, theirs, strict=False)):
        if a != b:
            return [f"log image.registered differs at {i}: view {a} log {b}"]
    return [f"log image.registered: view has {len(mine)}, log has {len(theirs)}"]


def render(kind: str, view: IconView) -> str:
    """Return the rendering of one kind for a view; any other kind is draws."""
    if kind == "sheets":
        return render_sheets(view)
    if kind == "tables":
        return render_tables()
    return render_draws(view)


def check(
    answers: Path, view_name: str, kind: str, view: IconView, context: int
) -> bool:
    """Diff one sheet (and its log); print what differs; return True if same.

    A sheet or log that cannot be read prints an ERROR line and returns
    False. Does not stop at the first difference.
    """
    path = answers / view_name / f"{kind}.txt"
    try:
        expected = path.read_text(encoding="latin-1")
        log_path = path.with_suffix(".log")
        log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
    except OSError as exc:
        print(f"ERROR {view_name}/{kind}: {exc}")
        return False
    diff = list(
        difflib.unified_diff(
            expected.splitlines(),
            render(kind, view).splitlines(),
            f"{view_name}/{kind}.txt",
            "rendered",
            n=0,
            lineterm="",
        )
    )
    problems = log_problems(view, log) if kind in LOGGED_KINDS else []
    if not diff and not problems:
        logger.info("same: %s/%s", view_name, kind)
        return True
    print(
        f"DIFF {view_name}/{kind} ({len(diff)} diff lines, {len(problems)} log problems)"
    )
    for problem in problems:
        print(f"  {problem}")
    if diff:
        print("\n".join(diff[:context]))
    return False


def compare(answers: Path, install: Path, context: int) -> int:
    """Check the six sheets; print each difference; return how many match.

    A view that cannot be built prints an ERROR line and counts its three
    sheets as different. Does not check ``sheets.md``.
    """
    same = 0
    for view_name in VIEWS:
        try:
            view = icons_view(install, balance_of_power=view_name == "bop")
        except (BmpFormatError, ListFormatError, OSError) as exc:
            print(f"ERROR {view_name}: {exc}")
            continue
        sheets = len(view.sheets)
        print(f"view {view_name}: {sheets} sheets read from {view.list_file}")
        for kind in KINDS:
            same += check(answers, view_name, kind, view, context)
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when all six sheets match."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)
    same = compare(Path(args.answers), Path(args.install), args.context)
    total = len(VIEWS) * len(KINDS)
    print(f"{same} of {total} sheets match")
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
