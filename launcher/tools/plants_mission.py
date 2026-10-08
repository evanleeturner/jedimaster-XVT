"""The planted faults of the mission reader, its install and command line.

Purpose:
    Break one piece of ``jedimaster/mission/``, ``jedimaster/install.py`` or
    the mission commands of ``jedimaster/__main__.py`` per plant.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Each plant's text is exactly as it was when it lived in
      ``plant_faults.py``; ids are unique across all tables.
    - Each ``old`` text occurs exactly once in its file (the mission package, the install module, the command line).

Call:
    ``from plants_mission import MISSION_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import CLI
from plants_common import I
from plants_common import Plant

logger = logging.getLogger(__name__)

R = "jedimaster/mission/reader.py"
B = "jedimaster/mission/binary.py"
V = "jedimaster/mission/render.py"
J = "jedimaster/mission/to_json.py"
C = "jedimaster/mission/checks.py"
N = "jedimaster/mission/names.py"
M = "jedimaster/mission/model.py"

T_BIN = "tests/test_binary.py::"
T_SEC = "tests/test_reader_sections.py::"
T_REF = "tests/test_refusals.py::"
T_EV = "tests/test_events.py::"
T_REN = "tests/test_render.py::"
T_JS = "tests/test_json_schema.py::"
T_INS = "tests/test_install.py::"
T_CLI = "tests/test_cli.py::"

MISSION_PLANTS: list[Plant] = [
    # binary helpers
    Plant(
        "bin-s16-unsigned",
        B,
        'return struct.unpack("<h", self._take(2))[0]',
        'return struct.unpack("<H", self._take(2))[0]',
        (T_BIN + "test_integers_are_little_endian_and_signed_where_asked",),
    ),
    Plant(
        "bin-s16s-big-endian",
        B,
        'struct.unpack(f"<{count}h"',
        'struct.unpack(f">{count}h"',
        (T_BIN + "test_arrays_read_in_order",),
    ),
    Plant(
        "bin-no-nul-cut",
        B,
        'cut = raw.find(b"\\0")',
        "cut = -1",
        (T_BIN + "test_strings_cut_at_first_nul_and_decode_latin1",),
    ),
    Plant(
        "bin-no-bounds-check",
        B,
        "if size < 0 or end > len(self.data):",
        "if size < 0:",
        (
            T_BIN + "test_short_read_refused_and_offset_kept",
            T_REF + "test_short_read_in_every_section",
            T_REF + "test_version_14_file_cut_to_version_12_length_is_short",
        ),
    ),
    # header
    Plant(
        "hdr-swap-time-limit",
        R,
        "    h.time_limit_min = cur.u8()\n    h.time_limit_sec = cur.u8()\n",
        "    h.time_limit_sec = cur.u8()\n    h.time_limit_min = cur.u8()\n",
        (T_SEC + "test_header_fields",),
    ),
    Plant(
        "hdr-no-version-check",
        R,
        "if h.platform_id not in SUPPORTED_VERSIONS:",
        "if False:",
        (T_REF + "test_unknown_versions_refused",),
    ),
    Plant(
        "hdr-no-count-check",
        R,
        '        if not 0 <= value <= limit:\n            raise CountLimitError(f"{label}',
        '        if False:\n            raise CountLimitError(f"{label}',
        (T_REF + "test_header_counts_past_limits",),
    ),
    Plant(
        "hdr-fg-limit-255",
        R,
        "MAX_FLIGHT_GROUPS = 256",
        "MAX_FLIGHT_GROUPS = 255",
        (T_REF + "test_header_counts_at_limit_are_read_as_counts",),
    ),
    # flight groups
    Plant(
        "fg-swap-order-targets",
        R,
        "    o.target3 = cur.u8()\n    o.target4 = cur.u8()\n",
        "    o.target4 = cur.u8()\n    o.target3 = cur.u8()\n",
        (T_SEC + "test_flight_group_fields", T_REN + "test_orders_and_targets"),
    ),
    Plant(
        "fg-goal-points-unsigned",
        R,
        "    g.points = cur.s8()\n    g.enabled_for_team",
        "    g.points = cur.u8()\n    g.enabled_for_team",
        (T_SEC + "test_flight_group_fields", T_REN + "test_goals_and_points"),
    ),
    Plant(
        "fg-waypoint-axes-transposed",
        R,
        "Waypoint(*(axis[i] for axis in axes))",
        "Waypoint(*(axis[i] for axis in reversed(axes)))",
        (T_SEC + "test_flight_group_fields", T_REN + "test_goals_and_points"),
    ),
    Plant(
        "fg-roles-sixteen-bytes",
        R,
        "fg.roles_text = decode_fixed(role_bytes)",
        "fg.roles_text = decode_fixed(role_bytes[:16])",
        (T_SEC + "test_role_text_reads_past_sixteen_bytes",),
    ),
    # messages
    Plant(
        "msg-swap-delay-joiner",
        R,
        "    m.delay = cur.u8()\n    m.triggers12_or_triggers34 = cur.u8()\n",
        "    m.triggers12_or_triggers34 = cur.u8()\n    m.delay = cur.u8()\n",
        (T_SEC + "test_message_fields", T_REN + "test_messages_print_stored_index"),
    ),
    Plant(
        "msg-text-63",
        R,
        "    m.message = cur.string(64)\n",
        "    m.message = cur.string(63)\n    cur.skip(1)\n",
        (T_SEC + "test_message_of_full_64_characters_is_kept_whole",),
    ),
    # global goals and teams
    Plant(
        "gg-points-unsigned",
        R,
        "        g.delay = cur.u8()\n        g.points = cur.s8()\n",
        "        g.delay = cur.u8()\n        g.points = cur.u8()\n",
        (T_SEC + "test_global_goal_fields",),
    ),
    Plant(
        "gg-limit-4",
        R,
        "MAX_GLOBAL_GOALS = 3",
        "MAX_GLOBAL_GOALS = 4",
        (T_REF + "test_global_goal_count_past_three_refused",),
    ),
    Plant(
        "team-name-24",
        R,
        "    t.name = cur.string(16)\n    t.unknown_12 = cur.u8s(8)\n",
        "    t.name = cur.string(24)\n    t.unknown_12 = [0] * 8\n",
        (T_SEC + "test_team_fields",),
    ),
    # briefings
    Plant(
        "brf-tail-not-trimmed",
        R,
        "    while tail and tail[-1] == 0:\n        tail.pop()\n",
        "",
        (
            T_SEC + "test_briefing_fields",
            T_EV + "test_walk_stops_at_end_and_keeps_tail",
        ),
    ),
    Plant(
        "brf-tags-strings-swapped",
        R,
        "((b.tags, BRIEFING_TAGS), (b.strings, BRIEFING_STRINGS))",
        "((b.strings, BRIEFING_STRINGS), (b.tags, BRIEFING_TAGS))",
        (T_SEC + "test_briefing_fields",),
    ),
    Plant(
        "brf-no-count-check",
        R,
        "        if not 0 <= value <= EVENT_AREA_SHORTS:",
        "        if False:",
        (T_REF + "test_briefing_counts_past_event_area_refused",),
    ),
    Plant(
        "brf-no-negative-length-check",
        R,
        "            if length < 0:",
        "            if False:",
        (T_REF + "test_negative_briefing_string_length_refused",),
    ),
    Plant(
        "evt-zoom-one-arg",
        R,
        "    0x07: 2,",
        "    0x07: 1,",
        (T_EV + "test_every_documented_type_takes_its_argument_count",),
    ),
    Plant(
        "evt-no-stop-at-end",
        R,
        "        if kind == END_BRIEFING:\n            complete = True\n            break\n",
        "        if kind == END_BRIEFING:\n            complete = True\n",
        (T_EV + "test_walk_stops_at_end_and_keeps_tail",),
    ),
    Plant(
        "evt-unknown-type-zero-args",
        R,
        "argc = EVENT_ARG_COUNTS.get(kind)",
        "argc = EVENT_ARG_COUNTS.get(kind, 0)",
        (T_EV + "test_unnamed_type_stops_walk_and_keeps_rest",),
    ),
    # string sections and descriptions
    Plant(
        "str-global-dims-swapped",
        R,
        "            [[cur.string(GOAL_STRING_SIZE) for _ in range(3)] for _ in range(4)]\n"
        "            for _ in range(3)\n",
        "            [[cur.string(GOAL_STRING_SIZE) for _ in range(4)] for _ in range(3)]\n"
        "            for _ in range(3)\n",
        (T_SEC + "test_goal_strings",),
    ),
    Plant(
        "desc-v12-4096",
        R,
        "        mission.descriptions = [cur.string(1024)]",
        "        mission.descriptions = [cur.string(4096)]",
        (T_SEC + "test_description_version_12_is_one_string",),
    ),
    Plant(
        "desc-v14-1024",
        R,
        "mission.descriptions = [cur.string(4096) for _ in range(3)]",
        "mission.descriptions = [cur.string(1024) for _ in range(3)]",
        (T_SEC + "test_descriptions_version_14_are_three_strings",),
    ),
    Plant(
        "desc-v14-read-as-v12",
        R,
        "    if header.platform_id == 12:",
        "    if True:",
        (T_REF + "test_version_14_file_cut_to_version_12_length_is_short",),
    ),
    Plant(
        "layout-briefings-late",
        R,
        "    layout.briefings = cur.offset\n",
        "    layout.briefings = cur.offset + 1\n",
        (T_SEC + "test_layout_offsets",),
    ),
    Plant(
        "read-refuses-trailing-bytes",
        R,
        "    if layout.end != len(data):\n",
        "    if layout.end != len(data):\n        raise ShortReadError('trailing')\n",
        (T_SEC + "test_bytes_after_last_section_are_ignored",),
    ),
    Plant(
        "read-path-loses-last-byte",
        R,
        "        data = Path(source).read_bytes()",
        "        data = Path(source).read_bytes()[:-1]",
        (T_SEC + "test_reads_from_a_path",),
    ),
    Plant(
        "exc-count-not-format-error",
        R,
        "class CountLimitError(MissionFormatError):",
        "class CountLimitError(Exception):",
        (T_REF + "test_all_refusals_share_one_base_class",),
    ),
    # structural checks
    Plant(
        "chk-no-end-time",
        C,
        "        elif last.time != END_TIME:",
        "        elif False:",
        (T_EV + "test_check_flags_wrong_end_time",),
    ),
    Plant(
        "chk-no-length",
        C,
        "    if _shorts(briefing) != briefing.events_length:",
        "    if False:",
        (T_EV + "test_check_flags_length_disagreement",),
    ),
    Plant(
        "chk-no-end-finding",
        C,
        '    if not briefing.events_complete or not briefing.events:\n        findings.append("no-end")',
        "    if not briefing.events_complete or not briefing.events:\n        pass",
        (T_EV + "test_check_flags_missing_end",),
    ),
    Plant(
        "chk-no-arg-count",
        C,
        "        elif expected != len(event.variables):",
        "        elif False:",
        (T_EV + "test_check_flags_argument_count_mismatch",),
    ),
    Plant(
        "chk-no-time-order",
        C,
        "    if any(later < earlier for earlier, later in zip(times, times[1:], strict=False)):",
        "    if False:",
        (T_EV + "test_check_flags_time_order",),
    ),
    Plant(
        "chk-string-slots-33",
        C,
        "STRING_SLOTS = 32",
        "STRING_SLOTS = 33",
        (
            T_EV + "test_check_flags_bad_references[event0]",
            T_EV + "test_check_flags_bad_references[event2]",
        ),
    ),
    Plant(
        "chk-negative-reference-ok",
        C,
        "        return 0 <= first < STRING_SLOTS",
        "        return first < STRING_SLOTS",
        (T_EV + "test_check_flags_bad_references[event1]",),
    ),
    Plant(
        "chk-fg-tag-unchecked",
        C,
        "    if event.type in FG_TAG_EVENTS and num_fgs is not None:",
        "    if False:",
        (T_EV + "test_check_flags_bad_references[event3]",),
    ),
    Plant(
        "chk-start-no-zero-reading",
        C,
        "    by_zero = zero == briefing.start_events",
        "    by_zero = False",
        (T_EV + "test_start_events_rule[1-5-time-zero]",),
    ),
    Plant(
        "chk-start-no-current-reading",
        C,
        "    by_current = first_at_current == briefing.start_events",
        "    by_current = False",
        (T_EV + "test_start_events_rule[30-8-current-time]",),
    ),
    Plant(
        "chk-start-zero-only-mislabelled",
        C,
        '        return "time-zero-only"',
        '        return "neither"',
        (T_EV + "test_start_events_rule[30-5-time-zero-only]",),
    ),
    Plant(
        "chk-start-neither-mislabelled",
        C,
        '    return "neither"',
        '    return "time-zero"',
        (T_EV + "test_start_events_rule[1-7-neither]",),
    ),
    # renderer
    Plant(
        "ren-legend",
        V,
        '"fallback is tried if primary finds no target."',
        '"fallback is tried if primary finds no target"',
        (T_REN + "test_header_and_legend",),
    ),
    Plant(
        "ren-no-backslash-escape",
        V,
        """text.replace("\\\\", "\\\\\\\\").replace""",
        """text.replace""",
        (T_REN + "test_flight_group_identity_lines",),
    ),
    Plant(
        "ren-clock-unpadded",
        V,
        'return f"{minutes}:{seconds:02d}"',
        'return f"{minutes}:{seconds}"',
        (T_REN + "test_arrival_departure_and_motherships",),
    ),
    Plant(
        "ren-no-not-fg-name",
        V,
        "FLIGHT_GROUP_VARIABLE_TYPES = (1, 15)",
        "FLIGHT_GROUP_VARIABLE_TYPES = (1,)",
        (T_REN + "test_orders_and_targets",),
    ),
    Plant(
        "ren-no-craftwhen-label",
        V,
        '    12: "Operational",\n',
        "",
        (T_REN + "test_orders_and_targets",),
    ),
    Plant(
        "ren-points-enabled-1-only",
        V,
        "            if p.enabled:",
        "            if p.enabled == 1:",
        (T_REN + "test_goals_and_points",),
    ),
    Plant(
        "ren-message-position",
        V,
        'f"Message[{msg.message_index}] {quote(msg.message)} "',
        'f"Message[{self.m.messages.index(msg)}] {quote(msg.message)} "',
        (T_REN + "test_messages_print_stored_index",),
    ),
    Plant(
        "ren-strings-before-tags",
        V,
        "            for k, text in enumerate(brief.tags):\n"
        "                if text:\n"
        '                    self.out(f"Briefing[{b}] Label[{k}] {quote(text)}")\n',
        "            for k, text in enumerate(brief.tags[::-1]):\n"
        "                if text:\n"
        '                    self.out(f"Briefing[{b}] Label[{k}] {quote(text)}")\n',
        (T_REN + "test_global_goals_teams_briefings_and_goal_strings",),
    ),
    Plant(
        "ren-blank-before-first-team",
        V,
        "            if t:\n                self.out()\n",
        "            self.out()\n",
        (T_REN + "test_empty_mission_renders_whole_text_both_versions",),
    ),
    # JSON export and schema
    Plant(
        "json-name-key-renamed",
        J,
        'out[f.name + "_name"] = _name(f.metadata["enum"], raw)',
        'out[f.name + "_label"] = _name(f.metadata["enum"], raw)',
        (
            T_JS + "test_export_validates_against_schema",
            T_JS + "test_empty_mission_validates",
            T_JS + "test_raw_values_kept_with_document_names",
        ),
    ),
    Plant(
        "json-team-case-sensitive",
        N,
        "    if code in DESIGNATIONTEAM:\n        return DESIGNATIONTEAM[code]\n",
        "    return DESIGNATIONTEAM.get(code)\n",
        (T_JS + "test_raw_values_kept_with_document_names",),
    ),
    Plant(
        "schema-drift-name-size",
        M,
        "    name: str = _text(20)\n    roles:",
        "    name: str = _text(21)\n    roles:",
        (T_JS + "test_schema_file_matches_model",),
    ),
    Plant(
        "schema-no-int-range",
        J,
        '    return {"type": "integer", "minimum": low, "maximum": high}',
        '    return {"type": "integer"}',
        (
            T_JS + "test_schema_rejects_bad_data[path0-256]",
            T_JS + "test_schema_rejects_bad_data[path2-200]",
        ),
        regen_schema=True,
    ),
    Plant(
        "schema-int-any-type",
        J,
        '    if hint is int:\n        return _int_schema(meta.get("int"))',
        "    if hint is int:\n        return {}",
        (T_JS + "test_schema_rejects_bad_data[path1-2]",),
        regen_schema=True,
    ),
    Plant(
        "schema-no-max-length",
        J,
        '        schema["maxLength"] = size',
        "        pass",
        (T_JS + "test_schema_rejects_bad_data[path3-xxxxxxxxxxxxxxxxxxxxx]",),
        regen_schema=True,
    ),
    Plant(
        "schema-no-array-length",
        J,
        '        schema["minItems"] = schema["maxItems"] = count',
        "        pass",
        (T_JS + "test_schema_rejects_bad_data[path4-value4]",),
        regen_schema=True,
    ),
    Plant(
        "schema-extra-fields-allowed",
        J,
        '        "additionalProperties": False,',
        '        "additionalProperties": True,',
        (T_JS + "test_schema_rejects_bad_data[path5-1]",),
        regen_schema=True,
    ),
    Plant(
        "schema-bool-any-type",
        J,
        '    if hint is bool:\n        return {"type": "boolean"}',
        "    if hint is bool:\n        return {}",
        (T_JS + "test_schema_rejects_bad_data[path6-yes]",),
        regen_schema=True,
    ),
    # install and the path rule
    Plant(
        "ins-no-backslash",
        I,
        'text = game_path.replace("\\\\", "/")',
        "text = game_path",
        (T_INS + "test_backslashes_are_separators",),
    ),
    Plant(
        "ins-allow-dotdot",
        I,
        '    if ".." in parts:',
        "    if False:",
        (
            T_INS + "test_paths_leaving_the_install_are_refused[..\\\\x.tie]",
            T_INS + "test_paths_leaving_the_install_are_refused[TRAIN/../../x]",
        ),
    ),
    Plant(
        "ins-allow-absolute",
        I,
        '    if text.startswith("/") or re.match(r"^[A-Za-z]:", text):',
        "    if False:",
        (
            T_INS + "test_paths_leaving_the_install_are_refused[\\\\TRAIN\\\\x]",
            T_INS + "test_paths_leaving_the_install_are_refused[/etc/x]",
            T_INS + "test_paths_leaving_the_install_are_refused[C:\\\\x.tie]",
        ),
    ),
    Plant(
        "ins-install-before-bop",
        I,
        "        bases.append(bop)\n    bases.append(install)\n",
        "        bases.append(bop)\n    bases.insert(0, install)\n",
        (T_INS + "test_balance_of_power_is_searched_first",),
    ),
    Plant(
        "ins-no-install-fallback",
        I,
        "        bases.append(bop)\n    bases.append(install)\n",
        "        bases.append(bop)\n",
        (T_INS + "test_install_folder_is_the_fallback",),
    ),
    Plant(
        "ins-exact-case-only",
        I,
        "    wanted = name.casefold()\n",
        "    return None\n    wanted = name.casefold()\n",
        (T_INS + "test_every_part_matches_in_any_case",),
    ),
    Plant(
        "ins-missing-part-kept",
        I,
        "        if found is None:\n            return None\n        current = found\n",
        "        current = found if found is not None else current / part\n",
        (T_INS + "test_missing_paths_resolve_to_none",),
    ),
    Plant(
        "ins-no-exact-preference",
        I,
        "    if exact.exists():\n        return exact\n",
        "",
        (T_INS + "test_exact_case_wins_over_other_case",),
    ),
    Plant(
        "ins-any-folder-is-install",
        I,
        "    return train is not None and train.is_dir()",
        "    return True",
        (T_INS + "test_is_install_and_find_install",),
    ),
    Plant(
        "ins-no-windows-steam",
        I,
        '        Path("C:/Program Files (x86)/Steam"),\n',
        "",
        (T_INS + "test_candidate_folders_cover_steam_and_gog_on_both_systems",),
    ),
    Plant(
        "ins-battle-listed",
        I,
        'MISSION_FOLDERS = ("Train", "Combat", "Melee")',
        'MISSION_FOLDERS = ("Train", "Combat", "Melee", "Battle")',
        (T_INS + "test_list_missions_by_folder",),
    ),
    # command line
    Plant(
        "cli-dump-extra-newline",
        CLI,
        "    sys.stdout.write(render_mission(mission))",
        "    print(render_mission(mission))",
        (T_CLI + "test_dump_by_path",),
    ),
    Plant(
        "cli-dump-no-game-path",
        CLI,
        "resolved = resolve_game_path(install, args.mission) if install else None",
        "resolved = None",
        (T_CLI + "test_dump_by_game_path",),
    ),
    Plant(
        "cli-dump-bad-file-ok",
        CLI,
        '        logger.error("cannot read %s: %s", path, exc)\n        return 1',
        '        logger.error("cannot read %s: %s", path, exc)\n        return 0',
        (T_CLI + "test_dump_refusals",),
    ),
    Plant(
        "cli-export-file-name",
        CLI,
        'target = out / folder / (path.stem + ".json")',
        'target = out / folder / (path.name + ".json")',
        (T_CLI + "test_export_writes_one_json_per_mission",),
    ),
    Plant(
        "cli-export-no-install-ok",
        CLI,
        '        logger.error("not an install: %s", args.install)\n        return 2',
        '        logger.error("not an install: %s", args.install)\n        return 0',
        (T_CLI + "test_export_needs_an_install",),
    ),
]
