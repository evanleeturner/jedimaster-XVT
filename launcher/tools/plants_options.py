"""The planted faults of the held game options and the pilot the page names.

Purpose:
    Break one rule of V4 per plant: the default of each option, the
    description that sends all seven, the options the bot reads and sets,
    the Discord menu (marked choice, choices, help text), the pilot the
    ``status`` push names and the ``pilot.changed`` push, and the engine
    encoding the defaults fence reads.

Flow:
    ``plant_faults`` joins this table with the others and applies each
    plant alone: write ``new`` over ``old`` in ``path``, run the suite,
    restore, and check that every test in ``expect`` failed. An ``expect``
    entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``opt-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_options import OPTIONS_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

OPT = "jedimaster/launch/options.py"
WLK = "jedimaster/bot/walk.py"
PWK = "jedimaster/bot/pilot_walk.py"
COR = "jedimaster/bot/core.py"
LNG = "jedimaster/page/launching.py"
PRO = "jedimaster/page/protocol.py"
CTL = "jedimaster/page/control.py"
TD = "tests/test_launch_option_defaults.py::"
TO = "tests/test_launch_options.py::"
TW = "tests/test_bot_walk.py::"
TP = "tests/test_bot_pilot.py::"
TB = "tests/test_bot_page.py::"
TL = "tests/test_page_launching.py::"
TS = "tests/test_page_schema.py::"
DEFAULT_TEST = TD + "test_the_launcher_default_is_the_engines_starting_value"


def plant(
    pid: str, path: str, old: str, new: str, *expect: str, regen: bool = False
) -> Plant:
    """Return the plant ``opt-<pid>``."""
    return Plant(f"opt-{pid}", path, old, new, expect, regen)


OPTIONS_PLANTS: list[Plant] = [
    plant(
        "default-difficulty",
        OPT,
        '            "medium",\n        ),',
        '            "hard",\n        ),',
        DEFAULT_TEST + "[difficulty]",
        TO + "test_each_option_has_its_default[difficulty",
    ),
    plant(
        "default-battle-length",
        OPT,
        '            "3",\n        ),',
        '            "2",\n        ),',
        DEFAULT_TEST + "[battle_length]",
        TO + "test_each_option_has_its_default[battle_length",
    ),
    plant(
        "default-mission-time-limit",
        OPT,
        '            _minutes(),\n            "default",',
        '            _minutes(),\n            "none",',
        DEFAULT_TEST + "[mission_time_limit]",
        TO + "test_each_option_has_its_default[mission_time_limit",
    ),
    plant(
        "default-continue-sequence",
        OPT,
        '            "continue",\n        ),\n        OptionSpec(\n            "sith",',
        '            "restart",\n        ),\n        OptionSpec(\n            "sith",',
        DEFAULT_TEST + "[continue_sequence]",
        TO + "test_each_option_has_its_default[continue_sequence",
    ),
    plant(
        "for-description-skips-unset",
        OPT,
        "    settings = {name: parse_value(name, spec.default) for name, spec in OPTIONS.items()}\n    settings.update(options)",
        "    settings = dict(options)",
        TO + "test_every_description_holds_all_eight_options",
        TO + "test_a_description_with_the_defaults",
    ),
    plant(
        "get-unset-is-null",
        OPT,
        "    return OPTIONS[name].default\n",
        "    return None\n",
        TL + "test_options_start_at_their_defaults",
        TL + "test_options_get_with_nobody_linked_returns_the_eight_defaults",
    ),
    plant(
        "set-takes-null-schema",
        PRO,
        "    name: OptionName\n    value: str\n",
        "    name: OptionName\n    value: str | None\n",
        TS + "test_refused_example[request-options-set-null.json]",
        regen=True,
    ),
    plant(
        "set-takes-null-value-problem",
        LNG,
        "    if not isinstance(value, str) or value not in texts:",
        "    if value is not None and (not isinstance(value, str) or value not in texts):",
        TL + "test_options_set_refuses_what_is_off_the_table",
    ),
    plant(
        "menu-default-unmarked",
        WLK,
        "Option(c.words, c.text, spec.help, c.text == now)",
        "Option(c.words, c.text, spec.help, c.text == now and now != spec.default)",
        TW + "test_options_is_eight_menus_five_then_three_each_marking_its_default",
    ),
    plant(
        "menu-extra-choice",
        WLK,
        '            lines.append(f"{spec.words}: {words_of(name, _native(name, now))}.")',
        '            choices += (Option("Impossible", "impossible", spec.help),)\n            lines.append(f"{spec.words}: {words_of(name, _native(name, now))}.")',
        TW + "test_every_menu_holds_only_its_options_choices",
    ),
    plant(
        "menu-help-dropped",
        WLK,
        "Option(c.words, c.text, spec.help, c.text == now)",
        'Option(c.words, c.text, "", c.text == now)',
        TW + "test_options_is_eight_menus",
    ),
    plant(
        "status-pilot-of-another-account",
        COR,
        '            pilot = self.pilots.get(user)\n        except PilotFileError as exc:\n            logger.warning("pilot of account %d: %s", user, exc)\n            return None',
        '            pilot = self.pilots.get(9009)\n        except PilotFileError as exc:\n            logger.warning("pilot of account %d: %s", user, exc)\n            return None',
        TP + "test_status_names_the_linked_accounts_pilot",
        TP + "test_status_never_names_the_pilot_of_another_account",
    ),
    plant(
        "status-bad-file-crashes",
        COR,
        '        except PilotFileError as exc:\n            logger.warning("pilot of account %d: %s", user, exc)\n            return None',
        '        except ZeroDivisionError as exc:\n            logger.warning("pilot of account %d: %s", user, exc)\n            return None',
        TP + "test_status_names_no_pilot_and_warns_when_the_file_cannot_be_read",
    ),
    plant(
        "status-bad-file-silent",
        COR,
        'logger.warning("pilot of account %d: %s", user, exc)\n            return None',
        'logger.debug("pilot of account %d: %s", user, exc)\n            return None',
        TP + "test_status_names_no_pilot_and_warns_when_the_file_cannot_be_read",
    ),
    plant(
        "pilot-rename-not-pushed",
        PWK,
        "            edit=True,\n            pushes=[pilot_push(renamed.name)],",
        "            edit=True,\n            pushes=[],",
        TP + "test_a_rename_pushes_the_new_name",
        TB + "test_a_pilot_renamed_in_discord_reaches_every_open_page",
    ),
    plant(
        "pilot-make-not-pushed",
        PWK,
        "                pushes.append(pilot_push(pilot.name))",
        "                pass",
        TP + "test_the_pilot_command_that_makes_a_pilot_pushes_it",
        TB + "test_a_new_pilot_made_in_discord_reaches_an_open_page",
    ),
    plant(
        "pilot-rename-pushes-old-name",
        PWK,
        "            pushes=[pilot_push(renamed.name)],",
        "            pushes=[pilot_push(pilot.name)],",
        TP + "test_a_rename_pushes_the_new_name",
        TB + "test_a_pilot_renamed_in_discord_reaches_every_open_page",
    ),
    plant(
        "revision-left-at-7",
        PRO,
        "SCHEMA_REVISION = 8",
        "SCHEMA_REVISION = 7",
        "tests/test_page_control.py::test_hello",
        "tests/test_page_control.py::test_the_list_has_thirteen_commands_and_revision_8",
    ),
    plant(
        "fence-engine-battle-length-off",
        "tests/test_launch_option_defaults.py",
        '    "battle_length": lambda n: str(n + 2),',
        '    "battle_length": lambda n: str(n),',
        DEFAULT_TEST + "[battle_length]",
    ),
    plant(
        "default-sith",
        OPT,
        '            "none",\n        ),\n    )\n}',
        '            "lord",\n        ),\n    )\n}',
        TO + "test_each_option_has_its_default[sith",
        TO + "test_the_sith_option_has_three_choices_in_words_and_defaults_to_none",
    ),
    plant(
        "sith-none-is-sent",
        OPT,
        '    if settings.get("sith") == "none":',
        '    if settings.get("sith") == "never":',
        TO + "test_every_description_holds_all_eight_options_but_sith_when_none",
        TL + "test_no_options_set_means_seven_defaults_in_the_description_and_no_sith",
    ),
    plant(
        "result-version-3-refused",
        "jedimaster/launch/model.py",
        "RESULT_SCHEMA_VERSIONS = (2, 3, 4)",
        "RESULT_SCHEMA_VERSIONS = (2, 4)",
        "tests/test_launch_schema.py::test_every_result_version_reads_and_each_refuses_the_others_shapes",
        "tests/test_launch_command.py::test_a_version_3_result_prints_the_sith_clause_on_the_one_line",
    ),
    plant(
        "result-version-4-refused",
        "jedimaster/launch/model.py",
        "RESULT_SCHEMA_VERSIONS = (2, 3, 4)",
        "RESULT_SCHEMA_VERSIONS = (2, 3)",
        "tests/test_launch_schema.py::test_every_result_version_reads_and_each_refuses_the_others_shapes",
    ),
    plant(
        "sith-clause-lost-count",
        "jedimaster/launch/cli.py",
        'return f"{flown - lost} of {flown} {many} lived"',
        'return f"{lost} of {flown} {many} lived"',
        "tests/test_launch_command.py::test_the_sith_clause_for_each_fate",
        "tests/test_launch_command.py::test_a_version_3_result_prints_the_sith_clause_on_the_one_line",
    ),
    plant(
        "rank-seven-name",
        "jedimaster/mission/names.py",
        '    0x07: "Sith Lord",',
        '    0x07: "Sith Apprentice",',
        "tests/test_json_schema.py::test_the_two_sith_ranks_are_named_above_jedi",
    ),
    plant(
        "sith-launch-clause",
        LNG,
        '"lord": "A Sith Lord flies with the enemy."',
        '"lord": "Sith Apprentices fly with the enemy."',
        TL + "test_the_sith_setting_reaches_the_description_and_the_launch_message",
    ),
]
