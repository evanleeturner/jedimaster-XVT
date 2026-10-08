"""Plant faults in the code, one at a time, and prove the tests catch each.

Purpose:
    A test counts only after it has failed on a planted fault. Each plant
    below breaks one piece of code a test guards; the runner applies it, runs
    the whole suite, records which tests failed, and restores the file.

Flow:
    1. Snapshot every planted file; run the suite once: it must pass.
    2. For each plant: check its ``old`` text occurs exactly once, write
       ``new`` in its place (regenerating the schema file when the plant
       touches the schema generator), run the suite, restore the file (and
       the schema), and check that each test the plant names failed.
    3. Report each plant, the tests it failed, and any collected test that no
       plant made fail; run the suite again: it must pass, and every file
       must match its snapshot byte for byte.

Invariants:
    - Files are always restored, also on error or Ctrl-C (``finally``).
    - Exit status 0 only when every plant failed its named tests, every
      collected test failed under some plant, and the tree is restored.

Call:
    ``python tools/plant_faults.py [--only ID ...] [--markdown FILE]``
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
# A planted file and its restored original often have the same size and the
# same mtime second, which would let Python load a stale .pyc of the other
# version; so no bytecode is written or kept while plants run.
ENV = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")


def clear_bytecode() -> None:
    """Delete every __pycache__ folder under the project."""
    for cache in ROOT.rglob("__pycache__"):
        shutil.rmtree(cache, ignore_errors=True)


logger = logging.getLogger("plant_faults")


@dataclass(frozen=True)
class Plant:
    """One fault: replace ``old`` by ``new`` in ``path``; ``expect`` must fail."""

    id: str
    path: str
    old: str
    new: str
    expect: tuple[str, ...]
    regen_schema: bool = False


R = "jedimaster/mission/reader.py"
B = "jedimaster/mission/binary.py"
V = "jedimaster/mission/render.py"
J = "jedimaster/mission/to_json.py"
C = "jedimaster/mission/checks.py"
N = "jedimaster/mission/names.py"
M = "jedimaster/mission/model.py"
I = "jedimaster/install.py"  # noqa: E741
CLI = "jedimaster/__main__.py"

T_BIN = "tests/test_binary.py::"
T_SEC = "tests/test_reader_sections.py::"
T_REF = "tests/test_refusals.py::"
T_EV = "tests/test_events.py::"
T_REN = "tests/test_render.py::"
T_JS = "tests/test_json_schema.py::"
T_INS = "tests/test_install.py::"
T_CLI = "tests/test_cli.py::"
S = "jedimaster/setup.py"
SS = "schema/setup.schema.json"
T_SET = "tests/test_setup.py::"

PLANTS: list[Plant] = [
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
    # The lobby's setup document: jedimaster/setup.py and its schema.
    Plant(
        "setup-default-limit",
        S,
        "MISSION_TIME_LIMIT_DEFAULT = 255",
        "MISSION_TIME_LIMIT_DEFAULT = 254",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-ping-fingerprinted",
        S,
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms")',
        'PLAYER_UNFINGERPRINTED = ("ready",)',
        (T_SET + "test_fingerprint_ignores_order_revision_ready_ping_and_launch",),
    ),
    Plant(
        "setup-build-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "build")',
        (T_SET + "test_fingerprint_changes_with_the_battle",),
    ),
    Plant(
        "setup-easy-cheat-value",
        S,
        '"easy_cheat": 3}',
        '"easy_cheat": 2}',
        (T_SET + "test_engine_value_edges",),
    ),
    Plant(
        "setup-battle-length-value",
        S,
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 2}",
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 2, 4: 2}",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-keys-unsorted",
        S,
        "sort_keys=True",
        "sort_keys=False",
        (T_SET + "test_fingerprint_ignores_order_revision_ready_ping_and_launch",),
    ),
    Plant(
        "setup-shallow-view",
        S,
        "view = copy.deepcopy(document)",
        "view = copy.copy(document)",
        (T_SET + "test_fingerprint_does_not_change_the_document",),
    ),
    Plant(
        "setup-boolean-inverted",
        S,
        "int(bool(settings[name]))",
        "int(not settings[name])",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-last-team-none",
        S,
        'LAST_TEAM_TIME_LIMIT_NONE if last == "none"',
        '1 if last == "none"',
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-unknown-name-accepted",
        S,
        "    if value not in table:\n        raise ValueError",
        "    if False:\n        raise ValueError",
        (T_SET + "test_engine_values_refuse_an_unknown_name",),
    ),
    Plant(
        "setup-schema-mission-limit",
        SS,
        '"minimum": 1, "maximum": 20}',
        '"minimum": 1, "maximum": 21}',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-last-team-limit",
        SS,
        '"minimum": 1, "maximum": 10}',
        '"minimum": 1, "maximum": 20}',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-ninth-player",
        SS,
        '"maxItems": 8,\n      "items": {\n        "type": "object"',
        '"maxItems": 9,\n      "items": {\n        "type": "object"',
        (T_SET + "test_unknown_setting_and_ninth_player_are_refused",),
    ),
    Plant(
        "setup-schema-update-rate",
        SS,
        '"enum": [4, 6, 8]',
        '"enum": [4, 5, 6, 8]',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-path-slashes",
        SS,
        '"pattern": "^[^/\\\\\\\\][^\\\\\\\\]*\\\\.(tie|TIE)$"',
        '"pattern": "^.*$"',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-invalid",
        SS,
        '  "type": "object",\n  "additionalProperties": false,\n  "required": ["schema_version"',
        '  "type": "objectt",\n  "additionalProperties": false,\n  "required": ["schema_version"',
        (T_SET + "test_schema_is_a_valid_schema",),
    ),
    Plant(
        "setup-schema-no-medium",
        SS,
        '"enum": ["easy", "medium", "hard", "easy_cheat"]',
        '"enum": ["easy", "hard", "easy_cheat"]',
        (T_SET + "test_sample_is_valid",),
    ),
    Plant(
        "setup-schema-zero-minutes",
        SS,
        '"minimum": 1, "maximum": 20}',
        '"minimum": 0, "maximum": 20}',
        (T_SET + "test_out_of_range_values_are_refused[path1-0]",),
    ),
    Plant(
        "setup-schema-last-team-default",
        SS,
        '"oneOf": [{"const": "none"}',
        '"oneOf": [{"enum": ["none", "default"]}',
        (T_SET + "test_out_of_range_values_are_refused[path3-default]",),
    ),
    Plant(
        "setup-schema-one-win",
        SS,
        '"enum": [2, 3, 4]',
        '"enum": [1, 2, 3, 4]',
        (T_SET + "test_out_of_range_values_are_refused[path5-1]",),
    ),
    Plant(
        "setup-schema-insane",
        SS,
        '"enum": ["easy", "medium", "hard", "easy_cheat"]',
        '"enum": ["easy", "medium", "hard", "easy_cheat", "insane"]',
        (T_SET + "test_out_of_range_values_are_refused[path6-insane]",),
    ),
    Plant(
        "setup-schema-seed-wide",
        SS,
        '"maximum": 4294967295',
        '"maximum": 4294967296',
        (T_SET + "test_out_of_range_values_are_refused[path7-4294967296]",),
    ),
    Plant(
        "setup-schema-rules-1998",
        SS,
        '"enum": ["1997", "fixed"]',
        '"enum": ["1997", "1998", "fixed"]',
        (T_SET + "test_out_of_range_values_are_refused[path8-1998]",),
    ),
    Plant(
        "setup-schema-version-2",
        SS,
        '"schema_version": {"const": 1}',
        '"schema_version": {"enum": [1, 2]}',
        (T_SET + "test_out_of_range_values_are_refused[path10-2]",),
    ),
    Plant(
        "setup-settings-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "settings")',
        (
            T_SET + "test_fingerprint_changes_with_the_battle[path0-False]",
            T_SET + "test_fingerprint_changes_with_the_battle[path1-4243]",
        ),
    ),
    Plant(
        "setup-engine-options-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "engine_options")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path2-fixed]",),
    ),
    Plant(
        "setup-mission-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "mission")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path3-",),
    ),
    Plant(
        "setup-teams-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "teams")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path6-",),
    ),
    Plant(
        "setup-flight-group-unfingerprinted",
        S,
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms")',
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms", "flight_group")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path5-2]",),
    ),
    Plant(
        "setup-none-limit",
        S,
        "MISSION_TIME_LIMIT_NONE = 0",
        "MISSION_TIME_LIMIT_NONE = 1",
        (T_SET + "test_engine_value_edges[mission_time_limit-none-0]",),
    ),
    Plant(
        "setup-minutes-wrapped",
        S,
        'values["mission_time_limit"] = int(limit)',
        'values["mission_time_limit"] = int(limit) % 20',
        (T_SET + "test_engine_value_edges[mission_time_limit-20-20]",),
    ),
    Plant(
        "setup-last-team-wrapped",
        S,
        '"none" else int(last)',
        '"none" else int(last) % 10',
        (T_SET + "test_engine_value_edges[last_team_time_limit-10-10]",),
    ),
    Plant(
        "setup-four-wins",
        S,
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 2}",
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 1}",
        (T_SET + "test_engine_value_edges[battle_length-4-2]",),
    ),
    Plant(
        "setup-host-only",
        S,
        '"host_only": 2}',
        '"host_only": 1}',
        (T_SET + "test_engine_value_edges[craft_selection-host_only-2]",),
    ),
    Plant(
        "setup-unlimited",
        S,
        '"unlimited": 2}',
        '"unlimited": 1}',
        (T_SET + "test_engine_value_edges[craft_waves-unlimited-2]",),
    ),
    Plant(
        "setup-favor-rebel",
        S,
        '"favor_rebel": 3,',
        '"favor_rebel": 2,',
        (T_SET + "test_engine_value_edges[combat_balance-favor_rebel-3]",),
    ),
    Plant(
        "setup-continue",
        S,
        '{"restart": 0, "continue": 1}',
        '{"restart": 0, "continue": 0}',
        (T_SET + "test_engine_value_edges[continue_sequence-continue-1]",),
    ),
]


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_suite() -> tuple[int, set[str]]:
    """Run pytest on the whole suite; return (exit code, failed test ids)."""
    proc = subprocess.run(
        [PYTHON, "-m", "pytest", "-q", "-p", "no:cacheprovider", "-rfE"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        env=ENV,
    )
    failed = set()
    for line in proc.stdout.splitlines():
        if line.startswith(("FAILED tests/", "ERROR tests/")):
            node = line.split(" ", 1)[1].split(" - ")[0].strip()
            failed.add(node)
    return proc.returncode, failed


def collect() -> list[str]:
    """Return every collected test id."""
    proc = subprocess.run(
        [PYTHON, "-m", "pytest", "--collect-only", "-q", "-p", "no:cacheprovider"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
        env=ENV,
    )
    return [line for line in proc.stdout.splitlines() if "::" in line]


def regenerate_schema() -> None:
    """Rewrite schema/mission.schema.json from the (possibly planted) model."""
    subprocess.run(
        [PYTHON, "tools/gen_schema.py"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        env=ENV,
    )


def apply(plant: Plant) -> set[str]:
    """Plant one fault, run the suite, restore; return the failed test ids."""
    path = ROOT / plant.path
    original = path.read_text(encoding="utf-8")
    count = original.count(plant.old)
    if count != 1:
        raise RuntimeError(f"{plant.id}: old text found {count} times in {plant.path}")
    schema = ROOT / "schema" / "mission.schema.json"
    schema_before = schema.read_bytes()
    try:
        path.write_text(original.replace(plant.old, plant.new), encoding="utf-8")
        if plant.regen_schema:
            regenerate_schema()
        _, failed = run_suite()
    finally:
        path.write_text(original, encoding="utf-8")
        schema.write_bytes(schema_before)
    return failed


def main(argv: list[str] | None = None) -> int:
    """Run every plant; return 0 when each was caught and the tree restored."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", nargs="*", help="plant ids to run")
    parser.add_argument("--markdown", help="also write a markdown table here")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    plants = [p for p in PLANTS if not args.only or p.id in args.only]
    files = sorted(
        {ROOT / p.path for p in PLANTS} | {ROOT / "schema/mission.schema.json"}
    )
    before = {f: _sha(f) for f in files}
    clear_bytecode()
    code, failed = run_suite()
    if code != 0:
        print(f"baseline suite is not green: {sorted(failed)}")
        return 1
    tests = collect()
    caught_all = True
    covered: set[str] = set()
    rows = []
    for plant in plants:
        failed = apply(plant)
        covered |= failed
        missing = [e for e in plant.expect if not any(f.startswith(e) for f in failed)]
        status = "CAUGHT" if not missing and failed else "MISSED"
        caught_all &= status == "CAUGHT"
        rows.append((plant, status, sorted(failed), missing))
        logger.info("%s %s: %d tests failed", status, plant.id, len(failed))
        for name in missing:
            logger.info("    expected failure did not happen: %s", name)
    code, failed = run_suite()
    restored = all(_sha(f) == before[f] for f in files)
    uncovered = [t for t in tests if t not in covered] if not args.only else []
    print(f"plants: {len(plants)}, caught: {sum(r[1] == 'CAUGHT' for r in rows)}")
    caught_tests = len(set(tests) & covered)
    print(f"tests collected: {len(tests)}, failed under some plant: {caught_tests}")
    for test in uncovered:
        print(f"  never failed: {test}")
    print(f"suite green after restore: {code == 0}; files restored: {restored}")
    if args.markdown:
        lines = [
            "| plant | file | tests that failed |",
            "| --- | --- | --- |",
        ]
        for plant, status, names, _ in rows:
            short = ", ".join(n.split("::", 1)[-1] for n in names)
            lines.append(f"| {plant.id} ({status}) | {plant.path} | {short} |")
        Path(args.markdown).write_text("\n".join(lines) + "\n", encoding="utf-8")
    ok = caught_all and not uncovered and code == 0 and restored
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
