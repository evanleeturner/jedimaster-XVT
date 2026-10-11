"""More planted faults of the solo launch: the control list's messages, options table and arguments.

Purpose:
    Break the control list's message classes one by one (so each accepted and
    refused example in ``tests/control-examples`` is guarded), and the small
    rules of the options table, the pilot name check, the solo walk and the
    argument checks that the first tables leave open.

Flow:
    ``plants_bot`` joins this table with the others; ``plant_faults``
    applies each plant alone: write ``new`` over ``old`` in ``path``
    (regenerating the schema files when ``regen_schema``), run the suite,
    restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``solo-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_solo_schema import SOLO_SCHEMA_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

PRO = "jedimaster/page/protocol.py"
OPT = "jedimaster/launch/options.py"
PIL = "jedimaster/launch/pilots.py"
RUN = "jedimaster/launch/run.py"
MOD = "jedimaster/launch/model.py"
CTL = "jedimaster/page/control.py"
LNG = "jedimaster/page/launching.py"
SOL = "jedimaster/page/solo.py"
PCL = "jedimaster/page/cli.py"
WLK = "jedimaster/bot/walk.py"
TS = "tests/test_page_schema.py::"


def plant(pid, path, old, new, *expect, regen=False) -> Plant:
    """Return the plant ``solo-<pid>``."""
    return Plant(f"solo-{pid}", path, old, new, expect, regen)


def unlisted(cls: str, *examples: str) -> Plant:
    """Return a plant that drops message class ``cls`` from the list of messages."""
    return plant(
        f"schema-unlisted-{cls}",
        PRO,
        f"    {cls},\n",
        "",
        *[TS + f"test_accepted_example[{e}]" for e in examples],
        regen=True,
    )


def unresult(cls: str, *examples: str) -> Plant:
    """Return a plant that drops result class ``cls`` from the reply's results."""
    return plant(
        f"schema-no-result-{cls}",
        PRO,
        f"        | {cls}\n",
        "",
        *[TS + f"test_accepted_example[{e}]" for e in examples],
        regen=True,
    )


SCHEMA_PLANTS: list[Plant] = [
    unlisted("LaunchMenuRequest", "request-launch-menu-unsided.json"),
    unlisted("PilotsListRequest", "request-pilots-list.json"),
    unlisted("LaunchCancelRequest", "request-launch-cancel.json"),
    unlisted(
        "OptionsGetRequest",
        "request-options-get.json",
        "request-options-get-linked.json",
    ),
    unlisted(
        "OptionsSetRequest", "request-options-set.json", "request-options-set-null.json"
    ),
    unlisted("LaunchStartedPush", "push-launch-started.json"),
    unlisted(
        "LaunchEndedPush", "push-launch-ended.json", "push-launch-ended-cancelled.json"
    ),
    unlisted("OptionsChangedPush", "push-options-changed.json"),
    unresult(
        "SoloMenuResult",
        "reply-ok-launch-menu.json",
        "reply-ok-launch-menu-missing.json",
    ),
    unresult(
        "PilotsListResult",
        "reply-ok-pilots-list.json",
        "reply-ok-pilots-list-unreadable.json",
    ),
    unresult(
        "LaunchSoloResult",
        "reply-ok-launch-solo-started.json",
        "reply-ok-launch-solo-refused.json",
    ),
    unresult("LaunchCancelResult", "reply-ok-launch-cancel.json"),
    unresult(
        "OptionsResult", "reply-ok-options.json", "reply-ok-options-no-player.json"
    ),
    plant(
        "schema-status-any",
        PRO,
        '    "flown", "refused", "not_reached", "failed", "cancelled", "error"\n]',
        '    "flown", "refused", "not_reached", "failed", "cancelled", "error", "won"\n]',
        TS + "test_refused_example[push-launch-ended-bad-status.json]",
        regen=True,
    ),
    plant(
        "schema-started-id-zero",
        PRO,
        '    launch_id: int = field(metadata={"minimum": 1, "maximum": MAX_ID})\n    mission_type: MissionTypeName',
        '    launch_id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    mission_type: MissionTypeName',
        TS + "test_refused_example[push-launch-started-id-zero.json]",
        regen=True,
    ),
    plant(
        "schema-result-id-negative",
        PRO,
        '    launch_id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    message: str',
        '    launch_id: int = field(metadata={"minimum": -5, "maximum": MAX_ID})\n    message: str',
        TS + "test_refused_example[reply-ok-launch-solo-negative-id.json]",
        regen=True,
    ),
    plant(
        "schema-difficulty-free",
        PRO,
        "    difficulty: DifficultyText | None",
        "    difficulty: str | None",
        TS + "test_refused_example[reply-ok-options-bad-value.json]",
        regen=True,
    ),
    plant(
        "schema-menu-type-free",
        PRO,
        'class SoloMenuArgs:\n    """A kind of battle and a side: the solo list to read."""\n\n    mission_type: MissionTypeName',
        'class SoloMenuArgs:\n    """A kind of battle and a side: the solo list to read."""\n\n    mission_type: str',
        TS + "test_refused_example[request-launch-menu-bad-type.json]",
        regen=True,
    ),
    plant(
        "schema-side-free",
        PRO,
        'SideName = Literal["rebel", "imperial", "none"]',
        'SideName = Literal["rebel", "imperial", "none", "neutral"]',
        TS + "test_refused_example[request-launch-solo-bad-side.json]",
        regen=True,
    ),
    plant(
        "schema-option-name-free",
        PRO,
        '    "continue_sequence",\n]\nOPTION_NAMES',
        '    "continue_sequence",\n    "speed",\n]\nOPTION_NAMES',
        TS + "test_refused_example[request-options-set-bad-name.json]",
        "tests/test_launch_options.py::test_the_table_lists_the_seven_options",
        regen=True,
    ),
    plant(
        "schema-option-value-number",
        PRO,
        "    name: OptionName\n    value: str | None",
        "    name: OptionName\n    value: int | None",
        TS + "test_refused_example[request-options-set-number.json]",
        regen=True,
    ),
    plant(
        "schema-options-get-no-player",
        PRO,
        "    player: str | None\n\n\n@dataclass\nclass OptionsSetArgs",
        "\n\n@dataclass\nclass OptionsSetArgs",
        TS + "test_refused_example[request-options-get-no-player.json]",
        regen=True,
    ),
    plant(
        "options-choice-not-boolean",
        OPT,
        'Choice("on", True, "On")',
        'Choice("on", "on", "On")',
        "tests/test_launch_options.py::test_every_choice_value_fits_the_launch_schema",
    ),
    plant(
        "options-hard-value",
        OPT,
        'Choice("hard", "hard", "Hard")',
        'Choice("hard", "HARD", "Hard")',
        "tests/test_launch_options.py::test_text_and_value_convert_both_ways",
    ),
    plant(
        "options-length-value",
        OPT,
        'Choice(str(n), n, f"{n} wins")',
        'Choice(str(n), n + 1, f"{n} wins")',
        "tests/test_launch_options.py::test_text_and_value_convert_both_ways",
    ),
    plant(
        "options-restart-value",
        OPT,
        'Choice("restart", "restart", "Start again")',
        'Choice("restart", "Restart", "Start again")',
        "tests/test_launch_options.py::test_text_and_value_convert_both_ways",
    ),
    plant(
        "model-empty-settings-written",
        MOD,
        '    if settings:\n        description["settings"]',
        '    if settings is not None:\n        description["settings"]',
        "tests/test_launch_options.py::test_an_unset_option_is_left_out",
        "tests/test_launch_command.py::test_without_a_setting_flag",
    ),
    plant(
        "pilots-none-pass",
        PIL,
        "    return not any(word in lowered for word in SWITCH_WORDS)",
        "    return False",
        "tests/test_launch_pilots.py::test_a_good_name_passes",
    ),
    plant(
        "run-async-result-changed",
        RUN,
        "return _read_result(written_path), int(process.returncode)",
        'return {**_read_result(written_path), "score": 0}, int(process.returncode)',
        "tests/test_launch_run_async.py::test_a_flown_battle_returns",
    ),
    plant(
        "cli-flight-without-engine",
        PCL,
        "__version__, flight)",
        '__version__, Flight("x", (), None))',
        "tests/test_page_cli.py::test_without_an_engine",
    ),
    plant(
        "control-kind-free",
        CTL,
        'if not isinstance(kind, str) or kind not in MISSION_TYPE_NAMES:\n        return f"no such mission type; one of {list(MISSION_TYPE_NAMES)}"\n    return side_problem',
        'if False:\n        return f"no such mission type; one of {list(MISSION_TYPE_NAMES)}"\n    return side_problem',
        "tests/test_page_launching.py::test_a_launch_with_a_bad_argument",
    ),
    plant(
        "control-option-name-free",
        CTL,
        "    if not isinstance(name, str) or name not in OPTION_NAMES:",
        "    if not isinstance(name, str):",
        "tests/test_page_launching.py::test_options_set_refuses_what_is_off_the_table",
    ),
    plant(
        "control-option-set-open",
        CTL,
        '    if set(args) != {"player", "name", "value"}:',
        '    if not {"player", "name"} <= set(args):',
        "tests/test_page_launching.py::test_options_set_refuses_what_is_off_the_table",
    ),
    plant(
        "control-command-list-open",
        CTL,
        "    if command not in COMMANDS:",
        "    if False:",
        "tests/test_page_launching.py::test_a_command_off_the_list_is_refused_not_run",
    ),
    plant(
        "launching-value-number-ok",
        LNG,
        "    if not isinstance(value, str) or value not in texts:",
        "    if value not in texts and not isinstance(value, int):",
        "tests/test_page_launching.py::test_options_set_refuses_what_is_off_the_table",
    ),
    plant(
        "launching-leaks-engine",
        LNG,
        'Watch it on the page."',
        'Watch it on the page. {self.flight.engine}"',
        "tests/test_bot_walk.py::test_nothing_in_a_reply_is_a_path",
    ),
    plant(
        "list-unsided-view",
        SOL,
        'view = side if mission_type in SIDED_TYPES else "network"',
        "view = side",
        "tests/test_page_solo.py::test_an_unsided_kind_reads_its_mission_list",
    ),
    plant(
        "walk-kinds-cut",
        WLK,
        "    return tuple(Option(title_case(n), n) for n in MISSION_TYPE_NAMES)",
        "    return tuple(Option(title_case(n), n) for n in MISSION_TYPE_NAMES[:5])",
        "tests/test_bot_walk.py::test_solo_offers_the_six_kinds",
    ),
    plant(
        "walk-pilot-parts",
        WLK,
        "        if len(parts) != 3 or parts[0] not in MISSION_TYPE_NAMES or not value:",
        "        if parts[0] not in MISSION_TYPE_NAMES or not value:",
        "tests/test_bot_walk.py::test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-unknown-step",
        WLK,
        "        }.get(step)",
        "        }.get(step, self._after_kind)",
        "tests/test_bot_walk.py::test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-solo-dm-only",
        WLK,
        'self._gate("solo", user_id, in_dm, dm_only=False)',
        'self._gate("solo", user_id, in_dm, dm_only=True)',
        "tests/test_bot_walk.py::test_the_walk_can_be_used_in_a_server_too",
    ),
]

SCHEMA_PLANTS += [
    plant(
        "list-side-neutral",
        SOL,
        '        if side not in ("rebel", "imperial"):',
        '        if side not in ("rebel", "imperial", "neutral"):',
        "tests/test_page_launching.py::test_a_launch_with_a_bad_argument",
    ),
    plant(
        "list-unsided-none-refused",
        SOL,
        '    elif side != "none":',
        '    elif side == "none":',
        "tests/test_page_solo.py::test_a_side_fits_a_kind_only",
    ),
    plant(
        "settings-nothing-checked",
        "jedimaster/page/settings.py",
        "        if name not in OPTIONS or (value is not None and text_of(name, value) is None):",
        "        if False:",
        "tests/test_page_settings.py::test_a_bad_player_option_or_value_raises",
    ),
    plant(
        "control-player-no-checks",
        CTL,
        "        and value.isascii()\n        and value.isdigit()\n        and 1 <= len(value) <= MAX_PLAYER_DIGITS\n",
        "",
        "tests/test_page_launching.py::test_a_launch_with_a_bad_argument",
    ),
]

SOLO_SCHEMA_PLANTS: list[Plant] = SCHEMA_PLANTS
