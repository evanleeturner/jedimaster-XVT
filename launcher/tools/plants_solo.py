"""The planted faults of the solo launch: options, pilots, battles, commands, settings.

Purpose:
    Break one rule of the launch and option code per plant: the options
    table and its conversions, the pilot folder and names, the background
    battle, the launch and option commands, the stored options, and the
    page's new wiring (server and command line).

Flow:
    ``plants_bot`` joins this table with the others; ``plant_faults``
    applies each plant alone: write ``new`` over ``old`` in ``path``, run the
    suite, restore, and check that every test in ``expect`` failed. An
    ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``solo-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_solo import SOLO_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

OPT = "jedimaster/launch/options.py"
PIL = "jedimaster/launch/pilots.py"
RUN = "jedimaster/launch/run.py"
LCL = "jedimaster/launch/cli.py"
SOL = "jedimaster/page/solo.py"
BAT = "jedimaster/page/battles.py"
LNG = "jedimaster/page/launching.py"
CTL = "jedimaster/page/control.py"
SET = "jedimaster/page/settings.py"
SRV = "jedimaster/page/server.py"
PCL = "jedimaster/page/cli.py"
PRO = "jedimaster/page/protocol.py"
TO = "tests/test_launch_options.py::"
TP = "tests/test_launch_pilots.py::"
TA = "tests/test_launch_run_async.py::"
TSO = "tests/test_page_solo.py::"
TB = "tests/test_page_battles.py::"
TL = "tests/test_page_launching.py::"
TST = "tests/test_page_settings.py::"
TSV = "tests/test_page_server.py::"
TCL = "tests/test_page_cli.py::"
TLC = "tests/test_launch_command.py::"
TSC = "tests/test_page_schema.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``solo-<pid>``."""
    return Plant(f"solo-{pid}", path, old, new, expect)


OPTION_PLANTS: list[Plant] = [
    plant(
        "options-switch-is-number",
        OPT,
        "return type(a) is type(b) and a == b",
        "return a == b",
        TO + "test_a_switch_is_not_a_number",
        TST + "test_a_bad_player_option_or_value_raises",
    ),
    plant(
        "options-cheat-everywhere",
        OPT,
        'and game_type != "campaign":',
        ":",
        TO + "test_an_easy_cheat_in_a_campaign_is_kept",
        TL + "test_an_easy_cheat_in_a_campaign_is_kept",
    ),
    plant(
        "options-cheat-never",
        OPT,
        'and game_type != "campaign":',
        "and False:",
        TO + "test_an_easy_cheat_outside_a_campaign",
        TL + "test_an_easy_cheat_outside_a_campaign",
    ),
    plant(
        "options-cheat-no-note",
        OPT,
        "        note = CHEAT_NOTE",
        "        note = None",
        TO + "test_an_easy_cheat_outside_a_campaign",
        TL + "test_an_easy_cheat_outside_a_campaign",
    ),
    plant(
        "options-cheat-edits-input",
        OPT,
        "    settings.update(options)\n",
        "    options.update({**settings, **options})\n    settings = options\n",
        TO + "test_for_description_does_not_change",
    ),
    plant(
        "options-time-limit-to-19",
        OPT,
        "for n in range(1, 21)",
        "for n in range(1, 20)",
        TO + "test_the_time_limit_has_none",
        TO + "test_every_choice_text_is_a_value",
    ),
    plant(
        "options-battle-length",
        OPT,
        "for n in (2, 3, 4)",
        "for n in (2, 3)",
        TO + "test_every_choice_text_is_a_value",
    ),
    plant(
        "options-parse-anything",
        OPT,
        '    raise ValueError(f"{name} must be one of {texts}")',
        "    return text",
        TO + "test_an_unknown_name_or_choice_is_refused",
        TO + "test_a_setting_argument_is_name_equals_value",
    ),
    plant(
        "options-unknown-name-parsed",
        OPT,
        'its choices.\n    """\n    if name not in OPTIONS:\n        raise ValueError(f"no such option {name!r}; one of {list(OPTIONS)}")',
        'its choices.\n    """\n    if False:\n        raise ValueError()',
        TO + "test_an_unknown_name_or_choice_is_refused",
    ),
    plant(
        "options-words-blank",
        OPT,
        '    raise ValueError(f"{value!r} is not a choice of {name}")\n\n\ndef current_text',
        '    return ""\n\n\ndef current_text',
        TO + "test_words_name_a_choice_and_refuse_what_is_no_choice",
    ),
    plant(
        "options-switch-values-swapped",
        OPT,
        'Choice("on", True, "On"), Choice("off", False, "Off")',
        'Choice("on", False, "On"), Choice("off", True, "Off")',
        TO + "test_text_and_value_convert_both_ways",
    ),
    plant(
        "options-cheat-choice-dropped",
        OPT,
        '                Choice("easy_cheat", "easy_cheat", "Easy with cheats (campaigns)"),\n',
        "",
        TO + "test_every_choice_text_is_a_value",
    ),
    plant(
        "options-words-unknown-kept",
        OPT,
        "def text_of(name: str, value: Any) -> str | None:",
        "def text_of(name: str, value: Any) -> str | None:\n    return None if value is None else str(value)",
        TO + "test_a_switch_is_not_a_number",
        TO + "test_text_and_value_convert_both_ways",
    ),
]

PILOT_PLANTS: list[Plant] = [
    plant(
        "pilots-xdg-ignored",
        PIL,
        '        data = env.get("XDG_DATA_HOME")',
        "        data = None",
        TP + "test_linux_uses_xdg_data_home",
    ),
    plant(
        "pilots-linux-state",
        PIL,
        'home / ".local" / "share"',
        'home / ".local" / "state"',
        TP + "test_linux_uses_xdg_data_home",
    ),
    plant(
        "pilots-appdata-ignored",
        PIL,
        '        appdata = env.get("APPDATA")',
        "        appdata = None",
        TP + "test_windows_uses_appdata",
    ),
    plant(
        "pilots-mac-preferences",
        PIL,
        'home / "Library" / "Application Support"',
        'home / "Library" / "Preferences"',
        TP + "test_macos_uses_application_support",
    ),
    plant(
        "pilots-other-systems-linux",
        PIL,
        '        logger.debug("no default pilot folder for %s", platform)\n        return None',
        '        base = home / ".local" / "share"',
        TP + "test_another_system_has_no_default_folder",
    ),
    plant(
        "pilots-org-app-swapped",
        PIL,
        "return base / ORGANISATION / APPLICATION",
        "return base / APPLICATION / ORGANISATION",
        TP + "test_linux_uses_xdg_data_home",
        TP + "test_windows_uses_appdata",
        TP + "test_macos_uses_application_support",
    ),
    plant(
        "pilots-name-length",
        PIL,
        "if not 1 <= len(name) <= NAME_LIMIT:",
        "if not 1 <= len(name) <= 20:",
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-empty",
        PIL,
        "if not 1 <= len(name) <= NAME_LIMIT:",
        "if not 0 <= len(name) <= NAME_LIMIT:",
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-ascii",
        PIL,
        "c.isascii() and (c.isalnum() or c in EXTRA_CHARACTERS)",
        "(c.isalnum() or c in EXTRA_CHARACTERS)",
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-edges",
        PIL,
        "if not (name[0].isalnum() and name[-1].isalnum()):",
        "if False:",
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-switch-words",
        PIL,
        "return not any(word in lowered for word in SWITCH_WORDS)",
        "return True",
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-tilde",
        PIL,
        "c in EXTRA_CHARACTERS)",
        'c in EXTRA_CHARACTERS + "~\\t")',
        TP + "test_a_bad_name_fails",
    ),
    plant(
        "pilots-name-zero-byte",
        PIL,
        'head.split(b"\\0", 1)[0]',
        'head.rstrip(b"\\0")',
        TP + "test_the_name_ends_at_the_first_zero_byte",
    ),
    plant(
        "pilots-suffix-case",
        PIL,
        'p.suffix.lower() == ".plt"',
        'p.suffix == ".plt"',
        TP + "test_only_plt_files_count",
    ),
    plant(
        "pilots-any-suffix",
        PIL,
        'p.suffix.lower() == ".plt"',
        "True",
        TP + "test_only_plt_files_count",
    ),
    plant(
        "pilots-reversed",
        PIL,
        "key=lambda p: p.name.lower(),",
        "key=lambda p: p.name.lower(), reverse=True,",
        TP + "test_the_listing_reads",
    ),
    plant(
        "pilots-repeats-kept",
        PIL,
        "if name.lower() in (n.lower() for n in names):",
        "if False:",
        TP + "test_a_bad_or_repeated_name_is_skipped",
    ),
    plant(
        "pilots-bad-names-kept",
        PIL,
        "if name is None or not name_ok(name):",
        "if name is None:",
        TP + "test_a_bad_or_repeated_name_is_skipped",
    ),
    plant(
        "pilots-missing-folder-empty",
        PIL,
        '        logger.debug("cannot list %s: %s", folder, exc)\n        return None',
        '        logger.debug("cannot list %s: %s", folder, exc)\n        return []',
        TP + "test_a_missing_folder_or_no_folder_is_none",
    ),
    plant(
        "pilots-no-folder-empty",
        PIL,
        "    if folder is None:\n        return None",
        "    if folder is None:\n        return []",
        TP + "test_a_missing_folder_or_no_folder_is_none",
    ),
]

RUN_PLANTS: list[Plant] = [
    plant(
        "run-stop-kills-at_once",
        RUN,
        "await asyncio.wait_for(process.wait(), STOP_SECONDS)",
        "await asyncio.wait_for(process.wait(), 0.0)",
        TA + "test_an_engine_that_ignores_terminate_is_killed",
    ),
    plant(
        "run-timeout-message",
        RUN,
        "            await _stop(process)\n            raise LaunchError(",
        '            await _stop(process)\n            raise LaunchError("x") from exc\n            raise LaunchError(',
        TA + "test_an_engine_past_its_limit",
        TB + "test_the_time_limit_ends_the_battle",
    ),
    plant(
        "run-cancel-leaves-engine",
        RUN,
        "            await asyncio.shield(_stop(process))",
        "            pass",
        TA + "test_no_engine_process_is_left_after_a_cancel",
    ),
    plant(
        "run-exit-code-zero",
        RUN,
        "return _read_result(written_path), int(process.returncode)",
        "return _read_result(written_path), 0",
        TA + "test_a_refusal_is_a_result_not_an_error",
    ),
    plant(
        "run-start-error-raw",
        RUN,
        "            process = await asyncio.create_subprocess_exec(*command)\n        except OSError as exc:",
        "            process = await asyncio.create_subprocess_exec(*command)\n        except ZeroDivisionError as exc:",
        TA + "test_an_engine_that_cannot_start_is_an_error",
    ),
    plant(
        "run-extra-dropped",
        RUN,
        "process = await asyncio.create_subprocess_exec(*command)",
        "process = await asyncio.create_subprocess_exec(*command[:3])",
        TA + "test_a_refusal_is_a_result_not_an_error",
        TB + "test_cancel_stops_the_engine",
    ),
    plant(
        "run-async-schema-unchecked",
        RUN,
        '    if problems:\n        raise LaunchError("; ".join(problems))\n    description_path',
        "    if False:\n        raise LaunchError()\n    description_path",
        TA + "test_a_description_that_breaks_its_schema_is_never_run",
    ),
]

BATTLE_PLANTS: list[Plant] = [
    plant(
        "battles-id-stuck",
        BAT,
        "else launch_id + 1",
        "else launch_id",
        TB + "test_a_new_battle_may_start_once_the_last_has_ended",
    ),
    plant(
        "battles-cancel-any-id",
        BAT,
        'if self._current is None or self._current["launch_id"] != launch_id:',
        "if self._current is None:",
        TB + "test_cancelling_another_launch_number",
    ),
    plant(
        "battles-early-cancel-lost",
        BAT,
        "            self._early_cancel = True\n",
        "            pass\n",
        TB + "test_the_answer_comes_before_the_battle_ends",
        TB + "test_only_one_battle_flies_at_a_time",
    ),
    plant(
        "battles-mismatch-passes",
        BAT,
        'if code != result["exit_code"]:',
        "if False:",
        TB + "test_an_exit_code_that_disagrees",
    ),
    plant(
        "battles-cancelled-status",
        BAT,
        '        except asyncio.CancelledError:\n            return "cancelled", "cancelled"',
        '        except asyncio.CancelledError:\n            return "error", "cancelled"',
        TB + "test_cancel_stops_the_engine",
    ),
    plant(
        "battles-error-line",
        BAT,
        'return "error", str(exc)',
        'return "error", "failed"',
        TB + "test_a_crash_with_no_result",
        TB + "test_the_time_limit_ends_the_battle",
    ),
    plant(
        "battles-stays-busy",
        BAT,
        "        self._current = None\n        logger.info(",
        "        logger.info(",
        TB + "test_a_new_battle_may_start_once_the_last_has_ended",
        TB + "test_cancel_stops_the_engine",
    ),
    plant(
        "battles-line-is-status",
        BAT,
        'return result["status"], summary(result)',
        'return result["status"], result["status"]',
        TB + "test_a_flown_battle_ends_with_the_summary_line",
    ),
    plant(
        "battles-other-errors-raise",
        BAT,
        "        except Exception as exc:  # the battle must always end with a push",
        "        except ZeroDivisionError as exc:",
        TB + "test_any_other_failure_still_ends",
    ),
    plant(
        "battles-timeout-fixed",
        BAT,
        "flight.engine, description, flight.args, flight.timeout",
        "flight.engine, description, flight.args, 3600.0",
        TB + "test_the_time_limit_ends_the_battle",
    ),
    plant(
        "battles-args-dropped",
        BAT,
        "flight.engine, description, flight.args, flight.timeout",
        "flight.engine, description, (), flight.timeout",
        TB + "test_cancel_stops_the_engine",
        TB + "test_a_refused_battle_ends_with_the_engines_reason",
    ),
    plant(
        "battles-second-allowed",
        BAT,
        "        if self.busy():\n            raise RuntimeError",
        "        if False:\n            raise RuntimeError",
        TB + "test_start_refuses_a_second_battle_by_raising",
    ),
    plant(
        "battles-running-push-none",
        BAT,
        '        return {"event": "launch.started", "data": dict(self._current)}',
        "        return None",
        TB + "test_a_page_that_connects_late",
    ),
    plant(
        "battles-cancel-before-start",
        BAT,
        "        if self._entered:\n            self._task.cancel()",
        "        if True:\n            self._task.cancel()",
        TB + "test_the_answer_comes_before_the_battle_ends",
    ),
]

LAUNCHING_PLANTS: list[Plant] = [
    plant(
        "launching-page-before-busy",
        LNG,
        "        if self.battles.busy():\n            return BUSY\n        if self.pages_open() == 0:\n            return NO_PAGE\n",
        "        if self.pages_open() == 0:\n            return NO_PAGE\n        if self.battles.busy():\n            return BUSY\n",
        TL + "test_busy_is_refused_before_no_page",
    ),
    plant(
        "launching-engine-unchecked",
        LNG,
        "        if self.flight is None:\n            return NO_ENGINE",
        "        if False:\n            return NO_ENGINE",
        TL + "test_the_no_engine_reason",
        TL + "test_the_refusals_come_in_the_promised_order",
    ),
    plant(
        "launching-pilots-engine-always",
        LNG,
        '            "engine_given": self.flight is not None,',
        '            "engine_given": True,',
        TL + "test_pilots_list_says_not_found_without_a_game_program",
    ),
    plant(
        "launching-no-page-needed",
        LNG,
        "        if self.pages_open() == 0:",
        "        if False:",
        TL + "test_the_no_page_reason",
        TL + "test_the_refusals_come_in_the_promised_order",
    ),
    plant(
        "launching-busy-ignored",
        LNG,
        "        if self.battles.busy():\n            return BUSY",
        "        if False:\n            return BUSY",
        TL + "test_busy_is_refused_before_no_page",
        TB + "test_only_one_battle_flies_at_a_time",
    ),
    plant(
        "launching-install-unchecked",
        LNG,
        "        if self.install is None:\n            return NO_INSTALL",
        "        if False:\n            return NO_INSTALL",
        TL + "test_the_refusals_come_in_the_promised_order",
    ),
    plant(
        "launching-mission-unchecked",
        LNG,
        "        if entry is None:\n            return self._refused(NO_MISSION)",
        "        if False:\n            return self._refused(NO_MISSION)",
        TL + "test_a_mission_off_the_list_is_refused",
    ),
    plant(
        "launching-pilot-case-matters",
        LNG,
        'n.lower() == args["pilot"].lower()',
        'n == args["pilot"]',
        TL + "test_the_description_holds_the_mission",
    ),
    plant(
        "launching-pilot-unchecked",
        LNG,
        'pilot = next((n for n in names if n.lower() == args["pilot"].lower()), None)',
        'pilot = args["pilot"]',
        TL + "test_a_pilot_the_game_does_not_have",
    ),
    plant(
        "launching-pilot-folder-unchecked",
        LNG,
        "        if names is None:\n            return self._refused(NO_PILOT_FOLDER)",
        "        if False:\n            return self._refused(NO_PILOT_FOLDER)",
        TL + "test_a_pilot_folder_that_cannot_be_read",
    ),
    plant(
        "launching-options-ignored",
        LNG,
        'for_description(self.store.get_options(args["player"]), kind)',
        "for_description({}, kind)",
        TL + "test_the_description_holds_the_mission",
        TL + "test_an_easy_cheat",
    ),
    plant(
        "launching-options-of-another",
        LNG,
        'for_description(self.store.get_options(args["player"]), kind)',
        'for_description(self.store.get_options("9"), kind)',
        TL + "test_the_description_holds_the_mission",
    ),
    plant(
        "launching-cheat-note-dropped",
        LNG,
        "        if note:\n",
        "        if False:\n",
        TL + "test_an_easy_cheat_outside_a_campaign",
    ),
    plant(
        "launching-started-words",
        LNG,
        "Watch it on the page.",
        "Good luck.",
        TL + "test_the_description_holds_the_mission",
        "tests/test_bot_walk.py::test_launch_asks_the_launcher",
    ),
    plant(
        "launching-push-dropped",
        LNG,
        '"message": message}, [push]',
        '"message": message}, []',
        TB + "test_a_started_push_comes",
        TSV + "test_a_launch_is_pushed_to_every_page",
    ),
    plant(
        "launching-cancel-always",
        LNG,
        '        if self.battles.cancel(args["launch_id"]):',
        "        if True:",
        TB + "test_cancelling_a_battle_that_is_not_flying",
    ),
    plant(
        "launching-options-get-no-host",
        LNG,
        'player = args["player"] if args["player"] is not None else self.host_player()',
        'player = args["player"]',
        TL + "test_options_get_with_no_player",
    ),
    plant(
        "launching-options-push-dropped",
        LNG,
        'return result, [{"event": "options.changed", "data": result}]',
        "return result, []",
        TL + "test_setting_an_option_pushes",
        TSV + "test_options_set_on_one_page",
    ),
    plant(
        "launching-pilots-always-found",
        LNG,
        '"found": names is not None',
        '"found": True',
        TL + "test_pilots_list_says_not_found",
    ),
    plant(
        "launching-start-error-raised",
        LNG,
        "        except RuntimeError as exc:",
        "        except ZeroDivisionError as exc:",
        TB + "test_starting_without_an_event_loop",
    ),
    plant(
        "launching-options-raw",
        LNG,
        "{name: current_text(name, stored) for name in OPTION_NAMES}",
        "{name: stored.get(name) for name in OPTION_NAMES}",
        TL + "test_the_description_holds_the_mission",
        TL + "test_options_are_kept_in_the_settings_file",
    ),
    plant(
        "launching-value-problem-open",
        LNG,
        "    if not isinstance(value, str) or value not in texts:",
        "    if not isinstance(value, str):",
        TL + "test_options_set_refuses_what_is_off_the_table",
        TO + "test_the_control_list_refuses_a_value_off_the_table",
    ),
    plant(
        "launching-sender-pilot-spelling",
        LNG,
        'kind, side, entry["file"], pilot, settings=settings',
        'kind, side, args["mission"], args["pilot"], settings=settings',
        TL + "test_the_description_holds_the_mission",
    ),
]

CONTROL_PLANTS: list[Plant] = [
    plant(
        "control-player-any-text",
        CTL,
        "        and value.isdigit()\n",
        "",
        TL + "test_a_launch_with_a_bad_argument",
        TL + "test_options_get_refuses",
        TL + "test_options_set_refuses_what_is_off_the_table",
    ),
    plant(
        "control-player-any-length",
        CTL,
        "and 1 <= len(value) <= MAX_PLAYER_DIGITS",
        "and 1 <= len(value)",
        TL + "test_a_launch_with_a_bad_argument",
        TL + "test_options_get_refuses",
    ),
    plant(
        "control-menu-side-unchecked",
        CTL,
        "    return side_problem(kind, side)\n\n\ndef _menu_args",
        "    return None\n\n\ndef _menu_args",
        TL + "test_launch_menu_refuses_a_side",
        TL + "test_a_side_that_does_not_fit",
    ),
    plant(
        "control-solo-limits",
        CTL,
        '(("mission", 64), ("pilot", 12))',
        '(("mission", 640), ("pilot", 120))',
        TL + "test_a_launch_with_a_bad_argument",
    ),
    plant(
        "control-solo-empty-ok",
        CTL,
        "if not isinstance(value, str) or not 1 <= len(value) <= limit:",
        "if not isinstance(value, str) or not len(value) <= limit:",
        TL + "test_a_launch_with_a_bad_argument",
    ),
    plant(
        "control-solo-extra-ok",
        CTL,
        '    if set(args) != names:\n        return "arguments must be exactly player,',
        '    if not names <= set(args):\n        return "arguments must be exactly player,',
        TL + "test_no_path_or_program_can_be_sent",
    ),
    plant(
        "control-cancel-zero-ok",
        CTL,
        'not 1 <= args["launch_id"] <= MAX_ID',
        'not 0 <= args["launch_id"] <= MAX_ID',
        TSC + "test_refused_example[request-launch-cancel-zero.json]",
    ),
    plant(
        "control-options-get-player-free",
        CTL,
        'if args["player"] is not None and not _is_player(args["player"]):',
        "if False:",
        TL + "test_options_get_refuses",
    ),
    plant(
        "control-late-page-told-nothing",
        CTL,
        "        return [running] if running is not None else []",
        "        return []",
        TB + "test_a_page_that_connects_late",
        TSV + "test_a_page_that_connects_mid_battle",
    ),
    plant(
        "control-listener-failure-stops",
        CTL,
        "            except Exception as exc:  # one failing front end must not stop the others",
        "            except ZeroDivisionError as exc:",
        TB + "test_a_failing_listener",
    ),
    plant(
        "control-launch-solo-push-lost",
        CTL,
        "        result, pushes = self.launching.solo(args)\n        self._pushes.extend(pushes)",
        "        result, pushes = self.launching.solo(args)",
        TB + "test_a_started_push_comes",
        TL + "test_the_description_holds_the_mission",
    ),
    plant(
        "control-options-set-push-lost",
        CTL,
        "        result, pushes = self.launching.options_set(args)\n        self._pushes.extend(pushes)",
        "        result, pushes = self.launching.options_set(args)",
        TL + "test_setting_an_option_pushes",
    ),
    plant(
        "control-revision",
        PRO,
        "SCHEMA_REVISION = 8",
        "SCHEMA_REVISION = 7",
        "tests/test_page_control.py::test_hello",
        "tests/test_page_control.py::test_the_list_has_thirteen",
    ),
    plant(
        "control-player-digits",
        PRO,
        "MAX_PLAYER_DIGITS = 20",
        "MAX_PLAYER_DIGITS = 30",
        TSC + "test_schema_file_matches_model",
    ),
]

STORE_PLANTS: list[Plant] = [
    plant(
        "settings-option-not-written",
        SET,
        '        logger.debug("game option of %s: %s = %r", player, name, value)\n        return self._write()',
        '        logger.debug("game option of %s: %s = %r", player, name, value)\n        return True',
        TST + "test_the_file_holds_only_the_options_that_are_set",
        TL + "test_options_are_kept_in_the_settings_file",
    ),
    plant(
        "settings-empty-player-kept",
        SET,
        "        if not chosen:\n            del self.options[player]",
        "        if False:\n            del self.options[player]",
        TST + "test_the_file_holds_only_the_options_that_are_set",
    ),
    plant(
        "settings-none-stored",
        SET,
        "        if value is None:\n            chosen.pop(name, None)",
        "        if False:\n            chosen.pop(name, None)",
        TST + "test_the_file_holds_only_the_options_that_are_set",
        TL + "test_null_leaves_an_option",
    ),
    plant(
        "settings-player-unchecked",
        SET,
        "        if not _is_player(player):\n            raise ValueError",
        "        if False:\n            raise ValueError",
        TST + "test_a_bad_player_option_or_value_raises",
    ),
    plant(
        "settings-value-unchecked",
        SET,
        "        if name not in OPTIONS or (value is not None and text_of(name, value) is None):",
        "        if name not in OPTIONS:",
        TST + "test_a_bad_player_option_or_value_raises",
    ),
    plant(
        "settings-name-unchecked",
        SET,
        "        if name not in OPTIONS or (value is not None",
        "        if (value is not None",
        TST + "test_a_bad_player_option_or_value_raises",
    ),
    plant(
        "settings-read-values-unchecked",
        SET,
        "                if name in OPTIONS and text_of(name, value) is not None:",
        "                if name in OPTIONS:",
        TST + "test_a_broken_game_options_entry_is_dropped",
    ),
    plant(
        "settings-read-players-unchecked",
        SET,
        "            if not _is_player(player) or not isinstance(chosen, dict):",
        "            if not isinstance(chosen, dict):",
        TST + "test_a_broken_game_options_entry_is_dropped",
    ),
    plant(
        "settings-read-not-object",
        SET,
        "        if not isinstance(stored, dict):",
        "        if False:",
        TST + "test_game_options_that_are_not_an_object",
    ),
    plant(
        "settings-write-always-options",
        SET,
        "            if self.options:\n                document[GAME_OPTIONS_KEY] = self.options",
        "            document[GAME_OPTIONS_KEY] = self.options",
        TST + "test_the_file_holds_only_the_options_that_are_set",
        TST + "test_a_change_is_written_and_read_back",
    ),
    plant(
        "settings-write-drops-options",
        SET,
        "            if self.options:\n                document[GAME_OPTIONS_KEY] = self.options",
        "            pass",
        TST + "test_options_and_settings_share_the_file",
    ),
    plant(
        "settings-options-shared-copy",
        SET,
        "        return dict(self.options.get(player, {}))",
        "        return self.options.setdefault(player, {})",
        TST + "test_options_start_empty_and_are_per_player",
    ),
    plant(
        "settings-player-digits-only",
        SET,
        "        and player.isdigit()\n",
        "",
        TST + "test_a_bad_player_option_or_value_raises",
    ),
]

WIRING_PLANTS: list[Plant] = [
    plant(
        "server-pages-always-open",
        SRV,
        "    control.pages_open = lambda: len(app[SOCKETS])",
        "    control.pages_open = lambda: 1",
        TSV + "test_the_page_count_is_the_open_sockets",
    ),
    plant(
        "server-no-subscription",
        SRV,
        "    control.subscribe(tell_pages)",
        "    pass",
        TSV + "test_a_launch_is_pushed_to_every_page_and_the_ending_too",
    ),
    plant(
        "server-no-connect-pushes",
        SRV,
        "        for push in control.connect_pushes():\n            await _send(ws, push)\n",
        "",
        TSV + "test_a_page_that_connects_mid_battle",
    ),
    plant(
        "cli-engine-unchecked",
        PCL,
        "    elif not Path(args.engine).is_file():",
        "    elif False:",
        TCL + "test_an_engine_that_is_not_a_file_exits_2",
    ),
    plant(
        "cli-engine-options-alone",
        PCL,
        "        if args.engine_arg or args.pilot_folder:",
        "        if False:",
        TCL + "test_engine_options_without_an_engine_exit_2",
    ),
    plant(
        "cli-folder-not-default",
        PCL,
        "folder = Path(args.pilot_folder) if args.pilot_folder else default_pilot_folder()",
        'folder = Path(args.pilot_folder or ".")',
        TCL + "test_the_pilot_folder_defaults_to_the_games_user_folder",
    ),
    plant(
        "cli-folder-none-ok",
        PCL,
        "    if folder is None:\n        logger.error(",
        "    if False:\n        logger.error(",
        TCL + "test_a_system_with_no_default_folder_must_be_given_one",
    ),
    plant(
        "cli-engine-args-dropped",
        PCL,
        "Flight(args.engine, tuple(args.engine_arg), folder)",
        "Flight(args.engine, (), folder)",
        TCL + "test_engine_engine_args_and_pilot_folder_reach_the_control",
    ),
    plant(
        "cli-flight-not-passed",
        PCL,
        "__version__, flight)",
        "__version__)",
        TCL + "test_engine_engine_args_and_pilot_folder_reach_the_control",
    ),
    plant(
        "launch-cli-settings-dropped",
        LCL,
        "            settings=settings,\n",
        "            settings={},\n",
        TLC + "test_setting_flags_become_the_settings",
    ),
    plant(
        "launch-cli-first-setting-only",
        LCL,
        "for item in args.setting)",
        "for item in args.setting[:1])",
        TLC + "test_setting_flags_become_the_settings",
    ),
    plant(
        "launch-cli-no-parse",
        LCL,
        "parse_setting(item) for item in args.setting",
        "(item, True) for item in args.setting",
        TLC + "test_a_setting_flag_that_is_not_an_option_exits_2",
        TLC + "test_setting_flags_become",
    ),
]

SOLO_LIST_PLANTS: list[Plant] = [
    plant(
        "list-ampersand-offered",
        SOL,
        '    if item.marker == "&":\n        logger.debug',
        "    if False:\n        logger.debug",
        TSO + "test_a_mission_marked_ampersand_is_never_offered",
        "tests/test_bot_walk.py::test_the_missions_leave_out",
    ),
    plant(
        "list-star-held-back",
        SOL,
        'if item.marker == "&":',
        'if item.marker in ("&", "*"):',
        TSO + "test_a_mission_marked_star",
    ),
    plant(
        "list-odd-names-offered",
        SOL,
        "    if not FILE_PATTERN.match(name):",
        "    if False:",
        TSO + "test_a_file_name_the_launch_cannot_carry",
    ),
    plant(
        "list-repeats-offered",
        SOL,
        'if entry is None or entry["file"] in seen:',
        "if entry is None:",
        TSO + "test_a_repeated_file_is_offered_once",
    ),
    plant(
        "list-side-ignored",
        SOL,
        'view = side if mission_type in SIDED_TYPES else "network"',
        'view = "network"',
        TSO + "test_a_sided_kind_reads",
        TSO + "test_a_mission_marked",
    ),
    plant(
        "list-sided-take-none",
        SOL,
        "    if mission_type in SIDED_TYPES:\n        if side not in",
        "    if mission_type in SIDED_TYPES and False:\n        if side not in",
        TSO + "test_a_side_fits_a_kind_only",
    ),
    plant(
        "list-unsided-take-side",
        SOL,
        '    elif side != "none":',
        "    elif False:",
        TSO + "test_a_side_fits_a_kind_only",
        TSO + "test_reading_a_list_with_a_side",
    ),
    plant(
        "list-find-case",
        SOL,
        'if entry["file"] == file.lower():',
        'if entry["file"] == file:',
        TSO + "test_find_matches_a_file_in_any_letter_case",
    ),
    plant(
        "list-section-lost",
        SOL,
        "            section = item.name",
        '            section = ""',
        TSO + "test_sections_and_titles_come_with_the_entries",
    ),
    plant(
        "list-not-resolved",
        SOL,
        '    menu["resolved"] = True',
        '    menu["resolved"] = False',
        TL + "test_launch_menu_answers",
    ),
    plant(
        "list-title-is-file",
        SOL,
        '"title": item.title_line.text,',
        '"title": name,',
        TSO + "test_sections_and_titles_come_with_the_entries",
    ),
    plant(
        "list-bad-side-silent",
        SOL,
        "    if problem is not None:\n        raise ValueError(problem)",
        "    if False:\n        raise ValueError(problem)",
        TSO + "test_reading_a_list_with_a_side",
    ),
]

SOLO_PLANTS: list[Plant] = [
    *OPTION_PLANTS,
    *PILOT_PLANTS,
    *RUN_PLANTS,
    *BATTLE_PLANTS,
    *LAUNCHING_PLANTS,
    *CONTROL_PLANTS,
    *STORE_PLANTS,
    *WIRING_PLANTS,
    *SOLO_LIST_PLANTS,
]
