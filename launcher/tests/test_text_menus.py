"""fronttxt.txt and xvterr.txt: menu entries, ``No text.``, the error messages.

Purpose:
    Prove the menu reader numbers every non-comment line from 0 (empty
    lines included) and answers ``No text.`` past the count, and the error
    reader counts every line (comments too), turns ``\\n`` into a line feed
    in a line's first 255 bytes, keeps the line's own line feed, and has
    no message past the file's end.

Flow:
    Build byte strings; read; inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_text_menus.py``
"""

from __future__ import annotations

import logging

from jedimaster.text import error_message
from jedimaster.text import front_text
from jedimaster.text import read_errors
from jedimaster.text import read_front
from jedimaster.text.menus import error_text

logger = logging.getLogger(__name__)


def test_front_entries_skip_comments_keep_empty_lines():
    front = read_front(b"First\r\n// note\r\n\r\nThird \xe9\r\nlast")
    assert front.entries == [b"First", b"", b"Third \xe9", b"last"]
    assert front.lines == [1, 3, 4, 5]


def test_front_long_line_is_two_entries():
    front = read_front(b"a" * 1030 + b"\n")
    assert front.entries == [b"a" * 1023, b"a" * 7]


def test_no_text_at_and_past_the_count():
    front = read_front(b"zero\none\n")
    assert front_text(front, 1) == b"one"
    assert front_text(front, 2) == b"No text."
    assert front_text(front, 99) == b"No text."
    assert front_text(front, -1) == b"No text."
    assert front_text(read_front(b""), 0) == b"No text."


def test_error_comments_are_messages_and_line_feeds_kept():
    errors = read_errors(b"// first\r\nsecond\\nline\r\nlast")
    assert errors.messages == [b"// first\n", b"second\nline\n", b"last"]


def test_error_escape_only_backslash_n():
    assert error_text(b"a\\nb\\tc\\\\n\\") == b"a\nb\\tc\\\n\\"


def test_error_line_cut_at_255_bytes():
    line = b"x" * 300 + b"\n"
    assert error_text(line) == b"x" * 255
    shrinking = b"\\n" * 200
    assert error_text(shrinking) == b"\n" * 128
    edge = b"y" * 254 + b"\\n" + b"tail"
    assert error_text(edge) == b"y" * 254 + b"\n"
    errors = read_errors(b"z" * 600 + b"\n")
    assert [len(m) for m in errors.messages] == [255, 90]


def test_message_past_the_end_does_not_exist():
    errors = read_errors(b"one\n")
    assert error_message(errors, 0) == b"one\n"
    assert error_message(errors, 1) is None
    assert error_message(errors, -1) is None
