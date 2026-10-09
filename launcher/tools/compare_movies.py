"""Build every movie sheet, diff it against its answer, and check the exit codes.

Purpose:
    The acceptance fence of the movie reader: build the frames and subtitles
    sheets the answer folder holds (Balance of Power views ``bop``, the
    install without its ``BalanceOfPower`` folder ``base``, and the made-up
    subtitle files ``synth``) through the ``movies dump`` command, diff each
    against its sheet, check the exit status each run must give, and check
    the game's log lines (cue numbers and line counts, why reading stopped,
    the warning for an unreadable number, each movie's size and audio).

Flow:
    1. List the sheets: ``<answers>/<view>/<kind>-<name>.txt``.
    2. Run the command for each (the base view with
       ``--no-balance-of-power``; the made-up files each served as
       ``movies/CASE.txt`` of a temporary install), capturing its output and
       its WARNING lines.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder, the install and the made-up inputs' folder are
      required options; nothing about the game's files is written into
      this tool, and the install is never changed.
    - The expected exit status is 1 for a frames sheet that is empty (the
      name does not resolve), else 0.
    - A sheet that cannot be read counts as a difference.
    - Exit status 0 only when every sheet matches.

Call:
    ``python tools/compare_movies.py --answers DIR --install DIR --inputs DIR``
"""

from __future__ import annotations

import argparse
import contextlib
import difflib
import io
import logging
import re
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.__main__ import main as jedimaster_main  # noqa: E402
from jedimaster.movies import find_movie  # noqa: E402
from jedimaster.movies import read_header  # noqa: E402
from jedimaster.movies import read_records  # noqa: E402
from jedimaster.movies import SubtitleFile  # noqa: E402

logger = logging.getLogger("compare_movies")

VIEWS = ("bop", "base", "synth")
KINDS = ("frames", "subtitles")
SCANNED = {"eof": -1, "bad_number": 0}
CUE = re.compile(r"movie\.subtitle_cue frame=(\d+) lines=(\d+)")
ENDED = re.compile(r"movie\.subtitles_ended scanned=(-?\d+)")
VIDEO = re.compile(r"video=(\w+) (\d+)x(\d+), audio=(?:none|(\w+) (\d+) Hz/(\d+) ch)")


class Capture(logging.Handler):
    """Collect the WARNING messages of the run."""

    def __init__(self) -> None:
        super().__init__(logging.WARNING)
        self.messages: list[str] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.messages.append(record.getMessage())


def run_command(argv: list[str]) -> tuple[int, str, list[str]]:
    """Run the command line in process; return (status, stdout, warnings).

    Does not catch an exception the command raises.
    """
    capture = Capture()
    package = logging.getLogger("jedimaster")
    saved = (package.level, package.propagate)
    package.setLevel(logging.WARNING)
    package.propagate = False
    package.addHandler(capture)
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            status = jedimaster_main(argv)
    finally:
        package.removeHandler(capture)
        package.setLevel(saved[0])
        package.propagate = saved[1]
    return status, out.getvalue(), capture.messages


def subtitle_problems(parsed: SubtitleFile, log: str, warnings: list[str]) -> list[str]:
    """Return the differences between the game's subtitle log and the reading.

    Checks the cue numbers and non-empty line counts in order, the
    ``scanned`` value (-1 at the end of the file, 0 for an unreadable number)
    and that a warning was logged exactly when a number was unreadable.
    Returns ``[]`` when all agree. Does not check the text of the messages.
    """
    problems = []
    mine = [(r.number, sum(1 for line in r.lines if line)) for r in parsed.records]
    theirs = [(int(a), int(b)) for a, b in CUE.findall(log)]
    if parsed.reason == "end_mark":
        # The game logs the record numbered 65535 too, but does not count it.
        if theirs and theirs[-1][0] == 65535:
            theirs = theirs[:-1]
        else:
            problems.append("end mark: the log holds no record numbered 65535")
    if mine != theirs:
        problems.append(f"cues differ: reading {mine} log {theirs}")
    ended = ENDED.findall(log)
    expected = SCANNED.get(parsed.reason)
    if parsed.reason == "end_mark" and ended:
        problems.append(f"end mark: the log ends with scanned {ended}")
    if expected is not None and ended != [str(expected)]:
        problems.append(f"scanned: reading says {expected}, log {ended}")
    logged = sum(1 for line in log.splitlines() if line.startswith("WARNING"))
    expected_warnings = 1 if parsed.reason == "bad_number" else 0
    if logged != expected_warnings or len(warnings) != expected_warnings:
        problems.append(
            f"warnings: log {logged}, reading {expected_warnings}, "
            f"this run {len(warnings)}"
        )
    return problems


def frames_problems(header, log: str) -> list[str]:
    """Return the differences between the game's frames log line and the header.

    Compares the stored width and height, and the audio track's codec,
    rate and channels (or none). Returns ``[]`` when they agree.
    """
    found = VIDEO.search(log)
    if found is None:
        return ["log has no video line"]
    _, width, height, acodec, rate, channels = found.groups()
    track = header.audio
    mine = (
        (header.width, header.height, track.codec, track.rate, track.channels)
        if track is not None
        else (header.width, header.height, None, None, None)
    )
    theirs = (
        (int(width), int(height), acodec, int(rate), int(channels))
        if acodec
        else (int(width), int(height), None, None, None)
    )
    return [] if mine == theirs else [f"video line: header {mine} log {theirs}"]


def check_sheet(
    answers: Path,
    view: str,
    kind: str,
    name: str,
    argv: list[str],
    bytes_of,
    context: int,
) -> bool:
    """Run one sheet's command, diff it, check status and log; print problems.

    ``bytes_of`` returns the subtitle file's bytes or the movie's path for
    the log checks. Returns True when everything agrees.
    """
    sheet = answers / view / f"{kind}-{name}.txt"
    try:
        expected = sheet.read_text(encoding="latin-1")
        log = sheet.with_suffix(".log").read_text(encoding="latin-1")
    except OSError as exc:
        print(f"ERROR {view}/{kind}-{name}: {exc}")
        return False
    status, text, warnings = run_command(argv)
    problems = []
    want = 1 if kind == "frames" and expected == "" else 0
    if status != want:
        problems.append(f"exit status {status}, expected {want}")
    problems += bytes_of(log, warnings, status)
    diff = list(
        difflib.unified_diff(expected.splitlines(), text.splitlines(), lineterm="", n=0)
    )
    if not diff and not problems:
        logger.info("same: %s/%s-%s", view, kind, name)
        return True
    print(
        f"DIFF {view}/{kind}-{name} ({len(diff)} diff lines, {len(problems)} problems)"
    )
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(diff[:context]))
    return False


def _sheet_names(answers: Path, view: str, kind: str) -> list[str]:
    prefix = f"{kind}-"
    return sorted(
        p.stem[len(prefix) :] for p in (answers / view).glob(f"{prefix}*.txt")
    )


def _subtitle_checker(install: Path, name: str, bop: bool):
    found = find_movie(install, name, bop)

    def check(log: str, warnings: list[str], status: int) -> list[str]:
        if found.subtitles is None:
            return [] if not CUE.search(log) else ["log has cues for a missing file"]
        parsed = read_records(found.subtitles.read_bytes())
        return subtitle_problems(parsed, log, warnings)

    return check


def _frames_checker(install: Path, name: str, bop: bool):
    found = find_movie(install, name, bop)

    def check(log: str, warnings: list[str], status: int) -> list[str]:
        if found.video is None:
            return []
        return frames_problems(read_header(found.video), log)

    return check


def compare(
    answers: Path, install: Path, inputs: Path, context: int
) -> tuple[int, int]:
    """Check every sheet; print each difference; return (matching, total).

    Does not check ``sheets.md`` itself.
    """
    same = total = 0
    flags = {"bop": [], "base": ["--no-balance-of-power"], "synth": []}
    with tempfile.TemporaryDirectory() as tmp:
        synth_install = Path(tmp)
        (synth_install / "Train").mkdir()
        (synth_install / "movies").mkdir()
        for view in VIEWS:
            root = synth_install if view == "synth" else install
            if view == "synth":
                for sub in (answers / "synth").glob("subtitles-*.txt"):
                    case = sub.stem[len("subtitles-") :]
                    shutil.copyfile(
                        inputs / f"{case}.txt", synth_install / "movies" / f"{case}.txt"
                    )
            for kind in KINDS:
                for name in _sheet_names(answers, view, kind):
                    argv = ["movies", "dump", kind, str(root), name, *flags[view]]
                    make = _subtitle_checker if kind == "subtitles" else _frames_checker
                    checker = make(root, name, view != "base")
                    total += 1
                    same += check_sheet(
                        answers, view, kind, name, argv, checker, context
                    )
    return same, total


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when every sheet matches."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--inputs", required=True, help="the made-up inputs' folder")
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    same, total = compare(
        Path(args.answers), Path(args.install), Path(args.inputs), args.context
    )
    print(f"{same} of {total} sheets match")
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
