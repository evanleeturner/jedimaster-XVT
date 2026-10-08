"""joystick.txt: codes, names and descriptions read from the game's line buffer.

Purpose:
    Prove the joystick reader takes the code up to the first space or end
    mark (leading blanks and a sign allowed, kept modulo 256), passes one
    byte over, then reads the name and the description, so a line with no
    space reads from the bytes earlier, longer lines left in the buffer;
    and reports a name or description longer than the game's room.

Flow:
    Build byte strings; read; inspect actions and WARNING lines.

Invariants:
    - No game data: the codes and words are made up.

Call:
    ``pytest tests/test_text_joystick.py``
"""

from __future__ import annotations

import logging

import pytest
from bmpdata import warned

from jedimaster.text import JoystickAction
from jedimaster.text import read_joystick
from jedimaster.text.joystick import leading_number
from jedimaster.text.joystick import report_action

logger = logging.getLogger(__name__)


def actions(data: bytes) -> list[tuple[int, bytes, bytes]]:
    return [(a.code, a.name, a.description) for a in read_joystick(data).actions]


def test_code_name_description():
    assert actions(b"65 Key Does a thing  here\r\n") == [
        (65, b"Key", b"Does a thing  here")
    ]


def test_every_line_is_an_entry_comments_included():
    found = actions(b"// x y\n\n7\n")
    assert [a[0] for a in found] == [0, 0, 7]
    assert found[0][1:] == (b"x", b"y")


def test_a_line_with_no_space_reads_leftover_bytes():
    found = actions(b"41 ) Save preset two\r\n\x1a")
    assert found[1] == (0, b"", b") Save preset two")
    found = actions(b"12 AB long words here\n9\n")
    assert found[1] == (9, b"", b"AB long words here")
    found = actions(b"12 AB long words here\n9")
    assert found[1] == (9, b"", b"AB long words here")
    found = actions(b"1 abcdef ghi\n12345\n")
    assert found[1] == (57, b"", b"f ghi")
    found = actions(b"1 n " + b"d" * 120 + b"\n2\n")
    assert found[1] == (2, b"", b" " + b"d" * 120)


def test_a_line_with_one_space_has_an_empty_description():
    assert actions(b"3 Name\n") == [(3, b"Name", b"")]
    assert actions(b"3 \n") == [(3, b"", b"")]
    after_long = actions(b"1 X long description words\n3 Name\n")
    assert after_long[1] == (3, b"Name", b"")


@pytest.mark.parametrize(
    ("code", "value"),
    [
        (b"+12", 12),
        (b"-1", 255),
        (b"\t7x", 7),
        (b"\x0b-3", 253),
        (b"300", 44),
        (b"99999999998", 99999999998 % 256),
        (b"abc", 0),
        (b"-", 0),
        (b"", 0),
        (b"\x1a", 0),
    ],
    ids=[
        "plus",
        "minus-wraps",
        "tab",
        "vtab-minus",
        "over-255",
        "huge",
        "letters",
        "sign-only",
        "empty",
        "byte-26",
    ],
)
def test_code_values(code, value):
    assert actions(code + b" N D\n")[0][0] == value


def test_leading_number_rules():
    assert leading_number(b"  42abc") == 42
    assert leading_number(b"+-4") == 0
    assert leading_number(b"\n\r\f-17") == -17


def test_a_long_line_is_two_entries():
    first = b"5 " + b"n" * 130
    found = actions(first + b"\n")
    assert len(found) == 2
    assert found[0] == (5, b"n" * 125, b"")


def test_long_name_and_description_are_reported(caplog):
    caplog.set_level(logging.WARNING)
    found = actions(b"1 " + b"N" * 21 + b" short\n2 " + b"M" * 20 + b" fits\n")
    assert found[0][1] == b"N" * 21 and found[1][1] == b"M" * 20
    text = warned(caplog)
    assert "action 0 (line 1): a 21-byte name, room for 20" in text
    assert "action 1" not in text
    caplog.clear()
    fits = JoystickAction(1, b"n", b"d" * 128, 4)
    assert not report_action(fits, 3, "x") and warned(caplog) == ""
    long = JoystickAction(1, b"n", b"d" * 129, 4)
    assert report_action(long, 3, "x")
    assert "action 3 (line 4): a 129-byte description, room for 128" in warned(caplog)
