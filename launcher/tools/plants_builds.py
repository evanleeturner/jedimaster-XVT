"""The planted faults of the game-build match: the AI version check, the builds, the update, the doors.

Purpose:
    Break one rule of the build-match code per plant: the five sentences and
    the reading of ``ai_wire=`` lines in ``launch/ai_wire_check.py``, the
    current-build file and the pruning in ``launch/builds.py``, the artifact,
    the run choice and the verifying in ``launch/update.py``, and the three
    doors that start a battle (``page/launching.py``, ``page/cli.py`` and
    ``launch/cli.py``), so each test of them is shown to fail on a fault in
    the code it guards.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order, and
    applies each plant alone: write ``new`` over ``old`` in ``path``, run the
    suite, restore, and check that every test in ``expect`` failed. An
    ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``g1-``.
    - Each ``old`` text occurs exactly once in its file.
    - Not planted here, for lack of a runner: the engine's ``--version`` (C),
      the AI program's ``--version`` and ``ai/tools/stage_package.py``; the
      last two were changed by hand and each failed an ``ai/tests`` test.

Call:
    ``from plants_builds import BUILDS_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

WCK = "jedimaster/launch/ai_wire_check.py"
BLD = "jedimaster/launch/builds.py"
UPD = "jedimaster/launch/update.py"
LNG = "jedimaster/page/launching.py"
PCL = "jedimaster/page/cli.py"
LCL = "jedimaster/launch/cli.py"
TW = "tests/test_launch_ai_wire_check.py::"
TB = "tests/test_launch_builds.py::"
TU = "tests/test_launch_update.py::"
TL = "tests/test_page_launching.py::"
TC = "tests/test_page_cli.py::"
TM = "tests/test_launch_command.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``g1-<pid>``."""
    return Plant(f"g1-{pid}", path, old, new, expect)


WIRE_PLANTS: list[Plant] = [
    plant(
        "wire-mismatch-words",
        WCK,
        "so computer craft could not fly. Run",
        "so craft could not fly. Run",
        TW + "test_a_mismatch_says_both_versions_in_the_exact_sentence",
    ),
    plant(
        "wire-engine-too-old-words",
        WCK,
        "is too old to say which AI version",
        "is old and cannot say which AI version",
        TW + "test_an_engine_that_rejects_version_is_too_old",
        TW + "test_an_engine_that_runs_and_prints_no_wire_line_is_too_old",
    ),
    plant(
        "wire-engine-silent-words",
        WCK,
        "could not be asked which AI version it speaks",
        "could not be asked what version it speaks",
        TW + "test_an_engine_that_never_answers_is_silent_at_the_limit",
        TW + "test_an_engine_that_cannot_be_started_is_silent_not_too_old",
    ),
    plant(
        "wire-no-program-words",
        WCK,
        '"No AI program was found beside this game build, so computer craft could "',
        '"No AI program was found, so computer craft could "',
        TW + "test_no_ai_program_beside_the_engine_is_refused",
    ),
    plant(
        "wire-program-silent-words",
        WCK,
        '"The AI program in {folder} did not say which AI version it speaks: {why}."',
        '"The AI program in {folder} did not say its version: {why}."',
        TW + "test_an_ai_program_with_no_version_line_says_why",
        TW + "test_a_folder_without_main_is_the_programs_silence",
    ),
    plant(
        "wire-pattern-matches-inside-a-line",
        WCK,
        "found = WIRE_LINE.fullmatch(line.strip())",
        "found = WIRE_LINE.search(line.strip())",
        TW + "test_a_wire_line_inside_a_longer_line_is_no_answer",
    ),
    plant(
        "wire-engine-wire-from-first-line",
        WCK,
        "    engine_wire = _wire_of(lines)\n",
        "    engine_wire = _wire_of(lines[:1])\n",
        TW + "test_nothing_is_remembered_between_calls",
        TW + "test_a_named_folder_wins_over_the_packaged_one",
    ),
    plant(
        "wire-engine-wire-skips-first-line",
        WCK,
        "    engine_wire = _wire_of(lines)\n",
        "    engine_wire = _wire_of(lines[1:])\n",
        TW + "test_the_wire_line_is_found_on_any_line_of_either_answer",
    ),
    plant(
        "wire-engine-says-wire-from-first-line",
        WCK,
        "    return text, _wire_of(lines)",
        "    return text, _wire_of(lines[:1])",
        TW + "test_the_engine_is_asked_with_version_alone",
    ),
    plant(
        "wire-program-wire-from-first-line",
        WCK,
        "        program_wire = _wire_of(lines)\n",
        "        program_wire = _wire_of(lines[:1])\n",
        TW + "test_the_wire_line_is_found_on_any_line_of_either_answer",
    ),
    plant(
        "wire-compare-dropped",
        WCK,
        "    if engine_wire != program_wire:",
        "    if False:",
        TW + "test_nothing_is_remembered_between_calls",
        TW + "test_a_mismatch_says_both_versions_in_the_exact_sentence",
    ),
    plant(
        "wire-compare-inverted",
        WCK,
        "    if engine_wire != program_wire:",
        "    if engine_wire == program_wire:",
        TW + "test_nothing_is_remembered_between_calls",
        TW + "test_a_named_folder_wins_over_the_packaged_one",
    ),
    plant(
        "wire-linux-candidate-wrong-place",
        WCK,
        '        folder / "ai" / "jedimaster_ai",\n',
        '        folder / "jedimaster_ai",\n',
        TW + "test_nothing_is_remembered_between_calls",
        TW + "test_an_engine_that_rejects_version_is_too_old",
    ),
    plant(
        "wire-macos-candidate-wrong-place",
        WCK,
        'folder.parent / "Resources" / "ai" / "jedimaster_ai"',
        'folder.parent / "ai" / "jedimaster_ai"',
        TW + "test_the_packaged_program_is_found_in_both_package_layouts",
    ),
    plant(
        "wire-main-not-needed",
        WCK,
        '        if (candidate / "__main__.py").is_file():',
        "        if candidate.is_dir():",
        TW + "test_a_packaged_folder_without_main_is_no_program",
    ),
    plant(
        "wire-time-limit-ignored",
        WCK,
        "            timeout=ASK_SECONDS,\n",
        "            timeout=None,\n",
        TW + "test_an_engine_that_never_answers_is_silent_at_the_limit",
        TW + "test_a_program_that_never_answers_is_stopped_at_the_limit",
    ),
    plant(
        "wire-unstartable-engine-called-too-old",
        WCK,
        "    if not ran:\n        raise PairRefused(ENGINE_SILENT",
        "    if False:\n        raise PairRefused(ENGINE_SILENT",
        TW + "test_an_engine_that_never_answers_is_silent_at_the_limit",
        TW + "test_an_engine_that_cannot_be_started_is_silent_not_too_old",
    ),
    plant(
        "wire-slow-engine-called-ran",
        WCK,
        'return [], f"it did not answer within {ASK_SECONDS:g} seconds", False',
        'return [], f"it did not answer within {ASK_SECONDS:g} seconds", True',
        TW + "test_an_engine_that_never_answers_is_silent_at_the_limit",
    ),
    plant(
        "wire-unstartable-engine-called-ran",
        WCK,
        "        return [], why, False\n    if done",
        "        return [], why, True\n    if done",
        TW + "test_an_engine_that_cannot_be_started_is_silent_not_too_old",
    ),
    plant(
        "wire-bad-exit-engine-called-silent",
        WCK,
        "if detail else reason, True",
        "if detail else reason, False",
        TW + "test_an_engine_that_rejects_version_is_too_old",
    ),
    plant(
        "wire-other-python",
        WCK,
        '[sys.executable, program["path"], "--version"]',
        '["python3", program["path"], "--version"]',
        TW + "test_the_ai_program_is_run_by_the_launchers_own_python",
    ),
    plant(
        "wire-engine-asked-without-version",
        WCK,
        'lines, why, ran = _ask([str(engine), "--version"])',
        "lines, why, ran = _ask([str(engine)])",
        TW + "test_nothing_is_remembered_between_calls",
        TW + "test_a_named_folder_wins_over_the_packaged_one",
    ),
]

BUILD_PLANTS: list[Plant] = [
    plant(
        "builds-current-from-previous",
        BLD,
        "    name = _named(builds, CURRENT)\n    if name is None",
        "    name = _named(builds, PREVIOUS)\n    if name is None",
        TB + "test_current_names_the_engine_of_the_build_it_holds",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
    ),
    plant(
        "builds-current-not-stripped",
        BLD,
        '.read_text(encoding="utf-8").strip()',
        '.read_text(encoding="utf-8")',
        TB + "test_current_names_the_engine_of_the_build_it_holds",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
    ),
    plant(
        "builds-current-with-separator-kept",
        BLD,
        "if not name or name != Path(name).name or",
        "if not name or",
        TB + "test_a_current_file_with_a_path_in_it_is_not_followed_out_of_the_folder",
    ),
    plant(
        "builds-current-written-in-place",
        BLD,
        '    temporary = path.with_name(path.name + ".tmp")\n    temporary.write_text(text + "\\n", encoding="utf-8")\n    os.replace(temporary, path)',
        '    path.write_text(text + "\\n", encoding="utf-8")',
        TB + "test_set_current_goes_through_a_temporary_file_and_a_replace",
    ),
    plant(
        "builds-prune-removes-current-and-replaced",
        BLD,
        "        if name in protected:",
        "        if False:",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
        TU + "test_only_the_newest_three_installs_stay_but_current_and_previous_do",
    ),
    plant(
        "builds-prune-removes-current",
        BLD,
        "    protected = {_named(builds, CURRENT), _named(builds, PREVIOUS)}",
        "    protected = {_named(builds, PREVIOUS)}",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
    ),
    plant(
        "builds-prune-removes-replaced",
        BLD,
        "    protected = {_named(builds, CURRENT), _named(builds, PREVIOUS)}",
        "    protected = {_named(builds, CURRENT)}",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
        TU + "test_only_the_newest_three_installs_stay_but_current_and_previous_do",
    ),
    plant(
        "builds-keep-two",
        BLD,
        "KEEP = 3\n",
        "KEEP = 2\n",
        TB + "test_prune_keeps_the_three_newest",
        TB + "test_prune_never_removes_the_current_build_or_the_one_it_replaced",
    ),
    plant(
        "builds-keep-four",
        BLD,
        "KEEP = 3\n",
        "KEEP = 4\n",
        TB + "test_prune_keeps_the_three_newest",
        TU + "test_only_the_newest_three_installs_stay_but_current_and_previous_do",
    ),
    plant(
        "builds-order-by-folder-time",
        BLD,
        "    return (marker if marker.is_file() else build).stat().st_mtime",
        "    return build.stat().st_mtime",
        TB + "test_prune_keeps_the_three_newest",
        TB + "test_installed_lists_newest_first_and_skips_partials_and_empties",
    ),
    plant(
        "builds-partial-counted-as-build",
        BLD,
        'if p.is_dir() and not p.name.endswith(".partial") and engine_in(p)',
        "if p.is_dir() and engine_in(p)",
        TB + "test_a_partial_install_with_an_engine_is_never_current_or_listed",
    ),
    plant(
        "builds-current-partial-kept",
        BLD,
        'or name != Path(name).name or name.endswith(".partial"):',
        "or name != Path(name).name:",
        TB + "test_a_partial_install_with_an_engine_is_never_current_or_listed",
    ),
    plant(
        "builds-current-without-engine-kept",
        BLD,
        "    if name is None or engine_in(builds / name) is None:",
        "    if name is None:",
        TB + "test_a_current_file_that_names_no_build_reads_as_none",
    ),
]

UPDATE_PLANTS: list[Plant] = [
    plant(
        "update-linux-artifact-name",
        UPD,
        '"openxvt-linux-x86_64-release", ".tar.xz"',
        '"openxvt-linux-x86_64", ".tar.xz"',
        TU + "test_list_marks_the_current_build",
        TU + "test_a_build_with_no_ai_program_is_refused",
    ),
    plant(
        "update-windows-artifact-name",
        UPD,
        '"openxvt-windows-x86_64-release", ".zip"',
        '"openxvt-windows-x86_64", ".zip"',
        TU + "test_the_artifact_and_package_kind_per_system",
    ),
    plant(
        "update-macos-not-refused",
        UPD,
        '    raise UpdateError(MACOS if platform == "darwin" else OTHER_SYSTEM)',
        '    return "openxvt-linux-x86_64-release", ".tar.xz"',
        TU + "test_macos_says_updating_is_not_built_yet",
        TU + "test_the_artifact_and_package_kind_per_system",
    ),
    plant(
        "update-newest-run-not-limited-to-main",
        UPD,
        '        query = f"{base}&branch={BRANCH}&event=push"',
        '        query = f"{base}&event=push"',
        TU + "test_the_newest_main_run_is_installed_and_made_current",
    ),
    plant(
        "update-newest-run-not-limited-to-push",
        UPD,
        '        query = f"{base}&branch={BRANCH}&event=push"',
        '        query = f"{base}&branch={BRANCH}"',
        TU + "test_the_newest_main_run_is_installed_and_made_current",
    ),
    plant(
        "update-newest-run-not-limited-to-success",
        UPD,
        'runs?status=success&per_page=1"',
        'runs?per_page=1"',
        TU + "test_the_newest_main_run_is_installed_and_made_current",
    ),
    plant(
        "update-commit-check-dropped",
        UPD,
        "    if not (name.startswith(text) or text.startswith(name)):",
        "    if False:",
        TU + "test_a_build_that_names_another_commit_is_refused_and_leaves_nothing",
    ),
    plant(
        "update-wire-check-dropped",
        UPD,
        "        check_pair(engine, None)\n    except PairRefused as exc:",
        "        pass\n    except PairRefused as exc:",
        TU + "test_a_build_with_no_ai_program_is_refused",
        TU + "test_a_build_whose_ai_program_does_not_match_is_refused",
    ),
    plant(
        "update-failed-verify-leaves-folder",
        UPD,
        "    finally:\n        shutil.rmtree(partial, ignore_errors=True)\n    return name",
        "    finally:\n        pass\n    return name",
        TU + "test_a_build_with_no_ai_program_is_refused",
        TU + "test_a_build_whose_ai_program_does_not_match_is_refused",
    ),
    plant(
        "update-gh-missing-not-refused",
        UPD,
        '    if shutil.which("gh") is None:\n        raise UpdateError(NO_GH)\n    try:\n        done = subprocess.run(\n            ["gh", "auth", "status"],\n            capture_output=True,\n            text=True,\n            timeout=60.0,\n            check=False,\n        )\n    except (OSError, subprocess.TimeoutExpired):\n        raise UpdateError(NO_GH) from None',
        '    done = subprocess.run(\n        ["gh", "auth", "status"],\n        capture_output=True,\n        text=True,\n        timeout=60.0,\n        check=False,\n    )',
        TU + "test_without_gh_one_sentence_and_exit_one",
    ),
    plant(
        "update-not-logged-in-not-refused",
        UPD,
        "    if done.returncode != 0:\n        raise UpdateError(NOT_LOGGED_IN)",
        "    if False:\n        raise UpdateError(NOT_LOGGED_IN)",
        TU + "test_not_logged_in_one_sentence_and_exit_one",
    ),
    plant(
        "update-use-without-verify",
        UPD,
        '            verify(folder / name, name)\n            message = f"Now using',
        '            message = f"Now using',
        TU + "test_use_refuses_an_unknown_build_and_one_that_fails_its_check",
    ),
    plant(
        "update-current-build-not-said",
        UPD,
        "            if name == before:",
        "            if False:",
        TU + "test_the_build_already_current_is_said_so_without_a_download",
    ),
]

DOOR_PLANTS: list[Plant] = [
    plant(
        "door-launching-mismatch-flies-anyway",
        LNG,
        "        except PairRefused as exc:\n            return self._refused(str(exc))",
        '        except PairRefused as exc:\n            logger.warning("%s", exc)\n            pair = type("Pair", (), {"program": None})()',
        TL + "test_the_pair_is_looked_up_again_for_every_battle",
        TL + "test_an_old_engine_and_a_missing_ai_program_each_get_their_sentence",
    ),
    plant(
        "door-launching-flight-folder-not-pair-program",
        LNG,
        "            ai_program=pair.program,",
        "            ai_program=self.flight.ai_program,",
        TL + "test_the_flights_ai_program_rides_in_the_description",
    ),
    plant(
        "door-launching-no-build-not-refused",
        LNG,
        "        if engine is None:\n            return self._refused(NO_BUILD)",
        "        if False:\n            return self._refused(NO_BUILD)",
        TL + "test_a_flight_without_an_engine_flies_the_current_build_at_each_battle",
    ),
    plant(
        "door-launching-engine-not-from-the-build",
        LNG,
        "        engine = self.flight.engine or current_engine(self.flight.builds)",
        "        engine = self.flight.engine",
        TL + "test_a_flight_without_an_engine_flies_the_current_build_at_each_battle",
    ),
    plant(
        "door-page-engine-looked-up-once-at-start",
        PCL,
        "        args.engine or None,\n",
        "        args.engine or (str(current_engine(builds)) if current_engine(builds) else None),\n",
        TC + "test_without_an_engine_the_current_build_is_flown_at_each_battle",
    ),
    plant(
        "door-page-start-warning-dropped",
        PCL,
        '        logger.warning("%s", exc)',
        "        pass",
        TC + "test_a_mismatched_pair_is_a_warning_at_start_and_the_launcher_runs",
    ),
    plant(
        "door-page-start-check-not-run",
        PCL,
        "    if flight is not None:\n        warn_if_unmatched(flight)",
        "    if flight is not None:\n        pass",
        TC + "test_a_mismatched_pair_is_a_warning_at_start_and_the_launcher_runs",
    ),
    plant(
        "door-launch-cli-unchecked",
        LCL,
        "        ai_program = check_pair(engine, args.ai_program).program",
        "        ai_program = ai_program_of(args.ai_program) if args.ai_program else None",
        TM + "test_no_ai_program_beside_the_engine_exits_1",
        TM + "test_the_ai_program_flag_reaches_the_description",
    ),
    plant(
        "door-launch-cli-no-build-not-refused",
        LCL,
        "        if found is None:\n            logger.error(",
        "        if False:\n            logger.error(",
        TM + "test_without_engine_or_build_exits_2",
    ),
]

BUILDS_PLANTS: list[Plant] = [
    *WIRE_PLANTS,
    *BUILD_PLANTS,
    *UPDATE_PLANTS,
    *DOOR_PLANTS,
]
