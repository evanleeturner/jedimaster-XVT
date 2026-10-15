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
import shutil
import stat
import sys
from pathlib import Path

import pytest
from solodata import make_ai_program
from solodata import WIRE

from jedimaster.__main__ import main
from jedimaster.launch.builds import set_current
from jedimaster.launch.check import errors
from jedimaster.launch.cli import sith_clause
from jedimaster.launch.cli import summary
from jedimaster.launch.model import ai_program_of
from jedimaster.launch.model import build_description
from jedimaster.launch.run import fly
from jedimaster.launch.run import LaunchError
from jedimaster.launch.schema import launch_schema

logger = logging.getLogger(__name__)

# The repository's own AI program, which a checkout's launcher points at.
REPO_AI_PROGRAM = Path(__file__).resolve().parents[2] / "ai" / "jedimaster_ai"

STAND_IN = """#!__PYTHON__
import json, sys, time
args = sys.argv[1:]
if args == ["--version"]:
    print("OpenXvT stand-in")
    print("ai_wire=__WIRE__")
    sys.exit(0)
description = json.load(open(args[1]))
open(args[1] + ".argv", "w").write(json.dumps(args))
pilot = description["pilot"]
result = {"schema_version": 2, "status": "flown", "exit_code": 0, "outcome": "completed",
          "score": 120, "kills": 4, "losses": 1, "goals_met_seconds": 305, "why": ""}
code = 0
if pilot == "Refused":
    result.update(status="refused", exit_code=3, outcome="none", score=0, kills=0,
                  losses=0, goals_met_seconds=0, why="The pilot is not among the pilots.")
    code = 3
if pilot == "Unchecked":
    result.update(status="refused", exit_code=3, outcome="none", score=0, kills=0,
                  losses=0, goals_met_seconds=0, why="A name holds a flight switch word.")
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
SITH = {"setting": "lord", "flight_group": 2, "flight_name": "Beta", "craft_type": 6,
        "apprentices_flown": 3, "apprentices_lost": 1, "lord_flown": 1, "lord_lost": 1,
        "why": ""}
if pilot in ("SithLord", "BadName"):
    result.update(schema_version=3, sith=SITH)
if pilot == "BadName":
    result["sith"]["flight_name"] = "Be\u00e9ta"
    raw = json.dumps(result, ensure_ascii=False).encode("latin-1")
    open(description["result"], "wb").write(raw)
    sys.exit(0)
if pilot == "Slow":
    time.sleep(30)
open(description["result"], "w").write(json.dumps(result))
sys.exit(code)
"""


@pytest.fixture
def engine(tmp_path: Path) -> str:
    """Return the path of an executable stand-in engine."""
    path = tmp_path / "engine"
    text = STAND_IN.replace("__PYTHON__", sys.executable)
    path.write_text(text.replace("__WIRE__", str(WIRE)), encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    make_ai_program(tmp_path / "ai" / "jedimaster_ai")
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
        == "flown: completed, score 120, kills 4, losses 1, goals met at 305 s\n"
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
        "json.dump({'schema_version': 2, 'status': 'refused', 'exit_code': 3, 'outcome': 'none',"
        " 'score': 0, 'kills': 0, 'losses': 0, 'goals_met_seconds': 0, 'why': 'x'}, open(d['result'], 'w'))\n"
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


def test_a_version_3_result_prints_the_sith_clause_on_the_one_line(engine, capsys):
    assert main(command(engine, "SithLord")) == 0
    assert capsys.readouterr().out == (
        "flown: completed, score 120, kills 4, losses 1, goals met at 305 s"
        "; Sith flight Beta (TIE Interceptor): the Lord was destroyed, 2 of 3 apprentices lived\n"
    )


def test_a_result_that_is_not_clean_utf8_still_reads_with_a_replacement_mark(
    engine, capsys
):
    assert main(command(engine, "BadName")) == 0
    out = capsys.readouterr().out
    assert "Sith flight Be\ufffdta (TIE Interceptor)" in out


SITH_BASE = {"setting": "lord", "flight_group": 2, "flight_name": "Beta",
             "craft_type": 6, "apprentices_flown": 3, "apprentices_lost": 1,
             "lord_flown": 1, "lord_lost": 1, "why": ""}  # fmt: skip


def test_the_sith_clause_for_each_fate():
    def clause(**changes):
        return sith_clause({**SITH_BASE, **changes})

    assert clause(setting="none") == ""
    assert clause(setting="none", flight_group=-1) == ""
    assert clause() == (
        "; Sith flight Beta (TIE Interceptor): the Lord was destroyed, "
        "2 of 3 apprentices lived"
    )
    assert clause(lord_lost=0, apprentices_lost=0) == (
        "; Sith flight Beta (TIE Interceptor): the Lord lived, all 3 apprentices lived"
    )
    assert clause(
        setting="apprentices", lord_flown=0, lord_lost=0, apprentices_lost=3
    ) == ("; Sith flight Beta (TIE Interceptor): all 3 apprentices were destroyed")
    assert clause(
        lord_flown=0, lord_lost=0, apprentices_flown=1, apprentices_lost=1
    ) == ("; Sith flight Beta (TIE Interceptor): the apprentice was destroyed")
    assert clause(
        lord_flown=0, lord_lost=0, apprentices_flown=0, apprentices_lost=0
    ) == ("; Sith flight Beta (TIE Interceptor): no Sith craft arrived")
    assert clause(craft_type=999) == (
        "; Sith flight Beta (craft type 999): the Lord was destroyed, "
        "2 of 3 apprentices lived"
    )
    assert clause(
        flight_group=-1, flight_name="", craft_type=-1, why="No enemy flight."
    ) == ("; no Sith flight: No enemy flight.")
    assert clause(flight_group=-1, flight_name="", craft_type=-1) == "; no Sith flight"


def test_a_version_2_result_has_no_sith_clause_and_a_none_block_adds_nothing():
    base = {"why": "", "outcome": "completed", "score": 1, "kills": 0,
            "losses": 0, "goals_met_seconds": 9, "status": "flown"}  # fmt: skip
    plain = summary(base)
    assert plain == "flown: completed, score 1, kills 0, losses 0, goals met at 9 s"
    assert summary({**base, "sith": {**SITH_BASE, "setting": "none"}}) == plain
    assert summary({**base, "sith": SITH_BASE}).startswith(plain + "; Sith flight")


def test_the_summary_line_for_each_status():
    base = {"why": "Because.", "outcome": "not_completed", "score": 0, "kills": 0,
            "losses": 2, "goals_met_seconds": 9}  # fmt: skip
    assert (
        summary({**base, "status": "flown"})
        == "flown: not completed, score 0, kills 0, losses 2"
    )
    assert summary({**base, "status": "not_reached"}) == "not reached: Because."
    assert summary({**base, "status": "refused"}) == "refused: Because."


def test_setting_flags_become_the_settings_of_the_description(
    monkeypatch, engine, capsys
):
    from jedimaster.launch import cli as launch_cli

    seen = []
    real = launch_cli.fly

    def spy(engine_path, description, *rest):
        seen.append(description)
        return real(engine_path, description, *rest)

    monkeypatch.setattr(launch_cli, "fly", spy)
    argv = command(engine, "Ace", "--setting", "difficulty=hard")
    argv += ["--setting", "collisions=off", "--setting", "mission_time_limit=5"]
    assert main(argv) == 0
    assert seen[0]["settings"] == {
        "difficulty": "hard",
        "collisions": False,
        "mission_time_limit": 5,
    }


def make_ai_folder(tmp_path: Path) -> Path:
    """Return a folder holding a stand-in AI program that answers ``--version``."""
    return make_ai_program(tmp_path / "brain")


def test_the_ai_program_is_this_python_and_the_folder_made_absolute(
    tmp_path, monkeypatch
):
    folder = make_ai_folder(tmp_path)
    monkeypatch.chdir(tmp_path)
    program = ai_program_of("brain")
    assert program == {"python": sys.executable, "path": str(folder.resolve())}
    doc = build_description(
        "combat", "imperial", "8braw10.tie", "Ace", ai_program=program
    )
    assert doc["ai_program"] == program and doc["ai_program"] is not program
    assert errors(doc, launch_schema()) == []
    assert "ai_program" not in build_description(
        "combat", "imperial", "8braw10.tie", "Ace"
    )


def test_the_repositorys_own_ai_program_is_found():
    assert ai_program_of(REPO_AI_PROGRAM)["path"] == str(REPO_AI_PROGRAM.resolve())


def test_a_folder_without_the_ai_program_is_refused(tmp_path):
    (tmp_path / "file").write_text("", encoding="utf-8")
    for folder in (tmp_path, tmp_path / "missing", tmp_path / "file"):
        with pytest.raises(ValueError, match="no AI program"):
            ai_program_of(folder)


def test_the_ai_program_flag_reaches_the_description(monkeypatch, engine, tmp_path):
    from jedimaster.launch import cli as launch_cli

    folder = make_ai_folder(tmp_path)
    seen = []
    real = launch_cli.fly
    monkeypatch.setattr(
        launch_cli, "fly", lambda e, d, *r: (seen.append(d), real(e, d, *r))[1]
    )
    assert main(command(engine, "Ace", "--ai-program", str(folder))) == 0
    assert seen[0]["ai_program"] == {
        "python": sys.executable,
        "path": str(folder.resolve()),
    }
    assert main(command(engine)) == 0
    packaged = Path(engine).parent / "ai" / "jedimaster_ai"
    assert seen[1]["ai_program"] == {
        "python": sys.executable,
        "path": str(packaged.resolve()),
    }


def test_an_ai_program_folder_without_the_program_exits_2(
    monkeypatch, engine, tmp_path, caplog
):
    from jedimaster.launch import cli as launch_cli

    flown = []
    monkeypatch.setattr(launch_cli, "fly", lambda *a: flown.append(a))
    assert main(command(engine, "Ace", "--ai-program", str(tmp_path))) == 2
    assert "no AI program" in caplog.text
    assert flown == []


def test_a_mismatched_pair_exits_1_with_the_sentence_and_never_starts_the_engine(
    monkeypatch, engine, tmp_path, caplog
):
    from jedimaster.launch import cli as launch_cli

    flown = []
    monkeypatch.setattr(launch_cli, "fly", lambda *a: flown.append(a))
    brain = make_ai_program(tmp_path / "brain", wire=100)
    assert main(command(engine, "Ace", "--ai-program", str(brain))) == 1
    assert (
        "This game build speaks AI version 11 and its AI program speaks AI "
        'version 100, so computer craft could not fly. Run "jedimaster update" '
        "for a matching pair."
    ) in caplog.text
    assert flown == []


def test_no_ai_program_beside_the_engine_exits_1(monkeypatch, engine, tmp_path, caplog):
    from jedimaster.launch import cli as launch_cli

    flown = []
    monkeypatch.setattr(launch_cli, "fly", lambda *a: flown.append(a))
    shutil.rmtree(tmp_path / "ai")
    assert main(command(engine)) == 1
    assert "No AI program was found beside this game build" in caplog.text
    assert flown == []


def test_without_engine_the_current_build_is_flown(engine, tmp_path, capsys):
    builds = tmp_path / "builds"
    build = builds / "aaaaaaaaaaaa"
    shutil.copytree(tmp_path / "ai", build / "ai")
    shutil.copy(engine, build / "OpenXvT")
    set_current(builds, "aaaaaaaaaaaa")
    argv = command(engine)
    argv[argv.index("--engine") : argv.index("--engine") + 2] = [
        "--builds",
        str(builds),
    ]
    assert main(argv) == 0
    assert capsys.readouterr().out.startswith("flown:")


def test_without_engine_or_build_exits_2(tmp_path, caplog):
    argv = command("x")
    argv[argv.index("--engine") : argv.index("--engine") + 2] = [
        "--builds",
        str(tmp_path / "none"),
    ]
    assert main(argv) == 2
    assert 'run "jedimaster update"' in caplog.text


def test_without_a_setting_flag_the_description_has_no_settings(monkeypatch, engine):
    from jedimaster.launch import cli as launch_cli

    seen = []
    real = launch_cli.fly
    monkeypatch.setattr(
        launch_cli, "fly", lambda e, d, *r: (seen.append(d), real(e, d, *r))[1]
    )
    assert main(command(engine)) == 0
    assert "settings" not in seen[0]


@pytest.mark.parametrize(
    "flag", ["difficulty=impossible", "speed=fast", "difficulty", "collisions=1"]
)
def test_a_setting_flag_that_is_not_an_option_exits_2(engine, flag, caplog):
    assert main(command(engine, "Ace", "--setting", flag)) == 2


AI_SETUP_STAND_IN = """#!__PYTHON__
import json, pathlib, sys
pathlib.Path(__file__).with_name("argv.json").write_text(json.dumps(sys.argv[1:]))
"""


def test_ai_setup_hands_the_engine_the_program_to_keep(tmp_path, capsys):
    engine = tmp_path / "engine"
    engine.write_text(
        AI_SETUP_STAND_IN.replace("__PYTHON__", sys.executable), encoding="utf-8"
    )
    engine.chmod(engine.stat().st_mode | stat.S_IXUSR)
    folder = tmp_path / "jedimaster_ai"
    folder.mkdir()
    (folder / "__main__.py").write_text("", encoding="utf-8")
    assert main(["ai-setup", "--engine", str(engine), "--ai-program", str(folder)]) == 0
    assert json.loads((tmp_path / "argv.json").read_text(encoding="utf-8")) == [
        "--ai-python", sys.executable, "--ai-program", str(folder.resolve()),
        "--save-config", "--check-installation",
    ]  # fmt: skip
    assert capsys.readouterr().out.startswith("kept:")


def test_ai_setup_refuses_a_folder_without_a_program(tmp_path, engine, caplog):
    assert main(["ai-setup", "--engine", engine, "--ai-program", str(tmp_path)]) == 1
    assert "no AI program" in caplog.text
