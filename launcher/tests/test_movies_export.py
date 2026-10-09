"""The movie export: MP4, PNG, VTT and movies.json, and the JSON's schema.

Purpose:
    Prove the export writes, for each name that resolves, an H.264
    ``yuv420p`` MP4 with AAC at 48,000 Hz (when there is sound) at the
    intended size with the moov atom first, the middle frame as a PNG at
    that size (each stored row drawn twice for a flagged file), and a
    WebVTT file of the shown captions; that ``movies.json`` reports every
    name once, validates against its schema, which matches its model and
    rejects bad data; and that a missing ``ffmpeg`` is an error before
    anything is written.

Flow:
    Build an install of small real Smacker files, export once per module,
    read the outputs back with ``ffprobe``, ``zlib`` and ``json``.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_export.py``
"""

from __future__ import annotations

import copy
import json
import logging
import struct
import subprocess
from pathlib import Path

import jsonschema
import pytest
from smkdata import movie_install
from smkdata import needs_ffmpeg
from smkdata import smk_file
from smkdata import smk_header
from test_fonts_output import png_pixels

from jedimaster.movies import Caption
from jedimaster.movies import export
from jedimaster.movies import export_movies
from jedimaster.movies import FfmpegFailed
from jedimaster.movies import FfmpegMissing
from jedimaster.movies import movies_schema
from jedimaster.movies import read_header
from jedimaster.movies import render_vtt
from jedimaster.movies.export import double_rows
from jedimaster.movies.export import middle_frame
from jedimaster.movies.export import mp4_command
from jedimaster.movies.export import output_stems
from jedimaster.movies.export import vtt_text
from jedimaster.movies.export import vtt_time
from jedimaster.movies.frames import iter_rgba
from jedimaster.movies.to_json import movie_entry
from jedimaster.movies.to_json import movies_to_json

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "movies.schema.json"
LIST = "4\nopening\n1 0 1\nt\nd\nGhost\n1 0 2\nt\nd\nBroken\n1 0 3\nt\nd\nOPENING\n1 0 4\nt\nd\n"
SUBTITLES = b"3\nHello & <b>goodbye</b>\n.\nThird caf\xe9\n20\n.\n.\n.\n"


def build_install(root: Path) -> Path:
    return movie_install(
        root,
        {
            "cutscene.lst": LIST.encode(),
            "Opening.smk": smk_file(frames=4, width=8, height=8, audio=True),
            "flyby1a.smk": smk_file(frames=3, width=8, height=4, flags=2),
            "flyby1a.txt": SUBTITLES,
            "broken.smk": b"SMK2" + bytes(40),
        },
    )


@pytest.fixture(scope="module")
def schema_validator():
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


@pytest.fixture(scope="module")
def exported(tmp_path_factory):
    root = tmp_path_factory.mktemp("export")
    install = build_install(root / "XvT")
    out = root / "out"
    data = export_movies(install, out)
    return install, out, data


def by_name(data):
    return {m["name"]: m for m in data["movies"]}


def probe(path, entries):
    done = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", entries, "-of", "json", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(done.stdout)


def test_vtt_time_rounds_to_the_millisecond():
    assert vtt_time(0) == "00:00:00.000"
    assert vtt_time(10009500) == "00:00:10.010"
    assert vtt_time(499) == "00:00:00.000"
    assert vtt_time(3_723_456_000) == "01:02:03.456"


def test_vtt_text_escapes_ampersand_and_angle_brackets_and_cr():
    assert vtt_text("a & b < c > d --> e") == "a &amp; b &lt; c &gt; d --&gt; e"
    assert vtt_text("x\ry") == "x y"
    assert vtt_text("&lt;") == "&amp;lt;"


def test_vtt_holds_only_the_non_empty_lines_and_skips_empty_captions():
    captions = [
        Caption(0, 1, 9, 0, 600_000, ("one", "", "three")),
        Caption(1, 10, 19, 600_000, 1_200_000, ("", "", "")),
        Caption(2, 20, 20, 1_200_000, 1_300_000, ("", "only", "")),
    ]
    assert render_vtt(captions) == (
        "WEBVTT\n\n"
        "00:00:00.000 --> 00:00:00.600\none\nthree\n\n"
        "00:00:01.200 --> 00:00:01.300\nonly\n"
    )
    assert render_vtt([]) == "WEBVTT\n"


def test_double_rows_draws_each_stored_row_twice():
    row_a, row_b = b"\x01\x02\x03\x04" * 2, b"\x05\x06\x07\x08" * 2
    assert double_rows(row_a + row_b, 2) == row_a + row_a + row_b + row_b


@pytest.mark.parametrize(
    ("frames", "middle"), [(1, 1), (2, 2), (3, 2), (150, 76), (885, 443)]
)
def test_the_middle_frame_is_the_one_showing_at_half_the_duration(frames, middle):
    assert middle_frame(frames) == middle


def test_output_stems_are_safe_and_never_repeat_in_any_case():
    stems = output_stems(["Opening", "a b/c", "OPENING", "opening", "x.y~z-1", ""])
    assert stems == {
        "Opening": "Opening",
        "a b/c": "a_b_c",
        "OPENING": "OPENING-2",
        "opening": "opening-3",
        "x.y~z-1": "x.y~z-1",
        "": "_",
    }


def test_the_mp4_command_names_the_codecs_and_puts_the_moov_atom_first():
    header = read_header(
        smk_header(width=640, height=180, flags=2, audio_words=(0x50002B11,) + (0,) * 6)
    )
    command = mp4_command("ffmpeg", "in.smk", "out.mp4", header)
    joined = " ".join(command)
    assert "-c:v libx264" in joined and "-pix_fmt yuv420p" in joined
    assert "-c:a aac" in joined and "-ar 48000" in joined
    assert "-movflags +faststart" in joined and joined.endswith("out.mp4")
    assert "scale=iw:2*ih:flags=neighbor,format=yuv420p" in joined
    assert "-map 0:a:0" in joined


def test_the_mp4_command_has_no_doubling_and_no_audio_when_the_file_has_none():
    command = mp4_command("ffmpeg", "in.smk", "out.mp4", read_header(smk_header()))
    joined = " ".join(command)
    assert "scale" not in joined and "-c:a" not in joined and "0:a" not in joined


@needs_ffmpeg
def test_every_name_is_reported_once_in_the_asked_order(exported):
    _, _, data = exported
    assert [m["name"] for m in data["movies"]] == [
        "Opening",
        "Ghost",
        "Broken",
        "Flyby1a",
    ]
    assert [m["status"] for m in data["movies"]] == [
        "found",
        "missing",
        "unreadable",
        "found",
    ]


@needs_ffmpeg
def test_movies_json_is_written_and_validates(exported, schema_validator):
    _, out, data = exported
    written = json.loads((out / "movies.json").read_text(encoding="utf-8"))
    assert written == data
    assert list(schema_validator.iter_errors(written)) == []


@needs_ffmpeg
def test_the_ffmpeg_version_and_settings_are_recorded(exported):
    _, _, data = exported
    done = subprocess.run(["ffmpeg", "-version"], capture_output=True, text=True)
    assert data["ffmpeg"]["version"] in done.stdout.splitlines()[0]
    settings = data["ffmpeg"]["settings"]
    assert settings["video_codec"] == "libx264" and settings["pix_fmt"] == "yuv420p"
    assert settings["audio_codec"] == "aac" and settings["audio_rate"] == 48000
    assert settings["movflags"] == "+faststart"


@needs_ffmpeg
def test_entry_reports_header_size_timing_and_audio(exported):
    _, _, data = exported
    opening = by_name(data)["Opening"]
    assert opening["file"] == "BalanceOfPower/movies/Opening.smk"
    assert (opening["picture_width"], opening["picture_height"]) == (8, 8)
    assert (opening["frames"], opening["frame_us"], opening["duration_us"]) == (
        4,
        66660,
        4 * 66660,
    )
    assert opening["audio"] == {
        "track": 0,
        "codec": "pcm_u8",
        "rate": 11025,
        "channels": 2,
        "bits": 8,
    }
    header = opening["header"]
    assert (
        header["signature"] == "SMK2"
        and header["frames"] == 4
        and header["rate"] == -6666
    )
    assert header["tracks"][0]["word"] == 0x50002B11 and len(header["tracks"]) == 7
    assert header["table_frames"] == 4 and header["palette_frames"] == 4
    assert header["key_frames"] == 1 and header["frame_bytes"] == 4 * 2016
    flyby = by_name(data)["Flyby1a"]
    assert (flyby["picture_width"], flyby["picture_height"]) == (8, 8)
    assert flyby["header"]["height"] == 4 and flyby["audio"] is None
    assert flyby["header"]["palette_frames"] == 3 and flyby["header"]["key_frames"] == 1


@needs_ffmpeg
def test_missing_and_unreadable_names_have_no_header_or_files(exported):
    _, _, data = exported
    ghost, broken = by_name(data)["Ghost"], by_name(data)["Broken"]
    assert ghost["file"] is None and ghost["header"] is None and ghost["error"] is None
    assert broken["file"] == "BalanceOfPower/movies/Broken.smk"
    assert "broken.smk" in broken["error"] and ": header: " in broken["error"]
    for entry in (ghost, broken):
        assert entry["files"] == {"video": None, "poster": None, "captions": None}
        assert entry["picture_width"] is None and entry["audio"] is None


@needs_ffmpeg
def test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size(exported):
    _, out, _ = exported
    info = probe(
        out / "Opening.mp4", "stream=codec_name,pix_fmt,width,height,sample_rate"
    )
    video, audio = info["streams"]
    assert (video["codec_name"], video["pix_fmt"]) == ("h264", "yuv420p")
    assert (video["width"], video["height"]) == (8, 8)
    assert (audio["codec_name"], audio["sample_rate"]) == ("aac", "48000")


@needs_ffmpeg
def test_a_flagged_file_is_drawn_at_twice_the_stored_height_and_has_no_audio(exported):
    _, out, _ = exported
    info = probe(out / "Flyby1a.mp4", "stream=codec_name,width,height")
    (video,) = info["streams"]
    assert (video["width"], video["height"]) == (8, 8)


@needs_ffmpeg
def test_the_moov_atom_comes_before_the_media_data(exported):
    _, out, _ = exported
    data = (out / "Opening.mp4").read_bytes()
    atoms, at = [], 0
    while at + 8 <= len(data):
        size, kind = struct.unpack(">I4s", data[at : at + 8])
        atoms.append(kind)
        at += size or len(data)
    assert atoms.index(b"moov") < atoms.index(b"mdat")


@needs_ffmpeg
def test_png_is_the_middle_frame_at_the_stored_size(exported):
    install, out, _ = exported
    path = next((install / "BalanceOfPower/MOVIES").glob("Opening.smk"))
    frames = list(iter_rgba(path, 8, 8))
    width, height, pixels = png_pixels((out / "Opening.png").read_bytes())
    assert (width, height) == (8, 8)
    middle = frames[middle_frame(4) - 1]
    assert b"".join(pixels) == middle


@needs_ffmpeg
def test_png_of_a_flagged_file_draws_each_stored_row_twice(exported):
    install, out, _ = exported
    path = install / "BalanceOfPower/MOVIES/flyby1a.smk"
    frame = list(iter_rgba(path, 8, 4))[middle_frame(3) - 1]
    width, height, pixels = png_pixels((out / "Flyby1a.png").read_bytes())
    assert (width, height) == (8, 8)
    assert b"".join(pixels) == double_rows(frame, 8)


@needs_ffmpeg
def test_png_pixels_read_back_in_a_known_picture(tmp_path):
    from jedimaster.icons.png import rgba_png

    rows = bytes([10, 20, 30, 255, 40, 50, 60, 255]) + bytes([1, 2, 3, 4, 5, 6, 7, 8])
    width, height, pixels = png_pixels(rgba_png(2, 2, rows))
    assert (width, height) == (2, 2)
    assert pixels == [
        bytes([10, 20, 30, 255]),
        bytes([40, 50, 60, 255]),
        bytes([1, 2, 3, 4]),
        bytes([5, 6, 7, 8]),
    ]


@needs_ffmpeg
def test_vtt_holds_the_shown_captions_escaped(exported):
    _, out, data = exported
    text = (out / "Flyby1a.vtt").read_text(encoding="utf-8")
    assert text.startswith("WEBVTT\n\n")
    assert "Hello &amp; &lt;b&gt;goodbye&lt;/b&gt;\nThird caf\xe9\n" in text
    assert "caf\xe9".encode() in (out / "Flyby1a.vtt").read_bytes()
    assert "00:00:00.000 --> 00:00:00.133" in text
    assert by_name(data)["Flyby1a"]["files"]["captions"] == "Flyby1a.vtt"


@needs_ffmpeg
def test_subtitle_records_and_captions_are_in_the_json(exported):
    _, _, data = exported
    subtitles = by_name(data)["Flyby1a"]["subtitles"]
    assert subtitles["file"] == "BalanceOfPower/movies/Flyby1a.txt"
    assert subtitles["reason"] == "eof"
    assert [r["number"] for r in subtitles["records"]] == [3, 20]
    (caption,) = subtitles["captions"]
    assert (caption["start_frame"], caption["end_frame"]) == (1, 2)
    assert caption["lines"] == ["Hello & <b>goodbye</b>", "", "Third caf\xe9"]
    assert (caption["start_us"], caption["end_us"]) == (0, 2 * 66660)
    assert by_name(data)["Opening"]["subtitles"] is None


@needs_ffmpeg
def test_text_keeps_every_byte_as_one_character_in_the_json_file(exported):
    _, out, _ = exported
    text = (out / "movies.json").read_text(encoding="utf-8")
    assert "Third caf\xe9" in text and "\\u00e9" not in text


@needs_ffmpeg
def test_the_poster_is_asked_for_at_the_middle_frame(tmp_path, monkeypatch):
    install = movie_install(
        tmp_path / "XvT", {"Opening.smk": smk_file(frames=5, width=8, height=8)}
    )
    asked = []
    real = export.frame_rgba
    monkeypatch.setattr(export, "frame_rgba", lambda *a: asked.append(a[3]) or real(*a))
    export_movies(install, tmp_path / "out")
    assert asked == [3]


@needs_ffmpeg
def test_a_movie_of_no_frames_is_found_and_writes_no_file(tmp_path):
    install = movie_install(tmp_path / "XvT", {"Opening.smk": smk_file(frames=0)})
    data = export_movies(install, tmp_path / "out")
    opening = by_name(data)["Opening"]
    assert opening["status"] == "found" and opening["frames"] == 0
    assert opening["files"] == {"video": None, "poster": None, "captions": None}
    assert [p.name for p in (tmp_path / "out").iterdir()] == ["movies.json"]


@needs_ffmpeg
def test_a_movie_of_one_frame_has_its_poster(tmp_path):
    install = movie_install(tmp_path / "XvT", {"Opening.smk": smk_file(frames=1)})
    data = export_movies(install, tmp_path / "out")
    assert by_name(data)["Opening"]["files"]["poster"] == "Opening.png"
    width, height, pixels = png_pixels((tmp_path / "out" / "Opening.png").read_bytes())
    assert (width, height) == (8, 8) and len(set(pixels)) == 1


@needs_ffmpeg
def test_a_movie_ffmpeg_cannot_decode_is_an_error(tmp_path):
    install = movie_install(
        tmp_path / "XvT", {"Opening.smk": smk_header(width=8, height=8)}
    )
    with pytest.raises(FfmpegFailed) as caught:
        export_movies(install, tmp_path / "out")
    assert "Opening.smk" in str(caught.value)


@needs_ffmpeg
def test_an_odd_picture_size_cannot_be_written_as_yuv420p_and_is_an_error(tmp_path):
    install = movie_install(
        tmp_path / "XvT", {"Opening.smk": smk_file(frames=2, width=7, height=7)}
    )
    with pytest.raises(FfmpegFailed) as caught:
        export_movies(install, tmp_path / "out")
    assert "divisible by 2" in str(caught.value)
    assert not (tmp_path / "out" / "Opening.png").exists()


@needs_ffmpeg
def test_files_written_are_the_files_the_json_names_and_no_others(exported):
    _, out, data = exported
    named = {"movies.json"}
    for movie in data["movies"]:
        named |= {name for name in movie["files"].values() if name}
    assert {p.name for p in out.iterdir()} == named
    assert named == {
        "movies.json",
        "Opening.mp4",
        "Opening.png",
        "Flyby1a.mp4",
        "Flyby1a.png",
        "Flyby1a.vtt",
    }


@needs_ffmpeg
def test_the_view_without_balance_of_power_reports_every_name_missing(tmp_path):
    install = build_install(tmp_path / "XvT")
    data = export_movies(install, tmp_path / "out", balance_of_power=False)
    assert data["balance_of_power"] is False
    assert [m["status"] for m in data["movies"]] == ["missing", "missing"]
    assert [p.name for p in (tmp_path / "out").iterdir()] == ["movies.json"]


@needs_ffmpeg
def test_a_movie_with_a_subtitle_file_but_no_movie_has_records_and_no_captions(
    tmp_path,
):
    install = movie_install(tmp_path / "XvT", {"flyby1a.txt": SUBTITLES})
    data = export_movies(install, tmp_path / "out")
    flyby = by_name(data)["Flyby1a"]
    assert flyby["status"] == "missing" and flyby["files"]["captions"] is None
    assert (
        len(flyby["subtitles"]["records"]) == 2 and flyby["subtitles"]["captions"] == []
    )


def test_a_missing_ffmpeg_stops_the_export_before_anything_is_written(
    tmp_path, monkeypatch
):
    install = build_install(tmp_path / "XvT")
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    with pytest.raises(FfmpegMissing):
        export_movies(install, tmp_path / "out")
    assert not (tmp_path / "out").exists()


def test_schema_file_matches_model():
    text = json.dumps(movies_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/movies.schema.json is stale: run python tools/gen_schema.py"
    )


def sample():
    header = read_header(smk_header(audio_words=(0x50002B11,) + (0,) * 6))
    entry = movie_entry(
        "a",
        "found",
        "movies/a.smk",
        None,
        header,
        {
            "file": "movies/a.txt",
            "reason": "eof",
            "records": [{"number": 3, "lines": ["a", "", ""]}],
            "captions": [
                {
                    "record": 0,
                    "start_frame": 1,
                    "end_frame": 2,
                    "start_us": 0,
                    "end_us": 5,
                    "lines": ["a", "", ""],
                }
            ],
        },
        {"video": "a.mp4", "poster": "a.png", "captions": "a.vtt"},
    )
    missing = movie_entry("b", "missing", None, None, None, None, {})
    return json.loads(
        json.dumps(movies_to_json([entry, missing], True, "6.1", {"crf": 18}))
    )


def test_a_sample_validates(schema_validator):
    assert list(schema_validator.iter_errors(sample())) == []


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("format",), "other"),
        (("movies", 0, "status"), "gone"),
        (("movies", 0, "header", "signature"), "SMK3"),
        (("movies", 0, "header", "tracks"), []),
        (("movies", 0, "audio", "bits"), 12),
        (("movies", 0, "files", "video"), "a.avi"),
        (("movies", 0, "subtitles", "reason"), "never"),
        (("movies", 0, "subtitles", "captions", 0, "start_frame"), 0),
        (("movies", 0, "subtitles", "records", 0, "lines"), ["a", "b"]),
        (("movies", 1, "extra"), 0),
        (("ffmpeg", "version"), 6),
    ],
    ids=[
        "format",
        "status",
        "signature",
        "tracks",
        "bits",
        "video-name",
        "reason",
        "start-frame",
        "lines",
        "extra",
        "version",
    ],
)
def test_schema_rejects_bad_data(schema_validator, path, value):
    data = copy.deepcopy(sample())
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(schema_validator.iter_errors(data)), path
