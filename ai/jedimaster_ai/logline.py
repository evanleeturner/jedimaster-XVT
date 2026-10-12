"""The AI program's log lines, in the engine's line format.

Purpose:
    Write the program's log as the engine writes its own, so one search
    reads both: a two-line header that states the run's constants once, then
    one line per event,

        = jedimaster_ai run <id> fmt 1 date <YYYY-MM-DD> tz utc
        = cwd "<working directory>"
        HH:MM:SS.mmm L event key=value ...

    in UTC, with one letter for the level (D I W E C). The event's sentence
    lives in ``events.json`` beside this file, never in the line.

Invariants:
    - Lines go to standard error, never standard output, which carries
      answers only; and to a file as well when one is named, appended to.
    - The header is written before any line, to each place lines go.
    - The home folder is written as ~, since a log may be pasted into a
      public bug report.
"""

from __future__ import annotations

import datetime
import logging
import os
import typing as t

PROGRAM = "jedimaster_ai"
GRAMMAR = 1
LEVEL_LETTERS = {
    logging.DEBUG: "D",
    logging.INFO: "I",
    logging.WARNING: "W",
    logging.ERROR: "E",
    logging.CRITICAL: "C",
}
LEVELS = {
    "debug": logging.DEBUG,
    "info": logging.INFO,
    "warn": logging.WARNING,
    "error": logging.ERROR,
}


class LineFormatter(logging.Formatter):
    """Renders a record as ``HH:MM:SS.mmm L <message>`` in UTC."""

    def format(self, record: logging.LogRecord) -> str:
        moment = datetime.datetime.fromtimestamp(record.created, tz=datetime.UTC)
        letter = LEVEL_LETTERS.get(record.levelno, "C")
        return (
            f"{moment:%H:%M:%S}.{moment.microsecond // 1000:03d} {letter} "
            f"{record.getMessage()}"
        )


def shorten_home(path: str, home: str) -> str:
    """Return path with the home folder at its start written as ~.

    Either of the system's separators may follow the home folder: Windows
    takes / as well as \\.
    """
    separators = [sep for sep in (os.sep, os.altsep) if sep]
    starts = tuple(home + sep for sep in separators)
    if home and (path == home or path.startswith(starts)):
        return "~" + path[len(home) :]
    return path


def header_lines(run_id: str, today: datetime.date, cwd: str, home: str) -> list[str]:
    """Return the two header lines, each without its line end."""
    return [
        f"= {PROGRAM} run {run_id} fmt {GRAMMAR} date {today:%Y-%m-%d} tz utc",
        f'= cwd "{shorten_home(cwd, home)}"',
    ]


def install(
    level: int, run_id: str, log_file: str | None, stream: t.TextIO
) -> logging.Logger:
    """Send the package's log lines, header first, to stream and log_file.

    Returns the package's logger. The file is appended to.
    """
    now = datetime.datetime.now(datetime.UTC)
    header = header_lines(run_id, now.date(), os.getcwd(), os.path.expanduser("~"))
    handlers: list[logging.Handler] = [logging.StreamHandler(stream)]
    if log_file is not None:
        handlers.append(logging.FileHandler(log_file, mode="a", encoding="utf-8"))
    package = logging.getLogger(PROGRAM)
    package.setLevel(level)
    package.propagate = False
    for handler in list(package.handlers):
        package.removeHandler(handler)
        handler.close()
    for handler in handlers:
        handler.stream.write("\n".join(header) + "\n")
        handler.stream.flush()
        handler.setFormatter(LineFormatter())
        package.addHandler(handler)
    return package
