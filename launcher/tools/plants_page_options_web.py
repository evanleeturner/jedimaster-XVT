"""The planted faults of the page's option words, the pilot lines and the options copy.

Purpose:
    Break one rule of V4's page per plant: the words of one option of each
    kind (switch, battle length, minutes, the mission's own limit), the
    state that ignores ``pilot.changed`` or the pilot of the ``status``
    push, the Pilot panel and bar words for a named pilot, for none and
    for one not yet known, and the options copy under the map.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test), restore, and check that every
    test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-opt-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>``.

Call:
    ``from plants_page_options_web import OPTIONS_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

STA = "page/src/state.ts"
MAI = "page/src/main.ts"
N = "node: "
B = "browser: "
WORDS = N + "optionWords names every choice of every option"
UNKNOWN = N + "the pilot is not known until the status push says"
STATUS = N + "the status push gives the pilot and a null pilot gives the none words"
CHANGED = N + "pilot.changed follows a rename and a pilot that goes away"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``web-opt-<pid>``."""
    return Plant(f"web-opt-{pid}", path, old, new, expect)


OPTIONS_WEB_PLANTS: list[Plant] = [
    plant("switch-words", STA, '  on: "On",', '  on: "Yes",', WORDS),
    plant(
        "battle-length-words",
        STA,
        "return `${value} wins`;",
        "return `${value} rounds`;",
        WORDS,
    ),
    plant(
        "minutes-words",
        STA,
        'value === "1" ? "1 minute" :',
        'value === "1" ? "1 minutes" :',
        WORDS,
    ),
    plant(
        "own-limit-words",
        STA,
        '  default: "The mission\'s own limit",',
        '  default: "No limit",',
        WORDS,
    ),
    plant(
        "pilot-changed-ignored",
        STA,
        "return { ...state, pilot: message.data.pilot, problem: null };",
        "return state;",
        CHANGED,
        B + "a pilot push redraws the Pilot panel and an unnamed pilot says so",
    ),
    plant(
        "status-pilot-ignored",
        STA,
        "        launcherVersion: message.data.launcher_version,\n        pilot: message.data.pilot,\n",
        "        launcherVersion: message.data.launcher_version,\n",
        STATUS,
        B + "the pilot's name shows at connect",
    ),
    plant(
        "no-pilot-line-blank",
        STA,
        "return pilot === null ? NO_PILOT_LINE : `Your pilot is ${pilot}.`;",
        'return pilot === null ? "" : `Your pilot is ${pilot}.`;',
        STATUS,
    ),
    plant(
        "no-pilot-bar-words",
        STA,
        'return pilot === null ? "Pilot: none yet" : `Pilot: ${pilot}`;',
        'return pilot === null ? "Pilot: -" : `Pilot: ${pilot}`;',
        STATUS,
    ),
    plant(
        "pilot-bar-words",
        STA,
        'return pilot === null ? "Pilot: none yet" : `Pilot: ${pilot}`;',
        'return pilot === null ? "Pilot: none yet" : `${pilot}`;',
        STATUS,
        B + "framed: the pilot's name is in the bar at connect",
    ),
    plant(
        "pilot-line-name-words",
        STA,
        "return pilot === null ? NO_PILOT_LINE : `Your pilot is ${pilot}.`;",
        "return pilot === null ? NO_PILOT_LINE : `${pilot}`;",
        STATUS,
        B + "the pilot's name shows at connect",
    ),
    plant(
        "unknown-line-is-none-words",
        STA,
        '  if (pilot === undefined) return "The pilot is not known yet.";\n',
        "",
        UNKNOWN,
    ),
    plant(
        "unknown-bar-is-none-words",
        STA,
        '  if (pilot === undefined) return "Pilot: not known yet";\n',
        "",
        UNKNOWN,
    ),
    plant(
        "pilot-starts-as-none",
        STA,
        "    pilot: undefined,",
        "    pilot: null,",
        UNKNOWN,
    ),
    plant(
        "options-copy-filled-once",
        MAI,
        "  optionsCopy.replaceChildren(\n    ...rows.map((row) => {",
        "  if (optionsCopy.childElementCount === 0) optionsCopy.replaceChildren(\n    ...rows.map((row) => {",
        B + "framed: a change to the options shows in the copy under the map at once",
    ),
]
