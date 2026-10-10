"""Run the briefing player on every stock mission and diff it against the sheets.

Purpose:
    The acceptance fence of the briefing player: for each mission and each
    team with a briefing, run the built TypeScript core through the recipe of
    each of the three sheets (play, forward, mixed) and compare its lines
    with the sheet, from ``state 0`` on, plus the sheet's ``briefing I
    frames D`` line.

Flow:
    1. Read the mission list; for each mission export its bundle with
       ``jedimaster.briefing`` and read the sheets' ``.teams`` file.
    2. Hand every job of the mission to ``page/scripts/briefing-sheet.ts``
       (one Node process per mission, on the built page: ``npm run build``).
    3. Compare line by line; print the first difference of each sheet that
       differs, then one summary line.

Invariants:
    - The sheets folder, the mission list and the install are required;
      nothing from the game is written into this tool.
    - A mission or a sheet that cannot be read, or a Node run that fails,
      counts every sheet it covers as different.
    - Exit status 0 only when every sheet is identical.

Call:
    ``python tools/compare_briefings.py --sheets DIR --missions FILE --install DIR``
"""

from __future__ import annotations

import argparse
import json
import logging
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.briefing import build_bundle  # noqa: E402
from jedimaster.mission import MissionFormatError  # noqa: E402
from jedimaster.mission import read_mission  # noqa: E402

logger = logging.getLogger("compare_briefings")

PAGE = HERE.parents[1] / "page"
SCRIPT = "scripts/briefing-sheet.ts"
TEAM_LINE = re.compile(r"^team (\d+) briefing=(\d+) frames=(\d+)$")
FORWARD = "run 1" + " forward run 1" * 40
MIXED = (
    "run 100 stop run 20 play run 30 rewind run 10 forward run 5 stop forward"
    " play run 5 rewind forward forward run 3 stop rewind run 4 play run 6"
)
KINDS = ("play", "forward", "mixed")
EXTRA_FRAMES = 48


def recipe(kind: str, frames: int) -> str:
    """Return the steps the sheet of ``kind`` was made with, for a briefing of ``frames``."""
    if kind == "play":
        return f"run {frames + EXTRA_FRAMES}"
    return FORWARD if kind == "forward" else MIXED


def sheet_name(mission: str) -> str:
    """Return a mission path as the sheets name it: slashes become underscores."""
    return mission.replace("/", "_")


def first_difference(expected: list[str], got: list[str]) -> str:
    """Return a one-line account of the first line where the two lists differ."""
    for index, (want, have) in enumerate(zip(expected, got, strict=False)):
        if want != have:
            return f"line {index}: sheet {want[:150]!r} / player {have[:150]!r}"
    return f"lengths differ: sheet {len(expected)} lines, player {len(got)}"


def sheet_lines(text: str) -> tuple[str, list[str]]:
    """Return a sheet's ``briefing`` line and its lines from ``state 0`` on.

    Raises ``ValueError`` for a sheet with no ``briefing`` or ``state 0`` line.
    """
    lines = text.splitlines()
    header = next((ln for ln in lines if ln.startswith("briefing ")), None)
    start = next((i for i, ln in enumerate(lines) if ln.startswith("state 0 ")), None)
    if header is None or start is None:
        raise ValueError("no briefing line or no state 0")
    return header, lines[start:]


def jobs_for(sheets: Path, mission: str) -> list[dict]:
    """Return the jobs of a mission: one per team with a briefing and per sheet kind."""
    teams = (sheets / f"{sheet_name(mission)}.teams").read_text(encoding="latin-1")
    jobs = []
    for line in teams.splitlines():
        found = TEAM_LINE.match(line)
        if found is None:
            continue
        team, _, frames = (int(g) for g in found.groups())
        for kind in KINDS:
            name = f"{sheet_name(mission)}.t{team}.{kind}"
            jobs.append({"name": name, "team": team, "steps": recipe(kind, frames)})
    return jobs


def run_node(bundle_path: Path, jobs: list[dict], work: Path) -> dict:
    """Run the built player on ``jobs``; return its JSON answer.

    Raises ``RuntimeError`` when Node fails.
    """
    request = work / "jobs.json"
    request.write_text(json.dumps({"bundle": str(bundle_path), "jobs": jobs}))
    proc = subprocess.run(
        ["node", SCRIPT, str(request)],
        cwd=PAGE,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip()[-400:])
    return json.loads(proc.stdout)


def check_mission(
    sheets: Path, install: Path, mission: str, work: Path
) -> tuple[int, int]:
    """Compare every sheet of one mission; return (same, total).

    Prints the first difference of each sheet that differs. A failure to
    read or run counts every sheet of the mission as different.
    """
    jobs: list[dict] = []
    try:
        jobs = jobs_for(sheets, mission)
        bundle = build_bundle(install, read_mission(install / mission))
        bundle_path = work / "bundle.json"
        bundle_path.write_text(json.dumps(bundle, ensure_ascii=False), encoding="utf-8")
        answers = run_node(bundle_path, jobs, work)
    except (OSError, ValueError, RuntimeError, MissionFormatError) as exc:
        print(f"ERROR {mission}: {exc}")
        return 0, max(len(jobs), len(KINDS))
    same = 0
    for job in jobs:
        name = job["name"]
        try:
            text = (sheets / name).read_text(encoding="latin-1")
        except OSError as exc:
            print(f"ERROR {name}: {exc}")
            continue
        header, expected = sheet_lines(text)
        got = answers[name]
        mine = [got["header"], *got["lines"]]
        want = [header, *expected]
        if mine == want:
            same += 1
        else:
            print(f"DIFF {name}: {first_difference(want, mine)}")
    return same, len(jobs)


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when every sheet is identical."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sheets", required=True, help="the answer sheets' folder")
    parser.add_argument("--missions", required=True, help="the list of missions")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--only", help="only missions whose path contains this text")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    sheets, install = Path(args.sheets), Path(args.install)
    missions = [
        line.strip()
        for line in Path(args.missions).read_text().splitlines()
        if line.strip() and (args.only is None or args.only in line)
    ]
    same = total = 0
    with tempfile.TemporaryDirectory() as folder:
        for mission in missions:
            got, count = check_mission(sheets, install, mission, Path(folder))
            same, total = same + got, total + count
    print(f"{same} of {total} sheets identical")
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
