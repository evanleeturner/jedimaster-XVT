"""The launcher's settings: one JSON file per user.

Purpose:
    Keep the player's settings between runs in one small JSON file, hand
    out the current values, and change one value at a time from the fixed
    set of names and values in ``protocol``. The same file keeps each
    player's game options (``launch.options``) under ``game_options``, by the
    player's account id, holding only the options that player has set.

Flow:
    ``default_settings_path`` names the file for the operating system.
    ``SettingsStore`` reads it once at creation (a missing file gives the
    defaults, a broken one gives the defaults and a WARNING without
    touching the file); ``set`` checks the name and value, changes the
    values and writes the file again. ``set_option`` does the same for one
    game option of one player; ``get_options`` returns what a player has set.

Invariants:
    - A value held by the store is always one of its setting's allowed
      values; a stored game option is always one of its option's choices
      (matched by value and type).
    - A player with no game option set has no entry in the file.
    - A broken file is not overwritten until a ``set`` or ``set_option``.
    - A write goes to a temporary file in the same folder, then replaces
      the settings file, so a reader never sees half a file.

Call:
    ``store = SettingsStore(default_settings_path()); store.set("art_scaling", "engine_fit")``
"""

from __future__ import annotations

import json
import logging
import os
import sys
from pathlib import Path
from typing import Any

from ..launch.options import OPTIONS
from ..launch.options import text_of
from .protocol import DEFAULT_SETTINGS
from .protocol import MAX_PLAYER_DIGITS
from .protocol import SETTINGS

logger = logging.getLogger(__name__)

GAME_OPTIONS_KEY = "game_options"
APP_FOLDER = "jedimaster"
FILE_NAME = "settings.json"


def default_settings_path(
    platform: str | None = None, env: dict[str, str] | None = None
) -> Path:
    """Return the settings file's path for ``platform`` (default: this one).

    Linux and others: ``$XDG_CONFIG_HOME`` or ``~/.config``; macOS:
    ``~/Library/Application Support``; Windows: ``%APPDATA%`` (the home
    folder's ``AppData/Roaming`` when it is unset); each followed by
    ``jedimaster/settings.json``. ``env`` stands in for the environment.
    Does not create or look at any file.
    """
    platform = sys.platform if platform is None else platform
    env = dict(os.environ) if env is None else env
    home = Path.home()
    if platform == "darwin":
        base = home / "Library" / "Application Support"
    elif platform.startswith("win"):
        base = Path(env["APPDATA"]) if env.get("APPDATA") else home / "AppData/Roaming"
    else:
        base = Path(env["XDG_CONFIG_HOME"]) if env.get("XDG_CONFIG_HOME") else None
        base = base if base is not None else home / ".config"
    return base / APP_FOLDER / FILE_NAME


def _is_player(player: object) -> bool:
    """Say whether ``player`` is an account id: 1 to 20 ASCII digits as text."""
    return (
        isinstance(player, str)
        and player.isascii()
        and player.isdigit()
        and 1 <= len(player) <= MAX_PLAYER_DIGITS
    )


class SettingsStore:
    """The current settings and the file that keeps them."""

    def __init__(self, path: Path) -> None:
        """Read ``path`` once; never raises for a missing or broken file."""
        self.path = path
        self.values: dict[str, str] = dict(DEFAULT_SETTINGS)
        self.options: dict[str, dict[str, Any]] = {}
        self._read()

    def _read(self) -> None:
        try:
            text = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            logger.debug("no settings file at %s: defaults", self.path)
            return
        except (OSError, UnicodeDecodeError) as exc:
            logger.warning("cannot read the settings file %s: %s", self.path, exc)
            return
        try:
            data = json.loads(text)
        except json.JSONDecodeError as exc:
            logger.warning("the settings file %s is not JSON: %s", self.path, exc)
            return
        if not isinstance(data, dict):
            logger.warning("the settings file %s is not an object", self.path)
            return
        for name, allowed in SETTINGS.items():
            if name not in data:
                continue
            if data[name] in allowed:
                self.values[name] = data[name]
            else:
                logger.warning("the settings file has a bad value for %s", name)
        self._read_options(data.get(GAME_OPTIONS_KEY))

    def _read_options(self, stored: object) -> None:
        if stored is None:
            return
        if not isinstance(stored, dict):
            logger.warning("the game options in the settings file are not an object")
            return
        for player, chosen in stored.items():
            if not _is_player(player) or not isinstance(chosen, dict):
                logger.warning("the settings file has a bad game options entry")
                continue
            for name, value in chosen.items():
                if name in OPTIONS and text_of(name, value) is not None:
                    self.options.setdefault(player, {})[name] = value
                else:
                    logger.warning("the settings file has a bad game option %r", name)

    def get(self) -> dict[str, str]:
        """Return a copy of every setting's current value."""
        return dict(self.values)

    def set(self, name: str, value: str) -> bool:
        """Change one setting and write the file; return whether it was saved.

        Raises ``ValueError`` for a name or value off the fixed list; nothing
        changes then. Returns False (the value still holds in memory, a
        WARNING is logged) when the file cannot be written. Does not
        validate against anything but ``protocol.SETTINGS``.
        """
        if name not in SETTINGS or value not in SETTINGS[name]:
            raise ValueError(f"not a setting: {name!r} = {value!r}")
        self.values[name] = value
        logger.debug("setting %s = %s", name, value)
        return self._write()

    def get_options(self, player: str) -> dict[str, Any]:
        """Return a copy of the game options ``player`` has set (empty if none)."""
        return dict(self.options.get(player, {}))

    def set_option(self, player: str, name: str, value: Any) -> bool:
        """Set one game option of ``player`` and write the file; return whether it was saved.

        ``value`` is the description's value (``True``, 3, ``"hard"``), or
        None to leave the option as the game has it, which removes it. Raises
        ``ValueError`` for a player that is not an account id, an unknown
        option or a value that is not one of its choices; nothing changes
        then. Returns False (the value still holds in memory, a WARNING is
        logged) when the file cannot be written.
        """
        if not _is_player(player):
            raise ValueError(f"not a player: {player!r}")
        if name not in OPTIONS or (value is not None and text_of(name, value) is None):
            raise ValueError(f"not a game option: {name!r} = {value!r}")
        chosen = self.options.setdefault(player, {})
        if value is None:
            chosen.pop(name, None)
        else:
            chosen[name] = value
        if not chosen:
            del self.options[player]
        logger.debug("game option of %s: %s = %r", player, name, value)
        return self._write()

    def _write(self) -> bool:
        temp = self.path.with_name(self.path.name + ".tmp")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            document: dict[str, Any] = dict(self.values)
            if self.options:
                document[GAME_OPTIONS_KEY] = self.options
            temp.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
            os.replace(temp, self.path)
        except OSError as exc:
            logger.warning("cannot write the settings file %s: %s", self.path, exc)
            return False
        return True
