"""The link code and the linked account.

Purpose:
    Tie the launcher to exactly one Discord account. The launcher makes a
    short code and shows it on the host's terminal; sending it to the bot in
    a direct message proves the sender is the person at the keyboard. The
    linked account's id is then kept in ``bot.json``.

Flow:
    ``BotFile`` reads and writes ``bot.json`` (the linked user id and the
    digest of the last command sync). ``Linker.start`` makes a code when no
    account is linked; ``Linker.attempt`` checks what an account sent and
    answers with one outcome word; ``Linker.unlink`` removes the link.

Invariants:
    - A code is 8 characters from ``ALPHABET``, shown as ``XXXX-XXXX``, good
      for ``LIFETIME_SECONDS``, used once, and void after ``MAX_WRONG``
      wrong tries from any account.
    - A code is compared in constant time; lower case and a missing dash
      are accepted.
    - The code is never logged; only that one was made, used, expired or
      voided.
    - ``bot.json`` is written to a temporary file and then moved over, so a
      reader never sees half a file.

Call:
    ``linker = Linker(BotFile(path)); shown = linker.start(); linker.attempt(7, "4f7k9qx2")``
"""

from __future__ import annotations

import hmac
import json
import logging
import os
import secrets
import time
from collections.abc import Callable
from pathlib import Path

logger = logging.getLogger(__name__)

ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ"
"""No 0, 1, I, L, O or U: nothing a player can misread."""
CODE_LENGTH = 8
LIFETIME_SECONDS = 600.0
MAX_WRONG = 5
FILE_NAME = "bot.json"

LINKED = "linked"
ALREADY_LINKED = "already_linked"
WRONG = "wrong"
EXPIRED = "expired"
VOID = "void"
NO_CODE = "no_code"
OUTCOMES = (LINKED, ALREADY_LINKED, WRONG, EXPIRED, VOID, NO_CODE)
"""What ``Linker.attempt`` can answer."""


def bot_file_path(settings_path: Path) -> Path:
    """Return the path of ``bot.json`` beside ``settings_path``.

    Does not create or look at any file.
    """
    return settings_path.parent / FILE_NAME


def show_code(code: str) -> str:
    """Return ``code`` as ``XXXX-XXXX``. Does not check its length."""
    return f"{code[:4]}-{code[4:]}"


def normalize(text: str) -> str:
    """Return ``text`` as a code is stored: no spaces or dashes, upper case."""
    return "".join(text.replace("-", "").split()).upper()


class BotFile:
    """The bot's small JSON file: the linked account and the last sync."""

    def __init__(self, path: Path) -> None:
        """Read ``path`` once; a missing or broken file means nothing is kept.

        A broken file is logged at WARNING and left alone until a write.
        """
        self.path = path
        self.linked_user_id: int | None = None
        self.commands_digest: str | None = None
        self._read()

    def _read(self) -> None:
        try:
            doc = json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            logger.debug("no bot file at %s", self.path)
            return
        except (OSError, ValueError) as exc:
            logger.warning("cannot read the bot file %s: %s", self.path, exc)
            return
        if not isinstance(doc, dict):
            logger.warning("the bot file %s is not an object", self.path)
            return
        user = doc.get("linked_user_id")
        if isinstance(user, int) and not isinstance(user, bool) and user > 0:
            self.linked_user_id = user
        digest = doc.get("commands_digest")
        if isinstance(digest, str):
            self.commands_digest = digest

    def save(self) -> bool:
        """Write the file; return whether it was saved.

        Returns False (a WARNING is logged, the values still hold in memory)
        when the file cannot be written.
        """
        doc = {
            "linked_user_id": self.linked_user_id,
            "commands_digest": self.commands_digest,
        }
        temp = self.path.with_name(self.path.name + ".tmp")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temp.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
            os.replace(temp, self.path)
        except OSError as exc:
            logger.warning("cannot write the bot file %s: %s", self.path, exc)
            return False
        return True


class Linker:
    """The link code in play and the account it may link."""

    def __init__(
        self,
        store: BotFile,
        clock: Callable[[], float] = time.monotonic,
        draw: Callable[[str], str] = secrets.choice,
    ) -> None:
        """Hold the bot file; ``clock`` and ``draw`` stand in for time and chance."""
        self.store = store
        self.clock = clock
        self.draw = draw
        self._code: str | None = None
        self._made_at = 0.0
        self._wrong = 0
        self._voided = False

    @property
    def linked_user_id(self) -> int | None:
        """The linked account's id, or None."""
        return self.store.linked_user_id

    def start(self) -> str | None:
        """Make a code and return it as ``XXXX-XXXX``; None when already linked.

        Replaces any code in play and clears the wrong tries. Logs that a
        code was made, never the code.
        """
        if self.store.linked_user_id is not None:
            return None
        self._code = "".join(self.draw(ALPHABET) for _ in range(CODE_LENGTH))
        self._made_at = self.clock()
        self._wrong = 0
        self._voided = False
        logger.info(
            "a link code was made; it is good for %d minutes", LIFETIME_SECONDS // 60
        )
        return show_code(self._code)

    def attempt(self, user_id: int, text: str) -> str:
        """Check ``text`` from account ``user_id``; return one of ``OUTCOMES``.

        ``already_linked`` when an account is linked (the text is not
        looked at); ``no_code`` when none was made; ``void`` when the code
        has taken ``MAX_WRONG`` wrong tries (the fifth wrong try answers
        ``void`` too); ``expired`` after ``LIFETIME_SECONDS``; ``wrong``
        for a different code; ``linked`` for the right one, which is then
        used up and saved with ``user_id``. Does not check who ``user_id``
        is beyond that it is a number.
        """
        if self.store.linked_user_id is not None:
            logger.warning("refused a link attempt: already linked")
            return ALREADY_LINKED
        if self._code is None:
            logger.warning("refused a link attempt: no code in play")
            return NO_CODE
        if self._voided:
            logger.warning("refused a link attempt: the code is void")
            return VOID
        if self.clock() - self._made_at >= LIFETIME_SECONDS:
            self._code = None
            logger.warning("refused a link attempt: the code expired")
            return EXPIRED
        given = normalize(text).encode("utf-8")
        if not hmac.compare_digest(given, self._code.encode("utf-8")):
            self._wrong += 1
            if self._wrong >= MAX_WRONG:
                self._voided = True
                logger.warning(
                    "the link code is void after %d wrong tries; "
                    "restart the launcher for a new code",
                    MAX_WRONG,
                )
                return VOID
            logger.warning("refused a link attempt: wrong code")
            return WRONG
        self._code = None
        self.store.linked_user_id = user_id
        self.store.save()
        logger.info("the link code was used; account %d is linked", user_id)
        return LINKED

    def unlink(self) -> bool:
        """Remove the link; return whether an account had been linked.

        Does not make a new code.
        """
        was = self.store.linked_user_id is not None
        if was:
            self.store.linked_user_id = None
            self.store.save()
            logger.info("the account was unlinked")
        return was
