"""Command line of the matchmaker: ``python -m jedimaster matchmaker``.

Purpose:
    Run the host's matchmaker by itself: load or make the install's
    certificate, issue the games asked for, print what the players' launch
    descriptions and games need, and serve until Ctrl-C or SIGTERM. A test
    run and a host trying their router's port use it; the bot will issue its
    games inside the launcher's own process.

Flow:
    ``add_parser`` adds the ``matchmaker`` command; ``run`` imports the web
    framework and the certificate library (the ``page`` and ``matchmaker``
    extras), loads or makes the certificate beside the settings file, issues
    ``--games`` ids and serves on ``--port``; once bound it prints
    ``port N``, ``fingerprint sha256//...`` and one ``game ID`` line per
    game.

Invariants:
    - Exit status: 0 after Ctrl-C or SIGTERM, 2 for a usage problem (a
      missing extra, a port out of range or that cannot be bound, a
      certificate pair that cannot be used).
    - SIGTERM stops the server as Ctrl-C does, so a script or a service
      manager stops it cleanly; a script's background job starts with
      Ctrl-C's signal ignored and still stops this way.
    - The game ids appear only on standard output, never in a log line.
    - The default port is 8781, beside the page's 8780; the server binds
      every address of the computer unless ``--host`` names one.

Call:
    ``python -m jedimaster matchmaker --games 1``
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import signal
from pathlib import Path

from ..page.settings import default_settings_path

logger = logging.getLogger(__name__)

DEFAULT_PORT = 8781
DEFAULT_ICE_HOST = "localhost"


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``matchmaker`` command and its options to ``sub``.

    Returns None. Does not parse anything.
    """
    matchmaker = sub.add_parser(
        "matchmaker", help="serve the host's matchmaker for network battles"
    )
    matchmaker.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"the port the games reach (default {DEFAULT_PORT}; 0 takes a free one)",
    )
    matchmaker.add_argument(
        "--host",
        help="the one address to serve on (default: every address of this computer)",
    )
    matchmaker.add_argument(
        "--games", type=int, default=1, help="how many game ids to issue (default 1)"
    )
    matchmaker.add_argument(
        "--ice-host",
        default=DEFAULT_ICE_HOST,
        help=f"the STUN and TURN host the games are told (default {DEFAULT_ICE_HOST})",
    )
    matchmaker.add_argument(
        "--settings",
        help="the settings file; the certificate is kept beside it (default: the user's own)",
    )


def run(args: argparse.Namespace) -> int:
    """Serve the matchmaker until interrupted; return the exit status."""
    try:
        from .certificate import CertificateError
        from .certificate import ensure_certificate
        from .certificate import tls_context
        from .server import serve
    except ImportError as err:
        logger.error(
            "the matchmaker needs %s: pip install 'jedimaster[page,matchmaker]'",
            err.name,
        )
        return 2
    from .directory import Directory

    if not 0 <= args.port <= 65535:
        logger.error("port out of range: %d", args.port)
        return 2
    if args.games < 0:
        logger.error("the number of games cannot be negative: %d", args.games)
        return 2
    settings_path = Path(args.settings) if args.settings else default_settings_path()
    try:
        certificate = ensure_certificate(settings_path.parent)
    except (CertificateError, OSError) as err:
        logger.error("%s", err)
        return 2
    directory = Directory(args.ice_host)
    games = [directory.issue_game() for _ in range(args.games)]

    def announce(port: int) -> None:
        lines = [f"port {port}", f"fingerprint {certificate.fingerprint}"]
        lines += [f"game {game}" for game in games]
        print("\n".join(lines), flush=True)

    previous = signal.signal(signal.SIGTERM, signal.default_int_handler)
    try:
        asyncio.run(
            serve(directory, tls_context(certificate), args.port, args.host, announce)
        )
    except KeyboardInterrupt:
        logger.info("stopped")
    except OSError as err:
        logger.error("cannot serve on port %d: %s", args.port, err)
        return 2
    finally:
        signal.signal(signal.SIGTERM, previous)
    return 0
