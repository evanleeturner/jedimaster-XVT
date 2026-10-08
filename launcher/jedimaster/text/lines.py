"""Read a text file a line at a time, the way the game reads its text files.

Purpose:
    One line reader for every text file of the game: bytes, never Unicode,
    cut into lines as the game's buffered line reads cut them, for any
    buffer size; and the game's line buffer, for the readers that look
    past a line's end into what earlier, longer lines left there.

Flow:
    ``load`` turns a path or bytes into bytes and a label. ``LineStream``
    turns CR LF into LF once, then hands out pieces: each piece runs to and
    includes the next line feed, or holds ``buffer_size - 1`` bytes, or
    ends at the file's end. ``read_lines`` collects the pieces as ``Line``
    records, line feed dropped, comments skipped when asked. ``LineBuffer``
    keeps one buffer as the game fills it: each piece written from byte 0,
    an end mark (byte 0) after it, its final line feed turned into an end
    mark, every byte past those left as it was.

Invariants:
    - A carriage return directly before a line feed is dropped; any other
      carriage return stays. Every other byte keeps its value.
    - An empty file, or the file's end, gives no line; a final line without
      a line feed is a line.
    - A comment is a piece whose first two bytes are ``//``; nothing else is.
    - Each piece counts as a line of its own: ``Line.number`` counts every
      piece from 1, comments included.

Call:
    ``for line in read_lines(data, 1024, skip_comments=True): line.text``
"""

from __future__ import annotations

import logging
import os
from typing import NamedTuple

logger = logging.getLogger(__name__)

LINE_FEED = 0x0A
END_MARK = 0x00
COMMENT = b"//"


def load(source: str | os.PathLike[str] | bytes | bytearray) -> tuple[bytes, str]:
    """Return a file's bytes and a label for messages.

    ``source`` is a path or the bytes themselves (label ``<bytes>``).
    Raises ``OSError`` when the path cannot be read. Does not look at the
    bytes.
    """
    if isinstance(source, bytes | bytearray):
        return bytes(source), "<bytes>"
    with open(source, "rb") as handle:
        data = handle.read()
    logger.debug("read %s: %d bytes", source, len(data))
    return data, os.fspath(source)


class Line(NamedTuple):
    """One piece of a file: its number (from 1, every piece) and its bytes."""

    number: int
    text: bytes


class LineStream:
    """A file's bytes, CR LF already LF, read from ``position`` onwards.

    ``read_piece`` hands out the next piece as the game's line read does;
    other readers (the credits header) may move ``position`` themselves.
    """

    def __init__(self, data: bytes, buffer_size: int) -> None:
        if buffer_size < 2:
            raise ValueError(f"a line buffer needs 2 bytes or more, not {buffer_size}")
        self.data = data.replace(b"\r\n", b"\n")
        self.limit = buffer_size - 1
        self.position = 0
        self.pieces = 0

    def at_end(self) -> bool:
        """Return True when no byte is left to read, else False.

        Does not read anything.
        """
        return self.position >= len(self.data)

    def read_piece(self) -> bytes | None:
        """Return the next piece, its line feed kept, or None at the end.

        A piece runs to and includes the next line feed, or holds
        ``buffer_size - 1`` bytes, or ends at the file's end, whichever comes
        first. Does not drop the line feed or skip comments.
        """
        if self.at_end():
            return None
        start = self.position
        feed = self.data.find(b"\n", start, start + self.limit)
        stop = feed + 1 if feed >= 0 else min(start + self.limit, len(self.data))
        self.position = stop
        self.pieces += 1
        return self.data[start:stop]


def drop_line_feed(piece: bytes) -> bytes:
    """Return ``piece`` without its final line feed, if it has one.

    Always returns the piece's other bytes unchanged; does not touch a
    carriage return or a line feed anywhere else.
    """
    return piece[:-1] if piece.endswith(b"\n") else piece


def is_comment(piece: bytes) -> bool:
    """Return True when the piece's first two bytes are ``//``.

    Returns False for anything else, a piece of one byte included. Does not
    look past the first two bytes.
    """
    return piece.startswith(COMMENT)


def read_lines(
    data: bytes, buffer_size: int, skip_comments: bool, keep_line_feed: bool = False
) -> list[Line]:
    """Return every line of ``data`` as the game reads it with this buffer.

    Each piece of ``LineStream`` becomes a ``Line``: its line feed dropped
    (unless ``keep_line_feed``), comments left out when ``skip_comments``.
    Returns ``[]`` for empty data. Raises ``ValueError`` for a buffer under
    2 bytes. Does not stop at byte 26 or byte 0.
    """
    stream = LineStream(data, buffer_size)
    lines = []
    while (piece := stream.read_piece()) is not None:
        if skip_comments and is_comment(piece):
            logger.debug("line %d: comment skipped", stream.pieces)
            continue
        text = piece if keep_line_feed else drop_line_feed(piece)
        lines.append(Line(stream.pieces, text))
    logger.debug("%d lines from %d pieces", len(lines), stream.pieces)
    return lines


def c_string(buffer: bytes | bytearray, start: int = 0) -> bytes:
    """Return the bytes of ``buffer`` from ``start`` up to its next end mark.

    Returns ``b""`` when ``start`` is at or past the buffer's end. Runs to
    the buffer's end when no end mark follows. Does not copy past it.
    """
    stop = buffer.find(END_MARK, start)
    return bytes(buffer[start : stop if stop >= 0 else len(buffer)])


class LineBuffer:
    """The game's line buffer: each piece written over what earlier ones left.

    ``load`` writes a piece from byte 0, then an end mark, and turns the
    piece's final line feed into an end mark; every byte after those keeps
    what an earlier, longer piece left there (0 before the first).
    """

    def __init__(self, size: int) -> None:
        self.bytes = bytearray(size)

    def load(self, piece: bytes) -> int:
        """Write one piece the game's way; return its length, line feed dropped.

        Raises ``ValueError`` when the piece and its end mark do not fit.
        Does not look for comments.
        """
        if len(piece) + 1 > len(self.bytes):
            raise ValueError(f"a {len(piece)}-byte piece overflows the buffer")
        self.bytes[: len(piece)] = piece
        self.bytes[len(piece)] = END_MARK
        if piece.endswith(b"\n"):
            self.bytes[len(piece) - 1] = END_MARK
            return len(piece) - 1
        return len(piece)

    def byte(self, offset: int) -> int:
        """Return the byte at ``offset``; 0 at or past the buffer's end.

        Does not check for a negative offset.
        """
        return self.bytes[offset] if offset < len(self.bytes) else END_MARK

    def string(self, offset: int) -> bytes:
        """Return the bytes from ``offset`` up to the next end mark.

        Returns ``b""`` at or past the buffer's end. Does not copy them
        anywhere.
        """
        return c_string(self.bytes, offset)
