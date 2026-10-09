"""The ``movies`` command line: dump frames and subtitles, export, captions.

Purpose:
    Prove ``movies dump frames`` prints the frames sheet (and nothing, exit
    1, for a name that does not resolve or a missing ``ffmpeg``), ``movies
    dump subtitles`` prints the sheet or its ``missing`` form, ``movies
    export`` writes the export, ``movies captions`` prints a subtitle file's
    records and shown captions, and a folder that is not an install is
    status 2.

Flow:
    Build a small install, call ``jedimaster.__main__.main`` with each
    command, capture stdout and the exit status.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_cli.py``
"""

from __future__ import annotations

import json
import logging

import pytest
from smkdata import movie_install
from smkdata import needs_ffmpeg
from smkdata import smk_file

from jedimaster.__main__ import main

logger = logging.getLogger(__name__)

SUBTITLES = b"3\nHello\n.\n.\n20\n.\n.\n.\n"


@pytest.fixture
def install(tmp_path):
    return movie_install(
        tmp_path / "XvT",
        {
            "Opening.smk": smk_file(frames=3, width=8, height=8, audio=True),
            "flyby1a.smk": smk_file(frames=2, width=8, height=4, flags=2),
            "flyby1a.txt": SUBTITLES,
        },
    )


@needs_ffmpeg
def test_dump_frames_prints_the_sheet(install, capsys):
    assert main(["movies", "dump", "frames", str(install), "Opening"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[:7] == [
        "kind frames",
        'name "movies/Opening.smk"',
        'file "BalanceOfPower/movies/Opening.smk"',
        "container smk",
        "video smackvid width=8 height=8 display=8x8 rate=0/0",
        "audio pcm_u8 rate=11025 channels=2",
        "duration_us 199980",
    ]
    assert [line.split()[1] for line in lines[7:10]] == ["1", "2", "3"]
    assert lines[7].startswith("frame 1 pts_us=0 duration_us=66660 rgba=")
    assert lines[-1] == "frames 3"


@needs_ffmpeg
def test_dump_frames_of_a_flagged_file_shows_the_half_width_display(install, capsys):
    assert main(["movies", "dump", "frames", str(install), "Flyby1a"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[4].startswith("video smackvid width=8 height=4 display=4x4 ")
    assert lines[5] == "audio none"


def test_dump_frames_of_a_name_that_does_not_resolve_prints_nothing_and_exits_1(
    install, capsys
):
    assert main(["movies", "dump", "frames", str(install), "nothing"]) == 1
    assert capsys.readouterr().out == ""
    assert (
        main(
            [
                "movies",
                "dump",
                "frames",
                str(install),
                "Opening",
                "--no-balance-of-power",
            ]
        )
        == 1
    )
    assert capsys.readouterr().out == ""


def test_dump_frames_without_ffmpeg_prints_nothing_and_exits_1(
    install, capsys, monkeypatch, caplog
):
    monkeypatch.setenv("PATH", str(install / "empty"))
    with caplog.at_level(logging.ERROR):
        assert main(["movies", "dump", "frames", str(install), "Opening"]) == 1
    assert capsys.readouterr().out == ""
    assert "ffmpeg was not found" in caplog.text


def test_dump_frames_of_a_refused_file_exits_1(tmp_path, capsys):
    install = movie_install(tmp_path / "XvT", {"bad.smk": b"RIFF" + bytes(200)})
    assert main(["movies", "dump", "frames", str(install), "bad"]) == 1
    assert capsys.readouterr().out == ""


def test_dump_subtitles_prints_the_sheet(install, capsys):
    assert main(["movies", "dump", "subtitles", str(install), "Flyby1a"]) == 0
    assert capsys.readouterr().out == (
        'kind subtitles\nname "movies/Flyby1a.txt"\n'
        'file "BalanceOfPower/movies/Flyby1a.txt"\n'
        'cue frame=3 "Hello" "" ""\ncue frame=20 "" "" ""\nend cues=2\n'
    )


def test_dump_subtitles_of_a_file_that_does_not_resolve_is_the_missing_sheet(
    install, capsys
):
    assert main(["movies", "dump", "subtitles", str(install), "Opening"]) == 0
    assert (
        capsys.readouterr().out
        == 'kind subtitles\nname "movies/Opening.txt"\nmissing\n'
    )
    code = main(
        [
            "movies",
            "dump",
            "subtitles",
            str(install),
            "Flyby1a",
            "--no-balance-of-power",
        ]
    )
    assert code == 0
    assert capsys.readouterr().out.endswith("missing\n")


def test_a_folder_that_is_not_an_install_is_status_2(tmp_path, capsys):
    for command in (["dump", "frames"], ["dump", "subtitles"]):
        assert main(["movies", *command, str(tmp_path), "Opening"]) == 2
    assert main(["movies", "export", str(tmp_path), str(tmp_path / "out")]) == 2
    assert capsys.readouterr().out == ""


def test_captions_prints_records_and_shown_captions_without_a_movie(tmp_path, capsys):
    path = tmp_path / "x.txt"
    path.write_bytes(b"3\nA\n.\n.\n8\nB\n.\n.\nzz\n")
    assert (
        main(["movies", "captions", str(path), "--frames", "10", "--frame-us", "100"])
        == 0
    )
    assert capsys.readouterr().out == (
        f'file "{path}"\nframes 10\n'
        'cue frame=3 "A" "" ""\ncue frame=8 "B" "" ""\n'
        "end cues=2 reason=bad_number\n"
        'caption frames=1-2 us=0-200 "A" "" ""\n'
        'caption frames=3-7 us=200-700 "B" "" ""\n'
        "captions 2\n"
    )


def test_captions_default_frames_is_the_last_number(tmp_path, capsys):
    path = tmp_path / "x.txt"
    path.write_bytes(b"3\nA\n.\n.\n8\nB\n.\n.\n")
    assert main(["movies", "captions", str(path)]) == 0
    out = capsys.readouterr().out
    assert "frames 8\n" in out
    assert "us=0-133460" in out and "us=133460-467110" in out


def test_captions_of_a_missing_file_exits_1(tmp_path, capsys):
    assert main(["movies", "captions", str(tmp_path / "none.txt")]) == 1
    assert capsys.readouterr().out == ""


@needs_ffmpeg
def test_export_without_balance_of_power_reports_every_name_missing(
    install, tmp_path, capsys
):
    out = tmp_path / "out"
    assert (
        main(["movies", "export", str(install), str(out), "--no-balance-of-power"]) == 0
    )
    assert (
        capsys.readouterr().out == f"exported 0 of 2 movies and movies.json to {out}\n"
    )


@needs_ffmpeg
def test_export_writes_the_files_and_prints_one_line(install, tmp_path, capsys):
    out = tmp_path / "out"
    assert main(["movies", "export", str(install), str(out)]) == 0
    assert (
        capsys.readouterr().out == f"exported 2 of 2 movies and movies.json to {out}\n"
    )
    data = json.loads((out / "movies.json").read_text(encoding="utf-8"))
    assert [m["name"] for m in data["movies"]] == ["Opening", "Flyby1a"]
    assert {p.name for p in out.iterdir()} >= {
        "Opening.mp4",
        "Flyby1a.vtt",
        "movies.json",
    }


def test_export_without_ffmpeg_exits_1_and_writes_nothing(
    install, tmp_path, capsys, monkeypatch
):
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    assert main(["movies", "export", str(install), str(tmp_path / "out")]) == 1
    assert capsys.readouterr().out == ""
    assert not (tmp_path / "out").exists()
