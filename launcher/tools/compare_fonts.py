"""Build the fonts view of an install, render it, and diff it against its sheets.

Purpose:
    The acceptance fence of the font reader and drawing: for both views
    (``base`` reads the install as if Balance of Power were absent, ``bop``
    as it is) and the four fonts, render each font with the sample strings
    its sheet drew (read from the sheet's ``text`` lines) and diff it
    against ``<answers>/<view>/<kind>.txt``; check the game's
    ``text.font_loaded`` log line (points, glyph data bytes, height,
    spacing, in use) against the font read.

Flow:
    1. For each view, build ``fonts_view(install, view == "bop")`` once.
    2. For each font, take the samples from the sheet, render, diff, check
       the log.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool, the sample strings
      included: they come from the sheets.
    - A sheet that cannot be read or a font that cannot be read counts as
      a difference.
    - Exit status 0 only when all eight sheets match and every log check
      passes.

Call:
    ``python tools/compare_fonts.py --answers DIR --install DIR [--context N]``
"""

from __future__ import annotations

import argparse
import difflib
import logging
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from compare_lists import log_fields  # noqa: E402

from jedimaster.fonts import FONT_FILES  # noqa: E402
from jedimaster.fonts import fonts_view  # noqa: E402
from jedimaster.fonts import FontsView  # noqa: E402
from jedimaster.fonts import render_font  # noqa: E402
from jedimaster.fonts.reader import Font  # noqa: E402
from jedimaster.text.sheet import unquote  # noqa: E402

logger = logging.getLogger("compare_fonts")

VIEWS = ("base", "bop")
KINDS = tuple(kind for kind, _ in FONT_FILES)
SAMPLE = re.compile(r'^text ("(?:[^"\\]|\\.)*") ', re.M)


def samples(sheet: str) -> list[bytes]:
    """Return the sample strings a font sheet drew, in order."""
    return [unquote(quoted) for quoted in SAMPLE.findall(sheet)]


def log_problems(font: Font, log: str) -> list[str]:
    """Return the differences between ``text.font_loaded`` lines and the font.

    Compares points, glyph data bytes, height, spacing and in use. Returns
    ``[]`` when all agree or the log has no such line. Does not check the
    font's slot or file name.
    """
    mine = {
        "points": font.points,
        "bytes": font.data_size,
        "height": font.height,
        "spacing": font.spacing,
        "in_use": font.in_use,
    }
    problems = []
    for fields in log_fields(log, "text.font_loaded"):
        theirs = {key: fields.get(key) for key in mine}
        if theirs != {key: str(value) for key, value in mine.items()}:
            problems.append(f"log text.font_loaded {theirs}: read {mine}")
    return problems


def check(
    answers: Path, view_name: str, kind: str, view: FontsView, context: int
) -> bool:
    """Diff one font sheet and check its log; print what differs.

    Returns True when both agree. A sheet or font that cannot be read
    prints an ERROR line and returns False.
    """
    path = answers / view_name / f"{kind}.txt"
    font_file = view.fonts[kind]
    try:
        expected = path.read_text(encoding="latin-1")
        log_path = path.with_suffix(".log")
        log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
    except OSError as exc:
        print(f"ERROR {view_name}/{kind}: {exc}")
        return False
    if font_file.font is None:
        print(f"ERROR {view_name}/{kind}: {font_file.error}")
        return False
    rendered = render_font(
        font_file.font, font_file.name, font_file.file, samples(expected)
    )
    diff = list(
        difflib.unified_diff(
            expected.splitlines(), rendered.splitlines(), lineterm="", n=0
        )
    )
    problems = log_problems(font_file.font, log)
    if not diff and not problems:
        logger.info("same: %s/%s", view_name, kind)
        return True
    print(f"DIFF {view_name}/{kind} ({len(diff)} diff lines, {len(problems)} problems)")
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(diff[:context]))
    return False


def compare(answers: Path, install: Path, context: int) -> int:
    """Check the eight sheets; print each difference; return how many match.

    Does not check ``sheets.md``.
    """
    same = 0
    for view_name in VIEWS:
        view = fonts_view(install, balance_of_power=view_name == "bop")
        read = sum(f.font is not None for f in view.fonts.values())
        print(f"view {view_name}: {read} of {len(view.fonts)} fonts read")
        for kind in KINDS:
            same += check(answers, view_name, kind, view, context)
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when all eight sheets match."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    same = compare(Path(args.answers), Path(args.install), args.context)
    total = len(VIEWS) * len(KINDS)
    print(f"{same} of {total} sheets match")
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
