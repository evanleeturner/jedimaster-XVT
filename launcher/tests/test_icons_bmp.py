"""The bitmap reader: every RLE8 code, the game's spills, refusals, the palette.

Purpose:
    Prove ``read_bmp`` decodes RLE8 and uncompressed pixels the game's way:
    runs and stretches going past a row's right edge land on the row below,
    a move keeps a column past the width, writes outside the buffer are
    counted and dropped, a missing end code is reported, the palette and
    the pixels are read at fixed places whatever the headers say, and only
    unreadable files are refused.

Flow:
    Build synthetic bitmaps with ``bmpdata``, read them back, compare pixels
    (as rows, top first) and the reader's report.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_icons_bmp.py``
"""

from __future__ import annotations

import logging
import random

import pytest
from bmpdata import bmp
from bmpdata import end
from bmpdata import eol
from bmpdata import move
from bmpdata import plain
from bmpdata import ramp
from bmpdata import run
from bmpdata import stretch
from bmpdata import warned

from jedimaster.icons import BmpFormatError
from jedimaster.icons import read_bmp

logger = logging.getLogger(__name__)


def rows(data: bytes) -> list[list[int]]:
    """Return a bitmap's pixels as rows of indices, top row first."""
    image = read_bmp(data)
    w = image.width
    return [list(image.pixels[y * w : (y + 1) * w]) for y in range(image.height)]


def test_run_writes_value_n_times():
    assert rows(bmp(4, 1, run(3, 5) + end())) == [[5, 5, 5, 0]]


def test_end_of_line_moves_up_one_row():
    data = bmp(4, 2, run(4, 1) + eol() + run(2, 2) + end())
    assert rows(data) == [[2, 2, 0, 0], [1, 1, 1, 1]]


def test_end_of_bitmap_stops_reading():
    image = read_bmp(bmp(4, 1, run(1, 3) + end() + run(4, 9)))
    assert list(image.pixels) == [3, 0, 0, 0]
    assert image.end_missing is False
    assert image.bytes_read == 4


def test_move_goes_up_dy_rows_and_right_dx_columns():
    data = bmp(4, 3, run(1, 1) + move(2, 1) + run(1, 2) + end())
    assert rows(data) == [[0, 0, 0, 0], [0, 0, 0, 2], [1, 0, 0, 0]]


def test_odd_stretch_skips_its_padding_byte():
    data = bmp(4, 1, stretch(4, 5, 6) + run(1, 7) + end())
    assert rows(data) == [[4, 5, 6, 7]]


def test_even_stretch_has_no_padding_byte():
    data = bmp(4, 2, stretch(4, 5, 6, 8) + eol() + run(1, 9) + end())
    assert rows(data) == [[9, 0, 0, 0], [4, 5, 6, 8]]


def test_run_past_the_right_edge_lands_on_the_row_below():
    image = read_bmp(bmp(4, 2, eol() + run(6, 3) + end()))
    assert rows(bmp(4, 2, eol() + run(6, 3) + end())) == [[3, 3, 3, 3], [3, 3, 0, 0]]
    assert (image.spilled, image.dropped) == (2, 0)


def test_stretch_past_the_right_edge_lands_on_the_row_below():
    data = bmp(4, 2, eol() + run(2, 1) + stretch(5, 6, 7) + end())
    assert rows(data) == [[1, 1, 5, 6], [7, 0, 0, 0]]
    assert read_bmp(data).spilled == 1


def test_later_row_overwrites_a_spilled_pixel():
    codes = run(4, 1) + eol() + run(6, 2) + eol() + run(9, 3) + end()
    image = read_bmp(bmp(4, 3, codes))
    assert rows(bmp(4, 3, codes)) == [[3, 3, 3, 3], [3, 3, 3, 3], [3, 2, 1, 1]]
    assert image.spilled == 7


def test_move_after_a_spill_keeps_the_column_past_the_width():
    data = bmp(4, 3, eol() + run(6, 1) + move(1, 1) + run(1, 9) + end())
    assert rows(data) == [[0, 0, 0, 0], [1, 1, 1, 9], [1, 1, 0, 0]]


def test_writes_before_the_buffer_are_counted_and_dropped(caplog):
    image = read_bmp(bmp(4, 1, eol() + run(2, 5) + end()))
    assert list(image.pixels) == [0, 0, 0, 0]
    assert image.dropped == 2
    assert "dropped" in warned(caplog)


def test_writes_past_the_buffer_are_counted_and_dropped():
    image = read_bmp(bmp(4, 1, run(6, 5) + end()))
    assert list(image.pixels) == [5, 5, 5, 5]
    assert (image.dropped, image.spilled) == (2, 0)


@pytest.mark.parametrize(
    ("codes", "pixels", "read"),
    [
        (run(2, 5), [5, 5, 0, 0], 2),
        (run(1, 5) + b"\0\2\1", [5, 0, 0, 0], 5),
        (b"\0\5\1\2", [1, 2, 0, 0], 4),
        (run(1, 5) + b"\0", [5, 0, 0, 0], 2),
    ],
    ids=["run", "move", "stretch", "half-code"],
)
def test_missing_end_code_is_reported(caplog, codes, pixels, read):
    image = read_bmp(bmp(4, 1, codes))
    assert list(image.pixels) == pixels
    assert image.end_missing is True
    assert image.bytes_read == read
    assert "end code" in warned(caplog)


def test_rle_reads_only_the_image_size_bytes(caplog):
    image = read_bmp(bmp(4, 1, run(2, 1) + run(2, 7) + end(), image_size=2))
    assert list(image.pixels) == [1, 1, 0, 0]
    assert (image.end_missing, image.bytes_read, image.bytes_missing) == (True, 2, 0)
    assert "end code" in warned(caplog)


def test_a_file_short_of_its_image_size_decodes_what_is_there(caplog):
    image = read_bmp(bmp(4, 1, run(2, 5) + end(), image_size=10))
    assert list(image.pixels) == [5, 5, 0, 0]
    assert (image.end_missing, image.bytes_read, image.bytes_missing) == (False, 4, 6)
    assert "6 of the image size's 10 bytes are missing" in warned(caplog)


def test_an_image_size_of_0_decodes_nothing():
    image = read_bmp(bmp(4, 1, run(4, 5) + end(), image_size=0))
    assert list(image.pixels) == [0, 0, 0, 0]
    assert (image.end_missing, image.bytes_read, image.bytes_missing) == (True, 0, 0)


@pytest.mark.parametrize("width", [4, 5, 6, 7])
def test_uncompressed_rows_are_padded_to_four(width):
    top = bytes(range(10, 10 + width))
    bottom = bytes(range(40, 40 + width))
    image = read_bmp(bmp(width, 2, plain([top, bottom]), compression=0))
    assert image.pixels == top + bottom
    stride = width + (-width % 4)
    assert (image.bytes_read, image.end_missing) == (2 * stride, False)


def test_uncompressed_short_data_leaves_zeros_and_is_reported():
    data = plain([b"\1\2\3\4", b"\5\6\7\x08"])[:4]
    image = read_bmp(bmp(4, 2, data, compression=0))
    assert image.pixels == b"\0\0\0\0\5\6\7\x08"
    assert image.end_missing is True


def test_palette_and_pixels_at_fixed_places_whatever_the_headers_say(caplog):
    image = read_bmp(bmp(4, 1, run(4, 3) + end(), colors_used=16, offset=118))
    assert image.palette == [ramp(i) for i in range(256)]
    assert list(image.pixels) == [3, 3, 3, 3]
    assert (image.colors_used, image.pixel_offset) == (16, 118)
    assert image.offset_differs is True
    assert "file header offset 118" in warned(caplog)


def test_offset_at_1078_is_not_reported():
    image = read_bmp(bmp(4, 1, run(4, 3) + end()))
    assert image.offset_differs is False


def test_header_fields_are_kept_as_written():
    data = bmp(3, 2, run(3, 1) + end(), image_size=99, colors_used=7, reserved=(3, 4))
    image = read_bmp(data)
    assert (image.file_size, image.reserved1, image.reserved2) == (1082, 3, 4)
    assert (image.header_size, image.width, image.height) == (40, 3, 2)
    assert (image.planes, image.bits, image.compression) == (1, 8, 1)
    assert (image.image_size, image.colors_used, image.colors_important) == (99, 7, 7)
    assert (image.x_pixels_per_meter, image.y_pixels_per_meter) == (2835, 2834)


def test_a_path_reads_as_its_bytes(tmp_path):
    data = bmp(4, 2, run(4, 1) + eol() + stretch(1, 2, 3) + end())
    path = tmp_path / "x.bmp"
    path.write_bytes(data)
    assert read_bmp(path) == read_bmp(data)


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"mark": b"BA"}, "no BM mark"),
        ({"planes": 2}, "planes"),
        ({"bits": 4}, "bits per pixel"),
        ({"bits": 24}, "bits per pixel"),
        ({"compression": 2}, "compression 2"),
        ({"compression": 3}, "compression 3"),
        ({"height": -1}, "negative height"),
        ({"width": -4}, "negative width"),
        ({"width": (1 << 13) + 1, "height": 1 << 13}, "too many pixels"),
    ],
    ids=[
        "mark",
        "planes",
        "bits4",
        "bits24",
        "rle4",
        "bitfields",
        "height",
        "width",
        "pixels",
    ],
)
def test_unreadable_files_are_refused(change, message):
    args = {"width": 4, "height": 1, **change}
    width, height = args.pop("width"), args.pop("height")
    with pytest.raises(BmpFormatError, match=message):
        read_bmp(bmp(width, height, run(4, 1) + end(), **args))


def test_a_file_too_short_for_its_palette_is_refused():
    with pytest.raises(BmpFormatError, match="too short"):
        read_bmp(bmp(4, 1, b"")[:1077])
    assert read_bmp(bmp(4, 1, b"")).end_missing is True


def test_random_codes_never_crash_or_write_outside():
    rng = random.Random(7)
    for _ in range(300):
        width, height = rng.randint(0, 9), rng.randint(0, 9)
        codes = bytes(rng.choice((0, 0, 1, 2, 3, 5, 200, 255)) for _ in range(60))
        image = read_bmp(bmp(width, height, codes))
        assert len(image.pixels) == width * height
        assert 0 <= image.bytes_read <= len(codes)
