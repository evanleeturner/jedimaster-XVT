"""The Smacker header: every field, the timing, the picture size, the refusals.

Purpose:
    Prove ``read_header`` unpacks each field of the format's header from
    bytes and from a path, derives the frame duration from the signed rate
    (milliseconds, units of 10 microseconds, or ten frames a second), the
    audio track from the rate words, the intended picture size from the
    flags (each stored row drawn twice), and refuses a file that is not a
    Smacker file or is cut short, naming the file and the field.

Flow:
    Build headers with ``smkdata.smk_header``; read; compare.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_header.py``
"""

from __future__ import annotations

import logging

import pytest
from smkdata import smk_header
from smkdata import STOCK_AUDIO

from jedimaster.movies import MovieFormatError
from jedimaster.movies import read_header
from jedimaster.movies.header import frame_duration_us

logger = logging.getLogger(__name__)


def test_every_field_is_read():
    data = smk_header(
        width=320,
        height=200,
        frames=3,
        rate=-6673,
        flags=0,
        audio_buffers=(11, 12, 13, 14, 15, 16, 17),
        audio_words=(STOCK_AUDIO, 0, 0x40000000 | 8000, 0, 0, 0, 5),
        tree_bytes=10,
        tree_sizes=(101, 102, 103, 104),
        sizes=[9, 20, 31],
        types=[1, 4, 7],
    )
    header = read_header(data)
    assert header.signature == "SMK2"
    assert (header.width, header.height, header.frames) == (320, 200, 3)
    assert (header.rate, header.flags) == (-6673, 0)
    assert header.audio_buffers == (11, 12, 13, 14, 15, 16, 17)
    assert header.tree_bytes == 10
    assert header.tree_sizes == (101, 102, 103, 104)
    assert header.audio_words == (STOCK_AUDIO, 0, 0x40000000 | 8000, 0, 0, 0, 5)
    assert header.unused == 0
    assert header.frame_sizes == (9, 20, 31)
    assert header.frame_types == (1, 4, 7)
    assert header.expected_size() == len(data)


def test_signature_smk4_is_read_and_a_path_reads_the_same(tmp_path):
    data = smk_header(signature=b"SMK4")
    path = tmp_path / "m.smk"
    path.write_bytes(data)
    assert read_header(path) == read_header(data)
    assert read_header(str(path)).signature == "SMK4"


def test_frame_size_clears_its_two_flag_bits_and_bit_0_is_the_key_frame():
    header = read_header(smk_header(frames=3, sizes=[12 | 3, 16 | 1, 20 | 2]))
    assert [header.frame_size(i) for i in range(3)] == [12, 16, 20]
    assert [header.is_key_frame(i) for i in range(3)] == [True, True, False]
    assert header.expected_size() == 104 + 15 + 12 + 16 + 20


def test_frame_types_bit_0_is_a_palette():
    header = read_header(smk_header(frames=3, types=[1, 2, 3]))
    assert [header.has_palette(i) for i in range(3)] == [True, False, True]


@pytest.mark.parametrize(
    ("rate", "expected"),
    [(40, 40000), (1, 1000), (-6666, 66660), (-6673, 66730), (-1, 10), (0, 100000)],
)
def test_frame_duration_from_the_signed_rate(rate, expected):
    assert frame_duration_us(rate) == expected
    assert read_header(smk_header(rate=rate)).frame_us == expected


def test_duration_is_frames_times_frame_duration():
    assert read_header(smk_header(frames=4, rate=-6666)).duration_us == 4 * 66660


@pytest.mark.parametrize(
    ("flags", "doubled", "height"),
    [(0, False, 180), (1, False, 180), (2, True, 360), (4, True, 360), (6, True, 360)],
)
def test_intended_picture_size_doubles_the_stored_height(flags, doubled, height):
    header = read_header(smk_header(width=640, height=180, flags=flags))
    assert header.doubled is doubled
    assert (header.picture_width, header.picture_height) == (640, height)
    assert header.display_width == (320 if doubled else 640)
    assert header.height == 180


def test_flag_bits_have_their_names():
    header = read_header(smk_header(flags=2))
    assert (header.ring, header.interlaced, header.y_doubled) == (False, True, False)
    header = read_header(smk_header(flags=4))
    assert (header.ring, header.interlaced, header.y_doubled) == (False, False, True)


def test_ring_flag_adds_one_table_entry_not_one_frame():
    data = smk_header(frames=2, flags=1)
    header = read_header(data)
    assert header.ring is True
    assert header.frames == 2
    assert len(header.frame_sizes) == len(header.frame_types) == 3
    assert header.expected_size() == len(data)


def test_a_path_with_the_ring_flag_reads_the_longer_tables(tmp_path):
    data = smk_header(frames=2, flags=1)
    path = tmp_path / "ring.smk"
    path.write_bytes(data)
    header = read_header(path)
    assert header == read_header(data)
    assert len(header.frame_sizes) == 3


def test_audio_track_is_the_first_with_data_and_its_word_is_split():
    words = (
        0,
        0x8000_0000 | 22050,
        STOCK_AUDIO,
        0x6000_0000 | 0x1000_0000 | 96000,
        0,
        0,
        0,
    )
    header = read_header(
        smk_header(audio_words=words, audio_buffers=(0, 1, 2, 3, 4, 5, 6))
    )
    track = header.audio
    assert track is not None and track.index == 2
    assert (track.rate, track.channels, track.codec) == (11025, 2, "smackaud")
    assert (track.compressed, track.has_data) == (True, True)
    assert (track.sixteen_bit, track.stereo, track.buffer_size) == (False, True, 2)
    other = header.tracks[3]
    assert (other.rate, other.sixteen_bit, other.stereo, other.compressed) == (
        96000,
        True,
        True,
        False,
    )
    assert other.codec == "pcm_s16le"
    assert header.tracks[1].has_data is False


def test_compression_kind_bits_and_mono_8_bit_pcm():
    track = read_header(
        smk_header(audio_words=(0x4000_0000 | 0x0C00_0000 | 8000,) * 7)
    ).audio
    assert track is not None
    assert track.compression == 3
    assert (track.channels, track.codec) == (1, "pcm_u8")


def test_no_track_with_data_gives_no_audio():
    assert read_header(smk_header(audio_words=(0x2B11, 0, 0, 0, 0, 0, 0))).audio is None


def test_a_file_longer_than_the_header_describes_is_accepted():
    assert read_header(smk_header() + b"extra").frames == 3


def refused(data, tmp_path=None):
    with pytest.raises(MovieFormatError) as caught:
        read_header(data)
    return caught.value


def test_refuses_a_file_that_is_not_smacker_naming_the_signature():
    error = refused(b"RIFF" + bytes(200))
    assert error.field == "signature"
    assert "RIFF" in str(error)
    assert "<bytes>" in str(error)


def test_refuses_an_empty_and_a_three_byte_file():
    assert refused(b"").field == "signature"
    assert refused(b"SMK").field == "signature"


def test_refuses_a_cut_fixed_header():
    error = refused(smk_header()[:103])
    assert error.field == "header"
    assert "103 of 104" in str(error)


def test_refuses_a_cut_frame_table():
    data = smk_header(frames=3, body=False)
    assert refused(data[: 104 + 14]).field == "frame table"
    assert refused(data[: 104 + 12]).field == "frame table"


def test_refuses_a_file_cut_inside_the_frames():
    data = smk_header(frames=3, tree_bytes=5)
    error = refused(data[:-1])
    assert error.field == "frame data"
    assert str(len(data)) in str(error)


def test_refuses_a_huge_frame_count_in_a_small_file():
    data = smk_header(frames=3, body=False)
    patched = data[:12] + b"\xff\xff\xff\xff" + data[16:]
    assert refused(patched).field == "frame table"


def test_the_error_names_the_path(tmp_path):
    path = tmp_path / "broken.smk"
    path.write_bytes(smk_header()[:50])
    with pytest.raises(MovieFormatError) as caught:
        read_header(path)
    assert str(path) in str(caught.value)
    assert caught.value.label == str(path)


def test_a_path_cut_inside_the_frames_is_refused(tmp_path):
    path = tmp_path / "cut.smk"
    path.write_bytes(smk_header()[:-1])
    with pytest.raises(MovieFormatError) as caught:
        read_header(path)
    assert caught.value.field == "frame data"


def test_a_missing_path_raises_os_error(tmp_path):
    with pytest.raises(OSError):
        read_header(tmp_path / "none.smk")
