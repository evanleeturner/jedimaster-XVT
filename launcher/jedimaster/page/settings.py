"""The launcher's settings: one JSON file per user.

Purpose:
    Keep the player's settings between runs in one small JSON file, hand
    out the current values, and change one value at a time from the fixed
    set of names and values in ``protocol``.

Flow:
    ``default_settings_path`` names the file for the operating system.
    ``SettingsStore`` reads it once at creation (a missing file gives the
    defaults, a broken one gives the defaults and a WARNING without
    touching the file); ``set`` checks the name and value, changes the
    values and writes the file again.

Invariants:
    - A value held by the store is always one of its setting's allowed
      values.
    - A broken file is not overwritten until a ``set``.
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

from .protocol import DEFAULT_SETTINGS
from .protocol import SETTINGS

logger = logging.getLogger(__name__)

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


class SettingsStore:
    """The current settings and the file that keeps them."""

    def __init__(self, path: Path) -> None:
        """Read ``path`` once; never raises for a missing or broken file."""
        self.path = path
        self.values: dict[str, str] = dict(DEFAULT_SETTINGS)
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

    def _write(self) -> bool:
        temp = self.path.with_name(self.path.name + ".tmp")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temp.write_text(json.dumps(self.values, indent=2) + "\n", encoding="utf-8")
            os.replace(temp, self.path)
        except OSError as exc:
            logger.warning("cannot write the settings file %s: %s", self.path, exc)
            return False
        return True
