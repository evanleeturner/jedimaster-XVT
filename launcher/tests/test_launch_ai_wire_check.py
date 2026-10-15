"""The AI version check: each sentence a player can be refused with, and the pair that passes.

Purpose:
    Prove ``check_pair`` against stand-in engines and AI programs (small
    Python scripts that answer ``--version`` as the real ones do): the pair
    that matches, the five refusals in their exact words, where the packaged
    AI program is looked for, and that nothing is remembered between calls.

Flow:
    ``make_engine`` and ``make_ai_program`` (``solodata``) write the stand-ins
    in a temporary folder; each test calls ``check_pair`` and reads the
    sentence of the ``PairRefused`` it raises.

Invariants:
    - No game data and no real engine or AI program.

Call:
    ``pytest tests/test_launch_ai_wire_check.py``
"""

from __future__ import annotations

import logging
import os
import stat
import sys

import pytest
from solodata import make_ai_program
from solodata import make_engine

from jedimaster.launch import ai_wire_check
from jedimaster.launch.ai_wire_check import check_pair
from jedimaster.launch.ai_wire_check import engine_says
from jedimaster.launch.ai_wire_check import packaged_ai_program
from jedimaster.launch.ai_wire_check import PairRefused

logger = logging.getLogger(__name__)

UPDATE = 'Run "jedimaster update"'


def refusal(engine, folder=None) -> str:
    """Return the sentence ``check_pair`` is refused with."""
    with pytest.raises(PairRefused) as caught:
        check_pair(engine, folder)
    return str(caught.value)


def test_a_matching_packaged_pair_passes_and_names_the_program(tmp_path):
    engine = make_engine(tmp_path, wire=7)
    make_ai_program(tmp_path / "ai" / "jedimaster_ai", wire=7)
    pair = check_pair(engine, None)
    folder = tmp_path / "ai" / "jedimaster_ai"
    assert pair.version == 7
    assert pair.program == {"python": sys.executable, "path": str(folder.resolve())}


def test_a_named_folder_wins_over_the_packaged_one(tmp_path):
    engine = make_engine(tmp_path, wire=7)
    named = make_ai_program(tmp_path / "named", wire=7)
    assert check_pair(engine, named).program["path"] == str(named.resolve())
    assert check_pair(engine, str(named)).version == 7


def test_a_mismatch_says_both_versions_in_the_exact_sentence(tmp_path):
    engine = make_engine(tmp_path, wire=12)
    make_ai_program(tmp_path / "ai" / "jedimaster_ai", wire=11)
    assert refusal(engine) == (
        "This game build speaks AI version 12 and its AI program speaks AI "
        'version 11, so computer craft could not fly. Run "jedimaster update" '
        "for a matching pair."
    )


def test_an_engine_that_rejects_version_is_too_old(tmp_path):
    engine = make_engine(tmp_path, wire=None)
    assert refusal(engine) == (
        "This game build is too old to say which AI version it speaks. "
        'Run "jedimaster update" for a current one.'
    )


def test_an_engine_that_cannot_be_started_is_silent_not_too_old(tmp_path):
    make_ai_program(tmp_path / "ai" / "jedimaster_ai")
    sentence = refusal(tmp_path / "missing-engine")
    assert sentence.startswith(
        "This game build could not be asked which AI version it speaks: "
        "it could not be started ("
    )
    assert sentence.endswith(").")
    assert "too old" not in sentence


def test_no_ai_program_beside_the_engine_is_refused(tmp_path):
    engine = make_engine(tmp_path, packaged=False)
    assert refusal(engine) == (
        "No AI program was found beside this game build, so computer craft could "
        'not fly. Run "jedimaster update", or name one with --ai-program.'
    )


def test_an_ai_program_with_no_version_line_says_why(tmp_path):
    engine = make_engine(tmp_path)
    quiet = make_ai_program(tmp_path / "quiet", says="    sys.exit(0)")
    assert refusal(engine, quiet) == (
        f"The AI program in {quiet} did not say which AI version it speaks: "
        "it printed no ai_wire= line."
    )


def test_an_ai_program_that_fails_gives_its_exit_status_and_last_complaint(tmp_path):
    engine = make_engine(tmp_path)
    old = make_ai_program(tmp_path / "old", wire=None)
    assert refusal(engine, old) == (
        f"The AI program in {old} did not say which AI version it speaks: "
        "it exited with status 2: unknown argument."
    )


def test_a_folder_without_main_is_the_programs_silence(tmp_path):
    engine = make_engine(tmp_path)
    empty = tmp_path / "empty"
    empty.mkdir()
    assert refusal(engine, empty) == (
        f"The AI program in {empty} did not say which AI version it speaks: "
        "the folder holds no __main__.py."
    )


def test_a_program_that_never_answers_is_stopped_at_the_limit(tmp_path, monkeypatch):
    engine = make_engine(tmp_path)
    slow = make_ai_program(
        tmp_path / "slow", says="    import time\n    time.sleep(30)"
    )
    monkeypatch.setattr(ai_wire_check, "ASK_SECONDS", 0.5)
    assert refusal(engine, slow).endswith("it did not answer within 0.5 seconds.")


def test_an_engine_that_runs_and_prints_no_wire_line_is_too_old(tmp_path):
    engine = make_engine(tmp_path)
    engine.write_text(
        f"#!{sys.executable}\nprint('OpenXvT abc1234')\n", encoding="utf-8"
    )
    engine.chmod(engine.stat().st_mode | stat.S_IXUSR)
    assert "too old" in refusal(engine)


def test_an_engine_that_never_answers_is_silent_at_the_limit(tmp_path, monkeypatch):
    engine = make_engine(tmp_path)
    engine.write_text(
        f"#!{sys.executable}\nimport time\ntime.sleep(30)\n", encoding="utf-8"
    )
    engine.chmod(engine.stat().st_mode | stat.S_IXUSR)
    monkeypatch.setattr(ai_wire_check, "ASK_SECONDS", 0.5)
    assert refusal(engine) == (
        "This game build could not be asked which AI version it speaks: "
        "it did not answer within 0.5 seconds."
    )


def test_the_engine_is_asked_with_version_alone(tmp_path):
    engine = make_engine(tmp_path)
    engine.write_text(
        f"#!{sys.executable}\nimport sys\nprint('OpenXvT abc1234')\n"
        "print('some note')\nprint('ai_wire=3')\n"
        "open(sys.argv[0] + '.argv', 'w').write(repr(sys.argv[1:]))\n",
        encoding="utf-8",
    )
    assert engine_says(engine) == ("abc1234", 3)
    assert engine.with_name("engine.py.argv").read_text() == "['--version']"


def test_the_packaged_program_is_found_in_both_package_layouts(tmp_path):
    linux = tmp_path / "linux"
    (linux / "ai" / "jedimaster_ai").mkdir(parents=True)
    (linux / "ai" / "jedimaster_ai" / "__main__.py").write_text("")
    assert packaged_ai_program(linux / "OpenXvT") == linux / "ai" / "jedimaster_ai"
    mac = tmp_path / "OpenXvT.app" / "Contents"
    (mac / "MacOS").mkdir(parents=True)
    assert packaged_ai_program(mac / "MacOS" / "OpenXvT") is None
    resources = mac / "Resources" / "ai" / "jedimaster_ai"
    resources.mkdir(parents=True)
    (resources / "__main__.py").write_text("")
    found = packaged_ai_program(mac / "MacOS" / "OpenXvT")
    assert found is not None and found.resolve() == resources.resolve()


def test_nothing_is_remembered_between_calls(tmp_path):
    # The versions differ in length: Python reuses a compiled __main__ whose
    # source has the same size and the same second, which would hide the swap.
    engine = make_engine(tmp_path, wire=9)
    program = tmp_path / "ai" / "jedimaster_ai"
    make_ai_program(program, wire=9)
    assert check_pair(engine, None).version == 9
    make_ai_program(program, wire=100)
    assert "AI program speaks AI version 100" in refusal(engine)
    make_ai_program(program, wire=9)
    assert check_pair(engine, None).version == 9


def test_a_wire_line_inside_a_longer_line_is_no_answer(tmp_path):
    engine = make_engine(tmp_path)
    engine.write_text(
        f"#!{sys.executable}\nprint('OpenXvT abc1234')\n"
        "print('note: ai_wire=5 is planned')\n",
        encoding="utf-8",
    )
    assert "too old" in refusal(engine)
    quiet = make_ai_program(
        tmp_path / "quiet", says='    print("my_ai_wire=11")\n    sys.exit(0)'
    )
    assert refusal(make_engine(tmp_path / "again"), quiet).endswith(
        "it printed no ai_wire= line."
    )


def test_the_wire_line_is_found_on_any_line_of_either_answer(tmp_path):
    engine = make_engine(tmp_path)
    engine.write_text(
        f"#!{sys.executable}\nprint('ai_wire=11')\nprint('OpenXvT abc1234')\n",
        encoding="utf-8",
    )
    noisy = make_ai_program(
        tmp_path / "noisy",
        says='    print("starting up")\n    print("ai_wire=11")\n    sys.exit(0)',
    )
    assert check_pair(engine, noisy).version == 11


def test_a_packaged_folder_without_main_is_no_program(tmp_path):
    engine = make_engine(tmp_path, packaged=False)
    (tmp_path / "ai" / "jedimaster_ai").mkdir(parents=True)
    assert refusal(engine).startswith("No AI program was found beside this game build")
    assert packaged_ai_program(engine) is None


def test_the_ai_program_is_run_by_the_launchers_own_python(tmp_path):
    engine = make_engine(tmp_path)
    named = make_ai_program(
        tmp_path / "named",
        says=(
            "    open(__file__ + '.ran', 'w').write(sys.executable)\n"
            '    print("ai_wire=11")\n    sys.exit(0)'
        ),
    )
    assert check_pair(engine, named).version == 11
    ran = (named / "__main__.py.ran").read_text()
    assert os.path.normpath(ran) == os.path.normpath(sys.executable)
