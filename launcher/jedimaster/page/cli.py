"""Command line of the local page: ``python -m jedimaster page``.

Purpose:
    Start the launcher's page on this computer: find the install, read the
    settings, draw this run's secret, serve, and open the browser.

Flow:
    ``add_parser`` adds the ``page`` command; ``run`` finds the install
    (``--install``, else ``find_install()``), builds the ``Control`` and
    runs ``server.serve`` until Ctrl-C; with ``--bot`` the bot starts in the
    same event loop and shares the ``Control``. ``--engine`` names the game
    program a solo battle is flown in (``--engine-arg`` hands it extra
    arguments); ``--pilot-folder`` names the folder of the game's pilot files,
    which defaults to the game's own user folder (``launch.pilots``). The web framework is imported only
    inside ``run``, so the package's other commands work without it.

Invariants:
    - The server binds to 127.0.0.1 only; the default port is 8780 and
      ``--port 0`` takes a free one.
    - The secret is drawn per run with ``secrets.token_urlsafe(32)``; it
      appears only in the URL printed on standard output.
    - Exit status: 0 after Ctrl-C, 2 for a usage problem (no web framework,
      a missing page folder, a port that cannot be bound).
    - Without ``--engine`` no battle can start, and the bot says so.
    - With ``--engine`` the pilot folder must be known: the default exists
      for Linux, macOS and Windows only; on any other system ``--pilot-folder``
      is required.
    - ``--page-dir`` defaults to ``launcher/page/dist`` beside the package
      in a source checkout; how the built page ships inside an installed
      package is not settled yet.

Call:
    ``python -m jedimaster page --install <folder> --port 0 --no-open``
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import secrets
import sys
from pathlib import Path

from .. import __version__
from ..install import find_install
from ..launch.pilots import default_pilot_folder
from .battles import Flight
from .control import Control
from .settings import default_settings_path
from .settings import SettingsStore

logger = logging.getLogger(__name__)

DEFAULT_PORT = 8780
DEFAULT_PAGE_DIR = Path(__file__).resolve().parents[2] / "page" / "dist"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``page`` command and its options to ``sub``.

    Returns None. Does not parse anything.
    """
    page = sub.add_parser("page", help="serve the launcher's page on this computer")
    page.add_argument("--install", help="install folder (default: look for one)")
    page.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"port on 127.0.0.1 (default {DEFAULT_PORT}; 0 takes a free one)",
    )
    page.add_argument("--settings", help="the settings file (default: the user's own)")
    page.add_argument("--page-dir", help="the built page (default: launcher/page/dist)")
    page.add_argument("--engine", help="the game program a solo battle is flown in")
    page.add_argument(
        "--engine-arg",
        action="append",
        default=[],
        help="one extra argument for the game program (repeatable)",
    )
    page.add_argument(
        "--pilot-folder",
        help="the folder of the game's pilot files (default: the game's user folder)",
    )
    page.add_argument(
        "--bot",
        action="store_true",
        help="also run the Discord bot (jedimaster bot setup first)",
    )
    page.add_argument(
        "--no-open", action="store_true", help="print the URL, do not open it"
    )


def flight_from(args: argparse.Namespace) -> Flight | None | int:
    """Return the ``Flight`` the options describe, None without ``--engine``, or the exit status 2.

    Logs why when the options do not fit: ``--engine-arg`` or
    ``--pilot-folder`` without ``--engine``, an engine that is not a file, or
    a system with no default pilot folder and no ``--pilot-folder``.
    """
    if not args.engine:
        if args.engine_arg or args.pilot_folder:
            logger.error("--engine-arg and --pilot-folder need --engine")
            return 2
        return None
    if not Path(args.engine).is_file():
        logger.error("engine not found: %s", args.engine)
        return 2
    folder = Path(args.pilot_folder) if args.pilot_folder else default_pilot_folder()
    if folder is None:
        logger.error(
            "no default pilot folder is known for %s: give --pilot-folder", sys.platform
        )
        return 2
    return Flight(args.engine, tuple(args.engine_arg), folder)


def run(args: argparse.Namespace) -> int:
    """Serve the page until interrupted; return the exit status.

    Returns 0 after Ctrl-C and 2 when the web framework is missing, the engine
    options do not fit (``flight_from``), the page folder does not exist, the
    port is out of range or cannot be bound. Does
    not build the page.
    """
    try:
        from .server import serve
    except ImportError:
        logger.error("the page needs aiohttp: pip install 'jedimaster[page]'")
        return 2
    settings_path = Path(args.settings) if args.settings else default_settings_path()
    token = None
    if args.bot:
        from ..bot import cli as bot_cli

        token = bot_cli.check(settings_path)
        if isinstance(token, int):
            return token
    flight = flight_from(args)
    if isinstance(flight, int):
        return flight
    page_dir = Path(args.page_dir) if args.page_dir else DEFAULT_PAGE_DIR
    if not page_dir.is_dir():
        logger.error("page folder not found: %s (build it: npm run build)", page_dir)
        return 2
    if not 0 <= args.port <= 65535:
        logger.error("port out of range: %d", args.port)
        return 2
    install = find_install(args.install) if args.install else find_install()
    if install is None:
        logger.warning("no install found: the page will say so")
    control = Control(install, SettingsStore(settings_path), __version__, flight)
    key = secrets.token_urlsafe(32)
    extra = {}
    if token is not None:
        extra["on_start"] = bot_cli.starter(control, settings_path, token)
    try:
        asyncio.run(serve(control, key, args.port, page_dir, not args.no_open, **extra))
    except KeyboardInterrupt:
        logger.info("stopped")
    except OSError as exc:
        logger.error("cannot serve on port %d: %s", args.port, exc)
        return 2
    return 0
