"""Read credits.txt, the credits pages, as the game reads them.

Purpose:
    Read the file page after page as the game does: a header of ten
    numbers scanned from the byte stream, then up to 32 lines into one of
    two page buffers, each line with its text and its 16-bit color, the
    color set by ``~`` lines and carried from line to line and page to page.

Flow:
    ``read_credits`` keeps one ``LineStream`` (buffer 256), one 256-byte
    ``LineBuffer``, the two page buffers and the color in force, and calls
    ``_read_page`` until a page says no other follows. A page: ``_header``
    scans up to ten numbers (``scan_number``); the page buffer is chosen by
    ``page_buffer`` and its 32 texts emptied; then lines, comments skipped,
    until a ``*`` line (another page follows) or the file's end (none
    does), the first 32 kept, ``~`` lines read by ``parse_color``.

Invariants:
    - A header number is read as the C library's ``%u`` reads one: white
      space skipped (line ends included), then a run of at most 511 bytes
      up to the next white space, whose leading sign and digits are the
      number (a minus wraps around 2^32, every value is kept modulo 2^32);
      the run's other bytes stay unread. A run without a leading number
      stops the header, nothing of it read. After the tenth number all
      white space is skipped. Comments are not skipped in the header.
    - A paragraph never read counts as 0. The page buffer is the paragraph
      less one, modulo 2^32; anything above 1 becomes 1.
    - Starting a page empties its buffer's 32 texts, never their colors; a
      line the page does not reach keeps the color the last page using
      that buffer gave it (0 when none did).
    - The color starts white (``0xffff``) and carries across pages.
    - A line's text is a C string: it ends at its first byte 0; a ``~``
      line's text starts after the bytes its three tokens read and, when
      they read its end mark, runs on into the line buffer's leftover bytes.

Call:
    ``pages = read_credits(path).pages; pages[0].lines[0].color``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass

from ..icons.game import color_565
from .joystick import leading_number
from .lines import c_string
from .lines import drop_line_feed
from .lines import is_comment
from .lines import LineBuffer
from .lines import LineStream
from .lines import load

logger = logging.getLogger(__name__)

BUFFER = 256
PAGE_LINES = 32
PAGE_BUFFERS = 2
HEADER_FIELDS = (
    "text_x",
    "text_y",
    "paragraph",
    "fade",
    "duration",
    "logo",
    "logo_x",
    "logo_y",
    "spare_1",
    "spare_2",
)
"""The header's ten numbers in file order; the last two are read, not kept."""
WHITE_SPACE = frozenset(b" \t\n\v\f\r")
RUN_LIMIT = 511
WORD = 1 << 32
WHITE = 0xFFFF
TOKENS = 3
TOKEN_BUFFER = 256
SPACE = 0x20
END_MARK = 0x00
PAGE_END = b"*"
COLOR_MARK = b"~"
SIGNS = b"+-"


@dataclass(frozen=True)
class CreditsLine:
    """One line of a page buffer: its text and its 16-bit 565 color."""

    text: bytes
    color: int


@dataclass
class CreditsPage:
    """One page: its header numbers, buffer, lines, and whether more follow."""

    index: int
    header: list[int]
    buffer: int
    more: bool
    used: int
    lines: list[CreditsLine]

    def value(self, name: str) -> int | None:
        """Return the header number called ``name``, or None when not read.

        Raises ``ValueError`` for a name not in ``HEADER_FIELDS``. Does not
        say what the game holds for a number it did not read.
        """
        position = HEADER_FIELDS.index(name)
        return self.header[position] if position < len(self.header) else None


@dataclass
class CreditsFile:
    """The pages in reading order; the last says no other follows."""

    pages: list[CreditsPage]


def _skip_white(stream: LineStream) -> None:
    data = stream.data
    while stream.position < len(data) and data[stream.position] in WHITE_SPACE:
        stream.position += 1


def scan_number(stream: LineStream) -> int | None:
    """Return the next header number, or None when the next run has none.

    Skips white space, takes the run up to the next white space (at most
    511 bytes) and reads its leading sign and digits, moving the stream
    past them; the value is kept modulo 2^32 (a minus wraps). Returns None,
    the stream left at the run's start, when the run does not start with a
    number or the stream has ended. Does not skip comments.
    """
    _skip_white(stream)
    data, start = stream.data, stream.position
    end = start
    while end < len(data) and end - start < RUN_LIMIT and data[end] not in WHITE_SPACE:
        end += 1
    run = data[start:end]
    digits = 1 if run[:1] and run[0] in SIGNS else 0
    stop = digits
    while stop < len(run) and 0x30 <= run[stop] <= 0x39:
        stop += 1
    if stop == digits:
        logger.debug("header run %r holds no number", run[:16])
        return None
    value = int(run[digits:stop]) * (-1 if run[:1] == b"-" else 1)
    stream.position = start + stop
    return value % WORD


def page_buffer(paragraph: int) -> int:
    """Return the page buffer a paragraph number gives: 0 or 1.

    The paragraph less one, modulo 2^32; any result above 1 becomes 1, so
    1 gives 0 and 0 or 2 give 1. Does not check the paragraph's range.
    """
    return min((paragraph - 1) % WORD, 1)


def parse_color(buffer: LineBuffer) -> tuple[tuple[int, int, int], int]:
    """Return a ``~`` line's red, green and blue and where its text starts.

    With ``L`` the line's length (``~`` included), each of three tokens
    reads bytes from after the ``~`` one at a time, each read lowering
    ``L``, until a space or end mark (passed over, ending the token) or
    until the token's count of bytes reaches ``L``; a token ended by the
    count runs on into what the shared token buffer (all zero at the
    line's start) held from earlier tokens. Each value is
    ``leading_number`` modulo 256. Does not check that the line starts
    with ``~``.
    """
    remaining = len(buffer.string(0))
    token = bytearray(TOKEN_BUFFER)
    position = 1
    values = []
    for _ in range(TOKENS):
        count = 0
        while count < remaining:
            byte = buffer.byte(position)
            position += 1
            remaining -= 1
            if byte in (SPACE, END_MARK):
                token[count] = END_MARK
                break
            token[count] = byte
            count += 1
        values.append(leading_number(c_string(token)) % 256)
    red, green, blue = values
    return (red, green, blue), position


def _header(stream: LineStream) -> list[int]:
    numbers: list[int] = []
    while len(numbers) < len(HEADER_FIELDS):
        value = scan_number(stream)
        if value is None:
            return numbers
        logger.debug("header %s: %d", HEADER_FIELDS[len(numbers)], value)
        numbers.append(value)
    _skip_white(stream)
    return numbers


class _Reader:
    """The game's state across pages: stream, line buffer, page buffers, color."""

    def __init__(self, data: bytes, label: str) -> None:
        self.stream = LineStream(data, BUFFER)
        self.line = LineBuffer(BUFFER)
        self.buffers = [
            [CreditsLine(b"", 0) for _ in range(PAGE_LINES)]
            for _ in range(PAGE_BUFFERS)
        ]
        self.color = WHITE
        self.label = label

    def next_line(self) -> bytes | None:
        """Return the next line that is not a comment, line feed dropped.

        Returns None at the file's end. Loads the line buffer. Does not
        check for a page end.
        """
        while (piece := self.stream.read_piece()) is not None:
            if not is_comment(piece):
                self.line.load(piece)
                return drop_line_feed(piece)
        return None

    def page_line(self, text: bytes) -> CreditsLine:
        """Return a loaded line as a page line, setting the color on ``~``.

        Always returns a line in the color then in force. Does not store it.
        """
        if not text.startswith(COLOR_MARK):
            return CreditsLine(self.line.string(0), self.color)
        rgb, start = parse_color(self.line)
        self.color = color_565(*rgb)
        logger.debug("color %s -> %04x", rgb, self.color)
        return CreditsLine(self.line.string(start), self.color)

    def read_page(self, index: int) -> CreditsPage:
        """Return the next page, read as the module's flow says.

        Always returns a page, its buffer's 32 lines as they stand after it.
        Does not check the header's values.
        """
        header = _header(self.stream)
        paragraph = header[2] if len(header) > 2 else 0
        buffer = page_buffer(paragraph)
        lines = self.buffers[buffer]
        lines[:] = [CreditsLine(b"", line.color) for line in lines]
        used = 0
        more = False
        while (text := self.next_line()) is not None:
            if text.startswith(PAGE_END):
                more = True
                break
            if used < PAGE_LINES:
                lines[used] = self.page_line(text)
                logger.debug("page %d line %d: %r", index, used, lines[used])
            used += 1
        if used > PAGE_LINES:
            logger.info(
                "%s: page %d has %d lines; the game keeps %d",
                self.label,
                index,
                used,
                PAGE_LINES,
            )
        logger.debug(
            "page %d: header %s, buffer %d, more %s", index, header, buffer, more
        )
        return CreditsPage(
            index, header, buffer, more, min(used, PAGE_LINES), list(lines)
        )


def read_credits(source: str | os.PathLike[str] | bytes | bytearray) -> CreditsFile:
    """Return credits.txt's pages as the game reads them, page after page.

    Reads until a page ends at the file's end (``more`` False): that page is
    the last. Every page holds its buffer's 32 lines as they stand after it.
    An empty file gives one page with no header and no lines. Raises
    ``OSError`` when the path cannot be read. Does not check the header's
    values or the logos.
    """
    data, label = load(source)
    reader = _Reader(data, label)
    pages = [reader.read_page(0)]
    while pages[-1].more:
        pages.append(reader.read_page(len(pages)))
    unread = sum(not page.header for page in pages)
    logger.info("credits %s: %d pages, %d with no header", label, len(pages), unread)
    return CreditsFile(pages)
