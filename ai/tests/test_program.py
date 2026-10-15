"""Fence for program: the program run the way the engine runs it.

Most tests start a real child process by the package's folder, as a launch
description's ``ai_program`` does, and talk to it over its standard input
and output.
"""

from __future__ import annotations

import io
import pathlib
import re
import subprocess
import sys
import types

from jedimaster_ai import program
from jedimaster_ai import wire
from jedimaster_ai.think import think
from jedimaster_ai.world import Question

AI = pathlib.Path(__file__).resolve().parents[1]
LINE = re.compile(
    r'\d\d:\d\d:\d\d\.\d{3} [DIWEC] [a-z]+\.[a-z_]+( [a-z_]+=("[^"]*"|\S+))*'
)


def questions(view) -> list[Question]:
    return [
        Question(1000, (view(object_index=4), view(object_index=9, order_code=99))),
        Question(1008, (view(object_index=4),)),
    ]


def run(
    stdin: bytes, *options: str, module: bool = False
) -> subprocess.CompletedProcess:
    start = ["-m", "jedimaster_ai"] if module else [str(AI / "jedimaster_ai")]
    return subprocess.run(
        [sys.executable, *start, *options],
        input=stdin,
        capture_output=True,
        cwd=AI,
        timeout=30,
        check=False,
    )


def check_log(stderr: bytes) -> list[str]:
    """Return the log lines after the header, each checked for its shape."""
    lines = stderr.decode().splitlines()
    assert lines[0].startswith("= jedimaster_ai run ")
    assert lines[1].startswith('= cwd "')
    for line in lines[2:]:
        assert LINE.fullmatch(line), line
    return lines[2:]


def test_each_question_gets_thinks_answer(view) -> None:
    asked = questions(view)
    done = run(b"".join(wire.question_bytes(question) for question in asked))
    assert done.returncode == 0
    assert done.stdout == b"".join(
        wire.answer_bytes(think(question)) for question in asked
    )
    lines = check_log(done.stderr)
    assert f" I program.started wire={wire.VERSION} pid=" in lines[0]
    assert " W order.no_behavior object=9 code=99" in lines[1]
    assert lines[-1].endswith(" I program.stopped questions=2")


def test_the_module_starts_the_same_program(view) -> None:
    asked = wire.question_bytes(questions(view)[1])
    done = run(asked, module=True)
    assert (done.returncode, done.stdout) == (
        0,
        wire.answer_bytes(think(questions(view)[1])),
    )


def test_a_broken_question_is_not_answered() -> None:
    done = run(b"\x05\x00\x00\x00abcde")
    assert (done.returncode, done.stdout) == (2, b"")
    assert " E program.refused error=" in check_log(done.stderr)[-1]


def test_options_name_the_run_level_and_file(view, tmp_path) -> None:
    log_file = tmp_path / "ai.log"
    asked = wire.question_bytes(questions(view)[1])
    options = (
        "--run-id",
        "0000abcd",
        "--log-level",
        "debug",
        "--log-file",
        str(log_file),
    )
    done = run(asked, *options)
    assert done.stderr.decode().startswith("= jedimaster_ai run 0000abcd ")
    assert any(" D think.view object=4 " in line for line in check_log(done.stderr))
    assert log_file.read_text().splitlines() == done.stderr.decode().splitlines()


def test_a_failure_ends_in_one_critical_line(view, monkeypatch) -> None:
    def fail(question):
        raise RuntimeError('no "think" today')

    asked = wire.question_bytes(questions(view)[1])
    monkeypatch.setattr(program, "think", fail)
    monkeypatch.setattr(sys, "stdin", types.SimpleNamespace(buffer=io.BytesIO(asked)))
    monkeypatch.setattr(sys, "stdout", types.SimpleNamespace(buffer=io.BytesIO()))
    monkeypatch.setattr(sys, "stderr", io.StringIO())
    assert program.main([]) == 1
    last = sys.stderr.getvalue().splitlines()[-1]
    assert LINE.fullmatch(last)
    assert ' C program.crashed error_type="RuntimeError" ' in last
    assert "error=\"no 'think' today\"" in last


def test_version_prints_the_wire_version_and_nothing_else() -> None:
    for module in (False, True):
        done = run(b"", "--version", module=module)
        assert done.returncode == 0
        assert done.stdout == f"ai_wire={wire.VERSION}\n".encode()
        assert done.stderr == b""


def test_version_wins_over_the_other_options(tmp_path) -> None:
    log = tmp_path / "never.log"
    done = run(b"", "--log-file", str(log), "--version")
    assert done.returncode == 0 and not log.exists()
    assert done.stdout == f"ai_wire={wire.VERSION}\n".encode()
