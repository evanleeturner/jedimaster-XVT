"""The launcher's own pilots: one file per Discord account, made at the first link.

Purpose:
    Keep each player's pilot on this computer in the launcher's own file,
    so a player who starts from Discord has a pilot without the game's
    screens. The game does not open these files.

Flow:
    ``pilot_folder`` names the folder beside the settings file.
    ``first_name`` makes a new pilot's name from the Discord names.
    ``PilotStore.get`` reads an account's pilot, ``make`` writes a new one
    and ``rename`` changes its name, once.

Invariants:
    - One file per account, ``<account id>.json``, holding exactly
      ``format`` (1), ``account`` (the id as text), ``name`` and
      ``renamed``.
    - A name in a file always passes ``name_ok``.
    - A pilot's name changes at most once: ``renamed`` turns true and stays.
    - A file is written whole: a temporary file in the same folder, then
      moved over the old one.
    - A file that is there but does not read as a pilot is never written
      over: ``PilotFileError`` says so and the caller decides.

Call:
    ``store = PilotStore(pilot_folder(settings_path)); pilot = store.get(7001)``
"""

from __future__ import annotations

import json
import logging
import os
from dataclasses import dataclass
from pathlib import Path

from .pilots import fit_name
from .pilots import name_ok

logger = logging.getLogger(__name__)

FOLDER_NAME = "pilots"
FORMAT = 1
KEYS = {"format", "account", "name", "renamed"}
FALLBACK_WORD = "Pilot"
FALLBACK_DIGITS = 4


class PilotFileError(ValueError):
    """A pilot file is there but cannot be used; the message names the file."""


@dataclass(frozen=True)
class Pilot:
    """One account's pilot: the account id, the name, whether it was renamed."""

    account: int
    name: str
    renamed: bool


def pilot_folder(settings_path: Path) -> Path:
    """Return the pilots' folder beside ``settings_path``.

    Does not create or look at any folder.
    """
    return settings_path.parent / FOLDER_NAME


def first_name(display: str | None, account_name: str, account: int) -> str:
    """Return a new pilot's name from the Discord names.

    The display name fitted by ``fit_name``; when nothing of it is left, the
    account name fitted the same way; when nothing of that is left either,
    ``Pilot`` and the account id's last four digits.
    """
    for text in (display or "", account_name):
        name = fit_name(text)
        if name:
            return name
    return f"{FALLBACK_WORD} {str(account)[-FALLBACK_DIGITS:]}"


class PilotStore:
    """The pilot files in one folder."""

    def __init__(self, folder: Path) -> None:
        """Hold ``folder``; nothing is read or made until asked."""
        self.folder = folder

    def path(self, account: int) -> Path:
        """Return the pilot file's path for ``account``; does not look at it."""
        return self.folder / f"{account}.json"

    def get(self, account: int) -> Pilot | None:
        """Return ``account``'s pilot, or None when it has no file.

        Raises ``PilotFileError`` when the file is there but cannot be read
        or is not this account's pilot in the format above.
        """
        path = self.path(account)
        try:
            text = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            logger.debug("no pilot file for account %d", account)
            return None
        except OSError as exc:
            raise PilotFileError(f"cannot read {path.name}: {exc}") from exc
        try:
            data = json.loads(text)
        except ValueError as exc:
            raise PilotFileError(f"{path.name} is not JSON") from exc
        if (
            not isinstance(data, dict)
            or set(data) != KEYS
            or type(data["format"]) is not int
            or data["format"] != FORMAT
            or data["account"] != str(account)
            or not isinstance(data["name"], str)
            or not name_ok(data["name"])
            or not isinstance(data["renamed"], bool)
        ):
            raise PilotFileError(f"{path.name} is not a pilot file of this launcher")
        pilot = Pilot(account, data["name"], data["renamed"])
        logger.debug("pilot read: %s", pilot)
        return pilot

    def make(self, account: int, name: str) -> Pilot:
        """Write a new pilot named ``name`` for ``account``; return it.

        Raises ``PilotFileError`` when ``account`` has a file already, good
        or not, and ``ValueError`` when ``name`` fails ``name_ok``.
        """
        if not name_ok(name):
            raise ValueError(f"not a pilot name: {name!r}")
        if self.path(account).exists():
            raise PilotFileError(f"{self.path(account).name} is there already")
        pilot = Pilot(account, name, renamed=False)
        self._write(pilot)
        logger.info("pilot made for account %d: %s", account, name)
        return pilot

    def rename(self, account: int, name: str) -> Pilot:
        """Give ``account``'s pilot the name ``name``, its one change; return it.

        Raises ``PilotFileError`` when there is no pilot or it was renamed
        before, and ``ValueError`` when ``name`` fails ``name_ok``.
        """
        if not name_ok(name):
            raise ValueError(f"not a pilot name: {name!r}")
        old = self.get(account)
        if old is None:
            raise PilotFileError(f"no pilot for account {account}")
        if old.renamed:
            raise PilotFileError(f"the pilot of account {account} was renamed before")
        pilot = Pilot(account, name, renamed=True)
        self._write(pilot)
        logger.info("pilot of account %d renamed: %s -> %s", account, old.name, name)
        return pilot

    def _write(self, pilot: Pilot) -> None:
        self.folder.mkdir(parents=True, exist_ok=True)
        path = self.path(pilot.account)
        temp = path.with_name(path.name + ".tmp")
        data = {
            "format": FORMAT,
            "account": str(pilot.account),
            "name": pilot.name,
            "renamed": pilot.renamed,
        }
        temp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        os.replace(temp, path)
        logger.debug("pilot file written: %s", path)
