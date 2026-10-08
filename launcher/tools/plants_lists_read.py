"""The planted faults of the list reader and the game's view of each list.

Purpose:
    Break one piece of ``jedimaster/lists/`` (text helpers, reader, game's
    view) per plant: lines and words, menus, sequences, image, ship and
    sound lists, the counted lists.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Each plant's text is exactly as it was when it lived in
      ``plant_faults.py``; ids are unique across all tables.
    - Each ``old`` text occurs exactly once in its file (the lists package).

Call:
    ``from plants_lists_read import LISTS_READ_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

LT = "jedimaster/lists/text.py"
LR = "jedimaster/lists/reader.py"
LG = "jedimaster/lists/game.py"
LV = "jedimaster/lists/render.py"
LF = "jedimaster/lists/files.py"
LJ = "jedimaster/lists/to_json.py"
TL_TXT = "tests/test_lists_text.py::"
TL_MENU = "tests/test_lists_menu.py::"
TL_SEQ = "tests/test_lists_sequence.py::"
TL_WORDS = "tests/test_lists_words.py::"
TL_CNT = "tests/test_lists_counted.py::"
TL_REN = "tests/test_lists_render.py::"
TL_INS = "tests/test_lists_install.py::"
TL_JSON = "tests/test_lists_json.py::"
TL_CLI = "tests/test_lists_cli.py::"
STAR_PARAMS = (
    "network-flown0-False",
    "network-flown1-True",
    "network-flown2-True",
    "rebel-flown3-True",
    "rebel-flown4-False",
    "imperial-flown5-True",
    "imperial-flown6-False",
)

LISTS_READ_PLANTS: list[Plant] = [
    # ---- the text lists ------------------------------------------------
    # shared text helpers
    Plant(
        "lists-cr-kept",
        LT,
        'if piece.endswith("\\r"):',
        "if False:",
        (TL_TXT + "test_lines_drop_the_carriage_return_and_keep_the_ending",),
    ),
    Plant(
        "lists-empty-last-line",
        LT,
        'if last and piece == "":',
        "if False:",
        (TL_TXT + "test_blank_lines_are_lines_and_nothing_after_the_last_ending",),
    ),
    Plant(
        "lists-end-mark-is-a-line",
        LT,
        "    if text.endswith(END_MARK):\n        text = text[:-1]\n",
        "",
        (TL_TXT + "test_end_mark_is_the_end_of_the_file",),
    ),
    Plant(
        "lists-no-end-mark-flag",
        LT,
        'return data.endswith(END_MARK.encode("latin-1"))',
        "return False",
        (
            TL_TXT + "test_end_mark_is_the_end_of_the_file",
            TL_MENU + "test_end_mark_and_missing_last_line_ending",
        ),
    ),
    Plant(
        "lists-tab-not-a-separator",
        LT,
        '_WORD = re.compile(r"[^ \\t\\n\\v\\f\\r]+")',
        '_WORD = re.compile(r"[^ \\n\\v\\f\\r]+")',
        (TL_TXT + "test_words_cross_line_breaks_and_tabs",),
    ),
    Plant(
        "lists-word-lines-stuck",
        LT,
        'line += text.count("\\n", position, match.start())',
        "line += 0",
        (TL_TXT + "test_words_cross_line_breaks_and_tabs",),
    ),
    Plant(
        "lists-always-skip-first-line",
        LT,
        "if skip_first_line and text:",
        "if text:",
        (
            TL_TXT + "test_words_without_a_skipped_line_start_at_the_first_word",
            TL_WORDS + "test_ship_pairs_from_the_first_word",
        ),
    ),
    Plant(
        "lists-atoi-no-leading-space",
        LT,
        '_ATOI = re.compile(r"[ \\t\\n\\v\\f\\r]*([+-]?[0-9]+)")',
        '_ATOI = re.compile(r"([+-]?[0-9]+)")',
        (TL_TXT + "test_atoi_reads_like_c[\\t42 -42]",),
    ),
    Plant(
        "lists-atoi-no-sign",
        LT,
        '_ATOI = re.compile(r"[ \\t\\n\\v\\f\\r]*([+-]?[0-9]+)")',
        '_ATOI = re.compile(r"[ \\t\\n\\v\\f\\r]*([0-9]+)")',
        (
            TL_TXT + "test_atoi_reads_like_c[  -7x--7]",
            TL_TXT + "test_atoi_reads_like_c[+3-3]",
        ),
    ),
    Plant(
        "lists-atoi-one-digit",
        LT,
        '_ATOI = re.compile(r"[ \\t\\n\\v\\f\\r]*([+-]?[0-9]+)")',
        '_ATOI = re.compile(r"[ \\t\\n\\v\\f\\r]*([+-]?[0-9])")',
        (TL_TXT + "test_atoi_reads_like_c[12-12]",),
    ),
    Plant(
        "lists-atoi-no-digit-minus-one",
        LT,
        "return int(match.group(1)) if match else 0",
        "return int(match.group(1)) if match else -1",
        (
            TL_TXT + "test_atoi_reads_like_c[-0]",
            TL_TXT + "test_atoi_reads_like_c[x1-0]",
            TL_TXT + "test_atoi_reads_like_c[--0]",
        ),
    ),
    Plant(
        "lists-whole-number-prefix",
        LT,
        "return int(text) if _WHOLE.fullmatch(text) else None",
        "return int(_WHOLE.match(text).group()) if _WHOLE.match(text) else None",
        (TL_TXT + "test_whole_numbers_and_scanned_numbers",),
    ),
    Plant(
        "lists-lower-all-letters",
        LT,
        "return text.translate(_ASCII_LOWER)",
        "return text.lower()",
        (TL_TXT + "test_ascii_lower_leaves_other_letters",),
    ),
    Plant(
        "lists-quote-newline-hex",
        LT,
        'out.append("\\\\n")',
        'out.append("\\\\x0a")',
        (TL_TXT + "test_c_quote_escapes",),
    ),
    Plant(
        "lists-nul-accepted",
        LT,
        'nul = data.find(b"\\0")',
        "nul = -1",
        (TL_TXT + "test_refusals", TL_CLI + "test_lists_dump_refusals"),
    ),
    # mission menus
    Plant(
        "lists-menu-no-comments",
        LR,
        'COMMENT = "//"',
        'COMMENT = "#"',
        (TL_MENU + "test_items_in_file_order",),
    ),
    Plant(
        "lists-menu-section-strips-brackets",
        LR,
        "items.append(MenuSection(line, line.text[1:-1]))",
        'items.append(MenuSection(line, line.text[1:].rstrip("]")))',
        (TL_MENU + "test_section_name_drops_the_bracket_and_the_last_character",),
    ),
    Plant(
        "lists-menu-amp-one-char",
        LR,
        "file_name = text[2:]",
        "file_name = text[1:]",
        (
            TL_MENU + "test_entry_values_and_lines_kept_raw",
            TL_MENU + "test_game_view_lowercases_and_resolves_markers",
        ),
    ),
    Plant(
        "lists-menu-skips-comments-inside-entry",
        LR,
        "file_line = lines[at + 1] if at + 1 < len(lines) else None",
        "file_line = next((x for x in lines[at + 1 :] if not _is_comment(x)), None)",
        (TL_MENU + "test_comment_and_section_lines_are_taken_inside_an_entry",),
    ),
    Plant(
        "lists-menu-id-strict",
        LR,
        "id=atoi(id_line.text),",
        "id=int(id_line.text or 0),",
        (TL_MENU + "test_blank_line_reads_as_id_zero_and_ids_read_like_atoi",),
    ),
    Plant(
        "lists-menu-cut-entry-complete",
        LR,
        "complete=title_line is not None,",
        "complete=file_line is not None,",
        (
            TL_MENU
            + "test_entry_cut_off_by_the_end_is_kept_raw_and_dropped_by_the_game",
        ),
    ),
    Plant(
        "lists-menu-star-name-empty",
        LR,
        "            return fields, None\n",
        '            return fields, ""\n',
        (TL_MENU + "test_star_line_without_its_name",),
    ),
    Plant(
        "lists-menu-star-numbers-dropped",
        LR,
        "star_numbers=[atoi(t) for t in star_texts],",
        "star_numbers=[],",
        (TL_JSON + "test_raw_values_and_lines_survive",),
    ),
    Plant(
        "lists-menu-not-lowercased",
        LG,
        'name = ascii_lower(item.file_name or "")',
        'name = item.file_name or ""',
        (TL_MENU + "test_game_view_lowercases_and_resolves_markers",),
    ),
    Plant(
        "lists-menu-amp-available",
        LG,
        "        available = False\n    elif",
        "        available = True\n    elif",
        tuple(TL_MENU + f"test_star_entry_needs_its_id_flown[{p}" for p in STAR_PARAMS),
    ),
    Plant(
        "lists-menu-star-never",
        LG,
        "available = item.id in flown",
        "available = False",
        (TL_MENU + "test_star_entry_needs_its_id_flown[network-flown1-True]",),
    ),
    Plant(
        "lists-menu-network-one-side",
        LG,
        'for side in ("rebel", "imperial")',
        'for side in ("rebel",)',
        (TL_MENU + "test_star_entry_needs_its_id_flown[network-flown2-True]",),
    ),
    Plant(
        "lists-menu-solo-any-side",
        LG,
        "return set(flown.get(view, ()))",
        "return {i for ids in flown.values() for i in ids}",
        (
            TL_MENU + "test_star_entry_needs_its_id_flown[rebel-flown4-False]",
            TL_MENU + "test_star_entry_needs_its_id_flown[imperial-flown6-False]",
        ),
    ),
    Plant(
        "lists-menu-all-sided",
        LG,
        'if view == "network" or not kind.sided:',
        'if view == "network":',
        tuple(
            TL_MENU + f"test_menu_file_per_type_and_view[{t}-"
            for t in ("melee", "tournament", "battle")
        ),
    ),
    Plant(
        "lists-menu-side-file-name",
        LG,
        'return f"{view}.lst"',
        'return f"{view}s.lst"',
        tuple(
            TL_MENU + f"test_menu_file_per_type_and_view[{t}-"
            for t in ("training", "combat", "campaign")
        ),
    ),
    Plant(
        "lists-menu-any-view",
        LG,
        "if view not in VIEWS:",
        "if False:",
        (TL_MENU + "test_unknown_type_or_view_refused",),
    ),
    # sequence files
    Plant(
        "lists-seq-cr-kept",
        LG,
        'return line.text + ("\\n" if line.ending.endswith("\\n") else "")',
        "return line.text + line.ending",
        (TL_SEQ + "test_count_missions_and_description",),
    ),
    Plant(
        "lists-seq-latin1-kept",
        LG,
        'kept = "".join(c for c in text if c == "\\n" or 32 <= ord(c) <= 126)',
        'kept = "".join(c for c in text if c == "\\n" or 32 <= ord(c) <= 255)',
        (TL_SEQ + "test_description_keeps_printable_characters_and_line_feeds_only",),
    ),
    Plant(
        "lists-seq-limit-4096",
        LG,
        "DESCRIPTION_LIMIT = 4095",
        "DESCRIPTION_LIMIT = 4096",
        (TL_SEQ + "test_description_limit_is_4095_characters",),
    ),
    Plant(
        "lists-last-line-gets-ending",
        LT,
        'ending = "" if last else "\\n"',
        'ending = "\\n"',
        (TL_SEQ + "test_last_line_without_ending_is_still_read",),
    ),
    Plant(
        "lists-seq-count-strict",
        LR,
        "count = atoi(count_line.text) if count_line else 0",
        "count = int(count_line.text) if count_line else 0",
        (TL_SEQ + "test_count_beyond_the_file_and_count_below_one",),
    ),
    Plant(
        "lists-seq-skips-comments",
        LR,
        "missions = lines[1 : 1 + max(count, 0)]",
        "missions = [x for x in lines[1:] if not _is_comment(x)][: max(count, 0)]",
        (TL_SEQ + "test_comment_lines_are_values_in_a_sequence_and_a_ship_list",),
    ),
    Plant(
        "lists-counted-skip-comment-count",
        LR,
        "first = lines[0] if lines else None",
        "first = next((x for x in lines if not _is_comment(x)), None)",
        (TL_CNT + "test_comment_lines_count_only_where_the_notes_skip_them",),
    ),
    # image lists
    Plant(
        "lists-no-repeats",
        LR,
        "return None if first == index else first",
        "return None",
        (
            TL_WORDS + "test_image_groups_read_by_word",
            TL_WORDS + "test_sound_pairs_and_repeats",
        ),
    ),
    Plant(
        "lists-tables-case-blind",
        LG,
        'sorted(table, key=lambda n: n.encode("latin-1"))',
        "sorted(table, key=str.lower)",
        (
            TL_WORDS + "test_image_table_sorted_first_name_stays_missing_skipped",
            TL_WORDS + "test_sound_table_sorted_first_stays_failed_loads_skipped",
        ),
    ),
    Plant(
        "lists-images-last-name-wins",
        LG,
        "        if name in table:\n            repeats.append(index)\n            logger",
        "        if False:\n            repeats.append(index)\n            logger",
        (TL_WORDS + "test_image_table_sorted_first_name_stays_missing_skipped",),
    ),
    Plant(
        "lists-images-flag-is-compressed",
        LG,
        "compressed=group.flag_value == COMPRESS_FLAG and bitmap.compresses,",
        "compressed=group.flag_value == COMPRESS_FLAG,",
        (TL_WORDS + "test_compressed_needs_the_flag_and_a_bitmap_that_shrinks",),
    ),
    Plant(
        "lists-images-missing-name-taken",
        LG,
        "        if name in table:\n            repeats.append(index)\n            logger",
        "        if name in table or any(\n"
        "            images.groups[j].name.text == name for j in missing\n"
        "        ):\n            repeats.append(index)\n            logger",
        (TL_WORDS + "test_missing_bitmap_does_not_register_its_name",),
    ),
    Plant(
        "lists-mark-not-seen",
        LR,
        '    if mark and words[at].text == END_MARK:\n        return "mark"\n',
        "",
        (TL_WORDS + "test_image_read_ends", TL_WORDS + "test_ship_read_ends"),
    ),
    Plant(
        "lists-images-mark-succeeds",
        LG,
        'result = 1 if images.end == "eof" else 0',
        'result = 1 if images.end in ("eof", "mark") else 0',
        (TL_WORDS + "test_image_read_ends", TL_REN + "test_images_sheet"),
    ),
    # ship list
    Plant(
        "lists-ships-skip-first-line",
        LR,
        "_, words = split_words(data, skip_first_line=False)",
        "_, words = split_words(data, skip_first_line=True)",
        (TL_WORDS + "test_ship_pairs_from_the_first_word",),
    ),
    Plant(
        "lists-ships-opt-case",
        LG,
        "if ascii_lower(text[-3:]) != SHIP_SUFFIX:",
        "if text[-3:] != SHIP_SUFFIX:",
        (TL_WORDS + "test_ship_view_keeps_opt_models_with_the_tail_lowercased",),
    ),
    Plant(
        "lists-ships-tail-kept",
        LG,
        "name = (text[:-3] + ascii_lower(text[-3:]))[:SHIP_NAME_LIMIT]",
        "name = text[:SHIP_NAME_LIMIT]",
        (TL_WORDS + "test_ship_view_keeps_opt_models_with_the_tail_lowercased",),
    ),
    Plant(
        "lists-ships-type-17-fills",
        LG,
        "TYPE_LIMIT = 17",
        "TYPE_LIMIT = 18",
        (
            TL_WORDS + "test_ship_type_table_overwrite_and_limit",
            TL_WORDS + "test_ship_type_overwrite_and_types_17_and_up",
        ),
    ),
    Plant(
        "lists-ships-first-wins",
        LG,
        "if 0 <= pair.type_value < TYPE_LIMIT:",
        "if 0 <= pair.type_value < TYPE_LIMIT and all(\n"
        "            s.type != pair.type_value for s in kept\n"
        "        ):",
        (TL_WORDS + "test_ship_type_table_overwrite_and_limit",),
    ),
    Plant(
        "lists-ships-negative-type-fills",
        LG,
        "if 0 <= pair.type_value < TYPE_LIMIT:",
        "if pair.type_value < TYPE_LIMIT:",
        (TL_WORDS + "test_ship_type_table_overwrite_and_limit",),
    ),
    Plant(
        "lists-ships-start-all-zero",
        LG,
        "TYPE_START = (0, 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 8, 0, 9, 0)",
        "TYPE_START = (0,) * 18",
        (
            TL_WORDS + "test_ship_table_starting_values",
            TL_WORDS + "test_untouched_slot_keeps_its_start_and_named_slots_change",
            TL_REN + "test_ships_sheet",
        ),
    ),
    Plant(
        "lists-ships-start-slot-14",
        LG,
        "TYPE_START = (0, 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 8, 0, 9, 0)",
        "TYPE_START = (0, 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 3, 0, 9, 0)",
        (TL_WORDS + "test_ship_table_starting_values",),
    ),
    Plant(
        "lists-images-nonzero-asks",
        LG,
        "compressed=group.flag_value == COMPRESS_FLAG and bitmap.compresses,",
        "compressed=group.flag_value != 0 and bitmap.compresses,",
        (TL_WORDS + "test_only_flag_one_asks_for_compression",),
    ),
    Plant(
        "lists-sounds-mark-ends",
        LR,
        "while (end := _word_end(words, at, 2, mark=False)) is None:",
        "while (end := _word_end(words, at, 2, mark=True)) is None:",
        (TL_WORDS + "test_sound_word_without_partner_fails",),
    ),
    Plant(
        "lists-cutscenes-result-always-1",
        LG,
        "result = 0 if cutscenes.count_line is None else 1",
        "result = 1",
        (TL_CNT + "test_counted_lists_fail_only_on_an_empty_file",),
    ),
    Plant(
        "lists-cutscenes-result-0-on-early-end",
        LG,
        "result = 0 if cutscenes.count_line is None else 1",
        'result = 0 if cutscenes.end != "count" else 1',
        (TL_CNT + "test_counted_lists_fail_only_on_an_empty_file",),
    ),
    Plant(
        "lists-awards-result-always-1",
        LG,
        "result = 0 if awards.count_line is None else 1",
        "result = 1",
        (TL_CNT + "test_counted_lists_fail_only_on_an_empty_file",),
    ),
    Plant(
        "lists-awards-result-0-on-early-end",
        LG,
        "result = 0 if awards.count_line is None else 1",
        'result = 0 if awards.end != "count" else 1',
        (TL_CNT + "test_counted_lists_fail_only_on_an_empty_file",),
    ),
    Plant(
        "lists-ships-name-64",
        LG,
        "SHIP_NAME_LIMIT = 63",
        "SHIP_NAME_LIMIT = 64",
        (TL_WORDS + "test_ship_name_cut_to_63_characters",),
    ),
    Plant(
        "lists-ships-type-atoi",
        LR,
        "value = whole_number(words[at + 1].text)",
        "value = atoi(words[at + 1].text)",
        (TL_WORDS + "test_ship_read_ends",),
    ),
    # sound list
    Plant(
        "lists-sounds-load-anything",
        LG,
        "if loadable is not None and pair.wav.text not in loadable:",
        "if loadable is not None and not loadable:",
        (TL_WORDS + "test_sound_table_sorted_first_stays_failed_loads_skipped",),
    ),
    Plant(
        "lists-sounds-short-succeeds",
        LG,
        'result = 0 if sounds.end == "short" else 1',
        "result = 1",
        (TL_WORDS + "test_sound_word_without_partner_fails",),
    ),
    # counted lists
    Plant(
        "lists-counted-no-comment-skip",
        LR,
        "while self.at < len(self.lines) and _is_comment(self.lines[self.at]):",
        "while False:",
        (
            TL_CNT + "test_cutscenes_read_count_entries_skipping_comments",
            TL_CNT + "test_award_records_skip_comments_between_values",
        ),
    ),
    Plant(
        "lists-cutscene-numbers-unchecked",
        LR,
        "if slot == 1 and len(scan_numbers(line.text, CUTSCENE_NUMBERS)) < 3:",
        "if False:",
        (TL_CNT + "test_bad_numbers_line_blanks_the_entry_and_stops",),
    ),
    Plant(
        "lists-cutscene-cut-kept",
        LG,
        'if entry.status != "read":',
        'if entry.status == "bad_numbers":',
        (TL_CNT + "test_cutscene_list_ending_early",),
    ),
    Plant(
        "lists-cutscene-movie-128",
        LG,
        "MOVIE_LIMIT = 127",
        "MOVIE_LIMIT = 128",
        (TL_CNT + "test_cutscene_cuts",),
    ),
    Plant(
        "lists-cutscene-thumbnail-32",
        LG,
        "THUMBNAIL_LIMIT = 31",
        "THUMBNAIL_LIMIT = 32",
        (TL_CNT + "test_cutscene_cuts",),
    ),
    Plant(
        "lists-awards-15-slots",
        LG,
        "AWARD_SLOTS = 16",
        "AWARD_SLOTS = 15",
        (TL_CNT + "test_award_records_skip_comments_between_values",),
    ),
    Plant(
        "lists-awards-cut-record-kept",
        LG,
        "        if not record.complete:\n            continue\n",
        "",
        (TL_CNT + "test_award_record_cut_off_is_not_counted",),
    ),
    Plant(
        "lists-awards-name-32",
        LG,
        "AWARD_NAME_LIMIT = 31",
        "AWARD_NAME_LIMIT = 32",
        (TL_CNT + "test_award_names_cut_to_31_characters",),
    ),
]
