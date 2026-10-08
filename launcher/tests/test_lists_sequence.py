"""Sequence files: count, mission lines as read, and the description rule.

Purpose:
    Prove the sequence reader and the game's view: the count read like an
    id, the N mission lines, the description kept from the rest with only
    printable characters and line feeds, at most 4,095, and a last line
    without its line ending.

Flow:
    Build sequence files with ``crlf`` or raw bytes, read, view, inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_sequence.py``
"""

from __future__ import annotations

import logging

from listdata import crlf

from jedimaster.lists import read_sequence
from jedimaster.lists import read_ships
from jedimaster.lists import sequence_view

logger = logging.getLogger(__name__)


def test_count_missions_and_description():
    data = crlf("2", "ONE.tie", "two.TIE", "First line.", "", "After a blank.")
    seq = read_sequence(data)
    assert seq.count == 2 and seq.count_line.text == "2"
    assert [(m.line, m.text) for m in seq.missions] == [(2, "ONE.tie"), (3, "two.TIE")]
    assert seq.description_line == 4
    assert seq.description == "First line.\r\n\r\nAfter a blank.\r\n"
    view = sequence_view(seq)
    assert view.count_line == "2\n" and view.count == 2
    assert view.missions == ["ONE.tie\n", "two.TIE\n"]
    assert view.description == "First line.\n\nAfter a blank.\n"


def test_description_keeps_printable_characters_and_line_feeds_only():
    data = b"1\r\na.tie\r\nTab\there\x1a caf\xe9 ~\x7f end\r\nnext"
    view = sequence_view(read_sequence(data))
    assert view.description == "Tabhere caf ~ end\nnext"


def test_description_limit_is_4095_characters():
    body = "x" * 5000
    view = sequence_view(read_sequence(crlf("0", body)))
    assert view.description == "x" * 4095
    view = sequence_view(read_sequence(crlf("0", "y" * 4094)))
    assert view.description == "y" * 4094 + "\n"


def test_last_line_without_ending_is_still_read():
    seq = read_sequence(b"2\r\na.tie\r\nb.tie")
    assert [m.text for m in seq.missions] == ["a.tie", "b.tie"]
    assert sequence_view(seq).missions == ["a.tie\n", "b.tie"]
    assert seq.description == "" and seq.description_line is None


def test_count_beyond_the_file_and_count_below_one():
    seq = read_sequence(crlf("5", "a.tie"))
    assert seq.count == 5 and len(seq.missions) == 1
    seq = read_sequence(crlf("-2", "Text only"))
    assert seq.missions == [] and seq.description == "Text only\r\n"
    seq = read_sequence(crlf(" 1x", "a.tie", "D"))
    assert seq.count == 1 and sequence_view(seq).count_line == " 1x\n"
    empty = read_sequence(b"")
    assert empty.count_line is None and empty.count == 0
    assert sequence_view(empty).count_line == ""


def test_comment_lines_are_values_in_a_sequence_and_a_ship_list():
    seq = read_sequence(crlf("2", "// one", "[two]", "// kept in the text"))
    assert [m.text for m in seq.missions] == ["// one", "[two]"]
    assert sequence_view(seq).description == "// kept in the text\n"
    ships = read_ships(crlf("// 1", "a.opt 2"))
    assert [(p.model.text, p.type_value) for p in ships.pairs] == [
        ("//", 1),
        ("a.opt", 2),
    ]
