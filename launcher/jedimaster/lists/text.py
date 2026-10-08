"""Text helpers shared by the list readers: lines, words, numbers, quoting.

Purpose:
    Split a list file's bytes the two ways the game reads its lists (by line
    and by word), read numbers the way the game does, and quote text the way
    the answer sheets print it.

Flow:
    ``load`` turns a path or bytes into bytes and refuses what is not text;
    ``split_lines`` and ``split_words`` cut the bytes into numbered lines or
    words; ``atoi`` and ``whole_number`` read numbers; ``ascii_lower`` and
    ``c_quote`` shape text for the game's view and for printing.

Invariants:
    - Bytes decode as Latin-1, one character per byte, so no byte is lost
      or changed; the game keeps bytes, not characters.
    - A line's text never holds its line ending; the ending is kept beside
      it (``"\\r\\n"``, ``"\\n"``, ``"\\r"`` or ``""`` for a last line with
      none).
    - Line numbers count from 1.

Call:
    ``data, label = load(path_or_bytes); lines = split_lines(data)``
"""

from __future__ import annotations

import logging
import os
import re
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

END_MARK = "\x1a"
"""The old DOS end-of-file byte some list files end with."""

WHITESPACE = " \t\n\v\f\r"
"""The characters that separate words: C's ``isspace`` set."""

_ASCII_LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
_ATOI = re.compile(r"[ \t\n\v\f\r]*([+-]?[0-9]+)")
_WHOLE = re.compile(r"[+-]?[0-9]+")
_WORD = re.compile(r"[^ \t\n\v\f\r]+")


class ListFormatError(ValueError):
    """A list file this package cannot read at all."""


class NotTextError(ListFormatError):
    """The file holds a NUL byte, so it is not a text list."""

    def __init__(self, label: str, offset: int) -> None:
        super().__init__(f"{label}: NUL byte at offset {offset}: not a text list")
        self.offset = offset


@dataclass
class TextLine:
    """One line as written: its number, its text, and its line ending."""

    line: int
    text: str
    ending: str


@dataclass
class Word:
    """One word as written and the line it starts on."""

    line: int
    text: str


def load(source: str | os.PathLike[str] | bytes) -> tuple[bytes, str]:
    """Return the bytes of ``source`` and a label naming it for messages.

    ``source`` is a path or the file's bytes. Raises ``OSError`` when the
    path cannot be read, ``TypeError`` for any other kind of value, and
    ``NotTextError`` when the bytes hold a NUL byte. Does not check the
    list's kind or anything else about its content.
    """
    if isinstance(source, (bytes, bytearray)):
        data, label = bytes(source), "<bytes>"
    elif isinstance(source, (str, os.PathLike)):
        data, label = Path(source).read_bytes(), str(source)
    else:
        raise TypeError(f"a list is a path or bytes, not {type(source).__name__}")
    nul = data.find(b"\0")
    if nul >= 0:
        raise NotTextError(label, nul)
    logger.debug("loaded %s: %d bytes", label, len(data))
    return data, label


def decode(data: bytes) -> str:
    """Return ``data`` as text, one character per byte (Latin-1).

    Never fails: every byte maps to one character. Does not check that the
    bytes are ASCII.
    """
    return data.decode("latin-1")


def has_end_mark(data: bytes) -> bool:
    """Return True when the file's last byte is the 0x1A end mark, else False.

    Does not check what precedes the mark (a line ending or not).
    """
    return data.endswith(END_MARK.encode("latin-1"))


def split_lines(data: bytes) -> list[TextLine]:
    """Return the file's lines in order, numbered from 1.

    A line ends at a line feed; one carriage return just before it (or at
    the very end of the file) is moved into the ending. Text after the last
    line feed is a last line with ending ``""``; nothing after it is no
    line. A final 0x1A end mark is dropped first, so it never shows as a
    line. Does not skip comments or blank lines.
    """
    text = decode(data)
    if text.endswith(END_MARK):
        text = text[:-1]
    lines: list[TextLine] = []
    pieces = text.split("\n")
    for index, piece in enumerate(pieces):
        last = index == len(pieces) - 1
        if last and piece == "":
            break
        ending = "" if last else "\n"
        if piece.endswith("\r"):
            piece, ending = piece[:-1], "\r" + ending
        lines.append(TextLine(index + 1, piece, ending))
    logger.debug("split %d lines", len(lines))
    return lines


def split_words(
    data: bytes, skip_first_line: bool
) -> tuple[TextLine | None, list[Word]]:
    """Return the skipped first line (or None) and the words after it.

    Words are runs of characters other than C's whitespace; each keeps the
    number of the line it starts on. With ``skip_first_line`` the text up
    to and including the first line feed is the skipped line (the whole file
    when it has no line feed). The 0x1A end mark is not dropped: it is a
    word like any other, and the readers decide what it means.
    """
    text = decode(data)
    first: TextLine | None = None
    start = 0
    if skip_first_line and text:
        cut = text.find("\n")
        start = len(text) if cut < 0 else cut + 1
        raw = text[:start]
        body = raw[:-1] if raw.endswith("\n") else raw
        ending = "\n" if raw.endswith("\n") else ""
        if body.endswith("\r"):
            body, ending = body[:-1], "\r" + ending
        first = TextLine(1, body, ending)
    words = []
    line = text.count("\n", 0, start) + 1
    position = start
    for match in _WORD.finditer(text, start):
        line += text.count("\n", position, match.start())
        position = match.start()
        words.append(Word(line, match.group()))
    logger.debug("split %d words", len(words))
    return first, words


def atoi(text: str) -> int:
    """Return ``text`` read as a whole number the way C's ``atoi`` reads one.

    Leading whitespace, an optional sign, then digits; reading stops at the
    first other character. Returns 0 when no digit follows. Does not clamp
    to any integer width.
    """
    match = _ATOI.match(text)
    return int(match.group(1)) if match else 0


def whole_number(text: str) -> int | None:
    """Return the word ``text`` as a whole number, or None when it is not one.

    A number is an optional sign and one or more digits, nothing else, so a
    word with trailing letters is not one. Does not clamp to any integer
    width.
    """
    return int(text) if _WHOLE.fullmatch(text) else None


def scan_numbers(text: str, count: int) -> list[int]:
    """Return up to ``count`` whole numbers read from the start of ``text``.

    Each number may follow whitespace and ends where its digits end, as C's
    ``sscanf`` with ``%d`` conversions reads them; reading stops at the first
    place no number follows. Returns fewer than ``count`` numbers then.
    Does not check what follows the last number read.
    """
    values: list[int] = []
    position = 0
    while len(values) < count:
        match = _ATOI.match(text, position)
        if match is None:
            break
        values.append(int(match.group(1)))
        position = match.end()
    return values


def ascii_lower(text: str) -> str:
    """Return ``text`` with only the letters A to Z lowercased, as C does.

    Every other character, accented letters included, is returned as it is.
    """
    return text.translate(_ASCII_LOWER)


def c_quote(text: str) -> str:
    """Return ``text`` in double quotes with C escapes, as the sheets print it.

    Backslash and double quote are escaped with a backslash, a line feed
    prints as ``\\n``, other characters outside 32 to 126 as ``\\xNN``
    (two lowercase hex digits). Always returns a quoted string; does not
    check the text's length.
    """
    out = []
    for char in text:
        code = ord(char)
        if char == "\\":
            out.append("\\\\")
        elif char == '"':
            out.append('\\"')
        elif char == "\n":
            out.append("\\n")
        elif 32 <= code <= 126:
            out.append(char)
        else:
            out.append(f"\\x{code:02x}")
    return '"' + "".join(out) + '"'
