"""Tests for the little-endian reading helpers.

Purpose:
    Prove signedness, byte order, ISO-8859-1 decoding, NUL cutting and the
    short-read refusal of ``jedimaster.mission.binary``.

Flow:
    Each test reads a hand-made byte string.

Invariants:
    - No game data; bytes are written inline.

Call:
    ``pytest tests/test_binary.py``
"""

from __future__ import annotations

import logging

import pytest

from jedimaster.mission.binary import Cursor
from jedimaster.mission.binary import decode_fixed
from jedimaster.mission.binary import ShortReadError

logger = logging.getLogger(__name__)


def test_integers_are_little_endian_and_signed_where_asked():
    cur = Cursor(bytes([0xFF, 0xFF, 0x34, 0x12, 0xFE, 0xFF, 0x80, 0x80, 1, 0, 0, 0x80]))
    assert cur.u8() == 0xFF
    assert cur.s8() == -1
    assert cur.u16() == 0x1234
    assert cur.s16() == -2
    assert cur.u8() == 0x80
    assert cur.s8() == -128
    assert cur.s32() == -0x7FFFFFFF  # 01 00 00 80 little-endian
    assert cur.offset == 12


def test_arrays_read_in_order():
    cur = Cursor(bytes([1, 2, 3, 0xFF, 0x7F, 0x00, 0x80]))
    assert cur.u8s(3) == [1, 2, 3]
    assert cur.s16s(2) == [0x7FFF, -0x8000]


def test_strings_cut_at_first_nul_and_decode_latin1():
    cur = Cursor(b"Ab\xe9\x00junk" + b"XYZ")
    assert cur.string(8) == "Ab\xe9"
    assert cur.offset == 8
    assert cur.string(3) == "XYZ"
    assert decode_fixed(b"\x00abc") == ""
    assert decode_fixed(b"full") == "full"


def test_short_read_refused_and_offset_kept():
    cur = Cursor(b"\x01\x02\x03")
    cur.u8()
    with pytest.raises(ShortReadError, match="offset 0x1"):
        cur.s32()
    assert cur.offset == 1
    with pytest.raises(ShortReadError):
        cur.string(3)
    with pytest.raises(ShortReadError):
        cur.skip(5)
