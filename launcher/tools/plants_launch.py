"""The planted faults of the launch package, its schemas and the comparison tool.

Purpose:
    Break one piece of ``jedimaster/launch/`` or ``tools/compare_launch.py``
    per plant, so each test of the launch command, the two schemas and the
    comparison tool is shown to fail on a fault in the code it guards.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order, and
    applies each plant alone: write ``new`` over ``old`` in ``path``, run the
    suite, restore, and check that every test in ``expect`` failed. A plant
    in a schema's model sets ``regen_schema`` so the published schema file
    follows the fault and only the behaviour tests can catch it.

Invariants:
    - Ids are unique across all tables and start with ``launch-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_launch import LAUNCH_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

M = "jedimaster/launch/model.py"
S = "jedimaster/launch/schema.py"
K = "jedimaster/launch/check.py"
R = "jedimaster/launch/run.py"
C = "jedimaster/launch/cli.py"
X = "tools/compare_launch.py"
T_CMD = "tests/test_launch_command.py::"
T_SCH = "tests/test_launch_schema.py::"
T_CMP = "tests/test_compare_launch.py::"
GOOD = T_SCH + "test_good_descriptions_fit_both_validators"
BAD = T_SCH + "test_bad_descriptions_are_refused_by_both_validators"
BAD_RESULT = T_SCH + "test_bad_results_are_refused_by_both_validators"
CHECKER = T_SCH + "test_the_checker_counts_a_bool_as_no_integer"

LAUNCH_PLANTS: list[Plant] = [
    Plant(
        "launch-mode",
        M,
        'MODE = "single_player"',
        'MODE = "single"',
        (GOOD, T_CMD + "test_the_description_is_built_from_the_arguments"),
    ),
    Plant(
        "launch-version",
        M,
        "SCHEMA_VERSION = 1",
        "SCHEMA_VERSION = 2",
        (T_CMD + "test_the_description_is_built_from_the_arguments",),
    ),
    Plant(
        "launch-folder-tourn",
        M,
        '"tournament": "tourn",',
        '"tournament": "tournament",',
        (T_CMD + "test_each_game_type_has_the_engines_folder",),
    ),
    Plant(
        "launch-folder-train",
        M,
        '"training": "train",',
        '"training": "training",',
        (T_CMD + "test_each_game_type_has_the_engines_folder",),
    ),
    Plant(
        "launch-sided",
        M,
        'SIDED_TYPES = ("training", "combat", "campaign")',
        'SIDED_TYPES = ("training", "combat")',
        (T_CMD + "test_a_description_that_does_not_fit_is_refused",),
    ),
    Plant(
        "launch-side-refused",
        M,
        '    elif side not in (None, "none"):',
        "    elif False:",
        (T_CMD + "test_a_description_that_does_not_fit_is_refused",),
    ),
    Plant(
        "launch-folder-check",
        M,
        'if path.split("/", 1)[0].lower() != folder:',
        'if path.split("/", 1)[0].lower() == folder:',
        (T_CMD + "test_a_description_that_does_not_fit_is_refused",),
    ),
    Plant(
        "launch-side-none",
        M,
        '"side": side if game_type in SIDED_TYPES else "none",',
        '"side": side,',
        (T_CMD + "test_the_description_is_built_from_the_arguments",),
    ),
    Plant(
        "launch-seed-zero",
        M,
        "if seed is not None:",
        "if seed:",
        (T_CMD + "test_a_seed_of_zero_is_kept",),
    ),
    Plant(
        "launch-settings-kept",
        M,
        "    if settings:\n",
        "    if False:\n",
        (T_CMD + "test_a_seed_of_zero_is_kept",),
    ),
    Plant("launch-exit-codes", M, '"failed": 5}', '"failed": 6}', (BAD_RESULT,), True),
    Plant(
        "launch-schema-seed-max",
        S,
        '"maximum": model.SEED_MAXIMUM,',
        '"maximum": model.SEED_MAXIMUM + 1,',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-pilot-length",
        S,
        '"maxLength": 12,',
        '"maxLength": 13,',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-side-required",
        S,
        '            "side",\n            "mission",',
        '            "mission",',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-settings-open",
        S,
        '"additionalProperties": False,\n        "properties": dict(sorted(properties.items())),',
        '"additionalProperties": True,\n        "properties": dict(sorted(properties.items())),',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-battle-length",
        S,
        '"battle_length": {"enum": list(model.BATTLE_LENGTHS)},',
        '"battle_length": {"enum": list(model.BATTLE_LENGTHS) + [5]},',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-time-minimum",
        S,
        '"minimum": model.TIME_LIMIT_MINUTES[0],',
        '"minimum": 0,',
        (BAD,),
        True,
    ),
    Plant(
        "launch-schema-exit-enum",
        S,
        '"enum": sorted(model.EXIT_CODES.values()),',
        '"enum": sorted(model.EXIT_CODES.values()) + [1],',
        (BAD_RESULT,),
        True,
    ),
    Plant(
        "launch-schema-number-minimum",
        S,
        'name: {"type": "integer", "minimum": 0}',
        'name: {"type": "integer"}',
        (BAD_RESULT,),
        True,
    ),
    Plant(
        "launch-check-minimum",
        K,
        'document < schema["minimum"]',
        'document <= schema["minimum"]',
        (GOOD,),
    ),
    Plant(
        "launch-check-maximum",
        K,
        'document > schema["maximum"]',
        'document >= schema["maximum"]',
        (GOOD,),
    ),
    Plant(
        "launch-check-bool",
        K,
        '    if name == "integer" and isinstance(value, bool):\n        return False',
        '    if name == "integer" and isinstance(value, bool):\n        return True',
        (CHECKER,),
    ),
    Plant(
        "launch-check-min-length",
        K,
        'len(document) < schema["minLength"]',
        'len(document) <= schema["minLength"]',
        (CHECKER,),
    ),
    Plant(
        "launch-check-max-length",
        K,
        'len(document) > schema["maxLength"]',
        'len(document) >= schema["maxLength"]',
        (CHECKER,),
    ),
    Plant(
        "launch-check-one-of",
        K,
        "if len(fitting) != 1:",
        "if len(fitting) < 1:",
        (CHECKER,),
    ),
    Plant(
        "launch-check-unknown",
        K,
        "    if unknown:\n",
        "    if False:\n",
        (T_SCH + "test_the_checker_refuses_a_keyword_it_does_not_know",),
    ),
    Plant(
        "launch-check-closed",
        K,
        'elif schema.get("additionalProperties") is False:',
        "elif False:",
        (BAD,),
    ),
    Plant(
        "launch-check-required",
        K,
        "        if name not in document\n",
        "        if name in document\n",
        (GOOD,),
    ),
    Plant(
        "launch-check-pattern",
        K,
        'not re.search(schema["pattern"], document)',
        're.search(schema["pattern"], document)',
        (GOOD,),
    ),
    Plant(
        "launch-check-enum",
        K,
        'document not in schema["enum"]',
        'document in schema["enum"]',
        (GOOD,),
    ),
    Plant(
        "launch-run-extra",
        "jedimaster/launch/run.py",
        'command = [engine, "--launch", str(description_path), *extra]\n        logger.info("starting %s", command)\n        try:\n            done',
        'command = [engine, "--launch", str(description_path)]\n        logger.info("starting %s", command)\n        try:\n            done',
        ("tests/test_launch_command.py::test_the_engine_sees_the_arguments_in_order",),
    ),
    Plant(
        "launch-run-flag",
        "jedimaster/launch/run.py",
        'command = [engine, "--launch", str(description_path), *extra]\n        logger.info("starting %s", command)\n        try:\n            done',
        'command = [engine, "--run", str(description_path), *extra]\n        logger.info("starting %s", command)\n        try:\n            done',
        ("tests/test_launch_command.py::test_the_engine_sees_the_arguments_in_order",),
    ),
    Plant(
        "launch-run-result-path",
        R,
        "written = dict(description, result=str(result_path))",
        "written = dict(description)",
        (T_CMD + "test_the_engine_sees_the_arguments_in_order",),
    ),
    Plant(
        "launch-run-beside",
        R,
        "if not result_path.exists() and beside.exists():",
        "if False:",
        (T_CMD + "test_a_refusal_written_beside_the_description_is_read",),
    ),
    Plant(
        "launch-run-stale",
        R,
        "result_path.unlink(missing_ok=True)",
        "None",
        (T_CMD + "test_a_result_left_by_an_earlier_run_is_never_read",),
    ),
    Plant(
        "launch-run-timeout",
        R,
        "subprocess.run(command, timeout=timeout, check=False)",
        "subprocess.run(command, check=False)",
        (T_CMD + "test_an_engine_that_runs_too_long_is_stopped",),
    ),
    Plant(
        "launch-run-result-check",
        R,
        'errors(document, result_schema(), "the result")',
        "[]",
        (T_CMD + "test_a_result_that_cannot_be_trusted_exits_one",),
    ),
    Plant(
        "launch-run-description-check",
        R,
        'errors(written, launch_schema(), "the description")',
        "[]",
        (T_CMD + "test_a_description_that_breaks_its_schema_never_reaches_the_engine",),
    ),
    Plant(
        "launch-cli-mismatch",
        C,
        'if code != result["exit_code"]:',
        "if False:",
        (T_CMD + "test_a_result_that_cannot_be_trusted_exits_one",),
    ),
    Plant(
        "launch-cli-code",
        C,
        "    return code\n",
        "    return 0\n",
        (T_CMD + "test_the_engines_exit_code_is_the_commands",),
    ),
    Plant(
        "launch-cli-bad-arguments",
        C,
        "        return 2\n    try:\n        result, code",
        "        return 1\n    try:\n        result, code",
        (T_CMD + "test_bad_arguments_exit_two",),
    ),
    Plant(
        "launch-cli-outcome-words",
        C,
        "result['outcome'].replace('_', ' ')",
        "result['outcome']",
        (T_CMD + "test_the_summary_line_for_each_status",),
    ),
    Plant(
        "launch-cli-status-words",
        C,
        "{result['status'].replace('_', ' ')}: {result['why']}",
        "{result['status']}: {result['why']}",
        (T_CMD + "test_the_summary_line_for_each_status",),
    ),
    Plant(
        "launch-cli-flown",
        C,
        'if result["status"] == "flown":',
        'if result["status"] != "flown":',
        (T_CMD + "test_the_summary_line_for_each_status",),
    ),
    Plant(
        "launch-compare-equal",
        X,
        "if name in ignore or left.get(name) == right.get(name):",
        "if name in ignore:",
        (T_CMP + "test_identical_recordings_have_no_difference_and_exit_zero",),
    ),
    Plant(
        "launch-compare-unnamed",
        X,
        "if differing and not named and not ignore:",
        "if differing and not ignore:",
        (T_CMP + "test_a_differing_pilot_member_is_named_by_the_layout",),
    ),
    Plant(
        "launch-compare-exit",
        X,
        "return 1 if differences else 0",
        "return 0",
        (T_CMP + "test_each_differing_state_value_is_named_with_both_values",),
    ),
    Plant(
        "launch-compare-ignore",
        X,
        "        if name in ignore:\n            continue",
        "        if False:\n            continue",
        (T_CMP + "test_an_ignored_name_is_left_out",),
    ),
    Plant(
        "launch-compare-state",
        X,
        "elif a[name] != b[name]:",
        "elif a[name] == b[name]:",
        (T_CMP + "test_each_differing_state_value_is_named_with_both_values",),
    ),
    Plant(
        "launch-compare-size",
        X,
        "if len(a) != size or len(b) != size:",
        "if len(a) != size and len(b) != size:",
        (T_CMP + "test_a_pilot_file_of_the_wrong_size_is_a_difference",),
    ),
    Plant(
        "launch-compare-side",
        X,
        'side = "menus" if name in a else "door"',
        'side = "door" if name in a else "menus"',
        (T_CMP + "test_a_line_only_one_side_holds_is_a_difference",),
    ),
    Plant(
        "launch-compare-cut",
        X,
        'return value if len(value) <= 60 else value[:57] + "..."',
        "return value",
        (T_CMP + "test_a_long_value_is_cut",),
    ),
    Plant(
        "launch-compare-raw",
        X,
        "values[parts[0]] = raw",
        "values[parts[0]] = raw[::-1]",
        (T_CMP + "test_a_state_line_is_name_size_and_hex",),
    ),
    Plant(
        "launch-compare-length",
        X,
        "if len(raw) != int(parts[1]):",
        "if False:",
        (T_CMP + "test_a_bad_state_line_is_a_difference_not_a_crash",),
    ),
    Plant(
        "launch-compare-text",
        X,
        "if text and all(32 <= byte < 127 for byte in text) and not any(raw[len(text) :]):",
        "if False:",
        (T_CMP + "test_a_value_reads_as_text_a_number_or_hex",),
    ),
]
