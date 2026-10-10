"""The planted faults of what lies under the map: the background, the picture, the heading.

Purpose:
    Break one rule of ``jedimaster/briefing/under.py`` (the background rule,
    the five layers of the saved picture, the shading, the band, the
    widening), of the art store's new pictures, of the bundle's new fields
    (each team's background, the heading and its font) and of the export
    command's directory per plant.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in ``expect``
    failed.

Invariants:
    - Ids are unique across all tables, all starting ``briefing-under-``.
    - Each ``old`` text occurs exactly once in its file.
    - Every new or changed test of the briefing files fails under at least
      one plant.

Call:
    ``from plants_briefing_under import BRIEFING_UNDER_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

U = "jedimaster/briefing/under.py"
B = "jedimaster/briefing/build.py"
A = "jedimaster/briefing/art.py"
TU = "tests/test_briefing_under.py::"
TA = "tests/test_briefing_art.py::"
TC = "tests/test_briefing_control.py::"
CP = "tools/compare_pixels.py"
TP = "tests/test_compare_pixels.py::"
RULE = (
    TU + "test_melee_and_tournament_take_the_neutral_background",
    TU + "test_combat_and_battle_are_imperial_for_team_0_and_rebel_for_any_other",
)


BRIEFING_UNDER_PLANTS: list[Plant] = [
    Plant(
        "briefing-under-tournament-not-neutral",
        U,
        'NEUTRAL_DIRECTORIES = ("melee", "tournament")',
        'NEUTRAL_DIRECTORIES = ("melee",)',
        (TU + "test_melee_and_tournament_take_the_neutral_background",),
    ),
    Plant(
        "briefing-under-battle-not-sided",
        U,
        'SIDED_DIRECTORIES = ("combat", "battle")',
        'SIDED_DIRECTORIES = ("combat",)',
        (
            TU
            + "test_combat_and_battle_are_imperial_for_team_0_and_rebel_for_any_other",
        ),
    ),
    Plant(
        "briefing-under-imperial-team-1",
        U,
        "IMPERIAL_TEAM = 0",
        "IMPERIAL_TEAM = 1",
        (
            TU
            + "test_combat_and_battle_are_imperial_for_team_0_and_rebel_for_any_other",
            TC + "test_briefing_get_for_a_combat_mission_is_imperial_for_team_0_only",
        ),
    ),
    Plant(
        "briefing-under-sides-swapped",
        U,
        'kind = "imperial" if team == IMPERIAL_TEAM else "rebel"',
        'kind = "rebel" if team == IMPERIAL_TEAM else "imperial"',
        RULE[1:],
    ),
    Plant(
        "briefing-under-iff-kinds-swapped",
        U,
        'IFF_KINDS = {0: "rebel", 1: "imperial"}',
        'IFF_KINDS = {0: "imperial", 1: "rebel"}',
        (
            TU + "test_other_directories_take_the_first_player_group_by_its_iff",
            TC + "test_export_takes_the_directory_from_the_files_folder",
        ),
    ),
    Plant(
        "briefing-under-other-iff-rebel",
        U,
        'kind = IFF_KINDS.get(iff, "neutral")',
        'kind = IFF_KINDS.get(iff, "rebel")',
        (TU + "test_other_directories_take_the_first_player_group_by_its_iff",),
    ),
    Plant(
        "briefing-under-no-player-rebel",
        U,
        '        kind = "neutral"\n        for iff in player_iffs:',
        '        kind = "rebel"\n        for iff in player_iffs:',
        (
            TU + "test_with_no_player_group_the_background_is_neutral",
            TU + "test_a_mission_with_no_player_group_is_neutral",
        ),
    ),
    Plant(
        "briefing-under-last-player-group",
        U,
        "        for iff in player_iffs:\n",
        "        for iff in reversed(player_iffs):\n",
        (TU + "test_other_directories_take_the_first_player_group_by_its_iff",),
    ),
    Plant(
        "briefing-under-team-picks-training",
        U,
        "        for iff in player_iffs:\n            if iff is not None:\n",
        "        for iff in player_iffs[team:]:\n            if iff is not None:\n",
        (TU + "test_the_team_does_not_change_the_background_of_a_training_mission",),
    ),
    Plant(
        "briefing-under-red-rounds-up",
        U,
        "red = ((under >> 11) + (over >> 11)) // 2",
        "red = ((under >> 11) + (over >> 11) + 1) // 2",
        (TU + "test_shading_halves_the_sum_of_red_and_green_rounding_down",),
    ),
    Plant(
        "briefing-under-green-rounds-up",
        U,
        "green = (((under >> 5) & 63) + ((over >> 5) & 63)) // 2",
        "green = (((under >> 5) & 63) + ((over >> 5) & 63) + 1) // 2",
        (TU + "test_shading_halves_the_sum_of_red_and_green_rounding_down",),
    ),
    Plant(
        "briefing-under-blue-like-red",
        U,
        "blue = (under & 31) // 2 + (over & 31) // 2",
        "blue = ((under & 31) + (over & 31)) // 2",
        (TU + "test_shading_blue_halves_each_before_adding",),
    ),
    Plant(
        "briefing-under-band-starts-late",
        U,
        "BAND_FIRST_ROW = 209",
        "BAND_FIRST_ROW = 210",
        (TU + "test_the_band_covers_its_first_and_last_rows_and_columns",),
    ),
    Plant(
        "briefing-under-band-ends-early",
        U,
        "BAND_LAST_ROW = 236",
        "BAND_LAST_ROW = 235",
        (TU + "test_the_band_covers_its_first_and_last_rows_and_columns",),
    ),
    Plant(
        "briefing-under-band-skips-last-row",
        U,
        "for row in range(BAND_FIRST_ROW, BAND_LAST_ROW + 1):",
        "for row in range(BAND_FIRST_ROW, BAND_LAST_ROW):",
        (TU + "test_the_band_covers_its_first_and_last_rows_and_columns",),
    ),
    Plant(
        "briefing-under-band-skips-last-column",
        U,
        "        for column in range(PANEL_WIDTH):\n            at = row",
        "        for column in range(PANEL_WIDTH - 1):\n            at = row",
        (TU + "test_the_band_covers_its_first_and_last_rows_and_columns",),
    ),
    Plant(
        "briefing-under-band-not-blue",
        U,
        "BLUE_565 = 0x001F",
        "BLUE_565 = 0x0010",
        (
            TU + "test_the_band_covers_its_first_and_last_rows_and_columns",
            TU + "test_each_background_picture_is_the_panel_in_that_backgrounds_color",
        ),
    ),
    Plant(
        "briefing-under-panel-left-83",
        U,
        "PANEL_LEFT = 84",
        "PANEL_LEFT = 83",
        (TU + "test_the_panel_is_the_part_of_the_picture_at_84_96",),
    ),
    Plant(
        "briefing-under-panel-top-95",
        U,
        "PANEL_TOP = 96",
        "PANEL_TOP = 95",
        (TU + "test_the_panel_is_the_part_of_the_picture_at_84_96",),
    ),
    Plant(
        "briefing-under-background-skips-index-0",
        U,
        "        if index == TRANSPARENT and not whole:",
        "        if index == TRANSPARENT:",
        (TU + "test_the_background_keeps_every_pixel_index_0_included",),
    ),
    Plant(
        "briefing-under-pad-keeps-index-0",
        U,
        "        if index == TRANSPARENT and not whole:",
        "        if False:",
        (TU + "test_the_pad_and_the_frame_skip_index_0_and_the_shading_skips_it_too",),
    ),
    Plant(
        "briefing-under-frame-before-pad",
        U,
        "    if pad is not None:\n        _lay(panel, pad, whole=False)\n"
        "    if frame is not None:\n        _lay(panel, frame, whole=False)\n",
        "    if frame is not None:\n        _lay(panel, frame, whole=False)\n"
        "    if pad is not None:\n        _lay(panel, pad, whole=False)\n",
        (TU + "test_the_layers_go_in_order_background_pad_frame_shading",),
    ),
    Plant(
        "briefing-under-shading-drawn-plain",
        U,
        "panel[at] = shade(panel[at], colors[index]) if shaded else colors[index]",
        "panel[at] = colors[index]",
        (
            TU + "test_the_pad_and_the_frame_skip_index_0_and_the_shading_skips_it_too",
            TU + "test_the_layers_go_in_order_background_pad_frame_shading",
        ),
    ),
    Plant(
        "briefing-under-layer-lost-last-column",
        U,
        "    columns = min(PANEL_WIDTH, bitmap.width - PANEL_LEFT)",
        "    columns = min(PANEL_WIDTH - 1, bitmap.width - PANEL_LEFT)",
        (TU + "test_a_solid_first_layer_shows_everywhere_outside_the_band",),
    ),
    Plant(
        "briefing-under-layer-lost-last-row",
        U,
        "range(min(PANEL_HEIGHT, bitmap.height - PANEL_TOP))",
        "range(min(PANEL_HEIGHT - 1, bitmap.height - PANEL_TOP))",
        (TU + "test_a_solid_first_layer_shows_everywhere_outside_the_band",),
    ),
    Plant(
        "briefing-under-small-layer-spills-over",
        U,
        "    columns = min(PANEL_WIDTH, bitmap.width - PANEL_LEFT)",
        "    columns = PANEL_WIDTH",
        (TU + "test_a_smaller_layer_covers_only_its_own_part_of_the_panel",),
    ),
    Plant(
        "briefing-under-widen5-drops-low-bits",
        U,
        "    return value << 3 | value >> 2",
        "    return value << 3",
        (TU + "test_widened_channels_pack_back_to_themselves",),
    ),
    Plant(
        "briefing-under-widen6-drops-low-bits",
        U,
        "    return value << 2 | value >> 4",
        "    return value << 2",
        (TU + "test_widened_channels_pack_back_to_themselves",),
    ),
    Plant(
        "briefing-under-png-translucent",
        U,
        "widen5(pixel & 31), 255)",
        "widen5(pixel & 31), 254)",
        (TU + "test_every_16_bit_pixel_survives_the_png_round_trip",),
    ),
    Plant(
        "briefing-under-png-red-blue-swapped",
        U,
        "(widen5(pixel >> 11), widen6((pixel >> 5) & 63), widen5(pixel & 31), 255)",
        "(widen5(pixel & 31), widen6((pixel >> 5) & 63), widen5(pixel >> 11), 255)",
        (TU + "test_every_16_bit_pixel_survives_the_png_round_trip",),
    ),
    Plant(
        "briefing-under-art-prefix",
        U,
        'UNDER_PREFIX = "under-"',
        'UNDER_PREFIX = "under_"',
        (
            TU + "test_the_art_name_of_a_background",
            TA + "test_the_set_is_fifteen_names",
        ),
    ),
    Plant(
        "briefing-under-rebel-file-is-imperial",
        U,
        '"rebel": "frontres\\\\PLAYERR.BMP",',
        '"rebel": "frontres\\\\PLAYERI.BMP",',
        (TU + "test_each_background_picture_is_the_panel_in_that_backgrounds_color",),
    ),
    Plant(
        "briefing-under-neutral-file-is-rebel",
        U,
        '"neutral": "frontres\\\\PLAYER.BMP",',
        '"neutral": "frontres\\\\PLAYERR.BMP",',
        (TU + "test_each_background_picture_is_the_panel_in_that_backgrounds_color",),
    ),
    Plant(
        "briefing-under-pad-is-the-frame",
        A,
        'PAD_FILE = "frontres\\\\BLANKPAD.BMP"',
        'PAD_FILE = "frontres\\\\ALLACTIV.BMP"',
        (TU + "test_a_picture_that_cannot_be_built_is_none_with_a_warning",),
    ),
    Plant(
        "briefing-under-shading-not-required",
        A,
        "    if any(layer is None for layer in layers):\n",
        "    if layers[0] is None:\n",
        (TU + "test_a_picture_that_cannot_be_built_is_none_with_a_warning",),
    ),
    Plant(
        "briefing-under-all-pictures-rebel",
        A,
        "UNDER_ART = {under_art_name(kind): kind for kind in BACKGROUND_FILES}",
        'UNDER_ART = {under_art_name(kind): "rebel" for kind in BACKGROUND_FILES}',
        (TU + "test_each_background_picture_is_the_panel_in_that_backgrounds_color",),
    ),
    Plant(
        "briefing-under-font-12-not-served",
        A,
        "    *SOUND_ART,\n    HEADING_FONT_ART,\n",
        "    *SOUND_ART,\n",
        (TA + "test_the_set_is_fifteen_names",),
    ),
    Plant(
        "briefing-under-font-12-atlas-is-font-10s",
        A,
        "game_name = FONT_GAME_NAME if name == FONT_ART else HEADING_FONT_NAME",
        "game_name = FONT_GAME_NAME",
        (TA + "test_font_12s_atlas_is_built_from_its_file_like_font_10s",),
    ),
    Plant(
        "briefing-under-font-12-read-as-font-10",
        A,
        "    return read_game_font(install, HEADING_FONT_NAME)",
        "    return read_game_font(install, FONT_GAME_NAME)",
        (TU + "test_the_heading_is_front_string_207_and_font_12_comes_with_it",),
    ),
    Plant(
        "briefing-under-pictures-not-kept",
        A,
        "        if name in self._kept:\n            return self._kept[name]\n",
        "        if name in self._kept and not name.startswith('under'):\n            return self._kept[name]\n",
        (TA + "test_a_built_under_picture_is_kept",),
    ),
    Plant(
        "briefing-under-team-0-background-for-all",
        B,
        "under_art_name(background_for(directory, team, player_iffs)),",
        "under_art_name(background_for(directory, 0, player_iffs)),",
        (
            TU + "test_each_team_names_the_background_its_directory_gives",
            TC + "test_briefing_get_for_a_combat_mission_is_imperial_for_team_0_only",
        ),
    ),
    Plant(
        "briefing-under-player-number-reversed",
        B,
        "g.iff if g.player_number != 0 else None",
        "g.iff if g.player_number == 0 else None",
        (
            TU + "test_each_team_names_the_background_its_directory_gives",
            TU + "test_no_directory_uses_the_first_player_group",
        ),
    ),
    Plant(
        "briefing-under-directory-dropped",
        B,
        "background_for(directory, team, player_iffs)",
        "background_for(None, team, player_iffs)",
        (TU + "test_each_team_names_the_background_its_directory_gives",),
    ),
    Plant(
        "briefing-under-heading-is-the-page-word",
        B,
        '_front_string(install, HEADING_STRING, ""),',
        '_front_string(install, PAGE_STRING, ""),',
        (TU + "test_the_heading_is_front_string_207_and_font_12_comes_with_it",),
    ),
    Plant(
        "briefing-under-missing-heading-has-text",
        B,
        '_front_string(install, HEADING_STRING, ""),',
        '_front_string(install, HEADING_STRING, "No text."),',
        (TU + "test_a_missing_heading_string_is_empty",),
    ),
    Plant(
        "briefing-under-heading-font-picture",
        B,
        "atlas_name(game_name),",
        "atlas_name(FONT_GAME_NAME),",
        (TU + "test_the_heading_is_front_string_207_and_font_12_comes_with_it",),
    ),
    Plant(
        "briefing-under-missing-font-12-breaks",
        B,
        "    font = read_font12(install)\n    if font is None:\n",
        "    font = read_font12(install)\n    if False:\n",
        (TU + "test_a_missing_font_12_leaves_the_heading_font_out",),
    ),
    Plant(
        "briefing-under-export-ignores-the-option",
        "jedimaster/briefing/cli.py",
        "directory = args.directory or directory_of(source)",
        "directory = directory_of(source)",
        (TC + "test_export_directory_option_beats_the_folder",),
    ),
    Plant(
        "briefing-under-export-ignores-the-folder",
        "jedimaster/briefing/cli.py",
        "directory = args.directory or directory_of(source)",
        "directory = args.directory",
        (TC + "test_export_takes_the_directory_from_the_files_folder",),
    ),
    Plant(
        "briefing-under-folder-match-is-case-sensitive",
        "jedimaster/briefing/find.py",
        "if kind.folder.casefold() == folder:",
        "if kind.folder == path.parent.name:",
        (TU + "test_a_directory_is_found_from_the_files_folder_in_any_case",),
    ),
    Plant(
        "briefing-under-unknown-folder-is-training",
        "jedimaster/briefing/find.py",
        '    logger.debug("folder %r is no mission type\'s", path.parent.name)\n    return None',
        '    logger.debug("folder %r is no mission type\'s", path.parent.name)\n    return "training"',
        (TU + "test_a_directory_is_found_from_the_files_folder_in_any_case",),
    ),
    Plant(
        "briefing-under-command-gives-training-always",
        "jedimaster/page/control.py",
        'self.install, read_mission(path), args["mission_type"]',
        'self.install, read_mission(path), "training"',
        (TC + "test_briefing_get_for_a_combat_mission_is_imperial_for_team_0_only",),
    ),
    Plant(
        "briefing-under-compare-crop-left",
        CP,
        "PANEL_LEFT = 84",
        "PANEL_LEFT = 85",
        (TP + "test_the_crop_starts_at_84_96_and_is_360_by_240",),
    ),
    Plant(
        "briefing-under-compare-crop-top",
        CP,
        "PANEL_TOP = 96",
        "PANEL_TOP = 97",
        (TP + "test_the_crop_starts_at_84_96_and_is_360_by_240",),
    ),
    Plant(
        "briefing-under-compare-crop-accepts-any-size",
        CP,
        "    if len(raw) != SCREEN_WIDTH * SCREEN_HEIGHT * 2:\n        raise",
        "    if len(raw) < 2:\n        raise",
        (TP + "test_a_screen_of_the_wrong_size_is_refused",),
    ),
    Plant(
        "briefing-under-compare-pixel-bytes-swapped",
        CP,
        "return panel[at * 2] | panel[at * 2 + 1] << 8",
        "return panel[at * 2 + 1] | panel[at * 2] << 8",
        (TP + "test_the_crop_starts_at_84_96_and_is_360_by_240",),
    ),
    Plant(
        "briefing-under-compare-sizes-unchecked",
        CP,
        "    if len(engine) != len(page):\n        raise",
        "    if False:\n        raise",
        (TP + "test_differences_are_the_pixel_numbers_that_differ",),
    ),
    Plant(
        "briefing-under-compare-position-swapped",
        CP,
        "x, y = at % PANEL_WIDTH, at // PANEL_WIDTH",
        "y, x = at % PANEL_WIDTH, at // PANEL_WIDTH",
        (TP + "test_a_difference_is_described_by_position_and_both_values",),
    ),
    Plant(
        "briefing-under-compare-values-swapped",
        CP,
        'f"({x},{y}) engine {pixel_at(engine, at):04x} page {pixel_at(page, at):04x}"',
        'f"({x},{y}) engine {pixel_at(page, at):04x} page {pixel_at(engine, at):04x}"',
        (TP + "test_a_difference_is_described_by_position_and_both_values",),
    ),
    Plant(
        "briefing-under-compare-shows-five",
        CP,
        "SHOWN_POSITIONS = 4",
        "SHOWN_POSITIONS = 5",
        (TP + "test_only_the_first_few_differences_are_described",),
    ),
    Plant(
        "briefing-under-compare-difference-not-red",
        CP,
        "rgba += bytes((255, 0, 0, 255))",
        "rgba += bytes((0, 255, 0, 255))",
        (TP + "test_the_difference_picture_marks_the_differing_pixels_red",),
    ),
    Plant(
        "briefing-under-compare-picture-too-bright",
        CP,
        "red // 3, green // 3, blue // 3",
        "red // 2, green // 2, blue // 2",
        (TP + "test_the_difference_picture_marks_the_differing_pixels_red",),
    ),
    Plant(
        "briefing-under-compare-widen-green",
        CP,
        "green << 2 | green >> 4",
        "green << 2",
        (TP + "test_a_16_bit_pixel_widens_by_repeating_its_top_bits",),
    ),
    Plant(
        "briefing-under-compare-comments-read",
        CP,
        'if not words or words[0].startswith("#"):',
        "if not words:",
        (TP + "test_moments_are_read_skipping_comments_and_blank_lines",),
    ),
    Plant(
        "briefing-under-compare-short-line-read",
        CP,
        "if len(words) < 4 or words[1] not in DIRECTORY_TYPES:",
        "if len(words) < 3 or words[1] not in DIRECTORY_TYPES:",
        (TP + "test_a_bad_moments_line_is_refused",),
    ),
    Plant(
        "briefing-under-compare-unknown-directory-read",
        CP,
        "if len(words) < 4 or words[1] not in DIRECTORY_TYPES:",
        "if len(words) < 4:",
        (TP + "test_a_bad_moments_line_is_refused",),
    ),
    Plant(
        "briefing-under-compare-key",
        CP,
        "return f\"{moment.mission.replace('/', '_')}.t{moment.team}\"",
        'return f"{moment.mission}.t{moment.team}"',
        (TP + "test_moments_are_read_skipping_comments_and_blank_lines",),
    ),
    Plant(
        "briefing-under-compare-train-is-train",
        CP,
        '"train": "training",',
        '"train": "train",',
        (TP + "test_directory_words_become_mission_types",),
    ),
    Plant(
        "briefing-under-compare-anywhere-for-pictures",
        CP,
        "    return not folder.resolve().is_relative_to(ROOT.parent)",
        "    return True",
        (TP + "test_a_folder_inside_the_repository_is_refused_for_pictures",),
    ),
    Plant(
        "briefing-under-melee-not-neutral",
        U,
        'NEUTRAL_DIRECTORIES = ("melee", "tournament")',
        'NEUTRAL_DIRECTORIES = ("tournament",)',
        (TU + "test_melee_and_tournament_take_the_neutral_background",),
    ),
    Plant(
        "briefing-under-png-size-unchecked",
        U,
        "    if len(panel) != PANEL_WIDTH * PANEL_HEIGHT:\n        raise",
        "    if False:\n        raise",
        (TU + "test_a_png_of_the_wrong_size_is_refused",),
    ),
]
