"""Render every pilot sheet with the ``pilot`` command and diff it against its sheet.

Purpose:
    The acceptance fence of the pilot reader: render the layout sheet from
    the package's layout; for every pilot folder of ``<answers>/files``
    run ``pilot load`` for the four loads (both files, the ``.pl2`` alone,
    the ``.plt`` alone with Balance of Power and without) and ``pilot
    dump`` of the ``.plt`` (the raw sheet); diff each output against its
    sheet, check each run's exit status against ``status.txt``, and check
    the log lines the package can answer for: the load's own ``pilot.*``
    lines, the menus' count and bytes (``strings.loaded``) and the three
    training lists the defaults read (``mission.setup_list_*``).

Flow:
    1. Diff the layout sheet.
    2. For each pilot: copy its files into one folder per load (both, the
       ``.pl2`` alone, the ``.plt`` alone) under ``--tmp``, run each load
       and the dump through the package's command line, capturing its
       output and the load's log lines.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - A sheet that cannot be read counts as a difference.
    - The ``pattern`` sheets are not compared: they record how ``p01`` and
      ``p02`` were made.
    - Exit status 0 only when every sheet matches (1 + 5 per pilot) and
      every check passes.

Call:
    ``python tools/compare_pilot.py --answers DIR --install DIR [--tmp DIR]
    [--context N]``
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

from compare_lists import log_fields  # noqa: E402

from jedimaster.__main__ import main as jedimaster_main  # noqa: E402
from jedimaster.lists import menu_game_path  # noqa: E402
from jedimaster.lists import menu_view  # noqa: E402
from jedimaster.lists import read_menu  # noqa: E402
from jedimaster.lists.files import resolve  # noqa: E402
from jedimaster.lists.game import MISSION_TYPES  # noqa: E402
from jedimaster.pilot import render_layout  # noqa: E402
from jedimaster.pilot.defaults import FRONT_FILE  # noqa: E402
from jedimaster.pilot.defaults import LIST_VIEWS  # noqa: E402
from jedimaster.pilot.defaults import TRAINING  # noqa: E402
from jedimaster.text import read_front  # noqa: E402

logger = logging.getLogger("compare_pilot")

LOAD_LOGGER = "jedimaster.pilot.load"
LOADS = {
    "both": (".plt", ".pl2"),
    "pl2": (".pl2",),
    "plt-bop": (".plt",),
    "plt-base": (".plt",),
}
"""Each load sheet and the files its folder holds."""
GAME_LINE = re.compile(r"^(WARNING: )?xvt: (\S+)(.*)$")


class Capture(logging.Handler):
    """Collects the load's log lines, as ``WARNING: `` + text or the text."""

    def __init__(self) -> None:
        super().__init__(logging.DEBUG)
        self.lines: list[str] = []

    def emit(self, record: logging.LogRecord) -> None:
        """Keep a record whose message is a ``pilot.`` line; drop the rest."""
        text = record.getMessage()
        if text.startswith("pilot."):
            prefix = "WARNING: " if record.levelno >= logging.WARNING else ""
            self.lines.append(prefix + text)


def read_status(answers: Path) -> dict[str, int]:
    """Return each run's exit status from ``status.txt`` ({} when absent)."""
    path = answers / "status.txt"
    if not path.exists():
        return {}
    found = re.findall(r"^(\S+) exit=(-?\d+)$", path.read_text(), re.M)
    return {name: int(code) for name, code in found}


def run_command(argv: list[str]) -> tuple[int, str, list[str]]:
    """Run the package's command line; return status, output, load log lines.

    Only the load module's ``pilot.`` lines are kept; they do not reach
    the root logger while the command runs. Does not catch exceptions.
    """
    capture = Capture()
    load_logger = logging.getLogger(LOAD_LOGGER)
    load_logger.addHandler(capture)
    load_logger.setLevel(logging.INFO)
    load_logger.propagate = False
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            code = jedimaster_main(argv)
    finally:
        load_logger.removeHandler(capture)
        load_logger.propagate = True
    return code, out.getvalue(), capture.lines


def game_lines(log: str, prefix: str) -> list[str]:
    """Return the game's log lines whose event starts with ``prefix``.

    Each as ``WARNING: `` (for a warning) + the text after ``xvt: ``.
    """
    found = []
    for line in log.splitlines():
        match = GAME_LINE.match(line)
        if match and match[2].startswith(prefix):
            found.append((match[1] or "") + match[2] + match[3])
    return found


def list_lines(install: Path, bop: bool) -> list[str]:
    """Return the ``mission.setup_list_*`` lines the three training lists give.

    Each list's game path, every entry of its game view (index, mission
    id, unavailability, file) and its count, as the game logs them.
    """
    lines = []
    directory = MISSION_TYPES[TRAINING].number
    for view in LIST_VIEWS:
        game_path = menu_game_path(TRAINING, view)
        path = resolve(install, game_path, bop)
        entries = menu_view(read_menu(path), TRAINING, view).entries if path else []
        lines.append(
            f'mission.setup_list_file directory={directory} file="{game_path}"'
        )
        lines += [
            f"mission.setup_list_entry index={i} mission={e.id} "
            f'unavailable={int(not e.available)} file="{e.file}"'
            for i, e in enumerate(entries)
        ]
        lines.append(
            f"mission.setup_list_loaded directory={directory} missions={len(entries)}"
        )
    return lines


def log_problems(log: str, mine: list[str], install: Path, bop: bool) -> list[str]:
    """Return how the game's log differs from what the package can tell.

    The ``pilot.*`` lines must equal the load's, in order; each
    ``strings.loaded`` count and bytes must be the menus' file's as the
    text reader reads it; the ``mission.setup_list_*`` lines, when the
    game logged any, must be the three training lists' game views.
    Returns ``[]`` when all agree. Does not check other lines.
    """
    problems = []
    theirs = game_lines(log, "pilot.")
    if theirs != mine:
        problems.append(f"pilot log lines {theirs} != {mine}")
    path = resolve(install, FRONT_FILE, bop)
    front = read_front(path) if path else None
    for fields in log_fields(log, "strings.loaded"):
        count = len(front.entries) if front else None
        stored = sum(len(t) + 1 for t in front.entries) if front else None
        if (fields.get("count"), fields.get("bytes")) != (str(count), str(stored)):
            problems.append(f"strings.loaded {fields}: read {count}, {stored}")
    listed = game_lines(log, "mission.setup_list_")
    if listed and listed != list_lines(install, bop):
        problems.append("mission.setup_list lines differ from the training lists")
    return problems


def check(
    name: str, answers: Path, run: tuple[int, str, list[str]], extra: list[str], n: int
) -> bool:
    """Diff one sheet and its status; print what differs; return True when same.

    ``extra`` holds the problems already found (the log's). A sheet that
    cannot be read prints an ERROR line and returns False. Does not read
    the sheet's log itself.
    """
    code, text, _ = run
    try:
        expected = (answers / f"{name}.txt").read_text(encoding="latin-1")
    except OSError as exc:
        print(f"ERROR {name}: {exc}")
        return False
    status = read_status(answers).get(name)
    problems = list(extra)
    if status != code:
        problems.append(f"exit status {code}, the sheet's run {status}")
    diff = list(
        difflib.unified_diff(expected.splitlines(), text.splitlines(), lineterm="", n=0)
    )
    if not diff and not problems:
        logger.info("same: %s", name)
        return True
    print(f"DIFF {name} ({len(diff)} diff lines, {len(problems)} problems)")
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(line[:200] for line in diff[:n]))
    return False


def _folders(pilot: Path, tmp: Path) -> dict[str, Path]:
    files = {p.suffix.casefold(): p for p in pilot.iterdir() if p.is_file()}
    folders = {}
    for load, suffixes in LOADS.items():
        folder = tmp / pilot.name / load
        folder.mkdir(parents=True, exist_ok=True)
        for suffix in suffixes:
            if suffix in files:
                shutil.copyfile(files[suffix], folder / files[suffix].name)
        folders[load] = folder
    return folders


def compare_pilot(pilot: Path, answers: Path, install: Path, tmp: Path, n: int) -> int:
    """Check one pilot's five sheets; return how many match."""
    plt = next(p for p in sorted(pilot.iterdir()) if p.suffix.casefold() == ".plt")
    same = 0
    for load, folder in _folders(pilot, tmp).items():
        bop = load != "plt-base"
        argv = ["pilot", "load", str(folder), plt.name, str(install)]
        run = run_command(argv + ([] if bop else ["--no-balance-of-power"]))
        log_path = answers / f"{pilot.name}-{load}.log"
        log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
        problems = log_problems(log, run[2], install, bop)
        same += check(f"{pilot.name}-{load}", answers, run, problems, n)
    raw = run_command(["pilot", "dump", str(plt)])
    same += check(f"{pilot.name}-raw", answers, raw, [], n)
    return same


def compare(answers: Path, install: Path, tmp: Path, n: int) -> tuple[int, int]:
    """Check the layout and every pilot's sheets; return (matching, total)."""
    layout = (0, render_layout(), [])
    same, total = int(check("layout", answers, layout, [], n)), 1
    pilots = sorted(p for p in (answers / "files").iterdir() if p.is_dir())
    for pilot in pilots:
        same += compare_pilot(pilot, answers, install, tmp, n)
        total += len(LOADS) + 1
        print(f"pilot {pilot.name}: {same} of {total} sheets match so far")
    return same, total


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when every sheet matches."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--tmp", help="where the loads' folders go (default: temp)")
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    answers, install = Path(args.answers), Path(args.install)
    if args.tmp:
        Path(args.tmp).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.tmp) as tmp:
        same, total = compare(answers, install, Path(tmp), args.context)
    print(f"{same} of {total} sheets match")
    return 0 if same == total and total > 1 else 1


if __name__ == "__main__":
    sys.exit(main())
