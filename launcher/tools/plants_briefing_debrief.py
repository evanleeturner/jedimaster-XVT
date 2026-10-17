"""The planted faults of the debriefing art: the set, the lists, the layers, the route.

Purpose:
    Break one rule of ``jedimaster/briefing/debrief.py`` (the 32 backgrounds,
    the shading of the overlay), of the list order it shares with
    ``side.py`` and of the ``/art/debrief/<name>`` route in
    ``page/server.py``.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run the suite, restore, and check that every test in
    ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``briefing-debrief-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_briefing_debrief import BRIEFING_DEBRIEF_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

D = "jedimaster/briefing/debrief.py"
SIDE = "jedimaster/briefing/side.py"
S = "jedimaster/page/server.py"
T = "tests/test_briefing_debrief.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``briefing-debrief-<pid>``."""
    return Plant(f"briefing-debrief-{pid}", path, old, new, expect)


BRIEFING_DEBRIEF_PLANTS: list[Plant] = [
    plant(
        "background-lost-from-the-set",
        D,
        '    *(f"deb{seq}{result}{side}" for seq in "bc" for result in "wlt" for side in "ir"),',
        '    *(f"deb{seq}{result}{side}" for seq in "c" for result in "wlt" for side in "ir"),',
        T + "test_the_set_is_79_names_and_the_32_backgrounds_are_the_game_code_s",
        T + "test_the_background_names_are_the_ones_the_game_code_registers",
    ),
    plant(
        "base-list-before-balance-of-power",
        SIDE,
        "for balance_of_power in (True, False):",
        "for balance_of_power in (False, True):",
        T + "test_balance_of_powers_list_is_read_before_the_base_list",
    ),
    plant(
        "overlay-laid-not-shaded",
        D,
        "screen[base + column] = shade(screen[base + column], colors[index])",
        "screen[base + column] = colors[index]",
        T + "test_the_overlay_is_shaded_over_the_screen_not_laid",
    ),
    plant(
        "route-not-added",
        S,
        '    app.router.add_get("/art/debrief/{name}", debrief)\n',
        "",
        T + "test_each_name_is_served",
    ),
]
