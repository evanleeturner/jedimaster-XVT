"""Subtitle files: the reading rules, the warning, and when each caption shows.

Purpose:
    Prove ``read_records`` reads the 22 made-up files as the game's answer
    sheets say (number, lines, why it stopped), each rule of the format
    alone (blanks, signs, wrapping, the 255-character line, CR LF, NUL, the
    dot, the end mark), logs a WARNING exactly where the game does, and that
    ``shown_captions`` gives the frames and times of the game's reading of
    the numbers.

Flow:
    ``SUBTITLE_CASES`` and small byte strings in, records out; records and
    frame counts in, captions out.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_subtitles.py``
"""

from __future__ import annotations

import logging

import pytest
from smkdata import SUBTITLE_CASES

from jedimaster.movies import read_records
from jedimaster.movies import render_subtitles
from jedimaster.movies import shown_captions
from jedimaster.movies.subtitles import Record

logger = logging.getLogger(__name__)

E = ("", "", "")


def records_of(data: bytes) -> list[tuple[int, tuple[str, str, str]]]:
    return [(r.number, r.lines) for r in read_records(data).records]


@pytest.mark.parametrize("name", sorted(SUBTITLE_CASES))
def test_made_up_files_read_as_the_sheets_say(name, caplog):
    data, records, reason = SUBTITLE_CASES[name]
    with caplog.at_level(logging.WARNING):
        parsed = read_records(data, name)
    assert [(r.number, r.lines) for r in parsed.records] == records
    assert parsed.reason == reason
    warned = [r for r in caplog.records if r.levelno == logging.WARNING]
    assert len(warned) == (1 if reason == "bad_number" else 0)


def test_the_warning_names_the_file_and_the_game_message(caplog):
    with caplog.at_level(logging.WARNING):
        read_records(b"100\nA\nB\nC\nxyz", "movies/x.txt")
    (record,) = [r for r in caplog.records if r.levelno == logging.WARNING]
    assert "movies/x.txt" in record.getMessage()
    assert "movie.subtitle_cue_invalid" in record.getMessage()
    assert "byte 10" in record.getMessage()


def test_a_clean_end_logs_no_warning(caplog):
    with caplog.at_level(logging.WARNING):
        read_records(b"100\nA\nB\nC\n")
    assert [r for r in caplog.records if r.levelno >= logging.WARNING] == []


def test_every_value_read_is_logged_at_debug(caplog):
    with caplog.at_level(logging.DEBUG):
        read_records(b"100\nA\nB\nC\n", "f")
    assert any("number=100" in r.getMessage() for r in caplog.records)


def test_sheet_of_a_made_up_file_renders_as_the_sheets_do():
    data, _, _ = SUBTITLE_CASES["latin1"]
    text = render_subtitles("latin1", "movies/latin1.txt", read_records(data))
    assert text == (
        'kind subtitles\nname "movies/latin1.txt"\nfile "movies/latin1.txt"\n'
        'cue frame=100 "caf\\xe9 \\xa9 \\x7f" "B" "C"\nend cues=1\n'
    )
    assert render_subtitles("x", None, None) == (
        'kind subtitles\nname "movies/x.txt"\nmissing\n'
    )


@pytest.mark.parametrize(
    ("data", "number"),
    [
        (b"+7\nA\n", 7),
        (b"-5\nA\n", 4294967291),
        (b"-1\nA\n", 4294967295),
        (b"4294967296\nA\n", 0),
        (b"007\nA\n", 7),
        (b" \t\r\n\n  42\nA\n", 42),
    ],
)
def test_the_number_is_an_unsigned_32_bit_value(data, number):
    assert records_of(data)[0][0] == number


def test_vertical_tab_and_form_feed_are_blanks_before_and_after_the_number():
    assert records_of(b"\x0b\x0c5\x0b\x0c\nA\n") == [(5, ("A", "", ""))]


def test_a_form_feed_inside_a_line_is_kept():
    assert records_of(b"5\nA\x0cB\n") == [(5, ("A\x0cB", "", ""))]


def test_a_number_past_32_bits_wraps_and_one_past_64_bits_saturates():
    assert records_of(b"5000000000\nA\n")[0][0] == 705032704
    assert records_of(b"99999999999999999999\nA\n")[0][0] == 4294967295
    assert records_of(b"18446744073709551616\nA\n")[0][0] == 4294967295
    assert records_of(b"-99999999999999999999\nA\n")[0][0] == 1


def test_digits_stop_the_number_and_the_rest_is_text():
    assert records_of(b"100abc\nA\n") == [(100, ("abc", "A", ""))]
    assert records_of(b"0x10\nA\n") == [(0, ("x10", "A", ""))]


def test_two_signs_are_an_unreadable_number():
    assert read_records(b"+-5\nA\n").reason == "bad_number"


def test_a_sign_without_digits_is_an_unreadable_number():
    assert read_records(b"+x\nA\n").reason == "bad_number"
    assert read_records(b"-").reason == "bad_number"


def test_text_after_the_number_on_its_line_is_the_first_line():
    assert records_of(b"5 hello\nA\nB\nC\n") == [(5, ("hello", "A", "B"))]


def test_blanks_after_the_number_skip_empty_lines_too():
    assert records_of(b"5\r\n\r\n\n   \nA\nB\nC\n") == [(5, ("A", "B", "C"))]


def test_a_dot_line_is_empty_but_a_dot_inside_is_kept():
    assert records_of(b"5\n.x\nx.\n.\n") == [(5, ("", "x.", ""))]


def test_a_line_of_255_characters_leaves_an_empty_line_behind():
    line = b"q" * 255
    assert records_of(b"5\n" + line + b"\nA\nB\n") == [(5, ("q" * 255, "", "A"))]


def test_a_line_of_256_characters_goes_on_as_the_next_line():
    assert records_of(b"5\n" + b"q" * 256 + b"\nA\nB\n") == [(5, ("q" * 255, "q", "A"))]


def test_crlf_ends_a_line_and_a_lone_cr_is_kept():
    assert records_of(b"5\r\nA\rB\r\nC\r\n.\r\n") == [(5, ("A\rB", "C", ""))]


def test_a_nul_byte_ends_the_lines_text_only():
    assert records_of(b"5\nab\x00cd\n\x00zz\nC\n") == [(5, ("ab", "", "C"))]


def test_the_end_mark_stops_reading_and_is_not_counted():
    parsed = read_records(b"5\nA\nB\nC\n65535\nX\nY\nZ\n9\nQ\n")
    assert [r.number for r in parsed.records] == [5]
    assert parsed.reason == "end_mark"


def test_numbers_around_the_end_mark_are_records():
    parsed = read_records(b"65534\n.\n.\n.\n65536\n.\n.\n.\n")
    assert [r.number for r in parsed.records] == [65534, 65536]


def test_missing_lines_at_the_end_read_as_empty():
    assert records_of(b"5\nA") == [(5, ("A", "", ""))]
    assert records_of(b"5") == [(5, E)]


def test_every_byte_value_survives_as_one_character():
    body = bytes(b for b in range(1, 256) if b not in (10, 13, 46))
    (record,) = read_records(b"5\n" + body[:200] + b"\n").records
    assert record.lines[0].encode("latin-1") == body[:200]


def test_any_bytes_read_without_raising():
    junk = bytes((i * 37 + 11) % 256 for i in range(3000))
    for start in (b"", b"5\n", b"5\n.\n", b"7\n" + b"\xff" * 300 + b"\n"):
        parsed = read_records(start + junk)
        assert parsed.reason in {"eof", "bad_number", "end_mark"}
    assert read_records(b"5\n" + junk).records[0].number == 5


def recs(*numbers_and_text):
    return [Record(n, (t, "", "")) for n, t in numbers_and_text]


def test_the_first_record_shows_from_frame_1_to_its_number_minus_1():
    (caption,) = shown_captions(recs((100, "A")), 150, 1000)
    assert (caption.start_frame, caption.end_frame) == (1, 99)
    assert (caption.start_us, caption.end_us) == (0, 99000)
    assert caption.lines == ("A", "", "")


def test_a_record_shows_from_the_previous_number_to_its_own_minus_1():
    captions = shown_captions(recs((10, "A"), (30, "B"), (60, "C")), 100, 1000)
    assert [(c.start_frame, c.end_frame, c.lines[0]) for c in captions] == [
        (1, 9, "A"),
        (10, 29, "B"),
        (30, 59, "C"),
    ]
    assert [c.record for c in captions] == [0, 1, 2]


def test_after_the_last_record_the_lines_are_cleared():
    captions = shown_captions(recs((10, "A")), 100, 10)
    assert [(c.start_frame, c.end_frame) for c in captions] == [(1, 9)]


def test_numbers_past_the_last_frame_keep_their_text_to_the_end():
    (caption,) = shown_captions(recs((1000, "A"), (2000, "B")), 150, 66730)
    assert (caption.start_frame, caption.end_frame) == (1, 150)
    assert (caption.start_us, caption.end_us) == (0, 150 * 66730)


def test_a_record_numbered_one_past_the_frame_still_shows_at_that_frame():
    captions = shown_captions(recs((2, "A"), (9, "B")), 10, 1)
    assert [(c.lines[0], c.start_frame, c.end_frame) for c in captions] == [
        ("A", 1, 1),
        ("B", 2, 8),
    ]


def test_a_record_not_past_the_frame_is_replaced_and_never_shows():
    captions = shown_captions(recs((1, "skipped"), (0, "also"), (5, "B")), 20, 1)
    assert [(c.lines[0], c.start_frame, c.end_frame) for c in captions] == [("B", 1, 4)]


def test_unsorted_numbers_are_read_in_file_order():
    captions = shown_captions(recs((30, "A"), (10, "B"), (50, "C")), 60, 1)
    assert [(c.lines[0], c.start_frame, c.end_frame) for c in captions] == [
        ("A", 1, 29),
        ("C", 30, 49),
    ]


def test_an_all_empty_record_clears_the_text_and_is_not_a_caption():
    records = [Record(5, ("A", "", "")), Record(9, E), Record(20, ("B", "", ""))]
    captions = shown_captions(records, 30, 1)
    assert [(c.lines[0], c.start_frame, c.end_frame) for c in captions] == [
        ("A", 1, 4),
        ("B", 9, 19),
    ]


def test_times_start_at_the_frame_start_and_end_at_the_frame_end():
    (caption,) = shown_captions(recs((11, "A")), 20, 66730)
    assert caption.start_us == 0
    assert caption.end_us == 10 * 66730
    caption = shown_captions(recs((3, "A"), (8, "B")), 20, 100)[1]
    assert (caption.start_frame, caption.start_us, caption.end_us) == (3, 200, 700)


def test_no_frames_or_no_records_give_no_captions():
    assert shown_captions(recs((5, "A")), 0, 100) == []
    assert shown_captions([], 10, 100) == []


def test_huge_numbers_do_not_walk_every_frame():
    (caption,) = shown_captions(recs((4294967295, "A")), 10**9, 1)
    assert caption.end_frame == 10**9
