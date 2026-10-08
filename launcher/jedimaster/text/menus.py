"""Read fronttxt.txt (the menus' words) and xvterr.txt (the error messages).

Purpose:
    The two plainest text files: one entry per line, numbered from 0. The
    menus ask for an entry by number and get ``No text.`` past the end;
    the error messages keep their line feed and turn ``\\n`` into one.

Flow:
    ``read_front``: lines of buffer 1,024, comments skipped, line feed
    dropped; every line, empty ones included, is the next entry.
    ``read_errors``: lines of buffer 512, comments kept, line feed kept;
    each becomes a message by ``error_text``. ``front_text`` and
    ``error_message`` answer a request by number.

Invariants:
    - Error messages count every piece of the file, comments included:
      message ``n`` is the ``n + 1``-th line.
    - An error message converts only its line's first 255 bytes: each
      backslash followed by ``n`` there becomes one line feed, every other
      byte stays; the message ends with those bytes. A backslash that is
      the 255th byte still looks at the byte after it.

Call:
    ``front_text(read_front(path), 12)``; ``error_message(read_errors(path), 0)``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass

from .lines import load
from .lines import read_lines

logger = logging.getLogger(__name__)

FRONT_BUFFER = 1024
ERRORS_BUFFER = 512
ERROR_BYTES = 255
"""The bytes of an error line the game converts and keeps."""
NO_TEXT = b"No text."
"""What a menu gets for an entry number at or past the count."""
BACKSLASH = 0x5C
LETTER_N = 0x6E
LINE_FEED = 0x0A


@dataclass
class FrontText:
    """The menus' entries, in file order, numbered from 0."""

    entries: list[bytes]
    lines: list[int]


@dataclass
class ErrorMessages:
    """The error messages, numbered from 0, as the game builds them."""

    messages: list[bytes]


def read_front(source: str | os.PathLike[str] | bytes | bytearray) -> FrontText:
    """Return fronttxt.txt's entries as the game reads them.

    Every line but a comment is the next entry, line feed dropped, empty
    lines included. Returns no entries for an empty file. Raises ``OSError``
    when the path cannot be read. Does not check the entries' contents.
    """
    data, label = load(source)
    lines = read_lines(data, FRONT_BUFFER, skip_comments=True)
    for i, line in enumerate(lines):
        logger.debug("front %d (line %d): %r", i, line.number, line.text)
    stored = sum(len(line.text) + 1 for line in lines)
    logger.info("front %s: %d entries, %d bytes stored", label, len(lines), stored)
    return FrontText([line.text for line in lines], [line.number for line in lines])


def front_text(front: FrontText, number: int) -> bytes:
    """Return entry ``number``, or ``No text.`` at or past the count.

    A negative number also gives ``No text.``. Does not log.
    """
    if 0 <= number < len(front.entries):
        return front.entries[number]
    return NO_TEXT


def error_text(line: bytes) -> bytes:
    """Return the message the game builds from one line of xvterr.txt.

    Converts the line's first 255 bytes: a backslash followed by ``n``
    becomes one line feed, every other byte is kept (the line's own final
    line feed included). Bytes past the first 255 are dropped. Does not
    check for comments.
    """
    out = bytearray()
    i = 0
    while i < min(len(line), ERROR_BYTES):
        if line[i] == BACKSLASH and i + 1 < len(line) and line[i + 1] == LETTER_N:
            out.append(LINE_FEED)
            i += 2
        else:
            out.append(line[i])
            i += 1
    return bytes(out)


def read_errors(source: str | os.PathLike[str] | bytes | bytearray) -> ErrorMessages:
    """Return xvterr.txt's messages as the game builds them.

    Every piece (buffer 512, comments counted) is message ``n`` in order,
    built by ``error_text``. Returns no messages for an empty file. Raises
    ``OSError`` when the path cannot be read. Does not check the texts.
    """
    data, label = load(source)
    lines = read_lines(data, ERRORS_BUFFER, skip_comments=False, keep_line_feed=True)
    messages = [error_text(line.text) for line in lines]
    for i, message in enumerate(messages):
        logger.debug("error %d: %r", i, message)
    logger.info("errors %s: %d messages", label, len(messages))
    return ErrorMessages(messages)


def error_message(errors: ErrorMessages, number: int) -> bytes | None:
    """Return message ``number``, or None when it does not exist.

    None for a number at or past the count, or negative. Does not log.
    """
    if 0 <= number < len(errors.messages):
        return errors.messages[number]
    return None
