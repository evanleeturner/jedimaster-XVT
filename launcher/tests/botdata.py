"""Build what the bot tests need: a stand-in for Discord and a bot over a made-up install.

Purpose:
    Give every ``test_bot_*`` file the same stand-in for Discord (plain
    objects with the attributes the handlers read, recording each call),
    the same made-up install and the same ready-built core, linker and door.

Flow:
    ``make_bot`` builds a ``Control`` over ``pagedata.make_install``, a
    ``Linker`` over a temporary ``bot.json`` with a clock that tests move,
    and a ``Core``; ``Talk`` is one interaction; ``everything_said`` reads
    every word the stand-in was given.

Invariants:
    - No network, no discord.py client connection, no game data.
    - The stand-in has only what the handlers read: ``user.id``, ``guild``,
      ``response`` (``send_message``, ``defer``, ``is_done``) and
      ``followup.send``.

Call:
    ``bot = make_bot(tmp_path); bot.link_account(7); talk = Talk(7)``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from types import SimpleNamespace
from typing import Any

from pagedata import LAUNCHER_VERSION
from pagedata import make_install

from jedimaster.bot.core import Core
from jedimaster.bot.link import BotFile
from jedimaster.bot.link import Linker
from jedimaster.page.control import Control
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

HOST = 7001
STRANGER = 9009


class Clock:
    """A clock the test moves by hand."""

    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


class Response:
    """The first answer of an interaction, recording what it was given."""

    def __init__(self) -> None:
        self.calls: list[tuple[str, tuple, dict]] = []
        self.done = False

    async def send_message(self, *args: Any, **kwargs: Any) -> None:
        self.calls.append(("send_message", args, kwargs))
        self.done = True

    async def defer(self, *args: Any, **kwargs: Any) -> None:
        self.calls.append(("defer", args, kwargs))
        self.done = True

    def is_done(self) -> bool:
        return self.done


class Followup:
    """The later answers of an interaction."""

    def __init__(self) -> None:
        self.calls: list[tuple[tuple, dict]] = []

    async def send(self, *args: Any, **kwargs: Any) -> None:
        self.calls.append((args, kwargs))


@dataclass
class Talk:
    """One interaction from account ``user_id``; ``in_server`` puts it in a server."""

    user_id: int
    in_server: bool = False
    response: Response = field(default_factory=Response)
    followup: Followup = field(default_factory=Followup)

    @property
    def user(self) -> SimpleNamespace:
        return SimpleNamespace(id=self.user_id)

    @property
    def guild(self) -> object | None:
        return SimpleNamespace(id=1) if self.in_server else None

    def said(self) -> list[tuple[str, dict]]:
        """Return every (text, options) the stand-in was asked to send."""
        sent = [
            (args[0], kwargs)
            for name, args, kwargs in self.response.calls
            if name == "send_message"
        ]
        return sent + [(args[0], kwargs) for args, kwargs in self.followup.calls]

    def texts(self) -> list[str]:
        """Return every text sent."""
        return [text for text, _ in self.said()]


@dataclass
class Bot:
    """A core over a made-up install, its link and the pieces tests reach into."""

    core: Core
    linker: Linker
    control: Control
    store: BotFile
    clock: Clock
    folder: Path

    def link_account(self, user_id: int = HOST) -> None:
        """Link ``user_id`` the way a right code would."""
        code = self.linker.start()
        assert code is not None
        assert self.linker.attempt(user_id, code) == "linked"


def make_bot(tmp_path: Path) -> Bot:
    """Return a bot over a made-up install, with nothing linked yet."""
    store = SettingsStore(tmp_path / "config" / "settings.json")
    control = Control(make_install(tmp_path), store, LAUNCHER_VERSION)
    clock = Clock()
    bot_file = BotFile(tmp_path / "config" / "bot.json")
    linker = Linker(bot_file, clock=clock)
    return Bot(Core(control, linker), linker, control, bot_file, clock, tmp_path)


def typed(text: str):
    """Return a stand-in for a prompt-reading function that answers ``text``."""
    asked: list[str] = []

    def read(prompt: str) -> str:
        asked.append(prompt)
        return text

    read.asked = asked
    return read
