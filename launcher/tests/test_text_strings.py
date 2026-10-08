"""strings.txt: the table walk, each table kind, the game's stops, the refusals.

Purpose:
    Prove the strings reader fills the tables in the package's order by
    each kind's rule (plain lines, goal rows by the 47-count pattern, model
    names with their gender, escaped in-flight messages), reads the
    feminine and neutered tables only when a model name asks, stops where
    the game stops (naming the table and line) and refuses what the game
    has no answer for.

Flow:
    Build a synthetic strings.txt with ``textdata``; read; inspect.

Invariants:
    - No game data: table lengths come from the package, words are made up.

Call:
    ``pytest tests/test_text_strings.py``
"""

from __future__ import annotations

import logging

import pytest
from textdata import crlf
from textdata import strings_bytes
from textdata import strings_lines

from jedimaster.text import read_strings
from jedimaster.text import StringsFormatError
from jedimaster.text.strings import decode_escapes
from jedimaster.text.tables import BAD_MODEL_LINE
from jedimaster.text.tables import ESCAPED
from jedimaster.text.tables import GENDER_TABLES
from jedimaster.text.tables import GOAL
from jedimaster.text.tables import goal_lines
from jedimaster.text.tables import GOAL_PATTERN
from jedimaster.text.tables import LINES
from jedimaster.text.tables import MESSAGES
from jedimaster.text.tables import MODELS
from jedimaster.text.tables import OUT_OF_SYNC_LINE
from jedimaster.text.tables import TABLES

logger = logging.getLogger(__name__)

MODEL = next(t for t in TABLES if t.kind == MODELS)
ESCAPES = next(t for t in TABLES if t.kind == ESCAPED)
FIRST_GOAL = next(t for t in TABLES if t.kind == GOAL)
FEMININE, NEUTERED = (table for _, table in GENDER_TABLES)
RAN_OUT = b"made-up: ran out \xe9"
BAD_BYTE = b"made-up: bad byte"
OUT_OF_SYNC, BAD_MODEL = 2, 3
"""The message table's lines (from 0) the game shows when it stops."""
STOPS = {(MESSAGES, OUT_OF_SYNC): RAN_OUT, (MESSAGES, BAD_MODEL): BAD_BYTE}
"""Made-up lines where the game finds the messages it stops with."""
MESSAGES_START = sum(
    t.length for t in TABLES[: [t.name for t in TABLES].index(MESSAGES)]
)
"""The file line (from 0) where the message table starts."""


def test_stop_lines_are_lines_2_and_3_of_the_message_table():
    assert (OUT_OF_SYNC_LINE, BAD_MODEL_LINE) == (OUT_OF_SYNC, BAD_MODEL)
    assert MESSAGES == TABLES[1].name


def test_tables_read_in_order_each_line_once():
    strings = read_strings(strings_bytes())
    names = [t.name for t in strings.tables]
    assert names == [t.name for t in TABLES] + [FEMININE.name, NEUTERED.name]
    first = strings.tables[0]
    assert first.kind == LINES
    assert [e.text for e in first.entries] == [
        f"{first.name} {i}".encode() for i in range(len(first.entries))
    ]
    assert strings.lines_read == len(strings_lines())


def test_goal_rows_follow_the_47_count_pattern():
    strings = read_strings(strings_bytes())
    goal = strings.table(FIRST_GOAL.name)
    rows = goal_lines(FIRST_GOAL.length)
    assert len(goal.entries) == sum(rows)
    assert rows[:47] == list(GOAL_PATTERN) and rows[47:94] == list(GOAL_PATTERN)
    last = goal.entries[-1]
    assert (last.index, last.variant) == (FIRST_GOAL.length - 1, rows[-1] - 1)
    assert last.text == f"{FIRST_GOAL.name} {last.index}.{last.variant}".encode()
    second = [e for e in goal.entries if e.index == 1]
    assert [e.variant for e in second] == list(range(GOAL_PATTERN[1]))


def test_model_names_give_gender_and_drop_the_second_byte():
    models = {0: b"mXAlpha", 1: b"f Beta", 2: b"n\tGamma", 3: b"m:"}
    strings = read_strings(strings_bytes(models=models))
    entries = strings.table(MODEL.name).entries[:4]
    assert [(e.gender, e.text) for e in entries] == [
        (0, b"Alpha"),
        (1, b"Beta"),
        (2, b"Gamma"),
        (0, b""),
    ]


@pytest.mark.parametrize(
    "bad", [b"Xbad", b"M upper", b"", b" m"], ids=["x", "upper", "empty", "space"]
)
def test_a_bad_model_first_byte_stops_the_game(bad):
    with pytest.raises(StringsFormatError) as caught:
        read_strings(strings_bytes(models={5: bad}, lines=STOPS))
    error = caught.value
    assert (error.table, error.line) == (MODEL.name, 6)
    assert error.stops and error.stop == BAD_BYTE
    assert 'the game stops with "made-up: bad byte"' in str(error)
    assert error.file_line == strings_lines().index(b"m model 5") + 1
    assert f"table {MODEL.name}, line 6" in str(error)


def test_a_model_line_of_one_byte_is_refused():
    with pytest.raises(StringsFormatError) as caught:
        read_strings(strings_bytes(models={0: b"m"}))
    assert caught.value.stop is None and caught.value.line == 1
    assert not caught.value.stops


def test_escapes_both_forms_and_their_wrap():
    assert decode_escapes(b"a\\03b") == b"a\x03b"
    assert decode_escapes(b"\\x7z") == bytes((ord("7") - 40, ord("z")))
    assert decode_escapes(b"\\0!") == bytes(((ord("!") - 48) % 256,))
    assert decode_escapes(b"\\1\x00") == bytes((216,))
    assert decode_escapes(b"\\\\\\") == bytes((ord("\\") - 40,))
    assert decode_escapes(b"\\0\\x") == bytes((ord("\\") - 48,)) + b"x"
    assert decode_escapes(b"plain") == b"plain"


@pytest.mark.parametrize(
    "line", [b"end\\", b"end\\0", b"\\"], ids=["last", "second-last", "alone"]
)
def test_a_backslash_at_a_lines_end_is_refused(line):
    assert decode_escapes(line) is None
    changes = {(ESCAPES.name, 2): line}
    with pytest.raises(StringsFormatError) as caught:
        read_strings(strings_bytes(lines=changes))
    assert (caught.value.table, caught.value.line) == (ESCAPES.name, 3)
    assert caught.value.stop is None and not caught.value.stops


def test_escaped_table_is_decoded_other_tables_are_not():
    changes = {(ESCAPES.name, 0): b"\\06go", (TABLES[0].name, 0): b"\\06go"}
    strings = read_strings(strings_bytes(lines=changes))
    assert strings.table(ESCAPES.name).entries[0].text == b"\x06go"
    assert strings.tables[0].entries[0].text == b"\\06go"


@pytest.mark.parametrize(
    "keep",
    [
        0,
        1,
        MESSAGES_START + OUT_OF_SYNC,
        MESSAGES_START + OUT_OF_SYNC + 1,
        500,
    ],
    ids=["empty", "one", "message-unread", "message-read", "500"],
)
def test_a_file_ending_early_is_out_of_sync(keep):
    lines = strings_lines(lines=STOPS)[:keep]
    with pytest.raises(StringsFormatError) as caught:
        read_strings(crlf(*lines))
    error = caught.value
    read = keep > MESSAGES_START + OUT_OF_SYNC
    assert error.stops and error.stop == (RAN_OUT if read else None)
    assert error.file_line is None
    assert ("before its message line" in str(error)) is not read
    walked = 0
    for table in TABLES:
        size = sum(goal_lines(table.length)) if table.kind == GOAL else table.length
        if walked + size > keep:
            assert (error.table, error.line) == (table.name, keep - walked + 1)
            break
        walked += size


def test_feminine_and_neutered_tables_present_and_absent():
    plain = read_strings(strings_bytes())
    assert not plain.table(FEMININE.name).present
    assert not plain.table(NEUTERED.name).present
    assert plain.table(NEUTERED.name).entries == []
    both = read_strings(strings_bytes(models={1: b"n neutral", 2: b"f she"}))
    assert both.table(FEMININE.name).present and both.table(NEUTERED.name).present
    feminine = both.table(FEMININE.name).entries
    assert len(feminine) == sum(goal_lines(FEMININE.length))
    assert feminine[0].text == f"{FEMININE.name} 0.0".encode()
    neutral = read_strings(strings_bytes(models={1: b"n neutral"}))
    assert not neutral.table(FEMININE.name).present
    assert (
        neutral.table(NEUTERED.name).entries[0].text == f"{NEUTERED.name} 0.0".encode()
    )


def test_a_gender_table_cut_short_is_out_of_sync():
    lines = strings_lines(models={0: b"f she"})[:-1]
    with pytest.raises(StringsFormatError) as caught:
        read_strings(crlf(*lines))
    assert caught.value.table == FEMININE.name


def test_comments_are_skipped_and_lines_after_the_last_table_unread():
    lines = strings_lines()
    lines.insert(3, b"// a comment")
    data = crlf(*lines, b"after the last table", b"\\")
    strings = read_strings(data)
    assert strings.tables[0].entries[3].text == f"{TABLES[0].name} 3".encode()
    assert strings.tables[0].entries[3].line == 5
    assert strings.lines_read == len(strings_lines())
