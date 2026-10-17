"""The planted faults of the 1997 debriefing screen on the page.

Purpose:
    Break one rule of ``page/src/debrief/`` (``look.ts``, ``lines.ts``,
    ``screen.ts``, ``panel.ts``) or of the screen's style sheet per plant:
    which background a result picks, the side letter, a drawn number, the
    tab semantics, the pieces of the Mission Selection screen that must
    leave the tab order, and the plain layout below the frame's size.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test), restore, and check that every
    test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-debrief-look-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>`` and
      match by prefix.

Call:
    ``from plants_debrief_look_web import DEBRIEF_LOOK_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

LOOK = "page/src/debrief/look.ts"
LIN = "page/src/debrief/lines.ts"
SCR = "page/src/debrief/screen.ts"
PAN = "page/src/debrief/panel.ts"
PAI = "page/src/debrief/paint.ts"
CSS = "page/public/frame.css"
N = "node: "
B = "browser: "


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``web-debrief-look-<pid>``."""
    return Plant(f"web-debrief-look-{pid}", path, old, new, expect)


DEBRIEF_LOOK_WEB_PLANTS: list[Plant] = [
    plant(
        "win-and-loss-swapped",
        LOOK,
        '  return d.outcome === "completed" ? "win" : "loss";',
        '  return d.outcome === "completed" ? "loss" : "win";',
        N + "training: the outcome names win or loss",
        N + "a result with no teams or players",
        B + "framed at 1x: a flown battle opens as the 1997 screen",
    ),
    plant(
        "imperial-and-rebel-swapped",
        LOOK,
        '  return side === "imperial" ? "i" : "r";',
        '  return side === "imperial" ? "r" : "i";',
        N + "training: the outcome names win or loss",
        N + "a kind that names no side takes the pilot's chosen side",
    ),
    plant(
        "battle-drawn-as-combat",
        LOOK,
        '  const kind = d.mission_type === "battle" ? "b" : "c";',
        '  const kind = "c";',
        N + "a battle's mission keeps the battle pictures",
        N + "every file the page can pick is one the game code names",
    ),
    plant(
        "battle-alone-loses",
        LOOK,
        '  return d.mission_type === "battle" ? "tie" : "loss";',
        '  return "loss";',
        N + "alone with neither side's goals met",
    ),
    plant(
        "campaign-drawn-as-training",
        LOOK,
        '    const kind = d.mission_type === "training" ? "tr" : "ca";',
        '    const kind = "tr";',
        N + "a campaign's mission",
    ),
    plant(
        "tournament-not-drawn-as-melee",
        LOOK,
        '  if (d.mission_type === "melee" || d.mission_type === "tournament") {',
        '  if (d.mission_type === "melee") {',
        N + "a tournament's mission shows the melee pictures",
    ),
    plant(
        "drawn-number-off-by-one",
        LIN,
        '        value: cells[1] ?? "",',
        '        value: String(Number.parseInt(cells[1] ?? "0", 10) + 1),',
        N + "every drawn statistics number is a cell of the tables",
        B + "framed at 1x: a flown battle opens as the 1997 screen",
    ),
    plant(
        "zone-plate-hides-the-art",
        PAI,
        "export const ZONE_ALPHA = 0.6;",
        "export const ZONE_ALPHA = 0.97;",
        N + "every other ink and colour byte needs its word plate",
    ),
    plant(
        "zone-plate-too-thin-for-yellow",
        PAI,
        "export const ZONE_ALPHA = 0.6;",
        "export const ZONE_ALPHA = 0.5;",
        N + "white and yellow keep 4.5:1 on the zone plate",
    ),
    plant(
        "word-plates-dropped",
        PAI,
        "    if (readsBare(colorOf(glyph.color, font))) {",
        "    if (colorOf(glyph.color, font).length > 0) {",
        N + "a word plate covers each run",
    ),
    plant(
        "aria-selected-never-set",
        PAN,
        '      tab.setAttribute("aria-selected", String(selected));',
        '      tab.setAttribute("aria-selected", "false");',
        B + "framed: a melee opens on the overview; tabs by click and by keyboard",
    ),
    plant(
        "side-buttons-stay-in-the-tab-order",
        CSS,
        "html.framed.debrief-screen .side-col,\n",
        "",
        B + "framed: nothing of the Mission Selection screen stays visible",
    ),
    plant(
        "button-picture-drawn-from-its-button",
        CSS,
        "  inset: calc(-1 * var(--at-y)) auto auto calc(-1 * var(--at-x));\n",
        "  inset: 0 auto auto 0;\n",
        B + "framed at 1x: a flown battle opens as the 1997 screen",
        B + "framed at 2x: a flown battle opens as the 1997 screen",
    ),
    plant(
        "leave-picture-takes-the-tabs-clicks",
        CSS,
        "  image-rendering: pixelated;\n  pointer-events: none;\n}",
        "  image-rendering: pixelated;\n}",
        B + "framed: a melee opens on the overview; tabs by click",
    ),
    plant(
        "plain-layout-lost-below-the-frame",
        SCR,
        "const on = this.#layout !== null && input.open && debrief !== null;",
        "const on = input.open && debrief !== null;",
        B + "plain at 639 px wide: the plain layout",
        B + "plain under forced colors: no screen",
    ),
]
