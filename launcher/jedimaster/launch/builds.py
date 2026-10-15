"""The folder of installed game builds and the one that is current.

Purpose:
    ``jedimaster update`` installs game builds from CI; the launcher flies
    battles in the current one. This module knows where they live and which
    one is current, and nothing else: it downloads and starts nothing.

Flow:
    ``default_builds_dir`` names the folder for the operating system.
    Each build is a folder named by the first 12 characters of its commit.
    ``builds/current`` is a text file holding the current build's folder
    name; ``builds/previous`` holds the one it replaced. ``set_current``
    rewrites both whole (a temporary file, then ``os.replace``), so a reader
    sees the old name or the new one, never half of one. ``current_engine``
    is read at the start of each battle, so a running launcher takes a new
    build at its next battle. ``prune`` removes the oldest installs.

Invariants:
    - ``current`` names a plain folder name (no separator) that holds the
      engine program; anything else reads as no current build.
    - ``prune`` keeps the ``KEEP`` newest installs, and never removes the
      current build or the one it replaced.
    - A folder that ends in ``.partial`` is never a build.

Call:
    ``engine = current_engine(default_builds_dir())``
"""

from __future__ import annotations

import logging
import os
import shutil
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

APP_FOLDER = "jedimaster"
BUILDS_FOLDER = "builds"
CURRENT = "current"
PREVIOUS = "previous"
KEEP = 3
ENGINE_NAMES = ("OpenXvT", "OpenXvT.exe")
INSTALLED_MARKER = ".installed"
"""The file ``update`` writes into a build when it installs it; its time orders the builds."""


def default_builds_dir(
    platform: str | None = None, env: dict[str, str] | None = None
) -> Path:
    """Return the builds folder for ``platform`` (default: this one).

    Linux and others: ``$XDG_DATA_HOME`` or ``~/.local/share``; macOS:
    ``~/Library/Application Support``; Windows: ``%LOCALAPPDATA%`` (the home
    folder's ``AppData/Local`` when it is unset); each followed by
    ``jedimaster/builds``. ``env`` stands in for the environment. Does not
    create or look at any folder.
    """
    platform = sys.platform if platform is None else platform
    env = dict(os.environ) if env is None else env
    home = Path.home()
    if platform == "darwin":
        base = home / "Library" / "Application Support"
    elif platform.startswith("win"):
        base = (
            Path(env["LOCALAPPDATA"])
            if env.get("LOCALAPPDATA")
            else home / "AppData/Local"
        )
    else:
        base = Path(env["XDG_DATA_HOME"]) if env.get("XDG_DATA_HOME") else None
        base = base if base is not None else home / ".local" / "share"
    return base / APP_FOLDER / BUILDS_FOLDER


def engine_in(build: Path) -> Path | None:
    """Return the engine program inside the build folder ``build``, or None."""
    for name in ENGINE_NAMES:
        if (build / name).is_file():
            return build / name
    return None


def _named(builds: Path, which: str) -> str | None:
    """Return the folder name the file ``builds/<which>`` holds, or None."""
    try:
        name = (builds / which).read_text(encoding="utf-8").strip()
    except OSError:
        return None
    if not name or name != Path(name).name or name.endswith(".partial"):
        return None
    return name


def current_name(builds: Path) -> str | None:
    """Return the current build's folder name, or None when there is none usable."""
    name = _named(builds, CURRENT)
    if name is None or engine_in(builds / name) is None:
        return None
    return name


def current_engine(builds: Path | None) -> Path | None:
    """Return the current build's engine program, or None.

    ``builds`` None means the default folder. Reads the file each call.
    """
    folder = default_builds_dir() if builds is None else builds
    name = current_name(folder)
    return engine_in(folder / name) if name else None


def _installed_at(build: Path) -> float:
    """Return when ``build`` was installed: its marker file's time, else the folder's."""
    marker = build / INSTALLED_MARKER
    return (marker if marker.is_file() else build).stat().st_mtime


def installed(builds: Path) -> list[str]:
    """Return the installed builds' folder names, newest install first."""
    if not builds.is_dir():
        return []
    found = [
        p
        for p in builds.iterdir()
        if p.is_dir() and not p.name.endswith(".partial") and engine_in(p)
    ]
    found.sort(key=lambda p: (_installed_at(p), p.name), reverse=True)
    return [p.name for p in found]


def _write_whole(path: Path, text: str) -> None:
    """Replace ``path`` with ``text`` whole, through a temporary file beside it."""
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text + "\n", encoding="utf-8")
    os.replace(temporary, path)


def set_current(builds: Path, name: str) -> None:
    """Make the installed build ``name`` current and remember the one it replaced."""
    old = current_name(builds)
    if old is not None and old != name:
        _write_whole(builds / PREVIOUS, old)
    _write_whole(builds / CURRENT, name)
    logger.info("current build: %s (was %s)", name, old)


def prune(builds: Path) -> list[str]:
    """Remove the oldest installs beyond ``KEEP``; return the names removed.

    Never removes the current build or the one it replaced.
    """
    protected = {_named(builds, CURRENT), _named(builds, PREVIOUS)}
    removed = []
    for name in installed(builds)[KEEP:]:
        if name in protected:
            continue
        shutil.rmtree(builds / name)
        removed.append(name)
        logger.info("removed the old build %s", name)
    return removed
