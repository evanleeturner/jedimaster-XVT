"""The ``page`` command: its options, its exits, its optional dependency.

Purpose:
    Prove the options and their defaults, the exit statuses for a missing
    page folder, a bad port, a missing web framework and a port that cannot
    be bound, that a secret is drawn per run, and that the other commands
    never import the web framework.

Flow:
    ``main`` called in-process with ``serve`` replaced by a stand-in; two
    subprocesses for the import checks.

Invariants:
    - No game data; no real server (see ``test_page_server.py``).

Call:
    ``pytest tests/test_page_cli.py``
"""

from __future__ import annotations

import logging
import subprocess
import sys
from pathlib import Path

import pytest
from pagedata import make_install

from jedimaster.__main__ import main
from jedimaster.page import cli
from jedimaster.page import server

logger = logging.getLogger(__name__)

LAUNCHER = Path(__file__).resolve().parents[1]


@pytest.fixture
def page_dir(tmp_path: Path) -> Path:
    folder = tmp_path / "dist"
    folder.mkdir()
    return folder


@pytest.fixture
def served(monkeypatch):
    """Replace ``serve`` by a stand-in that records its call; return the record."""
    calls: list[tuple] = []

    async def fake_serve(control, key, port, page_dir, open_browser):
        calls.append((control, key, port, page_dir, open_browser))

    monkeypatch.setattr(server, "serve", fake_serve)
    return calls


def test_defaults_and_options():
    assert cli.DEFAULT_PORT == 8780
    assert cli.DEFAULT_PAGE_DIR == LAUNCHER / "page" / "dist"


def test_run_passes_the_options_to_serve(tmp_path, page_dir, served):
    install = make_install(tmp_path)
    settings = tmp_path / "s.json"
    argv = ["page", "--install", str(install), "--port", "0", "--no-open"]
    argv += ["--page-dir", str(page_dir), "--settings", str(settings)]
    assert main(argv) == 0
    ((control, key, port, folder, open_browser),) = served
    assert control.install == install and control.store.path == settings
    assert (port, folder, open_browser) == (0, page_dir, False)
    assert len(key) >= 43  # token_urlsafe(32)


def test_the_browser_opens_unless_told_not_to(tmp_path, page_dir, served):
    install = make_install(tmp_path)
    argv = ["page", "--install", str(install), "--page-dir", str(page_dir)]
    assert main(argv + ["--settings", str(tmp_path / "s.json")]) == 0
    assert served[0][2] == 8780 and served[0][4] is True


def test_each_run_draws_a_new_secret(tmp_path, page_dir, served):
    argv = ["page", "--install", str(make_install(tmp_path))]
    argv += ["--page-dir", str(page_dir), "--settings", str(tmp_path / "s.json")]
    main(argv)
    main(argv)
    assert served[0][1] != served[1][1]


def test_a_missing_page_folder_exits_2(tmp_path, served, caplog):
    argv = ["page", "--page-dir", str(tmp_path / "none")]
    assert main(argv) == 2
    assert served == [] and "page folder not found" in caplog.text


@pytest.mark.parametrize("port", ["-1", "65536"])
def test_a_port_out_of_range_exits_2(tmp_path, page_dir, served, port):
    assert main(["page", "--page-dir", str(page_dir), "--port", port]) == 2
    assert served == []


def test_no_install_still_serves(tmp_path, page_dir, served, monkeypatch, caplog):
    monkeypatch.setenv("JEDIMASTER_XVT", str(tmp_path / "none"))
    monkeypatch.setattr(cli, "find_install", lambda given=None: None)
    argv = ["page", "--page-dir", str(page_dir), "--settings", str(tmp_path / "s")]
    assert main(argv) == 0
    assert served[0][0].install is None


def test_a_port_that_cannot_be_bound_exits_2(tmp_path, page_dir, monkeypatch):
    async def refuse(*args):
        raise OSError("address in use")

    monkeypatch.setattr(server, "serve", refuse)
    argv = ["page", "--install", str(make_install(tmp_path))]
    assert main(argv + ["--page-dir", str(page_dir), "--port", "1"]) == 2


def test_ctrl_c_exits_0(tmp_path, page_dir, monkeypatch):
    async def interrupted(*args):
        raise KeyboardInterrupt

    monkeypatch.setattr(server, "serve", interrupted)
    argv = ["page", "--install", str(make_install(tmp_path))]
    argv += ["--page-dir", str(page_dir), "--settings", str(tmp_path / "s.json")]
    assert main(argv) == 0


def run_python(code: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-c", code],
        cwd=LAUNCHER,
        capture_output=True,
        text=True,
        check=False,
    )


def test_other_commands_do_not_import_the_web_framework():
    code = "import sys, jedimaster.__main__; assert 'aiohttp' not in sys.modules"
    done = run_python(code)
    assert done.returncode == 0, done.stderr


def test_without_the_web_framework_the_command_says_so():
    code = (
        "import sys; sys.modules['aiohttp'] = None\n"
        "from jedimaster.__main__ import main\n"
        "sys.exit(main(['page', '--page-dir', '.']))"
    )
    done = run_python(code)
    assert done.returncode == 2
    assert "jedimaster[page]" in done.stderr


# ---- the game program and the pilot folder ---------------------------------


def serve_argv(tmp_path, page_dir, *extra):
    install = make_install(tmp_path)
    argv = ["page", "--install", str(install), "--port", "0", "--no-open"]
    return argv + [
        "--page-dir",
        str(page_dir),
        "--settings",
        str(tmp_path / "s.json"),
        *extra,
    ]


def test_without_an_engine_no_battle_can_start(tmp_path, page_dir, served):
    assert main(serve_argv(tmp_path, page_dir)) == 0
    assert served[0][0].launching.flight is None


def test_engine_engine_args_and_pilot_folder_reach_the_control(
    tmp_path, page_dir, served
):
    engine = tmp_path / "game"
    engine.write_text("x")
    folder = tmp_path / "pilots"
    argv = serve_argv(tmp_path, page_dir, "--engine", str(engine))
    argv += ["--engine-arg=--game-data", f"--engine-arg={tmp_path}"]
    argv += ["--pilot-folder", str(folder)]
    assert main(argv) == 0
    flight = served[0][0].launching.flight
    assert flight.engine == str(engine)
    assert flight.args == ("--game-data", str(tmp_path))
    assert flight.pilot_folder == folder
    assert flight.timeout == 3600.0
    assert flight.ai_program is None


def test_the_ai_program_reaches_the_flight(tmp_path, page_dir, served):
    engine = tmp_path / "game"
    engine.write_text("x")
    folder = tmp_path / "brain"
    folder.mkdir()
    (folder / "__main__.py").write_text("")
    argv = serve_argv(tmp_path, page_dir, "--engine", str(engine))
    assert main(argv + ["--ai-program", str(folder)]) == 0
    assert served[0][0].launching.flight.ai_program == {
        "python": sys.executable,
        "path": str(folder.resolve()),
    }


def test_an_ai_program_folder_without_the_program_exits_2(
    tmp_path, page_dir, served, caplog
):
    engine = tmp_path / "game"
    engine.write_text("x")
    argv = serve_argv(tmp_path, page_dir, "--engine", str(engine))
    assert main(argv + ["--ai-program", str(tmp_path)]) == 2
    assert "no AI program" in caplog.text and served == []


def test_the_pilot_folder_defaults_to_the_games_user_folder(tmp_path, page_dir, served):
    from jedimaster.launch.pilots import default_pilot_folder

    engine = tmp_path / "game"
    engine.write_text("x")
    assert main(serve_argv(tmp_path, page_dir, "--engine", str(engine))) == 0
    assert served[0][0].launching.flight.pilot_folder == default_pilot_folder()


def test_an_engine_that_is_not_a_file_exits_2(tmp_path, page_dir, served, caplog):
    assert main(serve_argv(tmp_path, page_dir, "--engine", str(tmp_path / "no"))) == 2
    assert "engine not found" in caplog.text and served == []


@pytest.mark.parametrize(
    "flag", [["--engine-arg=x"], ["--pilot-folder", "p"], ["--ai-program", "p"]]
)
def test_engine_options_without_an_engine_exit_2(tmp_path, page_dir, served, flag):
    assert main(serve_argv(tmp_path, page_dir, *flag)) == 2
    assert served == []


def test_a_system_with_no_default_folder_must_be_given_one(
    tmp_path, page_dir, served, monkeypatch, caplog
):
    engine = tmp_path / "game"
    engine.write_text("x")
    monkeypatch.setattr(cli, "default_pilot_folder", lambda: None)
    assert main(serve_argv(tmp_path, page_dir, "--engine", str(engine))) == 2
    assert "give --pilot-folder" in caplog.text
    given = serve_argv(
        tmp_path, page_dir, "--engine", str(engine), "--pilot-folder", "p"
    )
    assert main(given) == 0
