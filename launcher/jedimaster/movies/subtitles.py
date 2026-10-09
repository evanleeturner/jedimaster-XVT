"""Read a movie's subtitle file and work out when each caption is shown.

Purpose:
    Turn the bytes of a ``movies\\NAME.txt`` file into its records (a number
    and three lines each) the way the game reads them, say why reading
    stopped, and from the records and the movie's frame count and frame
    duration give the captions the game shows: the frames, the times and
    the three lines.

Flow:
    ``read_records`` skips blanks, reads a number, skips blanks again, then
    reads three lines; it repeats until the end of the file, an unreadable
    number or a record numbered 65535. ``shown_captions`` walks the frames
    as the game does: before each frame it reads records while the number
    read last is not past that frame, and a stop clears the lines.

Invariants:
    - Blanks are space, tab, line feed, vertical tab, form feed and carriage
      return.
    - A number is an optional ``+`` or ``-`` and decimal digits, kept as an
      unsigned 32-bit value (wrapping; a value past 64 bits saturates first,
      so a huge number reads as 4,294,967,295), so ``-5`` is 4,294,967,291.
    - A line is read without its line end, at most 255 characters; a CR LF
      pair ends it like a lone LF, a lone CR is a character, a NUL byte
      ends its text, a line that starts with ``.`` reads as empty.
    - Bytes become characters one to one (Latin-1).
    - A record numbered 65535 ends the reading and is not counted.
    - A WARNING is logged where the game reports an unreadable number.

Call:
    ``file = read_records(data); shown_captions(file.records, 150, 66730)``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass

logger = logging.getLogger(__name__)

BLANKS = b" \t\n\v\f\r"
LINE_LIMIT = 255
LINES = 3
END_MARK = 65535
MASK_32 = 0xFFFFFFFF
MAX_64 = 2**64 - 1
REASON_EOF = "eof"
REASON_BAD_NUMBER = "bad_number"
REASON_END_MARK = "end_mark"
REASONS = (REASON_EOF, REASON_BAD_NUMBER, REASON_END_MARK)
DIGITS = b"0123456789"


@dataclass(frozen=True)
class Record:
    """One record: the number and the three lines that follow it."""

    number: int
    lines: tuple[str, str, str]


@dataclass(frozen=True)
class SubtitleFile:
    """The records read, and why reading stopped (one of ``REASONS``)."""

    records: tuple[Record, ...]
    reason: str


@dataclass(frozen=True)
class Caption:
    """One record as shown: frames, times in microseconds, and its lines.

    ``record`` is the record's index from 0 in the file.
    """

    record: int
    start_frame: int
    end_frame: int
    start_us: int
    end_us: int
    lines: tuple[str, str, str]


def _skip_blanks(data: bytes, pos: int) -> int:
    while pos < len(data) and data[pos] in BLANKS:
        pos += 1
    return pos


def _read_number(data: bytes, pos: int) -> tuple[int | None, int]:
    """Return (number, next position) at ``pos``, or (None, pos) if unreadable."""
    start = pos
    negative = False
    if pos < len(data) and data[pos] in b"+-":
        negative = data[pos] == ord("-")
        pos += 1
    first = pos
    while pos < len(data) and data[pos] in DIGITS:
        pos += 1
    if pos == first:
        return None, start
    value = min(int(data[first:pos]), MAX_64)
    return (-value if negative else value) & MASK_32, pos


def _read_line(data: bytes, pos: int) -> tuple[str, int]:
    """Return (text, next position) of the line at ``pos``; see the module."""
    window = data[pos : pos + LINE_LIMIT]
    end = window.find(b"\n")
    if end >= 0:
        raw, pos = window[:end], pos + end + 1
        if raw.endswith(b"\r"):
            raw = raw[:-1]
    else:
        raw, pos = window, pos + len(window)
    if raw.startswith(b"."):
        raw = b""
    nul = raw.find(b"\0")
    if nul >= 0:
        raw = raw[:nul]
    return raw.decode("latin-1"), pos


def read_records(data: bytes, label: str = "<subtitles>") -> SubtitleFile:
    """Return the records of a subtitle file and why reading stopped.

    ``reason`` is ``eof`` (the file ended where a number should start),
    ``bad_number`` (a WARNING is logged naming ``label`` and the position)
    or ``end_mark`` (a record numbered 65535, which is not kept). Never
    raises for any bytes. Does not check that the numbers rise.
    """
    records: list[Record] = []
    pos = 0
    while True:
        pos = _skip_blanks(data, pos)
        if pos >= len(data):
            reason = REASON_EOF
            break
        number, after = _read_number(data, pos)
        if number is None:
            reason = REASON_BAD_NUMBER
            logger.warning(
                "%s: movie.subtitle_cue_invalid: no number at byte %d", label, pos
            )
            break
        if number == END_MARK:
            reason = REASON_END_MARK
            break
        pos = _skip_blanks(data, after)
        lines = []
        for _ in range(LINES):
            text, pos = _read_line(data, pos)
            lines.append(text)
        records.append(Record(number, (lines[0], lines[1], lines[2])))
        logger.debug(
            "%s: record %d number=%d lines=%s", label, len(records), number, lines
        )
    logger.info("%s: %d records, stopped: %s", label, len(records), reason)
    return SubtitleFile(tuple(records), reason)


def _close(
    captions: list[Caption],
    records: tuple[Record, ...] | list[Record],
    index: int | None,
    first: int,
    last: int,
    frame_us: int,
) -> None:
    """Append the caption of record ``index`` shown from ``first`` to ``last``."""
    if index is None or not any(records[index].lines):
        return
    captions.append(
        Caption(
            index,
            first,
            last,
            (first - 1) * frame_us,
            last * frame_us,
            records[index].lines,
        )
    )


def shown_captions(
    records: tuple[Record, ...] | list[Record], frames: int, frame_us: int
) -> list[Caption]:
    """Return the captions shown over ``frames`` frames of ``frame_us`` each.

    Frames count from 1. Before frame f the game reads records while the
    number read last (0 at first) is not past f, each replacing all three
    lines; running out of records clears the lines for good. A record is
    shown from the frame it is read to the frame before the next read, so a
    record whose number is not past the frame being shown never shows. A
    caption whose three lines are all empty is not returned. Returns ``[]``
    for ``frames`` below 1. Does not check the order of the numbers.
    """
    captions: list[Caption] = []
    current: int | None = None
    began = 1
    last = 0
    pos = 0
    ended = False
    frame = 1
    while frame <= frames:
        before = current
        while not ended and last <= frame:
            if pos < len(records):
                current, last = pos, records[pos].number
                pos += 1
            else:
                ended, current = True, None
        if current != before:
            _close(captions, records, before, began, frame - 1, frame_us)
            began = frame
        frame = frames + 1 if ended else last
    _close(captions, records, current, began, frames, frame_us)
    logger.debug("%d captions over %d frames", len(captions), frames)
    return captions
