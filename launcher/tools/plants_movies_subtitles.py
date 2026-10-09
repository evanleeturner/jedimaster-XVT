"""The planted faults of the subtitle reader and the captions' timing.

Purpose:
    Break one rule of ``jedimaster/movies/subtitles.py`` per plant: the
    blanks, the number (sign, digits, width, end mark), the three lines
    (limit, CR LF, NUL, dot, Latin-1), the stops and the warning, and when
    each caption shows.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``movies-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_movies_subtitles import SUBTITLE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

SB = "jedimaster/movies/subtitles.py"
T_S = "tests/test_movies_subtitles.py::"
CASES = T_S + "test_made_up_files_read_as_the_sheets_say"


def _plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    return Plant(f"movies-{pid}", path, old, new, expect)


SUBTITLE_PLANTS: list[Plant] = [
    _plant(
        "subtitles-cr-not-blank",
        SB,
        'BLANKS = b" \\t\\n\\v\\f\\r"',
        'BLANKS = b" \\t\\n\\v\\f"',
        CASES,
        T_S + "test_blanks_after_the_number_skip_empty_lines_too",
    ),
    _plant(
        "subtitles-leading-blanks",
        SB,
        "        pos = _skip_blanks(data, pos)\n        if pos >= len(data):",
        "        if pos >= len(data):",
        CASES,
    ),
    _plant(
        "subtitles-blanks-after-number",
        SB,
        "        pos = _skip_blanks(data, after)",
        "        pos = after",
        CASES,
        T_S + "test_text_after_the_number_on_its_line_is_the_first_line",
    ),
    _plant(
        "subtitles-minus-ignored",
        SB,
        '        negative = data[pos] == ord("-")',
        "        negative = False",
        CASES,
        T_S + "test_the_number_is_an_unsigned_32_bit_value",
    ),
    _plant(
        "subtitles-sign-skipped-not",
        SB,
        '    if pos < len(data) and data[pos] in b"+-":',
        '    if pos < len(data) and data[pos] in b"-":',
        CASES,
    ),
    _plant(
        "subtitles-number-16-bit",
        SB,
        "    return (-value if negative else value) & MASK_32, pos",
        "    return (-value if negative else value) & 0xFFFF, pos",
        CASES,
    ),
    _plant(
        "subtitles-number-signed",
        SB,
        "    return (-value if negative else value) & MASK_32, pos",
        "    return (-value if negative else value), pos",
        CASES,
        T_S + "test_the_number_is_an_unsigned_32_bit_value",
    ),
    _plant(
        "subtitles-digits-needed",
        SB,
        "    if pos == first:\n        return None, start",
        "    if pos == first and not negative:\n        return None, start",
        T_S + "test_a_sign_without_digits_is_an_unreadable_number",
    ),
    _plant(
        "subtitles-line-limit",
        SB,
        "LINE_LIMIT = 255",
        "LINE_LIMIT = 256",
        CASES,
        T_S + "test_a_line_of_255_characters_leaves_an_empty_line_behind",
        T_S + "test_a_line_of_256_characters_goes_on_as_the_next_line",
    ),
    _plant(
        "subtitles-crlf",
        SB,
        '        if raw.endswith(b"\\r"):',
        "        if False:",
        CASES,
        T_S + "test_crlf_ends_a_line_and_a_lone_cr_is_kept",
    ),
    _plant(
        "subtitles-lone-cr-dropped",
        SB,
        "    window = data[pos : pos + LINE_LIMIT]",
        '    window = data[pos : pos + LINE_LIMIT].replace(b"\\r", b"")',
        CASES,
    ),
    _plant(
        "subtitles-dot-only",
        SB,
        '    if raw.startswith(b"."):',
        '    if raw == b".":',
        CASES,
        T_S + "test_a_dot_line_is_empty_but_a_dot_inside_is_kept",
    ),
    _plant(
        "subtitles-nul-kept",
        SB,
        '    nul = raw.find(b"\\0")',
        "    nul = -1",
        CASES,
        T_S + "test_a_nul_byte_ends_the_lines_text_only",
    ),
    _plant(
        "subtitles-utf8",
        SB,
        '    return raw.decode("latin-1"), pos',
        '    return raw.decode("utf-8", "replace"), pos',
        CASES,
        T_S + "test_every_byte_value_survives_as_one_character",
    ),
    _plant(
        "subtitles-two-lines",
        SB,
        "LINES = 3",
        "LINES = 2",
        CASES,
    ),
    _plant(
        "subtitles-end-mark",
        SB,
        "END_MARK = 65535",
        "END_MARK = 65536",
        T_S + "test_the_end_mark_stops_reading_and_is_not_counted",
        T_S + "test_numbers_around_the_end_mark_are_records",
    ),
    _plant(
        "subtitles-end-mark-counted",
        SB,
        "            reason = REASON_END_MARK\n            break",
        "            reason = REASON_END_MARK\n"
        "            records.append(Record(number, ('', '', '')))\n"
        "            break",
        T_S + "test_the_end_mark_stops_reading_and_is_not_counted",
    ),
    _plant(
        "subtitles-eof-reason",
        SB,
        "            reason = REASON_EOF\n            break",
        "            reason = REASON_BAD_NUMBER\n            break",
        CASES,
    ),
    _plant(
        "subtitles-no-warning",
        SB,
        "            logger.warning(\n",
        "            logger.debug(\n",
        CASES,
        T_S + "test_the_warning_names_the_file_and_the_game_message",
    ),
    _plant(
        "subtitles-warning-without-file",
        SB,
        '"%s: movie.subtitle_cue_invalid: no number at byte %d", label, pos',
        '"movie.subtitle_cue_invalid: no number at byte %d", pos',
        T_S + "test_the_warning_names_the_file_and_the_game_message",
    ),
    _plant(
        "subtitles-warning-at-eof",
        SB,
        "            reason = REASON_EOF\n            break",
        "            reason = REASON_EOF\n            logger.warning('%s: ended', label)\n            break",
        T_S + "test_a_clean_end_logs_no_warning",
    ),
    _plant(
        "subtitles-no-debug",
        SB,
        '            "%s: record %d number=%d lines=%s", label, len(records), number, lines',
        '            "%s: record %d lines=%s", label, len(records), lines',
        T_S + "test_every_value_read_is_logged_at_debug",
    ),
    _plant(
        "subtitles-read-while-before",
        SB,
        "        while not ended and last <= frame:",
        "        while not ended and last <= frame + 1:",
        T_S + "test_a_record_numbered_one_past_the_frame_still_shows_at_that_frame",
    ),
    _plant(
        "subtitles-jump-past",
        SB,
        "        frame = frames + 1 if ended else last\n",
        "        frame = frames + 1 if ended else last + 1\n",
        T_S + "test_a_record_shows_from_the_previous_number_to_its_own_minus_1",
    ),
    _plant(
        "subtitles-end-frame",
        SB,
        "_close(captions, records, before, began, frame - 1, frame_us)",
        "_close(captions, records, before, began, frame, frame_us)",
        T_S + "test_a_record_shows_from_the_previous_number_to_its_own_minus_1",
    ),
    _plant(
        "subtitles-start-time",
        SB,
        "            (first - 1) * frame_us,\n            last * frame_us,\n",
        "            first * frame_us,\n            last * frame_us,\n",
        T_S + "test_times_start_at_the_frame_start_and_end_at_the_frame_end",
    ),
    _plant(
        "subtitles-end-time",
        SB,
        "            (first - 1) * frame_us,\n            last * frame_us,\n",
        "            (first - 1) * frame_us,\n            (last - 1) * frame_us,\n",
        T_S + "test_times_start_at_the_frame_start_and_end_at_the_frame_end",
    ),
    _plant(
        "subtitles-empty-captions-kept",
        SB,
        "    if index is None or not any(records[index].lines):",
        "    if index is None:",
        T_S + "test_an_all_empty_record_clears_the_text_and_is_not_a_caption",
    ),
    _plant(
        "subtitles-lines-not-cleared-at-end",
        SB,
        "                ended, current = True, None",
        "                ended = True",
        T_S + "test_after_the_last_record_the_lines_are_cleared",
    ),
    _plant(
        "subtitles-final-close",
        SB,
        "    _close(captions, records, current, began, frames, frame_us)",
        "    _close(captions, records, current, began, frames - 1, frame_us)",
        T_S + "test_numbers_past_the_last_frame_keep_their_text_to_the_end",
    ),
    _plant(
        "subtitles-no-frames",
        SB,
        "    while frame <= frames:",
        "    frames = max(frames, 1)\n    while frame <= frames:",
        T_S + "test_no_frames_or_no_records_give_no_captions",
    ),
    _plant(
        "subtitles-ascii",
        SB,
        '    return raw.decode("latin-1"), pos',
        '    return raw.decode("ascii"), pos',
        T_S + "test_any_bytes_read_without_raising",
    ),
    _plant(
        "subtitles-vt-not-blank",
        SB,
        'BLANKS = b" \\t\\n\\v\\f\\r"',
        'BLANKS = b" \\t\\n\\r"',
        T_S + "test_vertical_tab_and_form_feed_are_blanks_before_and_after_the_number",
    ),
    _plant(
        "subtitles-ff-dropped-in-line",
        SB,
        "    window = data[pos : pos + LINE_LIMIT]",
        '    window = data[pos : pos + LINE_LIMIT].replace(b"\\x0c", b"")',
        T_S + "test_a_form_feed_inside_a_line_is_kept",
    ),
    _plant(
        "subtitles-number-no-saturation",
        SB,
        "    value = min(int(data[first:pos]), MAX_64)",
        "    value = int(data[first:pos])",
        T_S + "test_a_number_past_32_bits_wraps_and_one_past_64_bits_saturates",
    ),
    _plant(
        "subtitles-number-saturates-at-32",
        SB,
        "    value = min(int(data[first:pos]), MAX_64)",
        "    value = min(int(data[first:pos]), MASK_32)",
        T_S + "test_a_number_past_32_bits_wraps_and_one_past_64_bits_saturates",
    ),
    _plant(
        "subtitles-digits-hex",
        SB,
        'DIGITS = b"0123456789"',
        'DIGITS = b"0123456789xX"',
        T_S + "test_digits_stop_the_number_and_the_rest_is_text",
    ),
    _plant(
        "subtitles-double-sign",
        SB,
        '    if pos < len(data) and data[pos] in b"+-":\n        negative = data[pos] == ord("-")\n        pos += 1',
        '    while pos < len(data) and data[pos] in b"+-":\n        negative = data[pos] == ord("-")\n        pos += 1',
        T_S + "test_two_signs_are_an_unreadable_number",
    ),
    _plant(
        "subtitles-leading-zero-refused",
        SB,
        "    value = min(int(data[first:pos]), MAX_64)",
        '    if data[first] == ord("0"):\n        return None, start\n    value = min(int(data[first:pos]), MAX_64)',
        CASES + "[leadzero]",
    ),
    _plant(
        "subtitles-letter-after-number",
        SB,
        "    value = min(int(data[first:pos]), MAX_64)",
        "    if data[pos : pos + 1].isalpha():\n        return None, start\n    value = min(int(data[first:pos]), MAX_64)",
        CASES + "[digitsalpha]",
    ),
]
