"""Command line of the bot: ``bot setup``, ``bot unlink`` and the ``page --bot`` start.

Purpose:
    Let the host keep the bot's token on their own computer, remove the link
    to a Discord account, and start the bot beside the page in one process.

Flow:
    ``add_parser`` adds ``bot`` with ``setup`` and ``unlink``; ``run`` runs
    one of them. ``setup`` reads the token without echo and saves it.
    ``unlink`` clears the linked account. ``check`` is called by the page's
    command line for ``page --bot``: it needs discord.py and the token file,
    loads the token and hides it from logs. ``starter`` then prints the link
    code when no account is linked and returns the function the web server
    calls once it is bound, which starts the bot in the same event loop.

Invariants:
    - discord.py is imported only by ``discord_door``, and only when the bot
      starts; ``bot setup`` and ``bot unlink`` work without it.
    - The token is read with no echo, never printed, never put in an error.
    - Exit status: 0 on success, 2 for a usage problem (no discord.py, no
      token file, an empty token or one with spaces).
    - ``--settings`` moves the token file and ``bot.json`` with the settings
      file.

Call:
    ``python -m jedimaster bot setup`` and ``python -m jedimaster page --bot``
"""

from __future__ import annotations

import argparse
import asyncio
import getpass
import importlib.util
import logging
from collections.abc import Callable
from pathlib import Path

from ..page.control import Control
from ..page.settings import default_settings_path
from .credentials import check_token
from .credentials import install_log_filter
from .credentials import read_token
from .credentials import Token
from .credentials import token_path
from .credentials import write_token
from .link import bot_file_path
from .link import BotFile
from .link import LIFETIME_SECONDS
from .link import Linker

logger = logging.getLogger(__name__)

EXTRA_HINT = "the bot needs discord.py: pip install 'jedimaster[bot]'"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``bot`` command with ``setup`` and ``unlink`` to ``sub``.

    Returns None. Does not parse anything.
    """
    bot = sub.add_parser("bot", help="set up and manage the launcher's Discord bot")
    actions = bot.add_subparsers(dest="bot_command", required=True)
    for name, help_text in (
        ("setup", "save the bot's token on this computer"),
        ("unlink", "remove the link to the Discord account"),
    ):
        action = actions.add_parser(name, help=help_text)
        action.add_argument(
            "--settings", help="the settings file (the bot's files sit beside it)"
        )


def _settings_path(args: argparse.Namespace) -> Path:
    return Path(args.settings) if args.settings else default_settings_path()


def run(args: argparse.Namespace) -> int:
    """Run ``bot setup`` or ``bot unlink``; return the exit status.

    Returns 0 on success and 2 when ``setup`` is given a token it refuses.
    Does not need discord.py.
    """
    path = _settings_path(args)
    if args.bot_command == "setup":
        return setup(path)
    return unlink(path)


def setup(settings_path: Path, read: Callable[[str], str] | None = None) -> int:
    """Ask for the token with ``read`` (default ``getpass``: no echo), save it.

    Returns the exit status: 0 when saved and 2 when the token is empty or
    has spaces (nothing is written). Says where the file is, never what is in it. Does not ask
    Discord whether the token works.
    """
    text = (read or getpass.getpass)(
        "Bot token (it will not show as you type): "
    ).strip()
    problem = check_token(text)
    if problem is not None:
        logger.error("%s; nothing was saved", problem)
        return 2
    path = token_path(settings_path)
    write_token(path, text)
    print(f"Saved the bot token in {path}")
    return 0


def unlink(settings_path: Path) -> int:
    """Remove the link to the Discord account; return 0.

    Says whether an account had been linked. Does not touch the token.
    """
    linker = Linker(BotFile(bot_file_path(settings_path)))
    if linker.unlink():
        print("Unlinked. Run the launcher with --bot to link again.")
    else:
        print("No account was linked.")
    return 0


def check(settings_path: Path) -> Token | int:
    """Prepare ``page --bot``: return the token, or the exit status 2.

    Returns 2 (and logs why) when discord.py is not installed or the token
    file is missing or holds no token. Hides the token from the log handlers
    in place now. Does not connect to Discord.
    """
    if importlib.util.find_spec("discord") is None:
        logger.error(EXTRA_HINT)
        return 2
    token = read_token(token_path(settings_path))
    if token is None:
        logger.error(
            "no bot token: run 'python -m jedimaster bot setup' first "
            "(it saves the token beside the settings file)"
        )
        return 2
    install_log_filter(token)
    return token


def starter(
    control: Control, settings_path: Path, token: Token
) -> Callable[[object], None]:
    """Return the function for ``server.serve``'s ``on_start``; print the link code.

    When no account is linked, prints a link code and one instruction first.
    The returned function takes the running web application and starts the
    bot as a task in the same event loop, sharing ``control`` and sending
    pushes to the application's open pages. Needs discord.py.
    """
    from ..page.server import broadcast
    from .core import Core
    from .discord_door import Door

    store = BotFile(bot_file_path(settings_path))
    linker = Linker(store)
    code = linker.start()
    if code is not None:
        minutes = int(LIFETIME_SECONDS // 60)
        print(f"Link code: {code}")
        print(
            f"To link the bot, send /link {code} to it in a direct message "
            f"within {minutes} minutes."
        )
    tasks: list[asyncio.Task] = []

    def start(app: object) -> None:
        async def deliver(pushes: list[dict]) -> int:
            return await broadcast(app, pushes)

        door = Door(Core(control, linker), store, deliver)
        task = asyncio.get_running_loop().create_task(door.run(token))
        tasks.append(task)

    return start
