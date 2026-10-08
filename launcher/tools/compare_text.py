"""Build the text view of an install, render it, and diff it against its sheets.

Purpose:
    The acceptance fence of the text readers: for both views (``base``
    reads the install as if Balance of Power were absent, ``bop`` as it
    is) and all six kinds, render the view and diff it against
    ``<answers>/<view>/<kind>.txt``; check the counts the game logged
    (``strings.loaded``, ``tech.spec_loaded``, ``options.joystick_list``,
    the bytes it loaded of ``strings.txt``) and each run's exit status. A
    run that stopped (a ``resources.fatal`` in its log) must have a sheet
    holding only its header, and the reader must refuse that file with a
    game stop, at a table the other view's sheet lays out, at a line that
    breaks that table's rule.

Flow:
    1. For each view, build ``text_view(install, view == "bop")`` once.
    2. For each kind, render, diff, check the log and the exit status.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - A sheet that cannot be read counts as a difference.
    - Exit status 0 only when all twelve sheets match and every check
      passes.

Call:
    ``python tools/compare_text.py --answers DIR --install DIR [--context N]``
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

from jedimaster.text import read_lines  # noqa: E402
from jedimaster.text import render_text  # noqa: E402
from jedimaster.text import StringsFormatError  # noqa: E402
from jedimaster.text import TEXT_FILES  # noqa: E402
from jedimaster.text import text_view  # noqa: E402
from jedimaster.text import TextFile  # noqa: E402
from jedimaster.text import TextView  # noqa: E402
from jedimaster.text.strings import BUFFER  # noqa: E402
from jedimaster.text.strings import GENDERS  # noqa: E402

logger = logging.getLogger("compare_text")

VIEWS = ("base", "bop")
KINDS = tuple(kind for kind, _ in TEXT_FILES)
HEADER_LINES = 3
TABLE_LINE = re.compile(r"^table (\S+) (\d+)$", re.M)


def read_status(answers: Path) -> dict[tuple[str, str], int]:
    """Return each run's exit status from ``status.txt`` ({} when absent)."""
    path = answers / "status.txt"
    if not path.exists():
        return {}
    found = re.findall(r"^(\S+) (\S+) exit=(-?\d+)$", path.read_text(), re.M)
    return {(view, kind): int(code) for view, kind, code in found}


def logged_counts(kind: str, result: object) -> tuple[str, dict[str, int]] | None:
    """Return the log event a kind's reading is checked against, and its fields.

    The menu entries' count and stored bytes (each entry and its end
    mark), the craft entries read whole, the joystick actions; None for a
    kind the game logs no count of. Does not read the log.
    """
    if kind == "front":
        stored = sum(len(text) + 1 for text in result.entries)
        return "strings.loaded", {"count": len(result.entries), "bytes": stored}
    if kind == "specs":
        return "tech.spec_loaded", {"entries": result.complete}
    if kind == "joystick":
        return "options.joystick_list", {"count": len(result.actions)}
    return None


def log_problems(kind: str, text_file: TextFile, log: str) -> list[str]:
    """Return the differences between the game's log and the reading.

    Checks ``logged_counts`` against each matching log line, and for
    ``strings.txt`` that the bytes the game loaded are the resolved file's
    size. Returns ``[]`` when all agree or there is nothing to compare.
    Does not check other log lines.
    """
    problems = []
    if kind == "strings" and text_file.path is not None:
        size = str(text_file.path.stat().st_size)
        loads = [f.get("bytes") for f in log_fields(log, "memory.allocated")][1:2]
        loads += [f.get("bytes") for f in log_fields(log, "strings.flight_loaded")]
        problems += [
            f"log loaded {b} bytes, file has {size}" for b in loads if b != size
        ]
    counted = logged_counts(kind, text_file.result) if text_file.result else None
    if counted is None:
        return problems
    event, mine = counted
    for fields in log_fields(log, event):
        theirs = {key: fields.get(key) for key in mine}
        if theirs != {key: str(value) for key, value in mine.items()}:
            problems.append(f"log {event} {theirs}: read {mine}")
    return problems


def layout(sheet: str) -> dict[str, int]:
    """Return the table names and line counts a strings sheet prints."""
    return {name: int(count) for name, count in TABLE_LINE.findall(sheet)}


def refusal_problems(text_file: TextFile, sheet: str, other: str) -> list[str]:
    """Return why a stopped run's reading does not match the stop.

    The sheet must hold only its header lines; the reader must refuse the
    file with a game stop, at a table ``other`` (the other view's sheet)
    lays out, at a line within its count; a ``model_names`` stop must name
    a line whose first byte gives no gender, an out-of-sync stop a line
    past the file's end. Returns ``[]`` when all hold. Does not check the
    message's wording.
    """
    problems = []
    if len(sheet.splitlines()) != HEADER_LINES:
        problems.append(f"sheet holds {len(sheet.splitlines())} lines, not its header")
    error = text_file.error
    if not isinstance(error, StringsFormatError) or not error.stops:
        return problems + [f"reader did not stop as the game does: {error!r}"]
    tables = layout(other)
    if error.table not in tables or not 1 <= error.line <= tables[error.table]:
        problems.append(f"stop at {error.table} line {error.line}: not in {tables}")
    if error.file_line is None:
        return problems
    lines = read_lines(text_file.path.read_bytes(), BUFFER, skip_comments=False)
    first = lines[error.file_line - 1].text[:1]
    if error.table == "model_names" and first and first[0] in GENDERS:
        problems.append(f"line {error.file_line} starts {first!r}: a gender byte")
    return problems


def _read(path: Path) -> str:
    return path.read_text(encoding="latin-1") if path.exists() else ""


def check(
    answers: Path, view_name: str, kind: str, view: TextView, context: int
) -> bool:
    """Diff one sheet, check its log and status; print what differs.

    Returns True when all agree. A sheet that cannot be read prints an
    ERROR line and returns False.
    """
    path = answers / view_name / f"{kind}.txt"
    try:
        expected = path.read_text(encoding="latin-1")
        log = _read(path.with_suffix(".log"))
    except OSError as exc:
        print(f"ERROR {view_name}/{kind}: {exc}")
        return False
    text_file = view.files[kind]
    rendered = render_text(kind, text_file.result, text_file.name, text_file.file)
    diff = list(
        difflib.unified_diff(
            expected.splitlines(), rendered.splitlines(), lineterm="", n=0
        )
    )
    problems = log_problems(kind, text_file, log)
    stopped = "resources.fatal" in log
    status = read_status(answers).get((view_name, kind), 0)
    if stopped:
        others = [v for v in VIEWS if v != view_name]
        other = _read(answers / others[0] / f"{kind}.txt")
        problems += refusal_problems(text_file, expected, other)
        if text_file.error is not None:
            print(
                f"  {view_name}/{kind} refused as the game stopped: {text_file.error}"
            )
    if (status != 0) != (text_file.result is None):
        problems.append(
            f"exit status {status}, reader result {text_file.result is not None}"
        )
    if not diff and not problems:
        logger.info("same: %s/%s", view_name, kind)
        return True
    print(f"DIFF {view_name}/{kind} ({len(diff)} diff lines, {len(problems)} problems)")
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(diff[:context]))
    return False


def compare(answers: Path, install: Path, context: int) -> int:
    """Check the twelve sheets; print each difference; return how many match.

    Does not check ``sheets.md``.
    """
    same = 0
    for view_name in VIEWS:
        view = text_view(install, balance_of_power=view_name == "bop")
        read = sum(f.result is not None for f in view.files.values())
        print(f"view {view_name}: {read} of {len(view.files)} files read")
        for kind in KINDS:
            same += check(answers, view_name, kind, view, context)
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when all twelve sheets match."""
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
