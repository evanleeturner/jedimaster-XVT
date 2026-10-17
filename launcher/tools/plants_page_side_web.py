"""The planted faults of the page's side buttons: the rules, the state, the markup, the style.

Purpose:
    Break one rule of the eight side buttons per plant: the order the
    lights are taken in, the lita exception, the step's wrap and skip, the
    remembered mission, the right click, the pressed state and the plain
    layout's hiding of the buttons.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-side-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>`` and
      match by prefix.

Call:
    ``from plants_page_side_web import SIDE_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

SID = "page/src/frame/side.ts"
COD = "page/src/codec.ts"
STA = "page/src/state.ts"
MAI = "page/src/main.ts"
CSS = "page/public/frame.css"
N = "node: "
B = "browser: "


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``web-side-<pid>``."""
    return Plant(f"web-side-{pid}", path, old, new, expect)


SIDE_WEB_PLANTS: list[Plant] = [
    plant(
        "lights-order-lost",
        SID,
        "const LIGHT_ORDER = [1, 2, 3, 4, 5, 8, 6, 7] as const;",
        "const LIGHT_ORDER = [1, 2, 3, 4, 5, 6, 7, 8] as const;",
        N + "lights: the neighbours' states are the digits",
        N + "lights: the first and seventh positions have no lita",
    ),
    plant(
        "lights-first-position-has-lita",
        SID,
        "if (at !== 0 && at !== 6) {",
        "if (at !== 6) {",
        N + "lights: the first and seventh positions have no lita",
    ),
    plant(
        "step-never-wraps",
        SID,
        "      choices[(((start + direction * lap) % count) + count) % count];",
        "      choices[start + direction * lap];",
        N + "a step skips what is not available and wraps",
    ),
    plant(
        "step-takes-an-unavailable-mission",
        SID,
        "if (choice?.available === true) return choice.key;",
        "if (choice !== undefined) return choice.key;",
        N + "a step skips what is not available and wraps",
    ),
    plant(
        "side-switch-forgets-the-remembered-mission",
        SID,
        "    fresh && keptOpen",
        "    fresh && keptOpen && false",
        N + "a side switch returns to the mission remembered for that side",
    ),
    plant(
        "right-click-steps-forward",
        MAI,
        "    onSideButton(id, true);",
        "    onSideButton(id, false);",
        B + "a right click and Shift step back",
    ),
    plant(
        "pressed-state-never-set",
        MAI,
        '    element.setAttribute("aria-pressed", String(button.selected));',
        '    element.setAttribute("aria-pressed", "false");',
        B + "a type click shows that kind's mission and presses its button",
    ),
    plant(
        "plain-layout-keeps-the-buttons",
        CSS,
        ".side-col,\n.side-sprites,\n.side-note {\n  display: none;\n}",
        ".side-col,\n.side-sprites,\n.side-note {\n  display: block;\n}",
        B + "below 640 x 480 the buttons are gone from the page and the tab order",
    ),
    plant(
        "sided-step-uses-the-network-availability",
        SID,
        "      available: true,\n    }));",
        "      available:\n        lists.menus\n          ?.find((m) => m.mission_type === kind)\n          ?.entries.find((e) => e.file.toLowerCase() === file)?.available ===\n        true,\n    }));",
        N + "a sided list is that side's solo list itself, every entry offered",
        N + "a file the network menu marks not available is reached from its solo list",
    ),
    plant(
        "solo-mark-matches-in-exact-case-only",
        STA,
        "    ? entry.file.toLowerCase() === shown.file.toLowerCase()",
        "    ? entry.file === shown.file",
        N + "a solo mission is matched to a network row by file in any letter case",
    ),
    plant(
        "solo-push-ignores-an-extra-id",
        COD,
        '    if (hasExactly(data, ["mission_type", "side", "file", "title"])) {',
        '    if ("file" in data) {',
        N + "a solo show-mission push refuses a wrong payload",
    ),
    plant(
        "solo-press-asks-by-id",
        SID,
        "    : { mission_type: type, side: key.side, file: key.file };",
        "    : { mission_type: type, id: 0 };",
        N + "the arguments name a mission by id or by side and file",
    ),
]
