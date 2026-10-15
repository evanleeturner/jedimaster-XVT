"""The AI player guide's numbers: each count on a made-up repo, and the real page.

The made-up repo is built with git in a temporary folder, so every expected
value below is counted by hand from the files the test writes.
"""

from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys

import pytest

TOOL = pathlib.Path(__file__).resolve().parents[1] / "tools" / "ai_guide.py"
spec = importlib.util.spec_from_file_location("ai_guide", TOOL)
ai_guide = importlib.util.module_from_spec(spec)
sys.modules["ai_guide"] = ai_guide
spec.loader.exec_module(ai_guide)

REPO = pathlib.Path(__file__).resolve().parents[2]

ORDERS = """\
import types
from jedimaster_ai.behaviors import drift
from jedimaster_ai.behaviors import hold

BEHAVIORS = types.MappingProxyType({1: drift.think, 2: hold.think, 3: hold.think})
"""


def git(repo: pathlib.Path, *args: str) -> str:
    """Run git in ``repo`` with a fixed identity and return its output."""
    done = subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@example.com", *args],
        cwd=repo,
        capture_output=True,
        check=True,
        text=True,
    )
    return done.stdout.strip()


def write(repo: pathlib.Path, name: str, text: str) -> None:
    """Write ``text`` to ``repo/name``, making folders."""
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, "utf-8")


@pytest.fixture(scope="module")
def made_up(tmp_path_factory: pytest.TempPathFactory) -> tuple[pathlib.Path, str]:
    """Return a small repo and the hash of the commit that deleted old code."""
    repo = tmp_path_factory.mktemp("repo")
    git(repo, "init", "-q")
    old = "engine/src/xvt/flight/ai"
    write(repo, f"{old}/one.c", "a\nb\nc\n")
    write(repo, f"{old}/two.c", "d\n")
    git(repo, "add", "-A")
    git(repo, "commit", "-qm", "AI: old pilots")
    git(repo, "rm", "-q", "-r", old)
    write(repo, "ai/jedimaster_ai/__init__.py", "")
    write(repo, "ai/jedimaster_ai/orders.py", ORDERS)
    write(repo, "ai/jedimaster_ai/weave.py", "x = 1\ny = 2\n")
    write(repo, "ai/jedimaster_ai/behaviors/__init__.py", "")
    write(repo, "ai/jedimaster_ai/behaviors/drift.py", "def think():\n    pass\n")
    write(repo, "ai/jedimaster_ai/behaviors/hold.py", "def think():\n    pass\n")
    write(repo, "ai/jedimaster_ai/events.json", '{"events": [{"a": 1}, {"a": 2}]}')
    write(
        repo,
        "ai/tests/test_a.py",
        "def test_one():\n    pass\n\n\ndef test_two():\n    pass\n",
    )
    write(
        repo,
        "ai/tests/test_b.py",
        "class T:\n    def test_three(self):\n        pass\n",
    )
    write(repo, "engine/src/xvt/flight/ai_brain.c", "1\n2\n3\n4\n")
    write(repo, "engine/src/xvt/flight/ai_brain.h", "1\n")
    write(repo, "engine/src/xvt/flight/not_ai.c", "1\n2\n")
    write(repo, "engine/tests/ai_in_tests.c", "1\n")
    write(
        repo,
        "engine/tests/test_ai_a.c",
        "static void check_x(void)\nstatic void helper(void)\n",
    )
    write(
        repo,
        "engine/tests/test_ai_b.c",
        "static void check_y(void)\nstatic void check_z(void)\n",
    )
    write(
        repo,
        "engine/tests/test_flight.c",
        "static void check_order_a(void)\nstatic void check_other(void)\nstatic void check_order_b(void)\n",
    )
    git(repo, "add", "-A")
    git(repo, "commit", "-qm", "AI: new pilots")
    removal = git(repo, "rev-parse", "--short", "HEAD")
    write(repo, "ai/jedimaster_ai/weave.py", "x = 1\n")
    git(repo, "add", "-A")
    git(repo, "commit", "-qm", "later tweak")
    return repo, removal


@pytest.fixture(scope="module")
def counted(made_up: tuple[pathlib.Path, str]) -> dict[str, tuple[int, ...]]:
    """Return label -> values for the made-up repo."""
    repo, _ = made_up
    # The deleting commit is the second one; its parent holds the old files.
    removal = git(repo, "rev-parse", "--short", "HEAD~1")
    numbers = ai_guide.compute_numbers(repo, removal=removal)
    return {n.label: n.values for n in numbers}


def test_commits_touching_the_pilot_program(counted) -> None:
    assert counted["Commits that changed the pilot program"] == (2,)


def test_commits_titled_ai(counted) -> None:
    label = "Commits whose title starts with `AI:` (the game engine's side too)"
    assert counted[label] == (2,)


def test_program_lines_count_main_and_behavior_files(counted) -> None:
    # orders 5, __init__ 0, weave 1, behaviors: drift 2 + hold 2
    assert counted["Lines of Python in the pilot program"] == (10,)


def test_test_lines_and_functions(counted) -> None:
    assert counted["Lines of Python in its tests"] == (9,)
    assert counted["Test functions in those tests"] == (3,)


def test_behavior_files_skip_the_package_marker(counted) -> None:
    assert counted["Files that each hold one kind of behavior"] == (2,)


def test_order_codes_with_a_written_behavior(counted) -> None:
    assert counted["Mission order codes that have a written behavior"] == (2, 3)


def test_log_events(counted) -> None:
    assert counted["Kinds of line the pilot program writes to its log"] == (2,)


def test_engine_c_lines_skip_tests_and_other_names(counted) -> None:
    assert counted["Lines of C in the engine's computer-pilot code"] == (5, 2)


def test_engine_test_files_and_checks(counted) -> None:
    assert counted["Test files and checks for that engine code"] == (2, 3)


def test_wingman_checks(counted) -> None:
    assert counted["Checks of the wingman commands (Shift+A and the others)"] == (2,)


def test_deleted_pilot_code(counted) -> None:
    assert counted["Lines of the 1997 pilot code that were deleted"] == (4, 2)


def make_page(numbers, body_override: str | None = None) -> str:
    """Return a page whose numbers block holds ``numbers`` rendered."""
    body = body_override or ai_guide.render_numbers(numbers)
    return (
        f"top\n{ai_guide.NUMBERS_START}\n{body}\n{ai_guide.NUMBERS_END}\n"
        f"mid\n{ai_guide.ISSUES_START}\nold\n{ai_guide.ISSUES_END}\nbottom\n"
    )


def numbers_of(made_up) -> list:
    repo, _ = made_up
    removal = git(repo, "rev-parse", "--short", "HEAD~1")
    return ai_guide.compute_numbers(repo, removal=removal)


def test_a_fresh_block_is_current(made_up) -> None:
    numbers = numbers_of(made_up)
    assert ai_guide.stale_numbers(numbers, make_page(numbers)) == []


def test_a_changed_line_count_is_named(made_up) -> None:
    numbers = numbers_of(made_up)
    body = ai_guide.render_numbers(numbers).replace("| 10 |", "| 11 |")
    problems = ai_guide.stale_numbers(numbers, make_page(numbers, body))
    assert len(problems) == 1
    assert "Lines of Python in the pilot program" in problems[0]


def test_commit_counts_may_trail_by_one_but_not_two(made_up) -> None:
    numbers = numbers_of(made_up)
    row = "| Commits that changed the pilot program | {} |"
    now = numbers[0].values[0]
    page_one = ai_guide.render_numbers(numbers).replace(
        row.format(now), row.format(now - 1)
    )
    assert ai_guide.stale_numbers(numbers, make_page(numbers, page_one)) == []
    page_two = ai_guide.render_numbers(numbers).replace(
        row.format(now), row.format(now - 2)
    )
    assert len(ai_guide.stale_numbers(numbers, make_page(numbers, page_two))) == 1


def test_a_missing_row_is_named(made_up) -> None:
    numbers = numbers_of(made_up)
    body = "\n".join(ai_guide.render_numbers(numbers).splitlines()[:-1])
    problems = ai_guide.stale_numbers(numbers, make_page(numbers, body))
    assert problems == [
        "missing from the page: Lines of the 1997 pilot code that were deleted"
    ]


def test_replace_block_changes_only_its_span() -> None:
    page = make_page([], "x")
    out = ai_guide.replace_block(
        page, ai_guide.ISSUES_START, ai_guide.ISSUES_END, "new"
    )
    assert out == page.replace("old", "new")


def test_a_page_without_markers_is_refused() -> None:
    with pytest.raises(ai_guide.GuideError):
        ai_guide.block_of("nothing here", ai_guide.NUMBERS_START, ai_guide.NUMBERS_END)


def test_issue_list_names_date_counts_and_links() -> None:
    opened = [{"number": 7, "title": "A bug", "url": "https://example.com/7"}]
    text = ai_guide.render_issues(opened, 3, "2026-10-14")
    assert "On 2026-10-14 (UTC)" in text
    assert "1 open issues and 3 closed ones" in text
    assert "- [#7](https://example.com/7): A bug" in text


def test_a_shallow_clone_fails_loudly(made_up, tmp_path) -> None:
    repo, _ = made_up
    shallow = tmp_path / "shallow"
    subprocess.run(
        ["git", "clone", "-q", "--depth", "1", f"file://{repo}", str(shallow)],
        check=True,
    )
    with pytest.raises(ai_guide.GuideError, match="shallow"):
        ai_guide.compute_numbers(shallow)
    assert ai_guide.main(["--check", "--repo", str(shallow)]) == 2


def test_the_real_page_numbers_are_current() -> None:
    """The guide's numbers match this repo; if not, run ai_guide.py --write."""
    problems = ai_guide.check_page(REPO, REPO / ai_guide.PAGE_DEFAULT)
    assert problems == [], "stale numbers: " + "; ".join(problems)
