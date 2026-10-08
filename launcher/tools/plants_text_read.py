"""The planted faults of the text readers: menus, errors, specs, joystick, credits.

Purpose:
    Break one rule of ``jedimaster/text/`` per plant: the menus' entries
    and ``No text.``; the error messages' escape and cut; the craft
    fields' sizes and early end; the joystick steps; the credits header,
    page buffers, colors and limits. The line reader's and strings.txt's
    plants are in ``plants_text_lines``.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``text-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_text_read import TEXT_READ_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

MN = "jedimaster/text/menus.py"
SP = "jedimaster/text/specs.py"
JS = "jedimaster/text/joystick.py"
CR = "jedimaster/text/credits.py"
T_MENU = "tests/test_text_menus.py::"
T_SPEC = "tests/test_text_specs.py::"
T_JOY = "tests/test_text_joystick.py::"
T_CRED = "tests/test_text_credits.py::"
T_TOUT = "tests/test_text_output.py::"
CODES = T_JOY + "test_code_values"
BUFFERS = T_CRED + "test_paragraph_gives_the_page_buffer"

MENU_PLANTS: list[Plant] = [
    Plant(
        "text-front-comments-kept",
        MN,
        "    lines = read_lines(data, FRONT_BUFFER, skip_comments=True)",
        "    lines = read_lines(data, FRONT_BUFFER, skip_comments=False)",
        (T_MENU + "test_front_entries_skip_comments_keep_empty_lines",),
    ),
    Plant(
        "text-front-empty-lines-dropped",
        MN,
        "    return FrontText([line.text for line in lines], [line.number for line in lines])",
        "    lines = [line for line in lines if line.text]\n"
        "    return FrontText([line.text for line in lines], [line.number for line in lines])",
        (T_MENU + "test_front_entries_skip_comments_keep_empty_lines",),
    ),
    Plant(
        "text-front-buffer-512",
        MN,
        "FRONT_BUFFER = 1024",
        "FRONT_BUFFER = 512",
        (T_MENU + "test_front_long_line_is_two_entries",),
    ),
    Plant(
        "text-no-text-at-the-count",
        MN,
        "    if 0 <= number < len(front.entries):",
        "    if 0 <= number <= len(front.entries) - 2:",
        (T_MENU + "test_no_text_at_and_past_the_count",),
    ),
    Plant(
        "text-no-text-negative-wraps",
        MN,
        "    if 0 <= number < len(front.entries):",
        "    if number < len(front.entries) and front.entries:",
        (T_MENU + "test_no_text_at_and_past_the_count",),
    ),
    Plant(
        "text-errors-comments-skipped",
        MN,
        "skip_comments=False, keep_line_feed=True)",
        "skip_comments=True, keep_line_feed=True)",
        (T_MENU + "test_error_comments_are_messages_and_line_feeds_kept",),
    ),
    Plant(
        "text-errors-line-feed-dropped",
        MN,
        "skip_comments=False, keep_line_feed=True)",
        "skip_comments=False, keep_line_feed=False)",
        (T_MENU + "test_error_comments_are_messages_and_line_feeds_kept",),
    ),
    Plant(
        "text-errors-any-escape",
        MN,
        "i + 1 < len(line) and line[i + 1] == LETTER_N:",
        "i + 1 < len(line):",
        (T_MENU + "test_error_escape_only_backslash_n",),
    ),
    Plant(
        "text-errors-whole-line",
        MN,
        "    while i < min(len(line), ERROR_BYTES):",
        "    while i < len(line):",
        (T_MENU + "test_error_line_cut_at_255_bytes",),
    ),
    Plant(
        "text-errors-window-lookahead",
        MN,
        "i + 1 < len(line) and line[i + 1] == LETTER_N:",
        "i + 1 < min(len(line), ERROR_BYTES) and line[i + 1] == LETTER_N:",
        (T_MENU + "test_error_line_cut_at_255_bytes",),
    ),
    Plant(
        "text-errors-past-end-wraps",
        MN,
        "    if 0 <= number < len(errors.messages):",
        "    if number < len(errors.messages):",
        (T_MENU + "test_message_past_the_end_does_not_exist",),
    ),
]

SPECS_PLANTS: list[Plant] = [
    Plant(
        "text-specs-field-order",
        SP,
        '    ("users", 64),\n    ("description", 256),',
        '    ("description", 256),\n    ("users", 64),',
        (T_SPEC + "test_entries_fill_five_fields_in_order",),
    ),
    Plant(
        "text-specs-no-cut",
        SP,
        "            value = line.text[:size]",
        "            value = line.text",
        (T_SPEC + "test_a_64_byte_value_fills_its_field",),
    ),
    Plant(
        "text-specs-63-warns",
        SP,
        "            if len(line.text) >= size:",
        "            if len(line.text) >= size - 1:",
        (T_SPEC + "test_a_64_byte_value_fills_its_field",),
    ),
    Plant(
        "text-specs-64-silent",
        SP,
        "            if len(line.text) >= size:",
        "            if len(line.text) > size:",
        (T_SPEC + "test_a_64_byte_value_fills_its_field",),
    ),
    Plant(
        "text-specs-buffer-512",
        SP,
        "BUFFER = 256\n",
        "BUFFER = 512\n",
        (T_SPEC + "test_a_line_over_255_bytes_is_read_in_pieces",),
    ),
    Plant(
        "text-specs-complete-counts-cut",
        SP,
        "            if taken >= len(lines):\n                break",
        "            if taken >= len(lines):\n"
        "                specs.complete += taken % len(FIELDS) > 0\n"
        "                break",
        (T_SPEC + "test_an_early_end_keeps_what_was_read",),
    ),
    Plant(
        "text-specs-cut-not-reported",
        SP,
        "        if taken % len(FIELDS):",
        "        if False:",
        (T_SPEC + "test_an_early_end_keeps_what_was_read",),
    ),
    Plant(
        "text-specs-boundary-cut",
        SP,
        "        if taken % len(FIELDS):",
        "        if True:",
        (T_SPEC + "test_an_end_on_an_entry_boundary_cuts_nothing",),
    ),
]

JOYSTICK_PLANTS: list[Plant] = [
    Plant(
        "text-joystick-buffer-cleared",
        JS,
        "        buffer.load(piece)\n",
        "        buffer = LineBuffer(LINE_BUFFER)\n        buffer.load(piece)\n",
        (T_JOY + "test_a_line_with_no_space_reads_leftover_bytes",),
    ),
    Plant(
        "text-joystick-no-pass-over",
        JS,
        "    name_start = code_end + 1",
        "    name_start = code_end + (buffer.byte(code_end) == SPACE)",
        (T_JOY + "test_a_line_with_no_space_reads_leftover_bytes",),
    ),
    Plant(
        "text-joystick-description-at-end-mark",
        JS,
        "    description = buffer.string(name_end + 1)",
        "    description = (\n"
        '        buffer.string(name_end + 1) if buffer.byte(name_end) == SPACE else b""\n'
        "    )",
        (T_JOY + "test_a_line_with_no_space_reads_leftover_bytes",),
    ),
    Plant(
        "text-joystick-description-from-name-end",
        JS,
        "    description = buffer.string(name_end + 1)",
        "    description = buffer.string(name_end + 2)",
        (T_JOY + "test_a_line_with_one_space_has_an_empty_description",),
    ),
    Plant(
        "text-joystick-description-stops-at-space",
        JS,
        "    description = buffer.string(name_end + 1)",
        '    description = buffer.string(name_end + 1).split(b" ")[0]',
        (T_JOY + "test_code_name_description",),
    ),
    Plant(
        "text-joystick-comments-skipped",
        JS,
        "    while (piece := stream.read_piece()) is not None:\n        buffer.load",
        "    while (piece := stream.read_piece()) is not None:\n"
        '        if piece.startswith(b"//"):\n            continue\n        buffer.load',
        (T_JOY + "test_every_line_is_an_entry_comments_included",),
    ),
    Plant(
        "text-joystick-no-sign",
        JS,
        "    if i < len(text) and text[i] in SIGNS:",
        "    if False:",
        (CODES + "[plus]", CODES + "[minus-wraps]", CODES + "[vtab-minus]"),
    ),
    Plant(
        "text-joystick-no-blanks",
        JS,
        "    while i < len(text) and text[i] in BLANKS:",
        "    while False:",
        (CODES + "[tab]", CODES + "[vtab-minus]", T_JOY + "test_leading_number_rules"),
    ),
    Plant(
        "text-joystick-no-byte-wrap",
        JS,
        "    code = leading_number(bytes(buffer.bytes[:code_end])) % 256",
        "    code = min(max(leading_number(bytes(buffer.bytes[:code_end])), 0), 255)",
        (CODES + "[minus-wraps]", CODES + "[over-255]", CODES + "[huge]"),
    ),
    Plant(
        "text-joystick-no-digits-one",
        JS,
        "    return sign * int(text[start:i]) if i > start else 0",
        "    return sign * int(text[start:i]) if i > start else 1",
        (
            CODES + "[letters]",
            CODES + "[sign-only]",
            CODES + "[empty]",
            CODES + "[byte-26]",
        ),
    ),
    Plant(
        "text-joystick-sign-twice",
        JS,
        "        sign = SIGNS[text[i]]\n        i += 1",
        "        sign = SIGNS[text[i]]\n        i += 1\n"
        "        if i < len(text) and text[i] in SIGNS:\n"
        "            sign *= SIGNS[text[i]]\n            i += 1",
        (T_JOY + "test_leading_number_rules",),
    ),
    Plant(
        "text-joystick-buffer-256-pieces",
        JS,
        "    stream = LineStream(data, PIECE_BUFFER)",
        "    stream = LineStream(data, LINE_BUFFER)",
        (T_JOY + "test_a_long_line_is_two_entries",),
    ),
    Plant(
        "text-joystick-name-not-reported",
        JS,
        "    long_name = len(action.name) > NAME_ROOM",
        "    long_name = len(action.name) > 2 * NAME_ROOM",
        (T_JOY + "test_long_name_and_description_are_reported",),
    ),
    Plant(
        "text-joystick-room-counts-end-mark",
        JS,
        "    long_description = len(action.description) > DESCRIPTION_ROOM",
        "    long_description = len(action.description) >= DESCRIPTION_ROOM",
        (T_JOY + "test_long_name_and_description_are_reported",),
    ),
]

CREDITS_PLANTS: list[Plant] = [
    Plant(
        "text-credits-header-order",
        CR,
        '    "fade",\n    "duration",',
        '    "duration",\n    "fade",',
        (T_CRED + "test_header_fields_and_pages",),
    ),
    Plant(
        "text-credits-buffer-from-paragraph",
        CR,
        "    return min((paragraph - 1) % WORD, 1)",
        "    return min(paragraph, 1)",
        (BUFFERS + "[0-1]", BUFFERS + "[1-0]"),
    ),
    Plant(
        "text-credits-buffer-signed",
        CR,
        "    return min((paragraph - 1) % WORD, 1)",
        "    return max(min(paragraph - 1, 1), 0)",
        (BUFFERS + "[0-1]",),
    ),
    Plant(
        "text-credits-buffer-modulo-2",
        CR,
        "    return min((paragraph - 1) % WORD, 1)",
        "    return (paragraph - 1) % 2",
        (BUFFERS + "[5-1]", BUFFERS + "[4294967295-1]"),
    ),
    Plant(
        "text-credits-buffer-plain-difference",
        CR,
        "    return min((paragraph - 1) % WORD, 1)",
        "    return 0 if paragraph == 1 else 1 if paragraph in (0, 2) else 0",
        (BUFFERS + "[5-1]", BUFFERS + "[4294967295-1]"),
    ),
    Plant(
        "text-credits-buffer-2",
        CR,
        "    return min((paragraph - 1) % WORD, 1)",
        "    return 0 if paragraph == 2 else min((paragraph - 1) % WORD, 1)",
        (BUFFERS + "[2-1]",),
    ),
    Plant(
        "text-credits-minus-not-wrapped",
        CR,
        '    value = int(run[digits:stop]) * (-1 if run[:1] == b"-" else 1)',
        "    value = int(run[digits:stop])",
        (T_CRED + "test_header_numbers_as_percent_u",),
    ),
    Plant(
        "text-credits-no-sign",
        CR,
        "    digits = 1 if run[:1] and run[0] in SIGNS else 0",
        "    digits = 0",
        (T_CRED + "test_header_numbers_as_percent_u",),
    ),
    Plant(
        "text-credits-run-rest-dropped",
        CR,
        "    stream.position = start + stop\n",
        "    stream.position = end\n",
        (
            T_CRED + "test_header_numbers_as_percent_u",
            T_CRED + "test_a_non_number_stops_the_header_and_is_read_as_text",
        ),
    ),
    Plant(
        "text-credits-run-unlimited",
        CR,
        "while end < len(data) and end - start < RUN_LIMIT and",
        "while end < len(data) and",
        (T_CRED + "test_a_run_is_at_most_511_bytes",),
    ),
    Plant(
        "text-credits-no-wrap",
        CR,
        "    return value % WORD\n",
        "    return value\n",
        (T_CRED + "test_header_numbers_as_percent_u",),
    ),
    Plant(
        "text-credits-failed-run-consumed",
        CR,
        '        logger.debug("header run %r holds no number", run[:16])\n'
        "        return None",
        '        logger.debug("header run %r holds no number", run[:16])\n'
        "        stream.position = end\n        return None",
        (T_CRED + "test_a_non_number_stops_the_header_and_is_read_as_text",),
    ),
    Plant(
        "text-credits-no-trailing-skip",
        CR,
        "        numbers.append(value)\n    _skip_white(stream)",
        "        numbers.append(value)",
        (T_CRED + "test_white_space_after_the_tenth_number_is_skipped",),
    ),
    Plant(
        "text-credits-header-comments-skipped",
        CR,
        "        header = _header(self.stream)\n",
        '        while self.stream.data.startswith(b"//", self.stream.position):\n'
        "            self.stream.read_piece()\n"
        "        header = _header(self.stream)\n",
        (T_CRED + "test_comments_are_not_skipped_in_the_header",),
    ),
    Plant(
        "text-credits-unread-paragraph-1",
        CR,
        "        paragraph = header[2] if len(header) > 2 else 0",
        "        paragraph = header[2] if len(header) > 2 else 1",
        (T_CRED + "test_an_unread_header", T_TOUT + "test_render_credits_pages"),
    ),
    Plant(
        "text-credits-token-buffer-fresh",
        CR,
        "            token[count] = byte\n            count += 1",
        "            token[count] = byte\n            count += 1\n"
        "            token[count] = END_MARK",
        (T_CRED + "test_a_token_cut_by_the_count_runs_into_earlier_bytes",),
    ),
    Plant(
        "text-credits-token-uncounted",
        CR,
        "        while count < remaining:",
        "        while True:",
        (T_CRED + "test_a_token_cut_by_the_count_runs_into_earlier_bytes",),
    ),
    Plant(
        "text-credits-token-length-fixed",
        CR,
        "            remaining -= 1\n",
        "",
        (T_CRED + "test_a_token_cut_by_the_count_runs_into_earlier_bytes",),
    ),
    Plant(
        "text-credits-color-no-wrap",
        CR,
        "        values.append(leading_number(c_string(token)) % 256)",
        "        values.append(min(max(leading_number(c_string(token)), 0), 255))",
        (T_CRED + "test_color_tokens",),
    ),
    Plant(
        "text-credits-leftover-text-dropped",
        CR,
        "        return CreditsLine(self.line.string(start), self.color)",
        "        return CreditsLine(text[start:], self.color)",
        (T_CRED + "test_text_past_the_end_mark_runs_into_leftover_bytes",),
    ),
    Plant(
        "text-credits-color-line-keeps-old-color",
        CR,
        "        rgb, start = parse_color(self.line)\n"
        "        self.color = color_565(*rgb)",
        "        rgb, start = parse_color(self.line)\n"
        "        line = CreditsLine(self.line.string(start), self.color)\n"
        "        self.color = color_565(*rgb)\n"
        "        return line",
        (T_CRED + "test_color_lines_set_the_color_others_keep_it",),
    ),
    Plant(
        "text-credits-color-reset-each-page",
        CR,
        "        header = _header(self.stream)\n",
        "        header = _header(self.stream)\n        self.color = WHITE\n",
        (T_CRED + "test_the_color_carries_across_pages",),
    ),
    Plant(
        "text-credits-colors-cleared",
        CR,
        '        lines[:] = [CreditsLine(b"", line.color) for line in lines]',
        '        lines[:] = [CreditsLine(b"", 0) for line in lines]',
        (T_CRED + "test_an_unused_line_keeps_its_color",),
    ),
    Plant(
        "text-credits-one-buffer",
        CR,
        "        lines = self.buffers[buffer]\n",
        "        lines = self.buffers[0]\n",
        (T_CRED + "test_an_unused_line_keeps_its_color",),
    ),
    Plant(
        "text-credits-texts-kept",
        CR,
        '        lines[:] = [CreditsLine(b"", line.color) for line in lines]',
        "        lines[:] = list(lines)",
        (T_CRED + "test_an_unused_line_keeps_its_color",),
    ),
    Plant(
        "text-credits-33-lines",
        CR,
        "            if used < PAGE_LINES:",
        "            if used <= PAGE_LINES:",
        (T_CRED + "test_more_than_32_lines",),
    ),
    Plant(
        "text-credits-skipped-lines-colored",
        CR,
        "            if used < PAGE_LINES:\n"
        "                lines[used] = self.page_line(text)",
        "            line = self.page_line(text)\n"
        "            if used < PAGE_LINES:\n"
        "                lines[used] = line",
        (T_CRED + "test_more_than_32_lines",),
    ),
    Plant(
        "text-credits-star-is-text",
        CR,
        "            if text.startswith(PAGE_END):",
        "            if text == PAGE_END:",
        (T_CRED + "test_comments_skipped_in_lines_and_star_ends_the_page",),
    ),
    Plant(
        "text-credits-comments-are-lines",
        CR,
        "            if not is_comment(piece):",
        "            if True:",
        (T_CRED + "test_comments_skipped_in_lines_and_star_ends_the_page",),
    ),
]

TEXT_READ_PLANTS: list[Plant] = [
    *MENU_PLANTS,
    *SPECS_PLANTS,
    *JOYSTICK_PLANTS,
    *CREDITS_PLANTS,
]
