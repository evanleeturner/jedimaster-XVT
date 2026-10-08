"""The counted lists: the cutscene list and the campaign medal list.

Purpose:
    Prove the count line, the comment lines skipped before each value, the
    numbers line, the early ends, and the 31- and 127-character cuts and the
    16 slots of the game's view.

Flow:
    Build lists with ``crlf``, read, derive the view, inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_counted.py``
"""

from __future__ import annotations

import logging

from listdata import crlf

from jedimaster.lists import awards_view
from jedimaster.lists import cutscenes_view
from jedimaster.lists import read_awards
from jedimaster.lists import read_cutscenes

logger = logging.getLogger(__name__)

CUTSCENES = crlf(
    "2",
    "// note",
    "// another",
    "movie1",
    "// inside",
    "1 0 10",
    "thumb1",
    "// before the description",
    "First scene.",
    "movie2",
    "2 1 20",
    "thumb2",
    "Second scene.",
    "movie3",
    "3 0 30",
    "thumb3",
    "Not read.",
)


def test_cutscenes_read_count_entries_skipping_comments():
    cutscenes = read_cutscenes(CUTSCENES)
    assert cutscenes.count == 2 and cutscenes.end == "count"
    first, second = cutscenes.entries
    assert [c.text for c in first.comments] == [
        "// note",
        "// another",
        "// inside",
        "// before the description",
    ]
    assert (first.movie.text, first.numbers, first.thumbnail.text) == (
        "movie1",
        [1, 0, 10],
        "thumb1",
    )
    assert first.description.line == 9 and second.numbers == [2, 1, 20]
    assert [line.text for line in cutscenes.unread][0] == "movie3"
    view = cutscenes_view(cutscenes)
    assert view.result == 1
    assert [
        (c.movie, c.campaign, c.after_debriefing, c.mission) for c in view.cutscenes
    ] == [
        ("movie1", 1, 0, 10),
        ("movie2", 2, 1, 20),
    ]
    assert view.cutscenes[1].thumbnail == "thumb2"
    assert view.cutscenes[1].description == "Second scene."


def test_bad_numbers_line_blanks_the_entry_and_stops():
    data = crlf(
        "3",
        "m1",
        "1 0 1",
        "t1",
        "d1",
        "m2",
        "1 0",
        "t2",
        "d2",
        "m3",
        "1 1 3",
        "t3",
        "d3",
    )
    cutscenes = read_cutscenes(data)
    assert cutscenes.end == "bad_numbers"
    assert [e.status for e in cutscenes.entries] == ["read", "bad_numbers"]
    assert cutscenes.entries[1].thumbnail is None
    assert [line.text for line in cutscenes.unread][0] == "t2"
    assert [c.movie for c in cutscenes_view(cutscenes).cutscenes] == ["m1"]


def test_cutscene_list_ending_early():
    cut = read_cutscenes(crlf("3", "m1", "1 0 1", "t1", "d1", "m2", "2 0 2"))
    assert cut.end == "eof" and [e.status for e in cut.entries] == ["read", "cut"]
    assert len(cutscenes_view(cut).cutscenes) == 1
    clean = read_cutscenes(crlf("3", "m1", "1 0 1", "t1", "d1"))
    assert clean.end == "eof" and len(clean.entries) == 1
    assert read_cutscenes(b"").count_line is None


def test_cutscene_cuts():
    long_movie, long_thumb, long_text = "m" * 140, "t" * 40, "d" * 130
    data = crlf("1", long_movie, "1 0 1", long_thumb, long_text)
    (scene,) = cutscenes_view(read_cutscenes(data)).cutscenes
    assert scene.movie == "m" * 127 and scene.thumbnail == "t" * 31
    assert scene.description == "d" * 127


def record(campaign: str, tag: str) -> list[str]:
    return (
        [campaign, f"main{tag}"]
        + [f"mp{tag}{i}" for i in range(15)]
        + [f"sp{tag}{i}" for i in range(15)]
    )


def test_award_records_skip_comments_between_values():
    lines = ["2", "// header", *record("1", "a")[:5], "// mid", *record("1", "a")[5:]]
    lines += ["// next", *record("7", "b"), *record("9", "c")]
    awards = read_awards(crlf(*lines, end_mark=True))
    assert awards.count == 2 and awards.end == "count" and awards.end_mark
    first, second = awards.records
    assert [c.text for c in first.comments] == ["// header", "// mid"]
    assert (first.campaign, first.main.text) == (1, "maina")
    assert (
        first.multiplayer[3].text == "mpa3" and first.singleplayer[14].text == "spa14"
    )
    assert first.multiplayer[0].line == 5
    assert second.campaign == 7 and second.complete
    assert awards.unread[0].text == "9"
    view = awards_view(awards)
    assert view.result == 1 and [a.campaign for a in view.awards] == [1, 7]
    assert view.awards[0].multiplayer == [f"mpa{i}" for i in range(15)] + [""]
    assert (
        view.awards[1].singleplayer[15] == "" and len(view.awards[1].singleplayer) == 16
    )


def test_award_record_cut_off_is_not_counted():
    awards = read_awards(crlf("2", *record("1", "a"), *record("2", "b")[:10]))
    assert awards.end == "eof" and [r.complete for r in awards.records] == [True, False]
    assert (
        len(awards.records[1].multiplayer) == 8 and awards.records[1].singleplayer == []
    )
    assert [a.campaign for a in awards_view(awards).awards] == [1]
    exact = read_awards(crlf("3", *record("1", "a")))
    assert exact.end == "eof" and len(exact.records) == 1


def test_award_names_cut_to_31_characters():
    lines = record("1", "a")
    lines[1] = "M" * 40
    lines[2] = "P" * 32
    (award,) = awards_view(read_awards(crlf("1", *lines))).awards
    assert award.main == "M" * 31 and award.multiplayer[0] == "P" * 31


def test_comment_lines_count_only_where_the_notes_skip_them():
    cutscenes = read_cutscenes(crlf("// not skipped", "1", "m", "1 0 1", "t", "d"))
    assert cutscenes.count == 0 and cutscenes.entries == []
    assert cutscenes.count_line.text == "// not skipped"
    awards = read_awards(crlf("//", "1"))
    assert awards.count == 0 and awards.records == []


def test_counted_lists_fail_only_on_an_empty_file():
    assert cutscenes_view(read_cutscenes(b"")).result == 0
    assert awards_view(read_awards(b"")).result == 0
    for data in (crlf("0"), crlf("3", "m", "1 0"), crlf("2", "m", "1 x 1", "t", "d")):
        assert cutscenes_view(read_cutscenes(data)).result == 1
    for data in (crlf("0"), crlf("2", "1", "main", "n1"), crlf("")):
        assert awards_view(read_awards(data)).result == 1
