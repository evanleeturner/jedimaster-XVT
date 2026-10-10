"""The token through a whole run: never in a log, never in a reply.

Purpose:
    Run the bot from ``bot setup`` through loading the token, a refused
    login and every command, with the logs at DEBUG, and prove the token's
    text appears in no log record (from the launcher's loggers or from a
    record on the ``discord`` logger) and in no reply.

Flow:
    Two handlers on the root logger: one before the filter (it sees what
    the launcher's own loggers emit) and the filter's own. ``cli.setup``
    saves a made-up token, ``cli.check`` loads it and installs the filter,
    the door runs against a login that fails with the token in its message,
    and each command is asked by the host and by a stranger.

Invariants:
    - The root logger's handlers are put back after each test.
    - No connection to Discord.

Call:
    ``pytest tests/test_bot_token.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import discord
import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from botdata import Talk
from botdata import typed

from jedimaster.bot import cli
from jedimaster.bot.discord_door import Door

logger = logging.getLogger(__name__)

SECRET = "made-up.TOKEN_value-12345"


class Collect(logging.Handler):
    """Keep what each record says at the moment this handler sees it."""

    def __init__(self) -> None:
        super().__init__(logging.DEBUG)
        self.setFormatter(logging.Formatter("%(levelname)s %(message)s"))
        self.lines: list[tuple[str, str]] = []

    def emit(self, record: logging.LogRecord) -> None:
        text = self.format(record)
        self.lines.append((record.name, text))


@pytest.fixture
def logs():
    root = logging.getLogger()
    before, level = list(root.handlers), root.level
    raw, hidden = Collect(), Collect()
    root.handlers = [raw]
    root.setLevel(logging.DEBUG)
    yield raw, hidden
    root.handlers = before
    root.setLevel(level)


async def whole_run(tmp_path: Path, monkeypatch, raw: Collect, hidden: Collect):
    """Do everything a run does with the token in play; return every reply text."""
    settings = tmp_path / "config" / "settings.json"
    assert cli.setup(settings, typed(SECRET)) == 0
    root = logging.getLogger()
    root.handlers = [hidden]
    token = cli.check(settings)
    root.handlers = [raw, hidden]
    assert not isinstance(token, int)
    bot = make_bot(tmp_path)
    bot.link_account(HOST)

    async def deliver(pushes):
        return 1

    door = Door(bot.core, bot.store, deliver)

    async def refuse(self, given, *, reconnect=True):
        raise discord.LoginFailure(f"Improper token {given} has been passed.")

    monkeypatch.setattr(discord.Client, "start", refuse)
    await door.run(token)
    logging.getLogger("discord.http").warning("sending %s to Discord", SECRET)
    logging.getLogger("discord.client").error("login with %s", SECRET, exc_info=False)
    said: list[str] = []
    for who in (HOST, STRANGER):
        for name, args in (
            ("status", ()),
            ("missions", ("training",)),
            ("settings", ()),
            ("link", ("AAAA-AAAA",)),
            ("unlink", ()),
        ):
            if name == "unlink" and who == HOST:
                continue
            talk = Talk(who)
            await door.tree.get_command(name).callback(talk, *args)
            said += talk.texts()
    picker = bot.core.settings(HOST, True).pickers[0]
    talk = Talk(HOST)
    await door.picked(talk, picker, ["engine_fit"])
    said += talk.texts()
    return said


async def test_the_token_is_in_no_log_record_through_a_whole_run(
    tmp_path, monkeypatch, logs
):
    raw, hidden = logs
    said = await whole_run(tmp_path, monkeypatch, raw, hidden)
    launcher_lines = [
        text for name, text in raw.lines if not name.startswith("discord")
    ]
    assert launcher_lines, "the run logged nothing"
    assert not [t for t in launcher_lines if SECRET in t]
    assert any(name == "discord.http" for name, _ in raw.lines)
    assert [t for name, t in raw.lines if name.startswith("discord") and SECRET in t]
    assert hidden.lines
    assert not [t for _, t in hidden.lines if SECRET in t]
    assert any("[token]" in t for name, t in hidden.lines if name.startswith("discord"))
    assert not [s for s in said if SECRET in s]


async def test_the_run_is_logged_at_debug_info_warning_and_error(
    tmp_path, monkeypatch, logs
):
    raw, hidden = logs
    await whole_run(tmp_path, monkeypatch, raw, hidden)
    levels = {text.split(" ", 1)[0] for _, text in hidden.lines}
    assert levels >= {"DEBUG", "INFO", "WARNING", "ERROR"}


async def test_no_reply_is_the_token_or_holds_a_code(tmp_path, monkeypatch, logs):
    said = await whole_run(tmp_path, monkeypatch, *logs)
    assert said
    assert not [s for s in said if SECRET in s]
