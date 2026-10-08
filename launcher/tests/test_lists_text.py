"""The shared text helpers: lines, words, numbers, quoting, refusals.

Purpose:
    Prove the two ways of reading (by line, by word), the C-style number
    readings, the sheets' quoting, and the refusal of what is not text.

Flow:
    Synthetic bytes in, helpers called directly.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_text.py``
"""

from __future__ import annotations

import logging

import pytest

from jedimaster.lists import text

logger = logging.getLogger(__name__)


def test_lines_drop_the_carriage_return_and_keep_the_ending():
    lines = text.split_lines(b"one\r\ntwo\nthree")
    assert [(x.line, x.text, x.ending) for x in lines] == [
        (1, "one", "\r\n"),
        (2, "two", "\n"),
        (3, "three", ""),
    ]


def test_blank_lines_are_lines_and_nothing_after_the_last_ending():
    lines = text.split_lines(b"a\r\n\r\n\r\n")
    assert [x.text for x in lines] == ["a", "", ""]
    assert text.split_lines(b"") == []


def test_end_mark_is_the_end_of_the_file():
    lines = text.split_lines(b"a\r\nb\r\n\x1a")
    assert [x.text for x in lines] == ["a", "b"]
    assert text.has_end_mark(b"a\r\n\x1a")
    assert not text.has_end_mark(b"a\x1a\r\n")


def test_words_cross_line_breaks_and_tabs():
    first, words = text.split_words(b"// skip me 1 2\r\na\tb  c\r\n\r\n\td\x1a", True)
    assert first.text == "// skip me 1 2" and first.ending == "\r\n"
    assert [(w.line, w.text) for w in words] == [
        (2, "a"),
        (2, "b"),
        (2, "c"),
        (4, "d\x1a"),
    ]


def test_words_without_a_skipped_line_start_at_the_first_word():
    first, words = text.split_words(b"x 1\r\ny 2", False)
    assert first is None
    assert [(w.line, w.text) for w in words] == [(1, "x"), (1, "1"), (2, "y"), (2, "2")]
    first, words = text.split_words(b"only a first line", True)
    assert first.text == "only a first line" and first.ending == "" and words == []


@pytest.mark.parametrize(
    ("raw", "value"),
    [("12", 12), ("  -7x", -7), ("+3", 3), ("", 0), ("x1", 0), ("\t42 ", 42), ("-", 0)],
)
def test_atoi_reads_like_c(raw, value):
    assert text.atoi(raw) == value


def test_whole_numbers_and_scanned_numbers():
    assert text.whole_number("-12") == -12
    assert text.whole_number("1x") is None
    assert text.whole_number("") is None
    assert text.scan_numbers("1 0 71", 3) == [1, 0, 71]
    assert text.scan_numbers(" 2   1  9tail", 3) == [2, 1, 9]
    assert text.scan_numbers("4 x 5", 3) == [4]


def test_ascii_lower_leaves_other_letters():
    assert text.ascii_lower("AbC\xc9.TIE") == "abc\xc9.tie"


def test_c_quote_escapes():
    assert text.c_quote('a\\b"c\nd\re\x1a\x7f~') == '"a\\\\b\\"c\\nd\\x0de\\x1a\\x7f~"'


def test_refusals(tmp_path):
    with pytest.raises(text.NotTextError) as caught:
        text.load(b"ab\0c")
    assert caught.value.offset == 2
    with pytest.raises(TypeError):
        text.load(12)
    with pytest.raises(OSError):
        text.load(tmp_path / "missing.lst")
    path = tmp_path / "x.lst"
    path.write_bytes(b"1\r\n")
    assert text.load(path) == (b"1\r\n", str(path))
    assert text.load(bytearray(b"z"))[0] == b"z"
