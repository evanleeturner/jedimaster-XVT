#!/usr/bin/env python3
"""Keep the numbers and the issue list of the AI player guide true.

Purpose:
    docs/ai/README.md tells players how the computer pilots fly. Two parts of
    it are counted, not written: a table of numbers about the work (commits,
    lines, tests) and a list of the open issues that carry the ``ai`` label.
    This tool recomputes them, so the page cannot quietly go out of date.

Flow:
    --write
      1. count every number from the git history and the tree
      2. ask ``gh`` for the open and the closed issues labelled ``ai``
      3. rewrite the two marked blocks of the page and nothing else
    --check
      1. count every number the same way
      2. read the numbers block from the page
      3. print each number that differs and exit 1 if any does

Invariants:
    - only the text between the ``ai-numbers`` markers and between the
      ``ai-issues`` markers is ever changed
    - a shallow clone gives wrong commit counts, so both modes refuse one
      (exit 2) instead of passing; check the tree out with full history
    - the two commit counts may trail the real count by 1: a commit cannot
      name itself, so the commit that refreshes the page leaves it one behind
    - stdlib only, apart from the ``gh`` command for the issue list

Does not check the issue list: that needs the network and a login, so
``--check`` leaves it alone; run ``--write`` to refresh it.

Call: ai_guide.py (--write | --check) [--repo PATH] [--page PATH]
          [--log-level LEVEL]
  -> exit 0 current, 1 stale numbers, 2 shallow clone, git or gh failed
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime
import json
import logging
import re
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

REPO_DEFAULT = Path(__file__).resolve().parents[2]
PAGE_DEFAULT = Path("docs/ai/README.md")
REMOVAL_COMMIT = "7d2a109"  # the commit that deleted the 1997 computer pilots
ISSUE_LABEL = "ai"

NUMBERS_START = "<!-- ai-numbers:start -->"
NUMBERS_END = "<!-- ai-numbers:end -->"
ISSUES_START = "<!-- ai-issues:start -->"
ISSUES_END = "<!-- ai-issues:end -->"

# Run in a child process with the repo's ai/ folder as the working directory,
# so the package under test is the repo's own and nothing is cached here.
ORDERS_PROGRAM = (
    "import sys; sys.path.insert(0, '.'); "
    "from jedimaster_ai import orders; "
    "from jedimaster_ai.behaviors import drift; "
    "print(sum(b is not drift.think for b in orders.BEHAVIORS.values()), "
    "len(orders.BEHAVIORS))"
)

ENGINE_AI_FILE = re.compile(r"/ai_[a-z_]+\.(c|h)$")
CHECK_FUNCTION = re.compile(rb"^static void check_", re.MULTILINE)
TEST_FUNCTION = re.compile(rb"^\s*def test_", re.MULTILINE)
INTEGER = re.compile(r"\d[\d,]*")


class GuideError(Exception):
    """The repo cannot give trustworthy numbers (shallow, missing, no gh)."""


@dataclasses.dataclass(frozen=True)
class Number:
    """One row of the numbers table.

    ``values`` are the counted integers; ``text`` is how the cell shows them;
    ``trails`` allows the page to be 1 behind (the commit counts).
    """

    label: str
    values: tuple[int, ...]
    text: str
    how: str
    trails: bool = False


def run(command: list[str], cwd: Path) -> str:
    """Return the standard output of ``command`` or raise GuideError."""
    logger.debug("run %s in %s", command, cwd)
    try:
        done = subprocess.run(
            command, cwd=cwd, capture_output=True, check=True, text=True
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise GuideError(f"{' '.join(command)} failed: {error}") from error
    return done.stdout


def count_lines(paths: list[Path]) -> int:
    """Return the newline count of all ``paths``, as ``cat ... | wc -l``."""
    return sum(path.read_bytes().count(b"\n") for path in paths)


def tracked(repo: Path, *pathspec: str) -> list[str]:
    """Return the tracked file names matching ``pathspec``, as git lists them."""
    out = run(["git", "ls-files", *pathspec], repo)
    return [line for line in out.splitlines() if line]


def commit_count(repo: Path, *args: str) -> int:
    """Return how many commits ``git log --oneline <args>`` lists."""
    return len(run(["git", "log", "--oneline", *args], repo).splitlines())


def refuse_shallow(repo: Path) -> None:
    """Raise GuideError when the clone lacks full history."""
    answer = run(["git", "rev-parse", "--is-shallow-repository"], repo).strip()
    if answer != "false":
        raise GuideError(
            "this is a shallow clone, so commit counts would be wrong; "
            "fetch full history (actions/checkout fetch-depth: 0)"
        )


def commas(value: int) -> str:
    """Return ``value`` with thousands separators."""
    return f"{value:,}"


def compute_numbers(repo: Path, removal: str = REMOVAL_COMMIT) -> list[Number]:
    """Return every number of the table, counted from ``repo``."""
    refuse_shallow(repo)
    package = repo / "ai" / "jedimaster_ai"

    pilot_commits = commit_count(repo, "--", "ai/jedimaster_ai")
    titled = commit_count(repo, "--grep=^AI:")
    program_files = sorted(package.glob("*.py")) + sorted(
        (package / "behaviors").glob("*.py")
    )
    program_lines = count_lines(program_files)
    test_files = sorted((repo / "ai" / "tests").glob("*.py"))
    test_lines = count_lines(test_files)
    test_functions = sum(
        len(TEST_FUNCTION.findall(path.read_bytes())) for path in test_files
    )
    behavior_files = [
        path
        for path in (package / "behaviors").glob("*.py")
        if path.name != "__init__.py"
    ]
    written, total = (
        int(word)
        for word in run([sys.executable, "-c", ORDERS_PROGRAM], repo / "ai").split()
    )
    events = len(json.loads((package / "events.json").read_text("utf-8"))["events"])

    engine_c = [
        name
        for name in tracked(repo)
        if ENGINE_AI_FILE.search(name) and not name.startswith("engine/tests")
    ]
    engine_lines = count_lines([repo / name for name in engine_c])
    engine_tests = tracked(repo, "engine/tests/test_ai_*.c")
    engine_checks = sum(
        len(CHECK_FUNCTION.findall((repo / name).read_bytes())) for name in engine_tests
    )
    flight = repo / "engine" / "tests" / "test_flight.c"
    wingman_checks = len(
        re.findall(rb"^static void check_order_", flight.read_bytes(), re.MULTILINE)
    )

    old_files = run(
        [
            "git",
            "ls-tree",
            "-r",
            "--name-only",
            f"{removal}^",
            "engine/src/xvt/flight/ai",
        ],
        repo,
    ).split()
    old_lines = sum(
        run(["git", "show", f"{removal}^:{name}"], repo).count("\n")
        for name in old_files
    )

    numbers = [
        Number(
            "Commits that changed the pilot program",
            (pilot_commits,),
            commas(pilot_commits),
            "`git log --oneline -- ai/jedimaster_ai`, counted with `wc -l`",
            trails=True,
        ),
        Number(
            "Commits whose title starts with `AI:` (the game engine's side too)",
            (titled,),
            commas(titled),
            "`git log --oneline --grep='^AI:'`, counted with `wc -l`",
            trails=True,
        ),
        Number(
            "Lines of Python in the pilot program",
            (program_lines,),
            commas(program_lines),
            "the `.py` files in `ai/jedimaster_ai/` and `ai/jedimaster_ai/behaviors/`,"
            " counted with `wc -l`",
        ),
        Number(
            "Lines of Python in its tests",
            (test_lines,),
            commas(test_lines),
            "the `.py` files in `ai/tests/`, counted with `wc -l`",
        ),
        Number(
            "Test functions in those tests",
            (test_functions,),
            commas(test_functions),
            "`grep -rhE '^\\s*def test_' ai/tests`, counted with `wc -l`; a test"
            " that runs on several inputs counts once here",
        ),
        Number(
            "Files that each hold one kind of behavior",
            (len(behavior_files),),
            commas(len(behavior_files)),
            "the `.py` files in `ai/jedimaster_ai/behaviors/` except `__init__.py`, "
            "with the files of orders whose behavior is not written yet",
        ),
        Number(
            "Mission order codes that have a written behavior",
            (written, total),
            f"{written} of {total}",
            "`OrderCode` and `BEHAVIORS` in `ai/jedimaster_ai/orders.py`; the rest"
            " keep their speed and heading",
        ),
        Number(
            "Kinds of line the pilot program writes to its log",
            (events,),
            commas(events),
            "the entries in `ai/jedimaster_ai/events.json`",
        ),
        Number(
            "Lines of C in the engine's computer-pilot code",
            (engine_lines, len(engine_c)),
            f"{commas(engine_lines)} in {len(engine_c)} files",
            "the files named `ai_*.c` and `ai_*.h` under `engine/`, outside"
            " `engine/tests/`",
        ),
        Number(
            "Test files and checks for that engine code",
            (len(engine_tests), engine_checks),
            f"{len(engine_tests)} files, {engine_checks} checks",
            "`engine/tests/test_ai_*.c`; a check is a function named `check_...`",
        ),
        Number(
            "Checks of the wingman commands (Shift+A and the others)",
            (wingman_checks,),
            commas(wingman_checks),
            "the `check_order_...` functions in `engine/tests/test_flight.c`",
        ),
        Number(
            "Lines of the 1997 pilot code that were deleted",
            (old_lines, len(old_files)),
            f"{commas(old_lines)} in {len(old_files)} files",
            f"`engine/src/xvt/flight/ai/` just before commit `{removal}`",
        ),
    ]
    for number in numbers:
        logger.debug("number %s = %s", number.label, number.values)
    logger.info("counted %d numbers", len(numbers))
    return numbers


def render_numbers(numbers: list[Number]) -> str:
    """Return the table that goes between the numbers markers."""
    lines = ["| What it counts | Today | How to check it |", "| --- | --- | --- |"]
    lines += [f"| {n.label} | {n.text} | {n.how} |" for n in numbers]
    return "\n".join(lines)


def block_of(text: str, start: str, end: str) -> str:
    """Return the text between the markers, raising GuideError if absent."""
    if text.count(start) != 1 or text.count(end) != 1:
        raise GuideError(f"the page needs exactly one {start} and one {end}")
    _, _, rest = text.partition(start)
    body, _, _ = rest.partition(end)
    return body.strip("\n")


def replace_block(text: str, start: str, end: str, body: str) -> str:
    """Return ``text`` with the span between the markers replaced by ``body``."""
    block_of(text, start, end)
    head, _, rest = text.partition(start)
    _, _, tail = rest.partition(end)
    return f"{head}{start}\n{body}\n{end}{tail}"


def page_values(block: str) -> dict[str, tuple[int, ...]]:
    """Return label -> integers of the Today cell for each table row."""
    found: dict[str, tuple[int, ...]] = {}
    for line in block.splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 3 or set(cells[0]) <= set("- ") or cells[0].startswith("What"):
            continue
        found[cells[0]] = tuple(
            int(word.replace(",", "")) for word in INTEGER.findall(cells[1])
        )
    return found


def stale_numbers(numbers: list[Number], page_text: str) -> list[str]:
    """Return one sentence per number whose page value is wrong or missing."""
    page = page_values(block_of(page_text, NUMBERS_START, NUMBERS_END))
    problems = []
    for number in numbers:
        shown = page.get(number.label)
        if shown is None:
            problems.append(f"missing from the page: {number.label}")
            continue
        slack = 1 if number.trails else 0
        ok = len(shown) == len(number.values) and all(
            0 <= actual - old <= slack
            for old, actual in zip(shown, number.values, strict=True)
        )
        logger.debug(
            "compare %s page=%s now=%s ok=%s", number.label, shown, number.values, ok
        )
        if not ok:
            problems.append(
                f"{number.label}: the page says {shown}, the repo has {number.values}"
            )
    extra = set(page) - {n.label for n in numbers}
    problems += [
        f"on the page but no longer counted: {label}" for label in sorted(extra)
    ]
    return problems


def fetch_issues(repo: Path) -> tuple[list[dict[str, object]], int]:
    """Return the open ``ai`` issues (number, title, url) and the closed count."""
    base = ["gh", "issue", "list", "--label", ISSUE_LABEL, "--limit", "1000"]
    opened = json.loads(
        run([*base, "--state", "open", "--json", "number,title,url"], repo)
    )
    closed = json.loads(run([*base, "--state", "closed", "--json", "number"], repo))
    opened.sort(key=lambda issue: issue["number"])
    logger.debug("issues open=%s closed=%d", [i["number"] for i in opened], len(closed))
    return opened, len(closed)


def render_issues(opened: list[dict[str, object]], closed: int, today: str) -> str:
    """Return the list that goes between the issues markers."""
    lines = [
        f"On {today} (UTC) the label `{ISSUE_LABEL}` was on {len(opened)} open"
        f" issues and {closed} closed ones. This list was made by"
        " `ai/tools/ai_guide.py --write`; run it again to refresh the list.",
        "",
    ]
    lines += [
        f"- [#{issue['number']}]({issue['url']}): {issue['title']}" for issue in opened
    ]
    return "\n".join(lines)


def write_page(repo: Path, page: Path) -> None:
    """Rewrite both marked blocks of ``page`` from the repo and GitHub."""
    numbers = compute_numbers(repo)
    opened, closed = fetch_issues(repo)
    today = datetime.datetime.now(datetime.UTC).date().isoformat()
    text = page.read_text("utf-8")
    text = replace_block(text, NUMBERS_START, NUMBERS_END, render_numbers(numbers))
    text = replace_block(
        text, ISSUES_START, ISSUES_END, render_issues(opened, closed, today)
    )
    page.write_text(text, "utf-8")
    logger.info("wrote %s: %d numbers, %d open issues", page, len(numbers), len(opened))


def check_page(repo: Path, page: Path) -> list[str]:
    """Return the problems with the page's numbers block (empty when current)."""
    problems = stale_numbers(compute_numbers(repo), page.read_text("utf-8"))
    logger.info("checked %s: %d stale", page, len(problems))
    return problems


def main(argv: list[str] | None = None) -> int:
    """Run one mode and return the exit code."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument("--repo", type=Path, default=REPO_DEFAULT)
    parser.add_argument("--page", type=Path, default=None)
    parser.add_argument("--log-level", default="INFO")
    options = parser.parse_args(argv)
    logging.basicConfig(level=options.log_level, format="%(levelname)s %(message)s")
    repo = options.repo
    page = options.page or repo / PAGE_DEFAULT
    try:
        if options.write:
            write_page(repo, page)
            return 0
        problems = check_page(repo, page)
    except GuideError as error:
        logger.error("%s", error)
        return 2
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
