"""The planted faults of showing and briefing a solo list entry by side and file.

Purpose:
    Break one rule per plant of the second argument shape of
    ``page.show_mission`` and ``briefing.get`` (``page/control.py``,
    ``page/solo.py``): the side that picks the list, the letter case of the
    file match, the file in the push, and the refusal of an unsided kind.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in
    ``expect`` failed. An ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``page-solo-show-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_page_show_solo import PAGE_SOLO_SHOW_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

CTL = "jedimaster/page/control.py"
SOL = "jedimaster/page/solo.py"
TS = "tests/test_page_show_solo.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return one plant with the table's id prefix."""
    return Plant(f"page-solo-show-{pid}", path, old, new, expect)


PAGE_SOLO_SHOW_PLANTS: list[Plant] = [
    plant(
        "ignores-its-side",
        CTL,
        '        if "file" in args:\n            entry = find(solo_menu(self.install, kind, args["side"]), args["file"])',
        '        if "file" in args:\n            entry = find(solo_menu(self.install, kind, "rebel"), args["file"])',
        TS + "test_a_file_not_in_that_sides_list_shows_nothing",
    ),
    plant(
        "briefing-ignores-its-side",
        CTL,
        '            return mission_file(self.install, kind, args["id"])\n        entry = find(solo_menu(self.install, kind, args["side"]), args["file"])',
        '            return mission_file(self.install, kind, args["id"])\n        entry = find(solo_menu(self.install, kind, "rebel"), args["file"])',
        TS + "test_a_file_not_in_that_sides_list_shows_nothing",
        TS + "test_the_other_sides_entry_is_briefed_from_its_own_list",
    ),
    plant(
        "file-matched-in-exact-case-only",
        SOL,
        '        if entry["file"] == file.lower():',
        '        if entry["file"] == file:',
        TS + "test_the_file_matches_in_any_letter_case_and_is_pushed_as_listed",
        TS + "test_a_solo_entry_is_briefed",
    ),
    plant(
        "push-drops-the-file",
        CTL,
        '                "file": entry["file"],\n                "title": entry["title"],\n            }\n            return self._show(data)',
        '                "title": entry["title"],\n            }\n            return self._show(data)',
        TS + "test_a_solo_entry_is_shown_with_its_side_and_file",
        TS + "test_a_new_page_gets_the_last_solo_mission_shown",
    ),
    plant(
        "unsided-kind-takes-the-solo-shape",
        CTL,
        "    if kind not in SIDED_TYPES:\n",
        "    if False:\n",
        TS + "test_bad_solo_arguments_are_refused_by_both_commands",
    ),
]
