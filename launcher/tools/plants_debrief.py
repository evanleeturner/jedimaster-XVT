"""The planted faults of the kept debriefings: the builder, the store, the push, the bot.

Purpose:
    Break one rule of ``jedimaster/page/debrief.py``, ``results.py``, the
    battle's ending in ``battles.py``, the two results commands and the bot's
    ``/results`` per plant, so each of their tests is shown to fail on a fault
    in the code it guards: a number dropped, two fields swapped, a stale
    revision, a name read from the wrong entry, a store that keeps 21.

Flow:
    ``plant_faults`` joins this table with the others and applies each plant
    alone: write ``new`` over ``old`` in ``path``, run the suite, restore, and
    check that every test in ``expect`` failed. A plant in the protocol's
    model sets ``regen_schema`` so the published schema follows the fault.

Invariants:
    - Ids are unique across all tables and start with ``debrief-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_debrief import DEBRIEF_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

DEB = "jedimaster/page/debrief.py"
RES = "jedimaster/page/results.py"
BAT = "jedimaster/page/battles.py"
CON = "jedimaster/page/control.py"
PRO = "jedimaster/page/protocol.py"
BOT = "jedimaster/bot/core.py"
T_DEB = "tests/test_page_debrief.py::"
T_RES = "tests/test_page_results.py::"
T_BAT = "tests/test_page_battles.py::"
T_CON = "tests/test_page_control.py::"
T_BOT = "tests/test_bot_core.py::"

DEBRIEF_PLANTS: list[Plant] = [
    Plant(
        "debrief-player-kills-dropped",
        DEB,
        '    "assists",\n    "losses",\n    "shots",',
        '    "assists",\n    "shots",',
        (T_DEB + "test_every_number_is_the_results_own",),
    ),
    Plant(
        "debrief-kills-losses-swapped",
        DEB,
        "        debrief[name] = result[name]",
        '        debrief[name] = result[{"kills": "losses", "losses": "kills"}.get(name, name)]',
        (T_DEB + "test_every_number_is_the_results_own",),
    ),
    Plant(
        "debrief-rank-entry",
        DEB,
        "RANK_ENTRY = 154\n",
        "RANK_ENTRY = 155\n",
        (
            T_DEB + "test_each_number_gets_the_name_the_1997_debriefing_draws",
            T_DEB + "test_names_come_from_the_install_front_text_and_the_mission",
        ),
    ),
    Plant(
        "debrief-craft-entry",
        DEB,
        "CRAFT_ENTRY = 21\n",
        "CRAFT_ENTRY = 20\n",
        (
            T_DEB + "test_each_number_gets_the_name_the_1997_debriefing_draws",
            T_DEB + "test_names_come_from_the_install_front_text_and_the_mission",
        ),
    ),
    Plant(
        "debrief-flight-group-of-entry",
        DEB,
        '    row["flight_group_name"] = names.flight_group(player["flight_group"])',
        '    row["flight_group_name"] = names.flight_group(player["entry"])',
        (T_DEB + "test_each_number_gets_the_name_the_1997_debriefing_draws",),
    ),
    Plant(
        "debrief-version-3-read-whole",
        DEB,
        '    if result["schema_version"] < 4:',
        '    if result["schema_version"] < 3:',
        (T_DEB + "test_an_older_result_gives_no_tables[3]",),
    ),
    Plant(
        "debrief-store-keeps-21",
        RES,
        "        for old in sorted(self._kept())[: -self.keep]:",
        "        for old in sorted(self._kept())[: -self.keep - 1]:",
        (T_RES + "test_the_newest_twenty_are_kept_and_the_oldest_dropped",),
    ),
    Plant(
        "debrief-store-oldest-first",
        RES,
        "        for result_id in sorted(self._kept(), reverse=True):",
        "        for result_id in sorted(self._kept()):",
        (
            T_RES + "test_ids_count_from_one_and_the_newest_comes_first",
            T_BOT + "test_results_lists_the_newest_first_one_line_each",
        ),
    ),
    Plant(
        "debrief-store-ids-reused",
        RES,
        "        return max(self._kept(), default=0) + 1",
        "        return len(self._kept()) + 1",
        (T_RES + "test_the_newest_twenty_are_kept_and_the_oldest_dropped",),
    ),
    Plant(
        "debrief-store-reads-another-file",
        RES,
        '        if not isinstance(debrief, dict) or debrief.get("result_id") != result_id:',
        "        if not isinstance(debrief, dict):",
        (T_RES + "test_a_broken_file_is_skipped_kept_and_warned_about",),
    ),
    Plant(
        "debrief-kept-when-refused",
        BAT,
        '        if status == "flown" and result is not None and self._keep is not None:',
        "        if result is not None and self._keep is not None:",
        (T_BAT + "test_a_battle_not_flown_carries_no_debriefing[refuse]",),
    ),
    Plant(
        "debrief-keep-failure-stops-the-push",
        BAT,
        "            except (OSError, KeyError) as exc:",
        "            except KeyError as exc:",
        (T_BAT + "test_a_debriefing_that_cannot_be_kept_still_ends_the_battle",),
    ),
    Plant(
        "debrief-results-get-takes-zero",
        CON,
        '    if not _is_int(args["result_id"]) or not 1 <= args["result_id"] <= MAX_ID:',
        '    if not _is_int(args["result_id"]) or not 0 <= args["result_id"] <= MAX_ID:',
        (T_RES + "test_results_get_refuses_bad_arguments[args1]",),
    ),
    Plant(
        "debrief-stale-revision",
        PRO,
        "SCHEMA_REVISION = 9\n",
        "SCHEMA_REVISION = 8\n",
        (
            T_CON + "test_the_list_has_fifteen_commands_and_revision_9",
            T_CON + "test_hello",
        ),
    ),
    Plant(
        "debrief-bot-lists-twenty",
        BOT,
        "RESULT_LINES = 10\n",
        "RESULT_LINES = 20\n",
        (T_BOT + "test_results_lists_ten_and_counts_the_rest",),
    ),
]
