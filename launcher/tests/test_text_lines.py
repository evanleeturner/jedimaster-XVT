"""The shared line reader: line ends, buffer pieces, comments, the line buffer.

Purpose:
    Prove the one line reader every text reader uses follows the game's
    line reading for any buffer size: CR LF read as LF, a lone CR kept, a
    final line without a line feed, pieces of ``buffer - 1`` bytes each
    counted as a line, comments only at a line's start; and that the line
    buffer keeps what earlier, longer lines left; and the sheets' quoting.

Flow:
    Build byte strings in each test; read; compare.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_text_lines.py``
"""

from __future__ import annotations

import logging

import pytest

from jedimaster.text import LineBuffer
from jedimaster.text import LineStream
from jedimaster.text import quote
from jedimaster.text import read_lines
from jedimaster.text.lines import c_string
from jedimaster.text.lines import load
from jedimaster.text.sheet import unquote

logger = logging.getLogger(__name__)

BUFFERS = (128, 256, 512, 1024)
"""The game's line buffers: joystick, specdesc and credits, xvterr, the rest."""


def texts(data: bytes, size: int = 64, skip: bool = False) -> list[bytes]:
    return [line.text for line in read_lines(data, size, skip_comments=skip)]


def test_crlf_reads_as_lf_and_a_lone_cr_stays():
    assert texts(b"a\r\nb\rc\r\n\r\r\n") == [b"a", b"b\rc", b"\r"]
    kept = read_lines(b"a\r\nb", 64, skip_comments=False, keep_line_feed=True)
    assert [line.text for line in kept] == [b"a\n", b"b"]


def test_final_line_without_line_feed_and_empty_file():
    assert texts(b"one\ntwo") == [b"one", b"two"]
    assert texts(b"") == []
    assert texts(b"\n\n") == [b"", b""]


@pytest.mark.parametrize("size", BUFFERS)
def test_a_line_at_and_over_each_buffer_size(size):
    fits = b"x" * (size - 2)
    assert texts(fits + b"\n", size) == [fits]
    exact = b"y" * (size - 1)
    assert texts(exact + b"\n", size) == [exact, b""]
    over = b"z" * (size + 5)
    assert texts(over + b"\n", size) == [b"z" * (size - 1), b"z" * 6]
    numbers = [line.number for line in read_lines(over, size, skip_comments=False)]
    assert numbers == [1, 2]


def test_comments_only_at_a_line_start():
    data = b"// one\n a // two\n/ three\n//\n/"
    assert texts(data, skip=True) == [b" a // two", b"/ three", b"/"]
    assert [line.number for line in read_lines(data, 64, True)] == [2, 3, 5]
    assert len(texts(data, skip=False)) == 5


def test_a_long_comment_continues_as_a_line():
    data = b"//" + b"c" * 10 + b"\n"
    assert texts(data, size=8, skip=True) == [b"ccccc"]


def test_stream_pieces_keep_their_line_feed():
    stream = LineStream(b"ab\r\ncd", 64)
    assert stream.read_piece() == b"ab\n"
    assert stream.read_piece() == b"cd"
    assert stream.read_piece() is None
    assert stream.pieces == 2 and stream.at_end()
    with pytest.raises(ValueError):
        LineStream(b"", 1)


def test_line_buffer_keeps_leftover_bytes():
    buffer = LineBuffer(16)
    assert buffer.load(b"abcdef\n") == 6
    assert bytes(buffer.bytes[:8]) == b"abcdef\0\0"
    assert buffer.load(b"xy\n") == 2
    assert bytes(buffer.bytes[:8]) == b"xy\0\0ef\0\0"
    assert buffer.string(0) == b"xy" and buffer.string(4) == b"ef"
    assert buffer.load(b"q") == 1
    assert bytes(buffer.bytes[:4]) == b"q\0\0\0"
    assert buffer.byte(99) == 0 and buffer.string(99) == b""
    with pytest.raises(ValueError):
        buffer.load(b"x" * 16)


def test_c_string_stops_at_the_end_mark():
    assert c_string(b"ab\0cd") == b"ab"
    assert c_string(b"ab\0cd", 3) == b"cd"
    assert c_string(b"abc") == b"abc"


def test_load_reads_paths_and_bytes(tmp_path):
    path = tmp_path / "f.txt"
    path.write_bytes(b"\xe9\n")
    assert load(path) == (b"\xe9\n", str(path))
    assert load(bytearray(b"x")) == (b"x", "<bytes>")


def test_quote_follows_the_sheets_rules():
    assert quote(b'a"b\\c\n\r\t\x01\x7f\xe9') == '"a\\"b\\\\c\\n\\r\\t\\x01\\x7f\\xe9"'
    assert quote(b"abc\0def") == '"abc"'
    assert quote(b"abcdef", 4) == '"abcd"'
    assert quote("caf\xe9") == '"caf\\xe9"'
    assert unquote(quote(b'\x02a\\b"c')) == b'\x02a\\b"c'
    with pytest.raises(ValueError):
        unquote('"a\\q"')
