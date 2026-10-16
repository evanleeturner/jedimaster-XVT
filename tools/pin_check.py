#!/usr/bin/env python3
"""Run the game's lobby pin check against a real host's matchmaker.

Purpose: a network battle's game reaches the host's matchmaker over TLS and
trusts it by the key fingerprint the matchmaker prints, since no authority
signed the certificate the matchmaker made itself. This check starts
``python -m jedimaster matchmaker`` on 127.0.0.1 and runs the engine's test
program (engine/tests/test_dplay_http_pin.c, built against the system's own
curl) with the printed port, fingerprint and game id, so each system's curl
is shown to let in the pinned key and refuse any other.

Flow:
  1. start the matchmaker on a free port of 127.0.0.1, its certificate in a
     fresh folder
  2. read the port, fingerprint and game id it announces, within 30 s
  3. run the test program with them; its output passes through
  4. stop the matchmaker (SIGTERM; on Windows, terminate) and wait for it

Invariants:
  - the matchmaker never outlives this check: it is stopped on every path,
    and killed if it has not ended 20 s after the stop
  - the game id is passed to the test program and never logged
  - stdlib only; the launcher with its page and matchmaker extras must be
    importable by --python

Call: pin_check.py TEST_PROGRAM [--python PATH] [--log-level LEVEL]
  -> the test program's exit status; 2 when the matchmaker did not announce
     itself
"""

from __future__ import annotations

import argparse
import logging
import signal
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

logger = logging.getLogger(__name__)

ANNOUNCE_SECONDS = 30
STOP_SECONDS = 20
PROGRAM_SECONDS = 120
ANNOUNCED = ("port", "fingerprint", "game")


def start(python: str, folder: Path) -> subprocess.Popen:
    """Start the matchmaker with one game on a free port of 127.0.0.1."""
    command = [python, "-m", "jedimaster", "matchmaker", "--host", "127.0.0.1"]
    command += ["--port", "0", "--games", "1"]
    command += ["--settings", str(folder / "settings.json")]
    logger.debug("starting %s", " ".join(command))
    return subprocess.Popen(command, stdout=subprocess.PIPE, text=True)


def announced(process: subprocess.Popen) -> dict[str, str]:
    """Return the announced port, fingerprint and game; fewer if it stopped.

    A matchmaker that has not announced all three within ANNOUNCE_SECONDS
    is killed, so the read ends.
    """
    watchdog = threading.Timer(ANNOUNCE_SECONDS, process.kill)
    watchdog.start()
    found: dict[str, str] = {}
    try:
        while len(found) < len(ANNOUNCED):
            line = process.stdout.readline()
            if not line:
                break
            name, _, value = line.strip().partition(" ")
            if name in ANNOUNCED:
                found[name] = value
                if name != "game":
                    logger.debug("matchmaker %s=%s", name, value)
    finally:
        watchdog.cancel()
    return found


def stop(process: subprocess.Popen) -> None:
    """Stop the matchmaker and wait for it; kill it after STOP_SECONDS."""
    if process.poll() is None:
        if sys.platform == "win32":
            process.terminate()
        else:
            process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=STOP_SECONDS)
    except subprocess.TimeoutExpired:
        logger.warning("the matchmaker did not stop in %d s; killed", STOP_SECONDS)
        process.kill()
        process.wait()
    logger.info("matchmaker ended with status %s", process.returncode)


def run(program: str, python: str) -> int:
    """Run the test program against a fresh matchmaker; return its status."""
    with tempfile.TemporaryDirectory() as folder:
        process = start(python, Path(folder))
        try:
            found = announced(process)
            if len(found) < len(ANNOUNCED):
                logger.error(
                    "the matchmaker announced %s, not all of %s",
                    sorted(found),
                    ", ".join(ANNOUNCED),
                )
                return 2
            logger.info("matchmaker serving on port %s", found["port"])
            try:
                result = subprocess.run(
                    [program, found["port"], found["fingerprint"], found["game"]],
                    timeout=PROGRAM_SECONDS,
                )
            except subprocess.TimeoutExpired:
                logger.error("the test program ran past %d s", PROGRAM_SECONDS)
                return 1
            logger.info("test program ended with status %d", result.returncode)
            return result.returncode
        finally:
            stop(process)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("program", help="the built test_dplay_http_pin program")
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="the Python that runs the launcher (default: this one)",
    )
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args(argv)
    logging.basicConfig(
        level=args.log_level.upper(), format="%(levelname)s %(message)s"
    )
    return run(args.program, args.python)


if __name__ == "__main__":
    sys.exit(main())
