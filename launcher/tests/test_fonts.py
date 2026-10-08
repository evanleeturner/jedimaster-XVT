"""The menu fonts: the header, every glyph code, drawing and measuring strings.

Purpose:
    Prove the font reader keeps the header's fields (the point size taken
    from the file, the height from entry 0), decodes every code kind into
    runs (the bytes that follow drawn runs passed over), reports a row
    length that disagrees with its codes, refuses what cannot be read; and
    that strings draw and measure by the game's rules: color bytes 1 to 6,
    the spacing, the stop at x 640, an empty string's measure.

Flow:
    Build synthetic fonts with ``textdata.font_bytes``; read; draw; inspect.

Invariants:
    - No game data: glyph shapes, sizes and colors are made up; the five
      text colors are read from the package.

Call:
    ``pytest tests/test_fonts.py``
"""

from __future__ import annotations

import logging

import pytest
from bmpdata import warned
from textdata import font_bytes
from textdata import rows

from jedimaster.fonts import COLOR_CODES
from jedimaster.fonts import draw_glyph
from jedimaster.fonts import draw_string
from jedimaster.fonts import FontFormatError
from jedimaster.fonts import measure_string
from jedimaster.fonts import read_font
from jedimaster.fonts import Run
from jedimaster.fonts import TEXT_COLORS

logger = logging.getLogger(__name__)

ALL_CODES = [b"\x02\xaa\x41\x82\xbb\xcc", b"\x40\x43\x01\x99", b""]
"""Row 0: draw 2 (one byte follows), skip 1, draw 2 (two bytes follow);
row 1: skip 0, skip 3, draw 1 (one byte follows); row 2: empty."""


def test_header_fields():
    data = font_bytes({}, height=5, points=33, in_use=4, spacing=2, spare=9)
    font = read_font(data)
    assert (font.points, font.in_use, font.spacing, font.spare) == (33, 4, 2, 9)
    assert font.height == 5
    assert font.data_size == len(data) - 1547
    assert len(font.glyphs) == 256 and font.offsets[65] == 0


def test_the_height_is_entry_0():
    tall = [b"\x41"] * 6
    font = read_font(font_bytes({0: (2, tall), 65: (2, [b"\x41", b"\x41"])}))
    assert font.height == 6 and font.heights[1] == 3
    assert font.glyphs[65].height == 2 and len(font.glyphs[65].rows) == 2


def test_every_code_kind():
    font = read_font(font_bytes({65: (6, ALL_CODES)}))
    glyph = font.glyphs[65]
    assert glyph.rows == [
        [Run(True, 2), Run(False, 1), Run(True, 2)],
        [Run(False, 0), Run(False, 3), Run(True, 1)],
        [],
    ]
    assert glyph.drawn == 5
    assert glyph.row_lengths == glyph.row_bytes == [11, 9, 5]
    assert glyph.width == 6


def test_a_row_length_that_disagrees_is_reported(caplog):
    caplog.set_level(logging.WARNING)
    body = bytearray(rows(b"\x02\x00", b"\x41"))
    body[0] = 99
    font = read_font(font_bytes({66: (3, bytes(body))}, heights={66: 2}))
    assert font.glyphs[66].rows == [[Run(True, 2)], [Run(False, 1)]]
    assert font.glyphs[66].row_lengths[0] == 99
    assert "glyph 66 row 0: row length 99, its codes take 7 bytes" in warned(caplog)


def test_unreadable_fonts_are_refused():
    with pytest.raises(FontFormatError, match="too short"):
        read_font(b"\0" * 1546)
    cut = font_bytes({67: (2, [b"\x02\x00"])}, height=1)[:-1]
    with pytest.raises(FontFormatError, match="glyph 67 row 0 runs past"):
        read_font(cut)
    with pytest.raises(FontFormatError, match="glyph 0 row 0 starts past"):
        read_font(font_bytes({}, height=1, size=2))


def test_glyph_data_size_mismatch_is_reported(caplog):
    caplog.set_level(logging.WARNING)
    data = font_bytes({}, height=1) + b"extra"
    read_font(data)
    assert "the file holds" in warned(caplog)


def test_draw_glyph_follows_the_runs():
    font = read_font(font_bytes({65: (6, ALL_CODES)}))
    pixels = draw_glyph(font, 65, 10, 20, 0x1234)
    assert [(p.x, p.y) for p in pixels] == [
        (10, 20),
        (11, 20),
        (13, 20),
        (14, 20),
        (13, 21),
    ]
    assert {p.color for p in pixels} == {0x1234}


def test_runs_past_the_width_still_draw_but_width_0_draws_nothing():
    font = read_font(font_bytes({65: (1, [b"\x03\x00"]), 0: (0, [b"\x02\x00"])}))
    assert len(draw_glyph(font, 65, 0, 0, 1)) == 3
    assert draw_glyph(font, 0, 0, 0, 1) == []


def one_pixel_font(width: int = 2, spacing: int = 1):
    glyph = (width, [b"\x01\x00"])
    return read_font(font_bytes({c: glyph for c in range(7, 256)}, spacing=spacing))


def test_color_bytes_1_to_6():
    font = one_pixel_font()
    drawn = draw_string(font, b"a\x02b\x03c\x04d\x05e\x06f\x01g", 0xABCD)
    colors = [p.color for p in drawn.pixels]
    assert colors == [0xABCD, *(TEXT_COLORS[b].color for b in range(2, 7)), 0xABCD]
    assert colors[1:6] == list(COLOR_CODES[1:])
    assert [p.x for p in drawn.pixels] == [0, 3, 6, 9, 12, 15, 18]
    assert drawn.pen == 21 and not drawn.stopped


def test_measure_and_spacing():
    font = one_pixel_font(width=4, spacing=2)
    assert measure_string(font, b"ab") == 4 + 2 + 4
    assert measure_string(font, b"a\x02\x01b") == 10
    assert measure_string(font, b"") == -2
    assert measure_string(font, b"\x03") == -2
    assert measure_string(font, b"ab\0cd") == 10
    assert measure_string(one_pixel_font(spacing=0), b"") == 0


def test_a_string_ends_at_byte_0():
    font = one_pixel_font()
    assert len(draw_string(font, b"ab\0cd", 1).pixels) == 2


def test_the_stop_at_x_640():
    font = one_pixel_font(width=100, spacing=0)
    drawn = draw_string(font, b"a" * 10, 1)
    assert len(drawn.pixels) == 7 and drawn.pen == 700 and drawn.stopped
    assert measure_string(font, b"a" * 10) == 1000
    late = draw_string(font, b"aa", 1, x=639)
    assert len(late.pixels) == 1 and late.stopped
    edge = draw_string(font, b"a\x02", 1, x=540)
    assert len(edge.pixels) == 1 and edge.stopped
    assert not draw_string(font, b"a", 1, x=539).stopped
