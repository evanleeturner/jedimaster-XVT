"""The bot's token file, and a log filter that keeps the token out of every log.

Purpose:
    Keep the bot's token on the host's computer in one file that only the
    host can read, load it for a run, and make sure no log line from any
    logger (discord.py's included) ever carries it.

Flow:
    ``token_path`` names the file beside the settings file. ``check_token``
    refuses an empty token or one with spaces. ``write_token`` creates the
    file so that only the current user can read it. ``read_token`` loads it into a
    ``Token``. ``install_log_filter`` puts a ``TokenFilter`` on the handlers,
    which replaces the token's text with ``[token]`` in every record.

Invariants:
    - The token file is created with mode 0600 on POSIX (set when the file
      is created, not changed afterwards). Windows has no such mode: the
      file sits in the user's own profile folder.
    - A ``Token`` shows as ``[token]`` in ``repr``, ``str`` and f-strings;
      ``reveal`` is the only way to read it.
    - No message of an error raised here holds the token.

Call:
    ``token = read_token(token_path(settings_path)); install_log_filter(token)``
"""

from __future__ import annotations

import logging
import os
from pathlib import Path

logger = logging.getLogger(__name__)

FILE_NAME = "bot_token"
MASK = "[token]"
FILE_MODE = 0o600


def token_path(settings_path: Path) -> Path:
    """Return the token file's path beside ``settings_path``.

    Does not create or look at any file.
    """
    return settings_path.parent / FILE_NAME


class Token:
    """The bot's token, held so that printing it prints ``[token]``."""

    def __init__(self, text: str) -> None:
        """Hold ``text`` (already checked by ``check_token``)."""
        self._text = text

    def reveal(self) -> str:
        """Return the token's text. Use only to log in."""
        return self._text

    def __repr__(self) -> str:
        return MASK

    def __str__(self) -> str:
        return MASK

    def __format__(self, spec: str) -> str:
        return MASK


def check_token(text: str) -> str | None:
    """Return None when ``text`` can be a token, else the reason in words.

    The reason never repeats the text. Does not ask Discord whether the
    token works.
    """
    if text == "":
        return "the token is empty"
    if any(ch.isspace() for ch in text):
        return "the token has spaces in it"
    return None


def write_token(path: Path, text: str) -> None:
    """Write ``text`` to ``path`` so that only the current user can read it.

    Raises ``ValueError`` (the reason, without the token) when ``check_token``
    refuses it, and ``OSError`` when the file cannot be written. An old file
    is replaced. The new file is made with mode 0600 in a temporary file in
    the same folder and then moved over ``path``.
    """
    problem = check_token(text)
    if problem is not None:
        raise ValueError(problem)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    if temp.exists():
        temp.unlink()
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, FILE_MODE)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
        os.replace(temp, path)
    except BaseException:
        temp.unlink(missing_ok=True)
        raise
    logger.info("the bot token was saved to %s", path)


def read_token(path: Path) -> Token | None:
    """Return the token in ``path``, or None when the file is missing or holds none.

    Surrounding whitespace is dropped. A token that ``check_token`` refuses
    gives None and a WARNING that does not repeat it. Does not check the mode
    of the file.
    """
    try:
        text = path.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        logger.debug("no token file at %s", path)
        return None
    except (OSError, UnicodeDecodeError) as exc:
        logger.warning("cannot read the token file %s: %s", path, type(exc).__name__)
        return None
    problem = check_token(text)
    if problem is not None:
        logger.warning("the token file %s is no good: %s", path, problem)
        return None
    return Token(text)


class TokenFilter(logging.Filter):
    """Replace the token's text with ``[token]`` in every record that passes."""

    def __init__(self, token: Token) -> None:
        """Hold the token to hide."""
        super().__init__()
        self._secret = token.reveal()

    def filter(self, record: logging.LogRecord) -> bool:
        """Hide the token in the message, its arguments and any traceback; return True.

        Always lets the record through. Works on the record itself, so every
        handler after this one sees the hidden text too.
        """
        try:
            message = record.getMessage()
        except (TypeError, ValueError):
            message = str(record.msg)
        if self._secret in message:
            record.msg = message.replace(self._secret, MASK)
            record.args = None
        if record.exc_info and record.exc_info[0] is not None:
            text = logging.Formatter().formatException(record.exc_info)
            if self._secret in text:
                record.exc_text = text.replace(self._secret, MASK)
        elif record.exc_text and self._secret in record.exc_text:
            record.exc_text = record.exc_text.replace(self._secret, MASK)
        if record.stack_info and self._secret in record.stack_info:
            record.stack_info = record.stack_info.replace(self._secret, MASK)
        return True


def install_log_filter(
    token: Token, handlers: list[logging.Handler] | None = None
) -> TokenFilter:
    """Put a ``TokenFilter`` on ``handlers`` (default: the root logger's); return it.

    Handlers added afterwards are not covered. Does not touch logger levels.
    """
    found = TokenFilter(token)
    for handler in logging.getLogger().handlers if handlers is None else handlers:
        handler.addFilter(found)
    return found
