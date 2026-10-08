"""The game's view of an install's text files: each resolved, read and kept.

Purpose:
    Resolve the six text files of an install the way the game does
    (``BalanceOfPower/`` first, or not, any letter case), read each with
    its kind's reader, and keep the result, or the reason there is none,
    so a whole view always builds: the base game's ``strings.txt``, which
    the game refuses, is kept as its refusal.

Flow:
    ``text_view`` walks ``TEXT_FILES``: ``resolve`` each game name, read it
    with ``READERS[kind]``, record a ``TextFile``; ``read_kind`` reads one
    file of a kind for the command line.

Invariants:
    - The kinds are the sheets' names: ``strings``, ``front``, ``specs``,
      ``errors``, ``joystick``, ``credits``, in that order.
    - A file that does not resolve, cannot be read or is refused keeps
      ``result`` None and its ``error``; nothing is guessed in its place.
    - ``file`` is the sheets' label: the game name lowercased, behind
      ``BalanceOfPower/`` when it resolved there.

Call:
    ``view = text_view(install); view.files["front"].result.entries``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..lists.files import resolve
from ..lists.files import sheet_file
from .credits import read_credits
from .joystick import read_joystick
from .menus import read_errors
from .menus import read_front
from .specs import read_specs
from .strings import read_strings
from .strings import StringsFormatError

logger = logging.getLogger(__name__)

TEXT_FILES: tuple[tuple[str, str], ...] = (
    ("strings", "strings.txt"),
    ("front", "fronttxt.txt"),
    ("specs", "specdesc.txt"),
    ("errors", "xvterr.txt"),
    ("joystick", "joystick.txt"),
    ("credits", "credits.txt"),
)
"""Each kind and the game name of its file, in the sheets' order."""

READERS: dict[str, Any] = {
    "strings": read_strings,
    "front": read_front,
    "specs": read_specs,
    "errors": read_errors,
    "joystick": read_joystick,
    "credits": read_credits,
}
"""Each kind's reader: a path or bytes in, its result out."""

GAME_NAMES = dict(TEXT_FILES)


@dataclass
class TextFile:
    """One text file of a view: kind, game name, where it resolved, result."""

    kind: str
    name: str
    path: Path | None
    file: str
    result: Any
    error: Exception | None


@dataclass
class TextView:
    """An install's text files as the game reads them, for one view."""

    install: Path
    balance_of_power: bool
    files: dict[str, TextFile]


def read_kind(kind: str, source: str | os.PathLike[str] | bytes | bytearray) -> Any:
    """Return one file read by its kind's reader.

    Raises ``KeyError`` for an unknown kind, and whatever the reader raises
    (``StringsFormatError`` for a refused ``strings.txt``, ``OSError`` for
    an unreadable path). Does not resolve game paths.
    """
    return READERS[kind](source)


def _read_one(install: Path, kind: str, name: str, balance_of_power: bool) -> TextFile:
    path = resolve(install, name, balance_of_power)
    if path is None or not path.is_file():
        logger.warning("%s: %s not found", install, name)
        error = FileNotFoundError(f"{name} not found")
        return TextFile(kind, name, None, name.lower(), None, error)
    label = sheet_file(install, name, path)
    try:
        result = read_kind(kind, path)
    except (StringsFormatError, OSError) as exc:
        logger.warning("%s refused: %s", label, exc)
        return TextFile(kind, name, path, label, None, exc)
    logger.debug("%s: %s read from %s", kind, name, path)
    return TextFile(kind, name, path, label, result, None)


def text_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> TextView:
    """Return an install's six text files as the game reads them.

    Each file of ``TEXT_FILES`` resolves in ``BalanceOfPower/`` first
    (unless ``balance_of_power`` is False), any letter case. Always
    returns all six kinds in order; a file not found, unreadable or
    refused keeps ``result`` None and its ``error``, with a WARNING. Does
    not check that the install is one.
    """
    install = Path(install)
    files = {
        kind: _read_one(install, kind, name, balance_of_power)
        for kind, name in TEXT_FILES
    }
    logger.info(
        "text files: %d of %d read, balance of power %s",
        sum(f.result is not None for f in files.values()),
        len(files),
        balance_of_power,
    )
    return TextView(install, balance_of_power, files)
