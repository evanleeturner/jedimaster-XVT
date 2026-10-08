"""The planted faults of the line reader and strings.txt.

Purpose:
    Break one rule of the shared line reader per plant (line ends, buffer
    pieces, comments, the line buffer, the sheets' quoting) and one of the
    strings.txt reader (the tables' order, kinds, escapes, stops and gender
    tables).

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``text-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_text_lines import TEXT_LINES_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

LN = "jedimaster/text/lines.py"
SH = "jedimaster/text/sheet.py"
TB = "jedimaster/text/tables.py"
ST = "jedimaster/text/strings.py"
T_LINES = "tests/test_text_lines.py::"
T_STR = "tests/test_text_strings.py::"
SIZES = T_LINES + "test_a_line_at_and_over_each_buffer_size"
BAD_MODEL = T_STR + "test_a_bad_model_first_byte_stops_the_game"
BACKSLASH_END = T_STR + "test_a_backslash_at_a_lines_end_is_refused"
EARLY = T_STR + "test_a_file_ending_early_is_out_of_sync"

LINE_PLANTS: list[Plant] = [
    Plant(
        "text-crlf-kept",
        LN,
        'self.data = data.replace(b"\\r\\n", b"\\n")',
        "self.data = data",
        (T_LINES + "test_crlf_reads_as_lf_and_a_lone_cr_stays",),
    ),
    Plant(
        "text-lone-cr-ends-line",
        LN,
        'self.data = data.replace(b"\\r\\n", b"\\n")',
        'self.data = data.replace(b"\\r\\n", b"\\n").replace(b"\\r", b"\\n")',
        (T_LINES + "test_crlf_reads_as_lf_and_a_lone_cr_stays",),
    ),
    Plant(
        "text-piece-full-buffer",
        LN,
        "        self.limit = buffer_size - 1",
        "        self.limit = buffer_size",
        (SIZES + "[128]", SIZES + "[256]", SIZES + "[512]", SIZES + "[1024]"),
    ),
    Plant(
        "text-final-line-dropped",
        LN,
        "stop = feed + 1 if feed >= 0 else min(start + self.limit, len(self.data))\n",
        "stop = feed + 1 if feed >= 0 else min(start + self.limit, len(self.data))\n"
        "        if feed < 0 and stop == len(self.data):\n"
        "            return None\n",
        (T_LINES + "test_final_line_without_line_feed_and_empty_file",),
    ),
    Plant(
        "text-line-feed-kept",
        LN,
        '    return piece[:-1] if piece.endswith(b"\\n") else piece',
        "    return piece",
        (T_LINES + "test_final_line_without_line_feed_and_empty_file",),
    ),
    Plant(
        "text-comment-anywhere",
        LN,
        "    return piece.startswith(COMMENT)",
        "    return COMMENT in piece",
        (T_LINES + "test_comments_only_at_a_line_start",),
    ),
    Plant(
        "text-comment-one-slash",
        LN,
        "    return piece.startswith(COMMENT)",
        '    return piece.startswith(b"/")',
        (T_LINES + "test_comments_only_at_a_line_start",),
    ),
    Plant(
        "text-comment-pieces-counted",
        LN,
        "        lines.append(Line(stream.pieces, text))",
        "        lines.append(Line(len(lines) + 1, text))",
        (T_LINES + "test_comments_only_at_a_line_start",),
    ),
    Plant(
        "text-pieces-not-counted",
        LN,
        "        self.pieces += 1\n",
        "",
        (T_LINES + "test_stream_pieces_keep_their_line_feed",),
    ),
    Plant(
        "text-comment-long-skipped-whole",
        LN,
        '            logger.debug("line %d: comment skipped", stream.pieces)\n'
        "            continue",
        '            logger.debug("line %d: comment skipped", stream.pieces)\n'
        '            stream.position = stream.data.find(b"\\n", stream.position)\n'
        "            stream.position = (\n"
        "                len(stream.data) if stream.position < 0 else stream.position + 1\n"
        "            )\n"
        "            continue",
        (T_LINES + "test_a_long_comment_continues_as_a_line",),
    ),
    Plant(
        "text-buffer-leftovers-cleared",
        LN,
        "        self.bytes[: len(piece)] = piece\n",
        "        self.bytes[:] = bytes(len(self.bytes))\n"
        "        self.bytes[: len(piece)] = piece\n",
        (T_LINES + "test_line_buffer_keeps_leftover_bytes",),
    ),
    Plant(
        "text-buffer-no-end-mark",
        LN,
        "        self.bytes[len(piece)] = END_MARK\n",
        "",
        (T_LINES + "test_line_buffer_keeps_leftover_bytes",),
    ),
    Plant(
        "text-buffer-overflow-silent",
        LN,
        "        if len(piece) + 1 > len(self.bytes):",
        "        if len(piece) > len(self.bytes):",
        (T_LINES + "test_line_buffer_keeps_leftover_bytes",),
    ),
    Plant(
        "text-c-string-runs-on",
        LN,
        "    stop = buffer.find(END_MARK, start)",
        "    stop = -1",
        (T_LINES + "test_c_string_stops_at_the_end_mark",),
    ),
    Plant(
        "text-load-label",
        LN,
        "    return data, os.fspath(source)",
        '    return data, "<file>"',
        (T_LINES + "test_load_reads_paths_and_bytes",),
    ),
    Plant(
        "text-quote-no-cr",
        SH,
        '0x0D: "\\\\r", ',
        "",
        (T_LINES + "test_quote_follows_the_sheets_rules",),
    ),
    Plant(
        "text-quote-past-nul",
        SH,
        "    end = data.find(0)\n",
        "    end = -1\n",
        (T_LINES + "test_quote_follows_the_sheets_rules",),
    ),
    Plant(
        "text-quote-hex-upper",
        SH,
        'out.append(f"\\\\x{byte:02x}")',
        'out.append(f"\\\\x{byte:02X}")',
        (T_LINES + "test_quote_follows_the_sheets_rules",),
    ),
]

STRINGS_PLANTS: list[Plant] = [
    Plant(
        "text-pattern-shifted",
        TB,
        "    return [GOAL_PATTERN[r % len(GOAL_PATTERN)] for r in range(rows)]",
        "    return [GOAL_PATTERN[(r + 1) % len(GOAL_PATTERN)] for r in range(rows)]",
        (T_STR + "test_goal_rows_follow_the_47_count_pattern",),
    ),
    Plant(
        "text-goal-variants-renumbered",
        ST,
        "            entries += [_plain(cursor, row, v) for v in range(variants)]",
        "            entries += [_plain(cursor, row, v + 1) for v in range(variants)]",
        (T_STR + "test_goal_rows_follow_the_47_count_pattern",),
    ),
    Plant(
        "text-strings-comments-read",
        ST,
        "    lines = read_lines(data, BUFFER, skip_comments=True)",
        "    lines = read_lines(data, BUFFER, skip_comments=False)",
        (T_STR + "test_comments_are_skipped_and_lines_after_the_last_table_unread",),
    ),
    Plant(
        "text-model-second-byte-kept",
        ST,
        "text[2:], GENDERS[text[0]], line.number)",
        "text[1:], GENDERS[text[0]], line.number)",
        (T_STR + "test_model_names_give_gender_and_drop_the_second_byte",),
    ),
    Plant(
        "text-gender-letters-swapped",
        ST,
        'GENDERS = {ord("m"): 0, ord("f"): 1, ord("n"): 2}',
        'GENDERS = {ord("m"): 0, ord("f"): 2, ord("n"): 1}',
        (
            T_STR + "test_model_names_give_gender_and_drop_the_second_byte",
            T_STR + "test_feminine_and_neutered_tables_present_and_absent",
        ),
    ),
    Plant(
        "text-model-any-case",
        ST,
        "    if not text or text[0] not in GENDERS:",
        "    if not text or text[0] | 0x20 not in GENDERS:",
        (BAD_MODEL + "[upper]",),
    ),
    Plant(
        "text-model-bad-byte-guessed",
        ST,
        '        raise cursor.refuse(line, stop, f"{first}: not m, f or n", stops=True)',
        "        text = b'm ' + text",
        (BAD_MODEL + "[x]", BAD_MODEL + "[empty]", BAD_MODEL + "[space]"),
    ),
    Plant(
        "text-model-line-numbered-from-0",
        ST,
        "            self.label, self.table, self.taken, line.number, stops, stop, why",
        "            self.label, self.table, self.taken - 1, line.number, stops, stop, why",
        (BAD_MODEL + "[x]", T_STR + "test_a_model_line_of_one_byte_is_refused"),
    ),
    Plant(
        "text-model-one-byte-guessed",
        ST,
        "    if len(text) < 2:",
        "    if len(text) < 1:",
        (T_STR + "test_a_model_line_of_one_byte_is_refused",),
    ),
    Plant(
        "text-escape-forms-swapped",
        ST,
        "        less = ZERO_FORM if text[i + 1] == ZERO else OTHER_FORM",
        "        less = OTHER_FORM if text[i + 1] == ZERO else ZERO_FORM",
        (T_STR + "test_escapes_both_forms_and_their_wrap",),
    ),
    Plant(
        "text-escape-no-wrap",
        ST,
        "        out.append((text[i + 2] - less) % 256)",
        "        out.append(max(text[i + 2] - less, 0))",
        (T_STR + "test_escapes_both_forms_and_their_wrap",),
    ),
    Plant(
        "text-escape-rescanned",
        ST,
        "        i += 3\n",
        "        i += 1\n",
        (T_STR + "test_escapes_both_forms_and_their_wrap",),
    ),
    Plant(
        "text-escape-end-kept",
        ST,
        "        if i + 2 >= len(text):\n            return None",
        "        if i + 1 >= len(text):\n            return None\n"
        "        if i + 2 >= len(text):\n            out.append(byte)\n"
        "            i += 1\n            continue",
        (BACKSLASH_END + "[second-last]",),
    ),
    Plant(
        "text-escape-last-byte-kept",
        ST,
        "        if i + 2 >= len(text):\n            return None",
        "        if i + 2 >= len(text):\n            return text[:i]",
        (BACKSLASH_END + "[last]", BACKSLASH_END + "[alone]"),
    ),
    Plant(
        "text-escape-everywhere",
        ST,
        "    return StringEntry(index, variant, line.text, None, line.number)",
        "    text = decode_escapes(line.text) or line.text\n"
        "    return StringEntry(index, variant, text, None, line.number)",
        (T_STR + "test_escaped_table_is_decoded_other_tables_are_not",),
    ),
    Plant(
        "text-out-of-sync-blank",
        ST,
        "        if self.next >= len(self.lines):\n            raise",
        "        if self.next >= len(self.lines):\n"
        '            return Line(0, b"m ")\n'
        "        if self.next < 0:\n            raise",
        (EARLY + "[empty]", EARLY + "[one]", EARLY + "[500]"),
    ),
    Plant(
        "text-out-of-sync-counted-wrong",
        ST,
        "    def take(self) -> Line:\n"
        '        """Return the next line, or raise the game\'s out-of-sync stop."""\n'
        "        self.taken += 1\n",
        "    def take(self) -> Line:\n"
        '        """Return the next line, or raise the game\'s out-of-sync stop."""\n',
        (EARLY + "[500]",),
    ),
    Plant(
        "text-gender-tables-always",
        ST,
        "        if gender in genders:",
        "        if True:",
        (T_STR + "test_feminine_and_neutered_tables_present_and_absent",),
    ),
    Plant(
        "text-gender-tables-never",
        ST,
        "        if gender in genders:",
        "        if False:",
        (
            T_STR + "test_feminine_and_neutered_tables_present_and_absent",
            T_STR + "test_a_gender_table_cut_short_is_out_of_sync",
        ),
    ),
    Plant(
        "text-tables-reordered",
        ST,
        "    tables = [_read_table(cursor, table) for table in TABLES]",
        "    tables = [_read_table(cursor, table) for table in TABLES[::-1]]",
        (T_STR + "test_tables_read_in_order_each_line_once",),
    ),
    Plant(
        "text-lines-read-counts-all",
        ST,
        "    return StringsFile(tables, cursor.next)",
        "    return StringsFile(tables, len(lines))",
        (T_STR + "test_comments_are_skipped_and_lines_after_the_last_table_unread",),
    ),
]

STOP_PLANTS: list[Plant] = [
    Plant(
        "text-stop-lines-swapped",
        TB,
        "OUT_OF_SYNC_LINE = 2\n",
        "OUT_OF_SYNC_LINE = 3\n",
        (
            EARLY + "[message-read]",
            EARLY + "[500]",
            T_STR + "test_stop_lines_are_lines_2_and_3_of_the_message_table",
        ),
    ),
    Plant(
        "text-bad-model-shows-line-2",
        TB,
        "BAD_MODEL_LINE = 3\n",
        "BAD_MODEL_LINE = 2\n",
        (
            BAD_MODEL + "[x]",
            T_STR + "test_stop_lines_are_lines_2_and_3_of_the_message_table",
        ),
    ),
    Plant(
        "text-stop-message-from-any-table",
        ST,
        "        if self.table == MESSAGES:",
        "        if self.table:",
        (EARLY + "[500]", BAD_MODEL + "[x]"),
    ),
    Plant(
        "text-stop-message-before-it-is-read",
        ST,
        "        return self.messages[index] if index < len(self.messages) else None",
        '        return self.messages[index] if index < len(self.messages) else b""',
        (EARLY + "[empty]", EARLY + "[one]", EARLY + "[message-unread]"),
    ),
    Plant(
        "text-out-of-sync-not-a-stop",
        ST,
        "                True,\n                self.message(OUT_OF_SYNC_LINE),",
        "                False,\n                self.message(OUT_OF_SYNC_LINE),",
        (EARLY + "[500]",),
    ),
    Plant(
        "text-bad-model-not-a-stop",
        ST,
        'f"{first}: not m, f or n", stops=True)',
        'f"{first}: not m, f or n", stops=False)',
        (BAD_MODEL + "[x]",),
    ),
]

TEXT_LINES_PLANTS: list[Plant] = [*LINE_PLANTS, *STRINGS_PLANTS, *STOP_PLANTS]
