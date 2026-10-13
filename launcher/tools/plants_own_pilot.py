"""The planted faults of the launcher's own pilots: the name rule, the store, the bot's steps.

Purpose:
    Break one rule of ``fit_name`` (``launch/pilots.py``), the pilot files
    (``launch/pilot_store.py``), the bot's pilot steps (``pilot_walk``,
    ``core``), the table's names (``registry``) and the door's buttons and
    window per plant.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in
    ``expect`` failed. An ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``own-pilot-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_own_pilot import OWN_PILOT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

PIL = "jedimaster/launch/pilots.py"
PST = "jedimaster/launch/pilot_store.py"
PWK = "jedimaster/bot/pilot_walk.py"
COR = "jedimaster/bot/core.py"
REG = "jedimaster/bot/registry.py"
DOR = "jedimaster/bot/discord_door.py"
TS = "tests/test_pilot_store.py::"
TB = "tests/test_bot_pilot.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return one plant with the table's id prefix."""
    return Plant(f"own-pilot-{pid}", path, old, new, expect)


FIT_PLANTS: list[Plant] = [
    plant(
        "fit-accents-kept",
        PIL,
        '    plain = unicodedata.normalize("NFKD", text)',
        "    plain = text",
        TS + "test_a_name_is_fitted_to_the_rule",
    ),
    plant(
        "fit-switch-words-once",
        PIL,
        "    while kept != previous:\n        previous = kept\n",
        "    for _ in (1,):\n        previous = kept\n",
        TS + "test_a_switch_word_made_by_taking_one_out_is_taken_out_too",
    ),
    plant(
        "fit-spaces-kept",
        PIL,
        '    kept = _trimmed(re.sub(" +", " ", kept))',
        "    kept = _trimmed(kept)",
        TS + "test_a_name_is_fitted_to_the_rule",
    ),
    plant(
        "fit-cut-untrimmed",
        PIL,
        "    kept = _trimmed(kept[:NAME_LIMIT])",
        "    kept = kept[:NAME_LIMIT]",
        TS + "test_a_name_cut_at_twelve_ends_on_a_letter_or_digit",
        TS + "test_every_fitted_name_passes_the_rule_or_is_empty",
    ),
    plant(
        "fit-symbols-kept",
        PIL,
        "c.isascii() and (c.isalnum() or c in EXTRA_CHARACTERS)\n    )",
        "c.isascii()\n    )",
        TS + "test_a_name_is_fitted_to_the_rule",
    ),
]

STORE_PLANTS: list[Plant] = [
    plant(
        "store-format-unchecked",
        PST,
        '            or data["format"] != FORMAT\n',
        "",
        TS + "test_a_file_that_is_not_this_account_s_pilot_is_refused",
    ),
    plant(
        "store-account-unchecked",
        PST,
        '            or data["account"] != str(account)\n',
        "",
        TS + "test_a_file_that_is_not_this_account_s_pilot_is_refused",
    ),
    plant(
        "store-name-unchecked",
        PST,
        '            or not name_ok(data["name"])\n',
        "",
        TS + "test_a_file_that_is_not_this_account_s_pilot_is_refused",
    ),
    plant(
        "store-extra-keys-allowed",
        PST,
        "            or set(data) != KEYS\n",
        "            or not set(data) >= KEYS\n",
        TS + "test_a_file_that_is_not_this_account_s_pilot_is_refused",
    ),
    plant(
        "store-make-writes-over",
        PST,
        "        if self.path(account).exists():",
        "        if False:",
        TS + "test_a_pilot_is_not_made_twice",
        TS + "test_a_bad_file_is_never_written_over",
    ),
    plant(
        "store-renamed-twice",
        PST,
        "        if old.renamed:",
        "        if False:",
        TS + "test_a_pilot_is_renamed_once",
    ),
    plant(
        "store-rename-unmarked",
        PST,
        "        pilot = Pilot(account, name, renamed=True)",
        "        pilot = Pilot(account, name, renamed=False)",
        TS + "test_a_pilot_is_renamed_once",
        TB + "test_after_the_change_no_other_is_allowed",
    ),
    plant(
        "store-temp-left",
        PST,
        "        os.replace(temp, path)",
        "        path.write_bytes(temp.read_bytes())",
        TS + "test_no_temporary_file_is_left",
    ),
    plant(
        "first-name-no-account-name",
        PST,
        '    for text in (display or "", account_name):',
        '    for text in (display or "",):',
        TS + "test_the_first_name_falls_back_to_the_account_name",
        TB + "test_the_pilot_is_named_from_the_account_name",
    ),
]

WALK_PLANTS: list[Plant] = [
    plant(
        "walk-renamed-offered",
        PWK,
        "        if pilot.renamed:\n            return Reply(SHOWN",
        "        if False:\n            return Reply(SHOWN",
        TB + "test_a_renamed_pilot_is_shown_without_buttons",
    ),
    plant(
        "walk-pilot-anywhere",
        PWK,
        '        refusal = self._gate("pilot", user_id, in_dm, dm_only=True)',
        '        refusal = self._gate("pilot", user_id, in_dm, dm_only=False)',
        TB + "test_the_pilot_command_is_for_the_linked_account_in_a_direct_message",
    ),
    plant(
        "walk-change-after-rename",
        PWK,
        "        if pilot.renamed:\n"
        '            self._end("change_name", user_id, "no change left")',
        "        if False:\n"
        '            self._end("change_name", user_id, "no change left")',
        TB + "test_after_the_change_no_other_is_allowed",
    ),
    plant(
        "walk-spaces-kept",
        PWK,
        "        name = typed.strip()",
        "        name = typed",
        TB + "test_a_typed_name_is_the_one_change",
    ),
    plant(
        "walk-same-name-used",
        PWK,
        "        if name == pilot.name:",
        "        if False:",
        TB + "test_the_same_name_changes_nothing",
    ),
    plant(
        "walk-bad-name-buttons-gone",
        PWK,
        "            return Reply(BAD_NAME, buttons=(KEEP, CHANGE), edit=True)",
        "            return Reply(BAD_NAME, edit=True)",
        TB + "test_a_name_off_the_rule_changes_nothing_and_keeps_the_buttons",
    ),
    plant(
        "walk-bad-file-made-over",
        PWK,
        "        except PilotFileError as exc:\n"
        '            logger.warning("pilot of account %d: %s", user_id, exc)\n'
        "            return Reply(BAD_FILE)\n",
        "        except PilotFileError:\n"
        "            pilot = self.pilots.make(user_id, 'Ace')\n",
        TB + "test_a_bad_pilot_file_is_said_and_left_alone",
    ),
]

CORE_PLANTS: list[Plant] = [
    plant(
        "core-link-makes-no-pilot",
        COR,
        "        if outcome != linking.LINKED:",
        "        if True:",
        TB + "test_the_first_link_makes_the_pilot_and_offers_the_name",
    ),
    plant(
        "core-status-no-pilot",
        COR,
        "        lines.append(self._pilot_line(user_id))\n",
        "",
        TB + "test_status_names_the_pilot",
        TB + "test_status_says_when_there_is_no_pilot",
    ),
]

DOOR_PLANTS: list[Plant] = [
    plant(
        "registry-names-dropped",
        REG,
        "    return head + (names,) if item.names else head",
        "    return head",
        TB + "test_the_slash_link_makes_the_pilot_from_the_asker_s_names",
    ),
    plant(
        "door-display-name-ignored",
        DOR,
        '    return (user.global_name or "", user.name)',
        "    return (user.name, user.name)",
        TB + "test_the_slash_link_makes_the_pilot_from_the_asker_s_names",
    ),
    plant(
        "door-window-not-opened",
        DOR,
        "        if reply.window is not None:",
        "        if False:",
        TB + "test_change_name_opens_the_window_as_the_first_answer",
    ),
    plant(
        "door-keep-unknown",
        DOR,
        '        elif spec.kind == "pilot_keep":',
        '        elif spec.kind == "pilot_kept":',
        TB + "test_keep_name_edits_the_message",
    ),
    plant(
        "door-typed-name-dropped",
        DOR,
        '        if window.kind != "pilot_name":',
        '        if window.kind == "pilot_name":',
        TB + "test_the_typed_name_renames_and_edits_the_message",
    ),
]

OWN_PILOT_PLANTS: list[Plant] = [
    *FIT_PLANTS,
    *STORE_PLANTS,
    *WALK_PLANTS,
    *CORE_PLANTS,
    *DOOR_PLANTS,
]
