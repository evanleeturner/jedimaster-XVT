"""The game's view of an install's four menu fonts: each resolved and read.

Purpose:
    Resolve ``times10.abp``, ``times12.abp``, ``times15.abp`` and
    ``times20.abp`` the way the game does (``BalanceOfPower/`` first, or
    not, any letter case) and read each with ``read_font``.

Flow:
    ``fonts_view`` walks ``FONT_FILES``: resolve, read, record a
    ``FontFile``; a font that cannot be found or read keeps its error.

Invariants:
    - The kinds are the sheets' names ``font10`` to ``font20``; the game
      files each font under the point size its header gives, not under its
      file name's number.
    - ``file`` is the sheets' label: the game name lowercased, behind
      ``BalanceOfPower/`` when it resolved there.

Call:
    ``fonts_view(install).fonts["font10"].font.height``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path

from ..lists.files import resolve
from ..lists.files import sheet_file
from .reader import Font
from .reader import FontFormatError
from .reader import read_font

logger = logging.getLogger(__name__)

FONT_FILES: tuple[tuple[str, str], ...] = (
    ("font10", "times10.abp"),
    ("font12", "times12.abp"),
    ("font15", "times15.abp"),
    ("font20", "times20.abp"),
)
"""Each font's kind and game name, in the sheets' order."""


@dataclass
class FontFile:
    """One font of a view: kind, game name, where it resolved, the font."""

    kind: str
    name: str
    path: Path | None
    file: str
    font: Font | None
    error: Exception | None


@dataclass
class FontsView:
    """An install's menu fonts as the game loads them, for one view."""

    install: Path
    balance_of_power: bool
    fonts: dict[str, FontFile]


def _read_one(install: Path, kind: str, name: str, balance_of_power: bool) -> FontFile:
    path = resolve(install, name, balance_of_power)
    if path is None or not path.is_file():
        logger.warning("%s: %s not found", install, name)
        return FontFile(kind, name, None, name, None, FileNotFoundError(name))
    label = sheet_file(install, name, path)
    try:
        font = read_font(path)
    except (FontFormatError, OSError) as exc:
        logger.warning("%s: cannot read: %s", label, exc)
        return FontFile(kind, name, path, label, None, exc)
    return FontFile(kind, name, path, label, font, None)


def fonts_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> FontsView:
    """Return an install's four menu fonts as the game loads them.

    Always returns the four kinds in order; a font not found or unreadable
    keeps ``font`` None and its ``error``, with a WARNING. Does not check
    that the point sizes match the names.
    """
    install = Path(install)
    fonts = {
        kind: _read_one(install, kind, name, balance_of_power)
        for kind, name in FONT_FILES
    }
    logger.info(
        "fonts: %d of %d read, balance of power %s",
        sum(f.font is not None for f in fonts.values()),
        len(fonts),
        balance_of_power,
    )
    return FontsView(install, balance_of_power, fonts)
