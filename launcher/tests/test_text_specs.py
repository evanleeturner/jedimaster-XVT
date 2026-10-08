"""specdesc.txt: 93 entries of five fixed-size fields, cut lines, an early end.

Purpose:
    Prove the craft database reader fills the entries five lines at a
    time, keeps each value to its field's size (reporting one that fills
    its field), reads a line over 255 bytes as pieces, and keeps what it
    read when the file ends early, reporting an entry cut part way.

Flow:
    Build byte strings; read; inspect values and WARNING lines.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_text_specs.py``
"""

from __future__ import annotations

import logging

from bmpdata import warned
from textdata import crlf

from jedimaster.text import read_specs
from jedimaster.text.specs import ENTRIES
from jedimaster.text.specs import FIELDS

logger = logging.getLogger(__name__)


NAMES = ("name", "manufacturer", "users", "description", "crew")


def entry_lines(i: int) -> list[bytes]:
    return [f"{name} {i}".encode() for name in NAMES]


def test_entries_fill_five_fields_in_order():
    lines = [line for i in range(ENTRIES) for line in entry_lines(i)]
    specs = read_specs(crlf(b"// header", *lines, b"never read"))
    assert [name for name, _ in FIELDS] == list(NAMES)
    assert len(specs.entries) == ENTRIES and specs.complete == ENTRIES
    last = specs.entries[-1]
    assert (last.name, last.description, last.crew) == (
        b"name 92",
        b"description 92",
        b"crew 92",
    )
    assert specs.cut is None


def test_a_64_byte_value_fills_its_field(caplog):
    caplog.set_level(logging.WARNING)
    long_name = b"N" * 70
    exact = b"M" * 64
    short = b"U" * 63
    specs = read_specs(crlf(long_name, exact, short, b"d", b"c"))
    entry = specs.entries[0]
    assert entry.name == b"N" * 64
    assert entry.manufacturer == exact and entry.users == short
    text = warned(caplog)
    assert "spec 0 name (line 1) holds 70 bytes" in text
    assert "spec 0 manufacturer" in text and "users" not in text


def test_a_line_over_255_bytes_is_read_in_pieces():
    description = b"D" * 300
    specs = read_specs(crlf(b"n", b"m", b"u", description, b"c"))
    entry = specs.entries[0]
    assert entry.description == b"D" * 255
    assert entry.crew == b"D" * 45
    assert specs.entries[1].name == b"c"


def test_an_early_end_keeps_what_was_read(caplog):
    caplog.set_level(logging.WARNING)
    lines = entry_lines(0) + entry_lines(1)[:2]
    specs = read_specs(crlf(*lines))
    assert specs.complete == 1 and specs.cut == 1
    assert specs.entries[1].manufacturer == b"manufacturer 1"
    assert specs.entries[1].users == b"" and specs.entries[2].name == b""
    assert "ends inside spec 1, after 2 of its 5 lines" in warned(caplog)


def test_an_end_on_an_entry_boundary_cuts_nothing(caplog):
    caplog.set_level(logging.WARNING)
    specs = read_specs(crlf(*entry_lines(0)))
    assert (specs.complete, specs.cut) == (1, None)
    assert warned(caplog) == ""
    empty = read_specs(b"")
    assert (empty.complete, empty.cut, len(empty.entries)) == (0, None, ENTRIES)
