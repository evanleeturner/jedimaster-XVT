"""Decoding through ffmpeg: frame numbers, times, CRC-32, early stops, errors.

Purpose:
    Prove the frames come out as the sheets need them: numbered from 1,
    frame i starting at (i - 1) frame durations and lasting one, hashed by
    ``zlib.crc32`` over exactly the red, green, blue, alpha bytes at the
    stored size; that the stream holds one frame at a time and stops its
    program early; that a missing ``ffmpeg`` or a file it cannot read is a
    clear error; and that the frames sheet prints as the sheets do.

Flow:
    Make a lossless movie of known frames with ``ffmpeg``, decode it, compare
    with ``zlib.crc32`` of the frames made; render sheets from small
    headers. Tests that need ``ffmpeg`` skip when it is not on the path.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_frames.py``
"""

from __future__ import annotations

import logging
import subprocess
import zlib

import pytest
from smkdata import lossless_movie
from smkdata import needs_ffmpeg
from smkdata import pattern_frames
from smkdata import smk_header
from smkdata import STOCK_AUDIO

from jedimaster.movies import FfmpegFailed
from jedimaster.movies import FfmpegMissing
from jedimaster.movies import Frame
from jedimaster.movies import iter_frames
from jedimaster.movies import read_header
from jedimaster.movies import render_frames
from jedimaster.movies.frames import ffmpeg_version
from jedimaster.movies.frames import find_tool
from jedimaster.movies.frames import frame_rgba
from jedimaster.movies.frames import iter_rgba
from jedimaster.movies.frames import probe_video

logger = logging.getLogger(__name__)

W, H, N = 6, 4, 5


@pytest.fixture(scope="module")
def movie(tmp_path_factory):
    frames = pattern_frames(N, W, H)
    path = tmp_path_factory.mktemp("lossless") / "known.mkv"
    return lossless_movie(path, frames, W, H), frames


@needs_ffmpeg
def test_each_frame_has_its_number_times_and_crc(movie):
    path, frames = movie
    got = list(iter_frames(path, W, H, 66730))
    assert got == [
        Frame(i + 1, i * 66730, 66730, zlib.crc32(frames[i])) for i in range(N)
    ]


@needs_ffmpeg
def test_the_decoded_bytes_are_the_frames_rgba_rows_top_first(movie):
    path, frames = movie
    assert list(iter_rgba(path, W, H)) == frames
    assert frame_rgba(path, W, H, 3) == frames[2]


@needs_ffmpeg
def test_a_limit_stops_after_that_many_frames(movie):
    path, _ = movie
    assert [f.index for f in iter_frames(path, W, H, 10, limit=2)] == [1, 2]
    assert list(iter_frames(path, W, H, 10, limit=0)) == []


@needs_ffmpeg
def test_the_stream_gives_one_frame_at_a_time_and_closes_early(movie):
    path, _ = movie
    stream = iter_frames(path, W, H, 10)
    assert next(stream).index == 1
    stream.close()


@needs_ffmpeg
def test_frame_past_the_end_is_an_error(movie):
    path, _ = movie
    with pytest.raises(ValueError, match="no frame 9"):
        frame_rgba(path, W, H, 9)


@needs_ffmpeg
def test_a_stored_size_that_is_not_the_streams_gives_other_frames(movie):
    path, frames = movie
    assert len(list(iter_rgba(path, W, H // 2))) == N * 2


@needs_ffmpeg
def test_a_file_ffmpeg_cannot_read_is_a_clear_error(tmp_path):
    path = tmp_path / "bad.smk"
    path.write_bytes(b"not a movie at all" * 20)
    with pytest.raises(FfmpegFailed) as caught:
        list(iter_rgba(path, 4, 4))
    assert "bad.smk" in str(caught.value)
    with pytest.raises(FfmpegFailed):
        probe_video(path)


@needs_ffmpeg
def test_version_and_probe_report_what_the_programs_say(movie):
    path, _ = movie
    first = subprocess.run(
        ["ffmpeg", "-version"], capture_output=True, text=True
    ).stdout.splitlines()[0]
    assert ffmpeg_version() == first.split()[2]
    info = probe_video(path)
    assert (info["width"], info["height"]) == (str(W), str(H))
    assert "avg_frame_rate" in info and "codec_name" in info


def test_a_missing_ffmpeg_is_a_clear_error(tmp_path, monkeypatch):
    monkeypatch.setenv("PATH", str(tmp_path))
    with pytest.raises(FfmpegMissing) as caught:
        find_tool("ffmpeg")
    assert "ffmpeg" in str(caught.value) and "system path" in str(caught.value)
    with pytest.raises(FfmpegMissing):
        list(iter_frames(tmp_path / "x.smk", 2, 2, 10))
    with pytest.raises(FfmpegMissing):
        ffmpeg_version()
    with pytest.raises(FfmpegMissing):
        probe_video(tmp_path / "x.smk")


def test_frames_sheet_prints_in_the_sheets_form():
    header = read_header(
        smk_header(
            width=640,
            height=360,
            frames=2,
            rate=-6673,
            audio_words=(STOCK_AUDIO, 0, 0, 0, 0, 0, 0),
            audio_buffers=(5,) + (0,) * 6,
        )
    )
    frames = [Frame(1, 0, 66730, 0xAB), Frame(2, 66730, 66730, 0x0123ABCD)]
    text = render_frames("Opening", "BalanceOfPower/movies/Opening.smk", header, frames)
    assert text == (
        "kind frames\n"
        'name "movies/Opening.smk"\n'
        'file "BalanceOfPower/movies/Opening.smk"\n'
        "container smk\n"
        "video smackvid width=640 height=360 display=640x360 rate=0/0\n"
        "audio smackaud rate=11025 channels=2\n"
        "duration_us 133460\n"
        "frame 1 pts_us=0 duration_us=66730 rgba=000000ab\n"
        "frame 2 pts_us=66730 duration_us=66730 rgba=0123abcd\n"
        "frames 2\n"
    )


def test_a_flagged_file_displays_half_its_width_and_no_audio_prints_none():
    header = read_header(
        smk_header(width=640, height=180, frames=1, flags=2, rate=-6666)
    )
    text = render_frames("flipped", "x", header, [Frame(1, 0, 66660, 1)], rate="25/2")
    lines = text.splitlines()
    assert lines[4] == "video smackvid width=640 height=180 display=320x180 rate=25/2"
    assert lines[5] == "audio none"
    assert lines[6] == "duration_us 66660"


def test_the_frames_line_counts_the_frames_given_not_the_headers():
    header = read_header(smk_header(frames=9))
    text = render_frames("n", "f", header, [Frame(1, 0, 1, 2)])
    assert text.splitlines()[-1] == "frames 1"
    assert text.splitlines()[6] == f"duration_us {9 * 66660}"
