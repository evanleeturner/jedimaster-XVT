"""The ``bot`` command and ``page --bot``: setup, unlink, the start and its exits.

Purpose:
    Prove ``bot setup`` reads the token without echo, refuses an empty one
    or one with spaces, and writes a private file; ``bot unlink`` removes
    the link; ``page --bot`` exits 2 naming the fix when the token file or
    discord.py is missing, prints a link code only while nothing is linked,
    and starts the bot in the page's event loop with the page's ``Control``.

Flow:
    ``main`` called in-process with the web server and the bot replaced by
    stand-ins; one subprocess for the missing-discord.py exit.

Invariants:
    - No connection to Discord or to a port.
    - The token used here is made up.

Call:
    ``pytest tests/test_bot_cli.py``
"""

from __future__ import annotations

import asyncio
import logging
import re
import stat
import subprocess
import sys
from pathlib import Path

import pytest
from botdata import typed
from pagedata import make_install

from jedimaster.__main__ import main
from jedimaster.bot import cli
from jedimaster.bot import discord_door
from jedimaster.bot.credentials import read_token
from jedimaster.bot.link import ALPHABET
from jedimaster.bot.link import BotFile
from jedimaster.page import server

logger = logging.getLogger(__name__)

LAUNCHER = Path(__file__).resolve().parents[1]
SECRET = "made-up.TOKEN_value-12345"
posix_only = pytest.mark.skipif(sys.platform.startswith("win"), reason="no POSIX modes")


@pytest.fixture
def settings(tmp_path: Path) -> Path:
    return tmp_path / "config" / "settings.json"


def test_the_token_is_read_with_getpass_by_default(settings, monkeypatch):
    read = typed(SECRET)
    monkeypatch.setattr(cli.getpass, "getpass", read)
    assert cli.setup(settings) == 0
    assert len(read.asked) == 1


def test_setup_saves_the_token_beside_the_settings_file(settings, capsys):
    read = typed(SECRET + "\n")
    assert cli.setup(settings, read) == 0
    assert read_token(settings.parent / "bot_token").reveal() == SECRET
    shown = capsys.readouterr()
    assert str(settings.parent / "bot_token") in shown.out
    assert SECRET not in shown.out + shown.err
    assert "will not show" in read.asked[0]


@posix_only
def test_setup_writes_a_private_file(settings):
    cli.setup(settings, typed(SECRET))
    mode = stat.S_IMODE((settings.parent / "bot_token").stat().st_mode)
    assert mode == 0o600


@pytest.mark.parametrize("text", ["", "   ", "two words", "a\tb"])
def test_setup_refuses_an_empty_token_or_one_with_spaces(
    settings, text, caplog, capsys
):
    with caplog.at_level(logging.ERROR):
        assert cli.setup(settings, typed(text)) == 2
    assert not (settings.parent / "bot_token").exists()
    assert "nothing was saved" in caplog.text
    assert text.strip() == "" or text not in caplog.text + capsys.readouterr().out


def test_setup_through_the_command_line_moves_the_file_with_settings(
    tmp_path, monkeypatch
):
    monkeypatch.setattr(cli.getpass, "getpass", typed(SECRET))
    path = tmp_path / "moved" / "s.json"
    assert main(["bot", "setup", "--settings", str(path)]) == 0
    assert (tmp_path / "moved" / "bot_token").is_file()


def test_unlink_removes_the_link_and_says_so(settings, capsys):
    store = BotFile(settings.parent / "bot.json")
    store.linked_user_id = 7
    store.save()
    assert main(["bot", "unlink", "--settings", str(settings)]) == 0
    assert BotFile(settings.parent / "bot.json").linked_user_id is None
    assert capsys.readouterr().out.startswith("Unlinked")


def test_unlink_with_nothing_linked_says_so_and_writes_nothing(settings, capsys):
    assert main(["bot", "unlink", "--settings", str(settings)]) == 0
    assert capsys.readouterr().out.startswith("No account")
    assert not (settings.parent / "bot.json").exists()


def test_unlink_leaves_the_token_alone(settings):
    cli.setup(settings, typed(SECRET))
    main(["bot", "unlink", "--settings", str(settings)])
    assert read_token(settings.parent / "bot_token").reveal() == SECRET


# ---- page --bot -----------------------------------------------------------


@pytest.fixture
def page_dir(tmp_path: Path) -> Path:
    folder = tmp_path / "dist"
    folder.mkdir()
    return folder


@pytest.fixture
def served(monkeypatch):
    calls: list[dict] = []

    async def fake_serve(control, key, port, page_dir, open_browser, on_start=None):
        calls.append({"control": control, "on_start": on_start})

    monkeypatch.setattr(server, "serve", fake_serve)
    return calls


def argv_for(tmp_path, page_dir, *more):
    argv = [
        "page",
        "--install",
        str(make_install(tmp_path)),
        "--page-dir",
        str(page_dir),
    ]
    return argv + [
        "--settings",
        str(tmp_path / "config" / "settings.json"),
        "--no-open",
        *more,
    ]


def test_page_bot_without_the_token_file_exits_2_and_names_bot_setup(
    tmp_path, page_dir, served, caplog
):
    with caplog.at_level(logging.ERROR):
        assert main(argv_for(tmp_path, page_dir, "--bot")) == 2
    assert "bot setup" in caplog.text
    assert served == []


def test_page_bot_with_an_unusable_token_file_exits_2(
    tmp_path, page_dir, served, caplog
):
    folder = tmp_path / "config"
    folder.mkdir()
    (folder / "bot_token").write_text("two words\n", encoding="utf-8")
    assert main(argv_for(tmp_path, page_dir, "--bot")) == 2
    assert served == []


def test_page_without_the_flag_never_looks_for_the_bot(tmp_path, page_dir, served):
    assert main(argv_for(tmp_path, page_dir)) == 0
    assert served[0]["on_start"] is None


def test_page_bot_without_discord_py_exits_2_naming_the_extra():
    code = (
        "import sys\n"
        "sys.modules['discord'] = None\n"
        "from jedimaster.__main__ import main\n"
        "sys.exit(main(['page', '--bot', '--page-dir', '.']))"
    )
    done = subprocess.run(
        [sys.executable, "-c", code], cwd=LAUNCHER, capture_output=True, text=True
    )
    assert done.returncode == 2
    assert "jedimaster[bot]" in done.stderr


def test_other_commands_work_without_discord_py(tmp_path):
    code = (
        "import sys\n"
        "sys.modules['discord'] = None\n"
        "from jedimaster.__main__ import main\n"
        f"sys.exit(main(['bot', 'unlink', '--settings', {str(tmp_path / 's.json')!r}]))"
    )
    done = subprocess.run(
        [sys.executable, "-c", code], cwd=LAUNCHER, capture_output=True, text=True
    )
    assert done.returncode == 0, done.stderr


def start_with_token(tmp_path, page_dir, settings):
    cli.setup(settings, typed(SECRET))
    return main(argv_for(tmp_path, page_dir, "--bot"))


def test_page_bot_prints_a_link_code_and_one_instruction(
    tmp_path, page_dir, served, capsys, settings
):
    assert start_with_token(tmp_path, page_dir, settings) == 0
    out = capsys.readouterr().out
    match = re.search(rf"Link code: ([{ALPHABET}]{{4}}-[{ALPHABET}]{{4}})", out)
    assert match, out
    code = match.group(1)
    assert f"send /link {code} to it in a direct message within 10 minutes" in out
    assert SECRET not in out


def test_page_bot_prints_no_code_once_linked(
    tmp_path, page_dir, served, capsys, settings
):
    cli.setup(settings, typed(SECRET))
    store = BotFile(settings.parent / "bot.json")
    store.linked_user_id = 7
    store.save()
    assert main(argv_for(tmp_path, page_dir, "--bot")) == 0
    assert "Link code" not in capsys.readouterr().out


def test_each_start_makes_a_new_code(tmp_path, page_dir, served, capsys, settings):
    start_with_token(tmp_path, page_dir, settings)
    first = re.search(r"Link code: (\S+)", capsys.readouterr().out).group(1)
    main(argv_for(tmp_path, page_dir, "--bot"))
    second = re.search(r"Link code: (\S+)", capsys.readouterr().out).group(1)
    assert first != second


async def test_the_bot_starts_in_the_pages_loop_and_shares_its_control(
    tmp_path, page_dir, monkeypatch, settings
):
    ran: list = []

    async def fake_run(self, token):
        ran.append((self, token))

    monkeypatch.setattr(discord_door.Door, "run", fake_run)
    cli.setup(settings, typed(SECRET))
    token = cli.check(settings)
    control = object.__new__(server.Control)
    start = cli.starter(control, settings, token)
    start(object())
    await asyncio.sleep(0)
    ((door, given),) = ran
    assert given is token and door.core.control is control


def test_check_hides_the_token_from_the_handlers(tmp_path, settings):
    cli.setup(settings, typed(SECRET))
    handler = logging.StreamHandler()
    logging.getLogger().addHandler(handler)
    try:
        assert cli.check(settings) is not None
        assert handler.filters
    finally:
        logging.getLogger().removeHandler(handler)
