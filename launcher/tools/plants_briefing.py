"""The planted faults of the briefing bundle, its art, the command and the route.

Purpose:
    Break one rule of ``jedimaster/briefing/``, of the control list's
    ``briefing.get`` or of the art route per plant: which briefing a team
    sees, the points, the page word, the fonts, sheets and sounds a bundle
    lists, the event walk, the art store, the mission lookup, the command's
    answers and refusals, the route and the export command.


Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in ``expect``
    failed.

Invariants:
    - Ids are unique across all tables, all starting ``briefing-``.
    - Each ``old`` text occurs exactly once in its file.
    - Every test of the briefing files fails under at least one plant.

Call:
    ``from plants_briefing import BRIEFING_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)


BRIEFING_PLANTS: list[Plant] = [
    Plant(
        "briefing-flag-test-inverted",
        "jedimaster/briefing/build.py",
        "        if flags[team]:",
        "        if not flags[team]:",
        (
            "tests/test_briefing_build.py::test_a_briefing_carries_its_time_events_and_texts",
            "tests/test_briefing_build.py::test_events_are_read_with_the_setup_screens_counts",
        ),
    ),
    Plant(
        "briefing-first-flagged-wins",
        "jedimaster/briefing/build.py",
        "            seen = index\n",
        "            seen = seen if seen is not None else index\n",
        (
            "tests/test_briefing_build.py::test_teams_list_their_briefing_and_name",
            "tests/test_briefing_build.py::test_the_last_briefing_with_the_teams_flag_is_the_one_it_sees",
        ),
    ),
    Plant(
        "briefing-one-briefing-only",
        "jedimaster/briefing/build.py",
        "used = sorted({c for c in choices if c is not None})",
        "used = sorted({c for c in choices if c is not None})[:1]",
        (
            "tests/test_briefing_build.py::test_only_the_briefings_some_team_sees_are_in_the_bundle",
        ),
    ),
    Plant(
        "briefing-point-axes-swapped",
        "jedimaster/briefing/build.py",
        "PointData(p.x, p.y, bool(p.enabled))",
        "PointData(p.y, p.x, bool(p.enabled))",
        (
            "tests/test_briefing_build.py::test_flight_groups_take_the_briefing_points_14_to_21",
            "tests/test_briefing_control.py::test_the_bundle_of_a_chosen_mission_has_what_the_file_says",
        ),
    ),
    Plant(
        "briefing-first-point-13",
        "jedimaster/briefing/build.py",
        "FIRST_BRIEFING_POINT = 14",
        "FIRST_BRIEFING_POINT = 13",
        (
            "tests/test_briefing_build.py::test_flight_groups_take_the_briefing_points_14_to_21",
            "tests/test_briefing_control.py::test_the_bundle_of_a_chosen_mission_has_what_the_file_says",
        ),
    ),
    Plant(
        "briefing-page-word-639",
        "jedimaster/briefing/names.py",
        "PAGE_STRING = 640",
        "PAGE_STRING = 639",
        (
            "tests/test_briefing_build.py::test_the_page_word_is_line_640_of_the_front_text",
        ),
    ),
    Plant(
        "briefing-no-text-word",
        "jedimaster/briefing/names.py",
        'NO_TEXT = "No text."',
        'NO_TEXT = "None"',
        ("tests/test_briefing_build.py::test_a_missing_front_text_gives_no_text",),
    ),
    Plant(
        "briefing-font-fields-swapped",
        "jedimaster/briefing/build.py",
        "        font.spacing,\n        font.height,\n",
        "        font.height,\n        font.spacing,\n",
        ("tests/test_briefing_build.py::test_font_10_metrics_come_from_the_font",),
    ),
    Plant(
        "briefing-labels-and-captions-swapped",
        "jedimaster/briefing/build.py",
        "            list(mission.briefings[index].tags),\n            list(mission.briefings[index].strings),",
        "            list(mission.briefings[index].strings),\n            list(mission.briefings[index].tags),",
        (
            "tests/test_briefing_build.py::test_a_briefing_carries_its_time_events_and_texts",
        ),
    ),
    Plant(
        "briefing-team-names-lost",
        "jedimaster/briefing/build.py",
        "TeamData(team, mission.teams[team].name, choice)",
        'TeamData(team, "", choice)',
        ("tests/test_briefing_build.py::test_teams_list_their_briefing_and_name",),
    ),
    Plant(
        "briefing-missing-sheets-listed",
        "jedimaster/briefing/build.py",
        "for n in ICON_SHEETS if n in have]",
        "for n in ICON_SHEETS]",
        ("tests/test_briefing_build.py::test_a_missing_sheet_is_left_out",),
    ),
    Plant(
        "briefing-grey-always-listed",
        "jedimaster/briefing/build.py",
        "return sheets, grey if GREY_SHEET in have else None",
        "return sheets, grey",
        ("tests/test_briefing_build.py::test_a_missing_sheet_is_left_out",),
    ),
    Plant(
        "briefing-missing-sounds-listed",
        "jedimaster/briefing/build.py",
        "for n in SOUND_NAMES if n in found]",
        "for n in SOUND_NAMES]",
        (
            "tests/test_briefing_build.py::test_a_missing_sound_list_leaves_no_sounds",
            "tests/test_briefing_build.py::test_sounds_are_those_of_the_list_that_have_a_file",
        ),
    ),
    Plant(
        "briefing-missing-font-passes",
        "jedimaster/briefing/build.py",
        "    if font is None:\n        raise BriefingBuildError",
        "    if False:\n        raise BriefingBuildError",
        (
            "tests/test_briefing_build.py::test_a_missing_font_is_an_error",
            "tests/test_briefing_control.py::test_a_missing_font_is_not_found",
        ),
    ),
    Plant(
        "briefing-zeros-not-restored",
        "jedimaster/briefing/build.py",
        "    shorts += [0] * (EVENT_AREA - len(shorts))\n",
        "",
        (
            "tests/test_briefing_build.py::test_the_event_walk_keeps_a_trailing_zero_variable_after_an_unknown_type",
            "tests/test_briefing_build.py::test_the_last_short_of_the_event_area_can_be_a_zero_variable",
        ),
    ),
    Plant(
        "briefing-events-run-past",
        "jedimaster/briefing/build.py",
        "if count is None or at + 2 + count > len(shorts):",
        "if count is None:",
        (
            "tests/test_briefing_build.py::test_the_event_walk_stops_when_an_event_would_run_past_the_area",
        ),
    ),
    Plant(
        "briefing-walk-past-the-end",
        "jedimaster/briefing/build.py",
        "        if kind == END_EVENT:\n            break",
        "        if False:\n            break",
        (
            "tests/test_briefing_build.py::test_a_briefing_carries_its_time_events_and_texts",
            "tests/test_briefing_build.py::test_events_are_read_with_the_setup_screens_counts",
        ),
    ),
    Plant(
        "briefing-type-2-no-variable",
        "jedimaster/briefing/names.py",
        "**dict.fromkeys((2, 4, 5, *range(9, 17)), 1),",
        "**dict.fromkeys((4, 5, *range(9, 17)), 1),",
        (
            "tests/test_briefing_build.py::test_events_are_read_with_the_setup_screens_counts",
            "tests/test_briefing_build.py::test_the_event_walk_keeps_a_trailing_zero_variable_after_an_unknown_type",
        ),
    ),
    Plant(
        "briefing-event-area-399",
        "jedimaster/briefing/build.py",
        "EVENT_AREA = 400",
        "EVENT_AREA = 399",
        (
            "tests/test_briefing_build.py::test_the_last_short_of_the_event_area_can_be_a_zero_variable",
        ),
    ),
    Plant(
        "briefing-art-any-name",
        "jedimaster/briefing/art.py",
        "        if name not in ART_NAMES or self.install is None:",
        "        if self.install is None:",
        (
            "tests/test_briefing_art.py::test_any_other_name_is_404",
            "tests/test_briefing_art.py::test_any_other_name_is_nothing",
        ),
    ),
    Plant(
        "briefing-art-not-kept",
        "jedimaster/briefing/art.py",
        "        if name in self._kept:\n            return self._kept[name]",
        "        if False:\n            return self._kept[name]",
        ("tests/test_briefing_art.py::test_a_built_file_is_kept",),
    ),
    Plant(
        "briefing-art-failure-kept",
        "jedimaster/briefing/art.py",
        "        if art is not None:\n            self._kept[name] = art",
        "        if True:\n            self._kept[name] = art",
        (
            "tests/test_briefing_art.py::test_a_file_that_cannot_be_built_is_none_and_tried_again",
        ),
    ),
    Plant(
        "briefing-wav-type",
        "jedimaster/briefing/names.py",
        'WAV_TYPE = "audio/wav"',
        'WAV_TYPE = "audio/x-wav"',
        (
            "tests/test_briefing_art.py::test_each_art_name_is_served",
            "tests/test_briefing_art.py::test_every_name_builds_with_its_media_type",
        ),
    ),
    Plant(
        "briefing-png-type",
        "jedimaster/briefing/names.py",
        'PNG_TYPE = "image/png"',
        'PNG_TYPE = "image/x-png"',
        (
            "tests/test_briefing_art.py::test_each_art_name_is_served",
            "tests/test_briefing_art.py::test_every_name_builds_with_its_media_type",
        ),
    ),
    Plant(
        "briefing-wav-suffix",
        "jedimaster/briefing/names.py",
        'WAV_SUFFIX = ".wav"',
        'WAV_SUFFIX = ".wave"',
        (
            "tests/test_briefing_art.py::test_a_file_that_cannot_be_built_is_none_and_tried_again",
            "tests/test_briefing_art.py::test_each_art_name_is_served",
        ),
    ),
    Plant(
        "briefing-font-never-read",
        "jedimaster/briefing/art.py",
        "        return read_font(path)\n",
        "        return None\n",
        (
            "tests/test_briefing_art.py::test_each_art_name_is_served",
            "tests/test_briefing_art.py::test_every_name_builds_with_its_media_type",
        ),
    ),
    Plant(
        "briefing-wrong-entry",
        "jedimaster/briefing/find.py",
        "        if entry.id != ident:\n            continue",
        "        if entry.id == ident:\n            continue",
        (
            "tests/test_briefing_control.py::test_a_file_that_is_not_a_mission_is_not_found",
            "tests/test_briefing_control.py::test_a_found_mission_answers_with_its_bundle",
        ),
    ),
    Plant(
        "briefing-rebel-menu",
        "jedimaster/briefing/find.py",
        'menu_game_path(mission_type, "network")',
        'menu_game_path(mission_type, "rebel")',
        (
            "tests/test_briefing_control.py::test_a_file_that_is_not_a_mission_is_not_found",
            "tests/test_briefing_control.py::test_a_found_mission_answers_with_its_bundle",
        ),
    ),
    Plant(
        "briefing-briefing-args-none",
        "jedimaster/page/control.py",
        '    "briefing.get": _show_args,',
        '    "briefing.get": _no_args,',
        (
            "tests/test_briefing_control.py::test_a_file_that_is_not_a_mission_is_not_found",
            "tests/test_briefing_control.py::test_a_found_mission_answers_with_its_bundle",
        ),
    ),
    Plant(
        "briefing-no-install-crash",
        "jedimaster/page/control.py",
        "        if self.install is None:\n            return missing",
        "        if False:\n            return missing",
        ("tests/test_briefing_control.py::test_no_install_is_not_found",),
    ),
    Plant(
        "briefing-build-error-escapes",
        "jedimaster/page/control.py",
        "            BriefingBuildError,\n            ListFormatError,",
        "            ListFormatError,",
        ("tests/test_briefing_control.py::test_a_missing_font_is_not_found",),
    ),
    Plant(
        "briefing-found-false",
        "jedimaster/page/control.py",
        '        return {"found": True, "bundle": bundle}',
        '        return {"found": False, "bundle": bundle}',
        (
            "tests/test_briefing_control.py::test_a_found_mission_answers_with_its_bundle",
        ),
    ),
    Plant(
        "briefing-missing-is-found",
        "jedimaster/page/control.py",
        'missing: Json = {"found": False, "bundle": None}',
        'missing: Json = {"found": True, "bundle": None}',
        (
            "tests/test_briefing_control.py::test_a_file_that_is_not_a_mission_is_not_found",
            "tests/test_briefing_control.py::test_a_missing_font_is_not_found",
        ),
    ),
    Plant(
        "briefing-command-unwired",
        "jedimaster/page/control.py",
        '            "briefing.get": self._briefing_get,\n',
        "",
        (
            "tests/test_briefing_control.py::test_a_file_that_is_not_a_mission_is_not_found",
            "tests/test_briefing_control.py::test_a_found_mission_answers_with_its_bundle",
        ),
    ),
    Plant(
        "briefing-revision-4",
        "jedimaster/page/protocol.py",
        "SCHEMA_REVISION = 3",
        "SCHEMA_REVISION = 4",
        (
            "tests/test_page_control.py::test_hello",
            "tests/test_page_control.py::test_the_list_has_seven_commands_and_revision_3",
        ),
    ),
    Plant(
        "briefing-route-missing",
        "jedimaster/page/server.py",
        '    app.router.add_get("/art/briefing/{name}", art)\n',
        "",
        ("tests/test_briefing_art.py::test_each_art_name_is_served",),
    ),
    Plant(
        "briefing-art-type-lost",
        "jedimaster/page/server.py",
        "web.Response(body=found.data, content_type=found.content_type)",
        'web.Response(body=found.data, content_type="application/octet-stream")',
        ("tests/test_briefing_art.py::test_each_art_name_is_served",),
    ),
    Plant(
        "briefing-art-store-empty",
        "jedimaster/page/server.py",
        "app[ART] = ArtStore(control.install)",
        "app[ART] = ArtStore(None)",
        ("tests/test_briefing_art.py::test_each_art_name_is_served",),
    ),
    Plant(
        "briefing-export-no-install",
        "jedimaster/briefing/cli.py",
        '        logger.error("not an install: %s", args.install)\n        return 2',
        '        logger.error("not an install: %s", args.install)\n        return 1',
        ("tests/test_briefing_control.py::test_export_needs_an_install_and_a_file",),
    ),
    Plant(
        "briefing-export-no-file",
        "jedimaster/briefing/cli.py",
        '        logger.error("mission not found: %s", source)\n        return 2',
        '        logger.error("mission not found: %s", source)\n        return 1',
        ("tests/test_briefing_control.py::test_export_needs_an_install_and_a_file",),
    ),
    Plant(
        "briefing-export-failure-ok",
        "jedimaster/briefing/cli.py",
        '        logger.error("cannot export %s: %s", source, exc)\n        return 1',
        '        logger.error("cannot export %s: %s", source, exc)\n        return 0',
        (
            "tests/test_briefing_control.py::test_export_exits_1_for_a_file_that_is_not_a_mission",
        ),
    ),
    Plant(
        "briefing-export-elsewhere",
        "jedimaster/briefing/cli.py",
        "Path(args.out).write_text(",
        "Path(args.out + '.x').write_text(",
        ("tests/test_briefing_control.py::test_export_writes_the_bundle_of_one_file",),
    ),
    Plant(
        "briefing-art-install-unchecked",
        "jedimaster/briefing/art.py",
        "if name not in ART_NAMES or self.install is None:",
        "if name not in ART_NAMES:",
        (
            "tests/test_briefing_art.py::test_no_install_answers_404_for_every_name",
            "tests/test_briefing_art.py::test_no_install_serves_nothing",
        ),
    ),
    Plant(
        "briefing-key-unchecked",
        "jedimaster/page/server.py",
        "middlewares=[add_headers, check_host, check_key]",
        "middlewares=[add_headers, check_host]",
        (
            "tests/test_briefing_art.py::test_any_other_name_is_404",
            "tests/test_briefing_art.py::test_each_art_name_is_served",
        ),
    ),
    Plant(
        "briefing-sheet-picture-cut",
        "jedimaster/briefing/art.py",
        "return Art(png_bytes(picture.bitmap), PNG_TYPE)",
        "return Art(png_bytes(picture.bitmap)[:-1], PNG_TYPE)",
        ("tests/test_briefing_art.py::test_the_sheet_picture_has_the_sheets_pixels",),
    ),
    Plant(
        "briefing-wrong-wav",
        "jedimaster/briefing/art.py",
        "found[name] = path\n",
        'found[name] = path.parent / "t1.wav"\n',
        (
            "tests/test_briefing_art.py::test_every_name_builds_with_its_media_type",
            "tests/test_briefing_art.py::test_the_sound_list_resolves_each_wav",
        ),
    ),
    Plant(
        "briefing-unknown-type-read",
        "jedimaster/briefing/build.py",
        "EVENT_VARIABLES.get(kind)",
        "EVENT_VARIABLES.get(kind, 0)",
        (
            "tests/test_briefing_build.py::test_the_event_walk_stops_at_a_type_it_does_not_know",
        ),
    ),
    Plant(
        "briefing-schema-open",
        "jedimaster/page/schema.py",
        'entry["additionalProperties"] = False',
        'entry["additionalProperties"] = True',
        (
            "tests/test_briefing_build.py::test_the_schema_closes_every_object",
            "tests/test_briefing_build.py::test_the_schema_file_equals_the_model",
        ),
    ),
    Plant(
        "briefing-field-renamed",
        "jedimaster/briefing/model.py",
        "    front_string: str\n",
        "    page_word: str\n",
        (
            "tests/test_briefing_build.py::test_a_briefing_carries_its_time_events_and_texts",
            "tests/test_briefing_build.py::test_a_missing_front_text_gives_no_text",
        ),
    ),
]
