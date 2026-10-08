"""Read strings.txt, the game's string tables, as the game reads it.

Purpose:
    Fill the tables of ``tables.TABLES`` (and the gender tables when a
    model name asks for them) from the file's lines, each table kind by
    its rule, and refuse a file the game stops on, naming the table and
    its line.

Flow:
    ``read_strings`` reads the lines (buffer 1,024, comments skipped, line
    feed dropped) and walks the tables in order with a ``_Cursor``:
    ``lines`` tables take one line per entry; ``goal`` tables take rows of
    ``goal_lines`` variants; ``models`` lines give a gender and a name;
    ``escaped`` lines have their backslash escapes decoded. Lines after the
    last table are never read.

Invariants:
    - The file ending before a table's last line stops the game, and so
      does a model name whose first byte is not ``m``, ``f`` or ``n``. Both
      raise ``StringsFormatError`` naming the table and its line (from 1),
      with the message the game shows: the file's own ``MESSAGES`` line
      ``OUT_OF_SYNC_LINE`` or ``BAD_MODEL_LINE`` as read, or None when the
      file stops before that line has been read.
    - Escapes: a backslash and the two bytes after it become one byte: the
      third byte less 48 when the middle byte is ``0``, else less 40,
      modulo 256. A backslash that starts an escape among a line's last two
      bytes reaches past the line: the game has no defined answer there,
      so the file is refused. A model name line under two bytes would take
      its name from past the line's end: refused the same way.
    - A gender table is absent unless some model name has its gender.

Call:
    ``strings = read_strings(path); strings.table("model_names").entries``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass

from .lines import Line
from .lines import load
from .lines import read_lines
from .tables import BAD_MODEL_LINE
from .tables import ESCAPED
from .tables import GENDER_TABLES
from .tables import GOAL
from .tables import goal_lines
from .tables import MESSAGES
from .tables import MODELS
from .tables import OUT_OF_SYNC_LINE
from .tables import StringTable
from .tables import TABLES

logger = logging.getLogger(__name__)

BUFFER = 1024
GENDERS = {ord("m"): 0, ord("f"): 1, ord("n"): 2}
"""A model name's first byte and the gender it gives."""
BACKSLASH = 0x5C
ZERO = 0x30
ZERO_FORM = 48
OTHER_FORM = 40


class StringsFormatError(ValueError):
    """A strings.txt the game stops on, or has no defined answer for.

    ``table`` and ``line`` (from 1, within the table) name where; ``file_line``
    is the file's line (from 1, comments counted), or None past its end;
    ``stops`` is True where the game stops, False where it has no defined
    answer; ``stop`` is the message the game shows (a line of the file
    itself), or None when it has none or the file stopped before that line
    was read; ``reason`` is the message without the file's label.
    """

    def __init__(
        self,
        label: str,
        table: str,
        line: int,
        file_line: int | None,
        stops: bool,
        stop: bytes | None,
        why: str,
    ) -> None:
        where = f"line {file_line} of the file" if file_line else "past the file's end"
        said = ""
        if stops and stop is not None:
            said = f'; the game stops with "{stop.decode("latin-1")}"'
        elif stops:
            said = "; the game stops before its message line is read"
        self.reason = f"table {table}, line {line} ({where}): {why}{said}"
        super().__init__(f"{label}: {self.reason}")
        self.table = table
        self.line = line
        self.file_line = file_line
        self.stops = stops
        self.stop = stop


@dataclass(frozen=True)
class StringEntry:
    """One entry: its number (a goal table's row), variant, text, gender, line."""

    index: int
    variant: int | None
    text: bytes
    gender: int | None
    line: int


@dataclass
class StringsTable:
    """One table as read: name, kind, whether the game read it, its entries."""

    name: str
    kind: str
    present: bool
    entries: list[StringEntry]


@dataclass
class StringsFile:
    """The tables of strings.txt in the game's order, gender tables last."""

    tables: list[StringsTable]
    lines_read: int

    def table(self, name: str) -> StringsTable:
        """Return the table called ``name``; raises ``KeyError`` for none.

        Does not tell a table the game did not read from one it read: see
        ``present``.
        """
        for table in self.tables:
            if table.name == name:
                return table
        raise KeyError(name)


class _Cursor:
    """The lines in order, handed out one at a time for one table at a time."""

    def __init__(self, lines: list[Line], label: str) -> None:
        self.lines = lines
        self.label = label
        self.next = 0
        self.table = ""
        self.taken = 0
        self.messages: list[bytes] = []

    def start(self, table: str) -> None:
        """Begin a table: its lines count from 1 again."""
        self.table = table
        self.taken = 0

    def take(self) -> Line:
        """Return the next line, or raise the game's out-of-sync stop."""
        self.taken += 1
        if self.next >= len(self.lines):
            raise StringsFormatError(
                self.label,
                self.table,
                self.taken,
                None,
                True,
                self.message(OUT_OF_SYNC_LINE),
                "the file ends before the table does",
            )
        line = self.lines[self.next]
        self.next += 1
        if self.table == MESSAGES:
            self.messages.append(line.text)
        return line

    def message(self, index: int) -> bytes | None:
        """Return ``MESSAGES`` line ``index`` as read, or None if not read yet."""
        return self.messages[index] if index < len(self.messages) else None

    def refuse(
        self, line: Line, stop: bytes | None, why: str, stops: bool = False
    ) -> StringsFormatError:
        """Return the error naming this table and the line just taken."""
        return StringsFormatError(
            self.label, self.table, self.taken, line.number, stops, stop, why
        )


def decode_escapes(text: bytes) -> bytes | None:
    """Return a line with its backslash escapes decoded, or None to refuse it.

    Each backslash and the two bytes after it become one byte: the third
    byte's value less 48 when the middle byte is ``0``, else less 40, both
    modulo 256; scanning goes on after the three bytes. Returns None when a
    backslash that starts an escape lies among the line's last two bytes.
    Does not decode the bytes an escape produces.
    """
    out = bytearray()
    i = 0
    while i < len(text):
        byte = text[i]
        if byte != BACKSLASH:
            out.append(byte)
            i += 1
            continue
        if i + 2 >= len(text):
            return None
        less = ZERO_FORM if text[i + 1] == ZERO else OTHER_FORM
        out.append((text[i + 2] - less) % 256)
        i += 3
    return bytes(out)


def _model(cursor: _Cursor, index: int) -> StringEntry:
    line = cursor.take()
    text = line.text
    if not text or text[0] not in GENDERS:
        first = f"first byte {text[:1]!r}" if text else "an empty line"
        stop = cursor.message(BAD_MODEL_LINE)
        raise cursor.refuse(line, stop, f"{first}: not m, f or n", stops=True)
    if len(text) < 2:
        raise cursor.refuse(line, None, "the name would start past the line's end")
    entry = StringEntry(index, None, text[2:], GENDERS[text[0]], line.number)
    logger.debug("%s %d: gender %d %r", cursor.table, index, entry.gender, entry.text)
    return entry


def _escaped(cursor: _Cursor, index: int) -> StringEntry:
    line = cursor.take()
    text = decode_escapes(line.text)
    if text is None:
        raise cursor.refuse(line, None, "an escape's backslash reaches past the line")
    logger.debug("%s %d: %r", cursor.table, index, text)
    return StringEntry(index, None, text, None, line.number)


def _plain(cursor: _Cursor, index: int, variant: int | None) -> StringEntry:
    line = cursor.take()
    logger.debug("%s %d.%s: %r", cursor.table, index, variant, line.text)
    return StringEntry(index, variant, line.text, None, line.number)


def _read_table(cursor: _Cursor, table: StringTable) -> StringsTable:
    """Read one table's lines by its kind's rule."""
    cursor.start(table.name)
    entries = []
    if table.kind == GOAL:
        for row, variants in enumerate(goal_lines(table.length)):
            entries += [_plain(cursor, row, v) for v in range(variants)]
    elif table.kind == MODELS:
        entries = [_model(cursor, index) for index in range(table.length)]
    elif table.kind == ESCAPED:
        entries = [_escaped(cursor, index) for index in range(table.length)]
    else:
        entries = [_plain(cursor, index, None) for index in range(table.length)]
    logger.debug("table %s: %d entries", table.name, len(entries))
    return StringsTable(table.name, table.kind, True, entries)


def read_strings(source: str | os.PathLike[str] | bytes | bytearray) -> StringsFile:
    """Return the tables of a strings.txt as the game reads them.

    ``source`` is a path or the file's bytes. Returns every table of
    ``TABLES`` in order, then the feminine and neutered goal tables, each
    present only when some model name has gender 1 (feminine) or 2
    (neutered), else ``present`` False with no entries. Raises
    ``StringsFormatError`` where the game stops (the file ends early, a bad
    model name byte) or has no defined answer (an escape or a model name
    reaching past its line), and ``OSError`` when the path cannot be read.
    Does not read past the last table's last line or check the texts.
    """
    data, label = load(source)
    lines = read_lines(data, BUFFER, skip_comments=True)
    cursor = _Cursor(lines, label)
    tables = [_read_table(cursor, table) for table in TABLES]
    models = next(t for t in tables if t.kind == MODELS)
    genders = {entry.gender for entry in models.entries}
    for gender, table in GENDER_TABLES:
        if gender in genders:
            tables.append(_read_table(cursor, table))
        else:
            logger.debug("table %s absent: no model has gender %d", table.name, gender)
            tables.append(StringsTable(table.name, table.kind, False, []))
    left = len(lines) - cursor.next
    logger.info(
        "strings %s: %d tables, %d lines read, %d lines after the last table",
        label,
        sum(t.present for t in tables),
        cursor.next,
        left,
    )
    return StringsFile(tables, cursor.next)
