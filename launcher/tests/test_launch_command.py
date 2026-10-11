"""The launch command: the description it writes, the engine it starts, the line it prints.

Purpose:
    Prove ``python -m jedimaster launch`` against a stand-in engine (a small
    Python program that reads the description and writes a result the way the
    real engine does): the description's content, the extra arguments passed
    on, the result kept or not, the one line printed, every exit status, and
    each way the result cannot be trusted.

Flow:
    ``engine`` writes the stand-in into a temporary folder; its pilot name
    picks what it does. Each test runs the command through ``main`` or
    ``fly``.

Invariants:
    - No game data and no real engine.

Call:
    ``pytest tests/test_launch_command.py``
"""

from __future__ import annotations

import json
import logging
import stat
import sys
from pathlib import Path

import pytest

from jedimaster.__main__ import main
from jedimaster.launch.cli import summary
from jedimaster.launch.model import build_description
from jedimaster.launch.run import fly
from jedimaster.launch.run import LaunchError

logger = logging.getLogger(__name__)

STAND_IN = """#!__PYTHON__
import json, sys, time
args = sys.argv[1:]
description = json.load(open(args[1]))
open(args[1] + ".argv", "w").write(json.dumps(args))
pilot = description["pilot"]
result = {"schema_version": 1, "status": "flown", "exit_code": 0, "outcome": "completed",
          "score": 120, "kills": 4, "losses": 1, "flight_seconds": 305, "why": ""}
code = 0
if pilot == "Refused":
    result.update(status="refused", exit_code=3, outcome="none", score=0, kills=0,
                  losses=0, flight_seconds=0, why="The pilot is not among the pilots.")
    code = 3
if pilot == "Unchecked":
    result.update(status="refused", exit_code=3, outcome="none", score=0, kills=0,
                  losses=0, flight_seconds=0, why="A name holds a flight switch word.")
    open(args[1] + ".result.json", "w").write(json.dumps(result))
    sys.exit(3)
if pilot == "Failed":
    result.update(status="failed", exit_code=5, outcome="none", why="The flight failed.")
    code = 5
if pilot == "Mismatch":
    code = 4
if pilot == "NoResult":
    sys.exit(0)
if pilot == "BadJson":
    open(description["result"], "w").write("{not json")
    sys.exit(0)
if pilot == "BadSchema":
    result["kills"] = -5
if pilot == "Slow":
    time.sleep(30)
open(description["result"], "w").write(json.dumps(result))
sys.exit(code)
"""


@pytest.fixture
def engine(tmp_path: Path) -> str:
    """Return the path of an executable stand-in engine."""
    path = tmp_path / "engine"
    path.write_text(STAND_IN.replace("__PYTHON__", sys.executable), encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return str(path)


def command(engine: str, pilot: str = "Ace", *extra: str) -> list[str]:
    """Return the launch command's arguments for a training battle."""
    return [
        "launch", "--type", "training", "--side", "rebel", "--mission", "1ta02bz.tie",
        "--pilot", pilot, "--engine", engine, *extra,
    ]  # fmt: skip


def test_the_description_is_built_from_the_arguments():
    doc = build_description("combat", "imperial", "8braw10.tie", "Ace", seed=9)
    assert doc == {
        "schema_version": 1,
        "mode": "single_player",
        "game_type": "combat",
        "side": "imperial",
        "mission": "combat/8braw10.tie",
        "pilot": "Ace",
        "seed": 9,
    }
    assert build_description("melee", None, "melee/a.tie", "Ace")["side"] == "none"
    assert (
        build_description("melee", "none", "a.tie", "Ace", result="r.json")["result"]
        == "r.json"
    )


MISFITS = [
    ("duel", "rebel", "a.tie"),
    ("training", None, "a.tie"),
    ("training", "none", "a.tie"),
    ("combat", None, "a.tie"),
    ("campaign", None, "a.lst"),
    ("campaign", "none", "a.lst"),
    ("combat", "hutt", "a.tie"),
    ("melee", "rebel", "a.tie"),
    ("melee", None, "train/a.tie"),
]


def test_a_description_that_does_not_fit_is_refused():
    for game_type, side, mission in MISFITS:
        with pytest.raises(ValueError):
            build_description(game_type, side, mission, "Ace")


FOLDER_CASES = [
    ("training", "train"),
    ("melee", "melee"),
    ("tournament", "tourn"),
    ("combat", "combat"),
    ("battle", "battle"),
    ("campaign", "campaign"),
]


def test_each_game_type_has_the_engines_folder():
    for game_type, folder in FOLDER_CASES:
        side = "rebel" if game_type in ("training", "combat", "campaign") else None
        assert (
            build_description(game_type, side, "a.tie", "Ace")["mission"]
            == f"{folder}/a.tie"
        )
        assert (
            build_description(game_type, side, f"{folder}/a.tie", "Ace")["mission"]
            == f"{folder}/a.tie"
        )


def test_a_seed_of_zero_is_kept():
    assert build_description("melee", None, "a.tie", "Ace", seed=0)["seed"] == 0
    assert "seed" not in build_description("melee", None, "a.tie", "Ace")
    settings = {"difficulty": "hard"}
    assert (
        build_description("melee", None, "a.tie", "Ace", settings=settings)["settings"]
        == settings
    )
    assert "settings" not in build_description(
        "melee", None, "a.tie", "Ace", settings={}
    )


def test_a_flown_battle_prints_one_line_and_exits_zero(engine, capsys):
    assert main(command(engine)) == 0
    assert (
        capsys.readouterr().out
        == "flown: completed, score 120, kills 4, losses 1, 305 s\n"
    )


def test_the_engine_gets_the_description_and_the_extra_arguments(
    engine, tmp_path, capsys
):
    keep = tmp_path / "kept.json"
    code = main(command(engine, "Ace", "--result", str(keep), "--engine-arg=--game-data",
                        "--engine-arg=/data", "--seed", "77"))  # fmt: skip
    assert code == 0
    capsys.readouterr()
    assert json.loads(keep.read_text())["status"] == "flown"


def test_fly_hands_over_a_checked_description(engine, tmp_path):
    keep = tmp_path / "r.json"
    doc = build_description("training", "rebel", "1ta02bz.tie", "Ace", seed=5)
    result, code = fly(engine, doc, ["--game-data", "/data"], str(keep))
    assert code == 0
    assert result["score"] == 120
    assert keep.exists()


def test_the_engine_sees_the_arguments_in_order(tmp_path):
    seen = tmp_path / "seen.json"
    script = tmp_path / "echo"
    script.write_text(
        f"#!{sys.executable}\nimport json, sys\n"
        f"d = json.load(open(sys.argv[2]))\n"
        f"json.dump({{'args': sys.argv[1:2] + sys.argv[3:], 'description': d}}, open({str(seen)!r}, 'w'))\n"
        "json.dump({'schema_version': 1, 'status': 'refused', 'exit_code': 3, 'outcome': 'none',"
        " 'score': 0, 'kills': 0, 'losses': 0, 'flight_seconds': 0, 'why': 'x'}, open(d['result'], 'w'))\n"
        "sys.exit(3)\n",
        encoding="utf-8",
    )
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    doc = build_description("training", "rebel", "1ta02bz.tie", "Ace", seed=5)
    result, code = fly(str(script), doc, ["--game-data", "/data"])
    assert (code, result["status"]) == (3, "refused")
    record = json.loads(seen.read_text())
    assert record["args"] == ["--launch", "--game-data", "/data"]
    assert record["description"]["pilot"] == "Ace"
    assert record["description"]["seed"] == 5
    assert record["description"]["result"].endswith("result.json")


def test_the_engines_exit_code_is_the_commands(engine, capsys):
    cases = [("Refused", 3, "refused: The pilot is not among the pilots."),
             ("Failed", 5, "failed: The flight failed.")]  # fmt: skip
    for pilot, code, line in cases:
        assert main(command(engine, pilot)) == code
        assert capsys.readouterr().out == line + "\n"


def test_a_refusal_written_beside_the_description_is_read(engine, tmp_path, capsys):
    assert main(command(engine, "Unchecked")) == 3
    assert capsys.readouterr().out == "refused: A name holds a flight switch word.\n"
    doc = build_description("training", "rebel", "1ta02bz.tie", "Unchecked")
    result, code = fly(engine, doc, result=str(tmp_path / "r.json"))
    assert (code, result["status"]) == (3, "refused")


def test_a_result_left_by_an_earlier_run_is_never_read(engine, tmp_path):
    keep = tmp_path / "r.json"
    fly(
        engine,
        build_description("training", "rebel", "1ta02bz.tie", "Ace"),
        result=str(keep),
    )
    doc = build_description("training", "rebel", "1ta02bz.tie", "NoResult")
    with pytest.raises(LaunchError, match="no result file"):
        fly(engine, doc, result=str(keep))


def test_a_result_that_cannot_be_trusted_exits_one(engine, capsys):
    for pilot in ("NoResult", "BadJson", "BadSchema", "Mismatch"):
        assert main(command(engine, pilot)) == 1, pilot
        printed = capsys.readouterr().out
        assert ("flown:" in printed) == (pilot == "Mismatch"), pilot


def test_an_engine_that_runs_too_long_is_stopped(engine):
    doc = build_description("training", "rebel", "1ta02bz.tie", "Slow")
    with pytest.raises(LaunchError, match="ran past"):
        fly(engine, doc, timeout=0.5)


def test_an_engine_that_cannot_start_is_an_error(tmp_path):
    doc = build_description("training", "rebel", "1ta02bz.tie", "Ace")
    with pytest.raises(LaunchError, match="could not be started"):
        fly(str(tmp_path / "missing"), doc)


def test_a_description_that_breaks_its_schema_never_reaches_the_engine(engine):
    doc = build_description("training", "rebel", "1ta02bz.tie", "A" * 20)
    with pytest.raises(LaunchError, match="at most 12"):
        fly(engine, doc)


def test_bad_arguments_exit_two(engine, capsys):
    assert main(["launch", "--type", "melee", "--side", "rebel", "--mission", "a.tie",
                 "--pilot", "Ace", "--engine", engine]) == 2  # fmt: skip
    assert main(["launch", "--type", "training", "--mission", "a.tie",
                 "--pilot", "Ace", "--engine", engine]) == 2  # fmt: skip
    capsys.readouterr()


def test_the_summary_line_for_each_status():
    base = {"why": "Because.", "outcome": "not_completed", "score": 0, "kills": 0,
            "losses": 2, "flight_seconds": 9}  # fmt: skip
    assert (
        summary({**base, "status": "flown"})
        == "flown: not completed, score 0, kills 0, losses 2, 9 s"
    )
    assert summary({**base, "status": "not_reached"}) == "not reached: Because."
    assert summary({**base, "status": "refused"}) == "refused: Because."
