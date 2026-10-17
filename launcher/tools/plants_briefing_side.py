"""The planted faults of the side button sprites: the set, the lists, the picture, the route.

Purpose:
    Break one rule of ``jedimaster/briefing/side.py`` (the set of 48 names,
    the order the image lists are read in, the comment lines, the cache),
    of the clear-index-0 picture in ``under.py`` and of the
    ``/art/side/<name>`` route in ``page/server.py``.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run the suite, restore, and check that every test in
    ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``briefing-side-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_briefing_side import BRIEFING_SIDE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

D = "jedimaster/briefing/side.py"
U = "jedimaster/briefing/under.py"
S = "jedimaster/page/server.py"
T = "tests/test_briefing_side.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``briefing-side-<pid>``."""
    return Plant(f"briefing-side-{pid}", path, old, new, expect)


SET_PLANTS: list[Plant] = [
    plant(
        "piece-missing-from-the-set",
        D,
        '    "7litb",\n    "8lita",\n)',
        '    "7litb",\n)',
        T + "test_the_set_is_the_48_names",
    ),
]

LIST_PLANTS: list[Plant] = [
    plant(
        "base-list-before-balance-of-power",
        D,
        "for balance_of_power in (True, False):",
        "for balance_of_power in (False, True):",
        T + "test_balance_of_powers_list_is_read_before_the_base_list",
    ),
    plant(
        "base-list-never-read",
        D,
        "for balance_of_power in (True, False):",
        "for balance_of_power in (True,):",
        T + "test_the_base_file_answers_when_balance_of_powers_line_has_no_file",
    ),
    plant(
        "comment-lines-kept",
        D,
        "if at == 0 or not line.lstrip().startswith(COMMENT)",
        "if True",
        T + "test_comment_lines_and_trailing_spaces_are_ignored",
    ),
]

PICTURE_PLANTS: list[Plant] = [
    plant(
        "index-0-drawn-opaque",
        U,
        "alpha = 0 if clear is not None and clear[at] else 255",
        "alpha = 255",
        T + "test_a_run_length_bitmap_is_a_png_at_its_own_size_with_index_0_clear",
        T + "test_index_0_is_clear_whatever_its_color",
    ),
]

STORE_PLANTS: list[Plant] = [
    plant(
        "built-picture-not-kept",
        D,
        "        self._kept[name] = art\n",
        "",
        T + "test_a_built_picture_is_kept",
    ),
    plant(
        "failure-kept-as-a-picture",
        D,
        "        if png is None:\n            return None\n",
        "        if png is None:\n            self._kept[name] = Art(b'', PNG_TYPE)\n            return None\n",
        T + "test_a_failed_build_is_none_with_a_warning_and_tried_again",
        T + "test_the_route_tries_again_after_a_failed_build",
    ),
]

ROUTE_PLANTS: list[Plant] = [
    plant(
        "route-not-added",
        S,
        '    app.router.add_get("/art/side/{name}", side)\n',
        "",
        T + "test_each_side_name_is_served",
    ),
]

BRIEFING_SIDE_PLANTS: list[Plant] = [
    *SET_PLANTS,
    *LIST_PLANTS,
    *PICTURE_PLANTS,
    *STORE_PLANTS,
    *ROUTE_PLANTS,
]
