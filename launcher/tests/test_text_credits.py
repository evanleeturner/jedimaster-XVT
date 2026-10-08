"""credits.txt: page headers, page buffers, ``~`` colors, the 32-line limit.

Purpose:
    Prove the credits reader scans each page's ten header numbers as the C
    library's ``%u`` does (white space and line ends skipped, a sign
    allowed, a minus wrapping, the run's rest left unread, a non-number
    stopping the header), picks the page buffer from the paragraph, reads
    ``~`` colors token by token as the game does (a token cut by the count
    running into an earlier token's bytes), carries the color across pages,
    keeps an unused line's color, and keeps only 32 lines a page.

Flow:
    Build byte strings with ``textdata.crlf``; read; inspect pages.

Invariants:
    - No game data: headers, colors and words are made up.

Call:
    ``pytest tests/test_text_credits.py``
"""

from __future__ import annotations

import logging

import pytest
from textdata import crlf

from jedimaster.icons import color_565
from jedimaster.text import read_credits
from jedimaster.text.credits import page_buffer
from jedimaster.text.credits import parse_color
from jedimaster.text.credits import scan_number
from jedimaster.text.lines import LineBuffer
from jedimaster.text.lines import LineStream

logger = logging.getLogger(__name__)

HEADER = b"10 20 1 30 40 2 50 60 70 80"
OTHER = b"11 21 2 31 41 3 51 61 71 81"


def pages(data: bytes):
    return read_credits(data).pages


def texts(page) -> list[bytes]:
    return [line.text for line in page.lines]


def test_header_fields_and_pages():
    found = pages(crlf(HEADER, b"one", b"*", OTHER, b"two"))
    assert len(found) == 2
    first, second = found
    assert first.header == [10, 20, 1, 30, 40, 2, 50, 60, 70, 80]
    assert (first.value("duration"), first.value("fade")) == (40, 30)
    assert (first.buffer, first.more, first.used) == (0, True, 1)
    assert texts(first)[:2] == [b"one", b""]
    assert (second.buffer, second.more, texts(second)[0]) == (1, False, b"two")
    assert len(first.lines) == 32


@pytest.mark.parametrize(
    ("paragraph", "buffer"), [(0, 1), (1, 0), (2, 1), (5, 1), (4294967295, 1)]
)
def test_paragraph_gives_the_page_buffer(paragraph, buffer):
    assert page_buffer(paragraph) == buffer
    header = f"1 2 {paragraph} 4 5 6 7 8 9 10".encode()
    assert pages(crlf(header, b"x"))[0].buffer == buffer


def test_header_numbers_as_percent_u():
    stream = LineStream(b" \t\r\n\v\f+7 -1 12abc", 256)
    assert scan_number(stream) == 7
    assert scan_number(stream) == 4294967295
    assert scan_number(stream) == 12
    assert stream.data[stream.position :] == b"abc"
    assert scan_number(stream) is None
    assert stream.data[stream.position :] == b"abc"
    assert scan_number(LineStream(b"   ", 256)) is None
    assert scan_number(LineStream(b"- 5", 256)) is None
    wrapped = LineStream(b"4294967301", 256)
    assert scan_number(wrapped) == 5


def test_a_run_is_at_most_511_bytes():
    stream = LineStream(b"0" * 600 + b"7", 256)
    assert scan_number(stream) == 0
    assert stream.position == 511
    assert scan_number(stream) == 7


def test_a_non_number_stops_the_header_and_is_read_as_text():
    found = pages(crlf(b"5 6 1 x 9", b"next"))
    page = found[0]
    assert page.header == [5, 6, 1]
    assert page.value("fade") is None and page.value("text_y") == 6
    assert texts(page)[:2] == [b"x 9", b"next"]
    assert pages(crlf(b"12abc 3", b"line"))[0].header == [12]


def test_an_unread_header():
    page = pages(crlf(b"~1 2 3 colored"))[0]
    assert page.header == [] and page.buffer == 1 and not page.more
    assert texts(page)[0] == b"colored"
    empty = pages(b"")
    assert len(empty) == 1 and empty[0].header == [] and empty[0].used == 0


def test_comments_are_not_skipped_in_the_header():
    page = pages(crlf(b"// note", HEADER, b"text"))[0]
    assert page.header == []
    assert texts(page)[:2] == [HEADER, b"text"]


def test_white_space_after_the_tenth_number_is_skipped():
    page = pages(HEADER + b"\r\n\r\n   indented\r\n  kept\r\n")[0]
    assert texts(page)[:2] == [b"indented", b"  kept"]
    partial = pages(b"1 2\r\n   kept")[0]
    assert texts(partial)[0] == b"kept"


def load(line: bytes) -> LineBuffer:
    buffer = LineBuffer(256)
    buffer.load(line)
    return buffer


def test_color_tokens():
    assert parse_color(load(b"~080 070 250 Name")) == ((80, 70, 250), 13)
    assert parse_color(load(b"~1 2 3")) == ((1, 2, 3), 6)
    assert parse_color(load(b"~300 -1 x Text")) == ((44, 255, 0), 10)


def test_a_token_cut_by_the_count_runs_into_earlier_bytes():
    rgb, start = parse_color(load(b"~123 456 78"))
    assert rgb == (123, 456 % 256, 786 % 256)
    assert start == 11
    rgb, start = parse_color(load(b"~1234"))
    assert rgb == (123, 423 % 256, 0)
    assert start == 6


def test_text_past_the_end_mark_runs_into_leftover_bytes():
    found = pages(crlf(HEADER, b"ABCDEFGHIJ") + b"~1 2 ")
    assert texts(found[0])[1] == b"GHIJ"
    found = pages(crlf(HEADER, b"ABCDEFGHIJ", b"~1 2 "))
    assert texts(found[0])[1] == b""


def test_color_lines_set_the_color_others_keep_it():
    page = pages(crlf(HEADER, b"white", b"~255 0 0 red", b"still red"))[0]
    assert [line.color for line in page.lines[:3]] == [
        0xFFFF,
        color_565(255, 0, 0),
        color_565(255, 0, 0),
    ]
    assert texts(page)[1] == b"red"


def test_the_color_carries_across_pages():
    found = pages(crlf(HEADER, b"~0 255 0 green", b"*", OTHER, b"plain"))
    assert found[1].lines[0].color == color_565(0, 255, 0)


def test_an_unused_line_keeps_its_color():
    data = crlf(
        HEADER,
        b"~1 2 3 a",
        b"~4 5 6 b",
        b"~7 8 9 c",
        b"*",
        OTHER,
        b"~10 20 30 other buffer",
        b"~11 21 31 second",
        b"*",
        HEADER,
        b"~40 50 60 only",
    )
    first, _, third = pages(data)
    assert texts(third)[:3] == [b"only", b"", b""]
    assert [line.color for line in third.lines[:4]] == [
        color_565(40, 50, 60),
        color_565(4, 5, 6),
        color_565(7, 8, 9),
        0,
    ]
    assert texts(first)[:3] == [b"a", b"b", b"c"]


def test_more_than_32_lines(caplog):
    caplog.set_level(logging.INFO)
    lines = [f"line {i}".encode() for i in range(34)]
    found = pages(crlf(HEADER, *lines, b"~9 9 9 skipped", b"*", OTHER, b"after"))
    first = found[0]
    assert first.used == 32 and first.more
    assert texts(first)[-1] == b"line 31"
    assert found[1].lines[0].color == 0xFFFF
    assert texts(found[1])[0] == b"after"
    assert "page 0 has 35 lines" in caplog.text
    ended = pages(crlf(HEADER, *lines))
    assert len(ended) == 1 and not ended[0].more


def test_comments_skipped_in_lines_and_star_ends_the_page():
    found = pages(crlf(HEADER, b"// comment", b"text", b"*rest ignored", b"\x1a"))
    assert texts(found[0])[0] == b"text" and found[0].used == 1
    assert found[1].header == [] and texts(found[1])[0] == b"\x1a"
