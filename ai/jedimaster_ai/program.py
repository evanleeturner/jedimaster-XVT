"""The AI program's run: answer the engine's questions until it stops.

Purpose:
    The engine starts the program as a child process and writes questions
    to its standard input. The program answers each on standard output,
    until standard input ends, the engine's sign to stop.

Flow:
    1. read the options and install the log lines (``logline``)
    2. read one question, think, write the answer and flush it; again
    3. at the end of standard input, exit 0

Invariants:
    - Standard output carries answers only.
    - A question that breaks the format ends the program with exit code 2
      after one ERROR line; it is never answered.
    - Any other failure ends it with exit code 1 after one CRITICAL line
      naming the error and where it was raised; never a bare traceback.

Call:
    ``python3 ai/jedimaster_ai [--log-level LEVEL] [--log-file PATH]
    [--run-id ID]``; ``--run-id`` is the game's run id, so the two logs
    name the same run.
"""

from __future__ import annotations

import argparse
import logging
import os
import re
import sys
import time
import traceback
import typing as t

from . import logline
from .think import think
from .wire import answer_bytes
from .wire import read_message
from .wire import read_question
from .wire import VERSION
from .wire import WireError

logger = logging.getLogger(__name__)

RUN_ID = re.compile(r"[0-9a-f]{8}")


def _run_id(text: str) -> str:
    if not RUN_ID.fullmatch(text):
        raise argparse.ArgumentTypeError(f"a run id is 8 hex digits, not {text!r}")
    return text


def parse_options(argv: list[str] | None) -> argparse.Namespace:
    """Return the options in argv, or in the command line when argv is None."""
    parser = argparse.ArgumentParser(
        prog="jedimaster_ai",
        description="Answer the engine's questions about computer craft.",
    )
    parser.add_argument("--log-level", choices=logline.LEVELS, default="info")
    parser.add_argument("--log-file", help="also append the log lines to this file")
    parser.add_argument(
        "--run-id",
        type=_run_id,
        help="the game's run id; else one from the start time",
    )
    return parser.parse_args(argv)


def answer_stream(source: t.BinaryIO, sink: t.BinaryIO) -> int:
    """Answer each question in source on sink; return how many were answered."""
    answered = 0
    while (body := read_message(source)) is not None:
        sink.write(answer_bytes(think(read_question(body))))
        sink.flush()
        answered += 1
    return answered


def main(argv: list[str] | None = None) -> int:
    """Run the program; return its exit code."""
    options = parse_options(argv)
    run_id = options.run_id or f"{int(time.time()) & 0xFFFFFFFF:08x}"
    logline.install(
        logline.LEVELS[options.log_level], run_id, options.log_file, sys.stderr
    )
    logger.info("program.started wire=%d pid=%d", VERSION, os.getpid())
    try:
        answered = answer_stream(sys.stdin.buffer, sys.stdout.buffer)
    except WireError as err:
        logger.error('program.refused error="%s"', err)
        return 2
    except Exception as err:  # the contract: every failure ends in one log line
        frame = traceback.extract_tb(err.__traceback__)[-1]
        logger.critical(
            'program.crashed error_type="%s" error="%s" where="%s"',
            type(err).__name__,
            str(err).replace('"', "'"),
            f"{os.path.basename(frame.filename)}:{frame.lineno}",
        )
        return 1
    logger.info("program.stopped questions=%d", answered)
    return 0
