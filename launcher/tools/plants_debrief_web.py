"""The planted faults of the page's Debriefing: decoder, tables, state, wiring, markup.

Purpose:
    Break one rule of ``page/src/debrief/`` and the debriefing's parts of the
    codec, the state and the page per plant: what the decoder accepts, the
    sums the 1997 statistics page made, the places of equal scores, the list
    of kept results, the new mark, the tab keys and the requests a page sends.

Flow:
    ``plant_page_web`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run ``npm test`` (and build and ``npm run test:browser``
    when the plant names a browser test or plants the page itself), restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the table, all starting ``web-debrief-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>`` and
      match by prefix.

Call:
    ``from plants_debrief_web import DEBRIEF_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

DEC = "page/src/debrief/decode.ts"
VIE = "page/src/debrief/view.ts"
PAN = "page/src/debrief/panel.ts"
COD = "page/src/codec.ts"
STA = "page/src/state.ts"
MAI = "page/src/main.ts"
HTM = "page/public/index.html"
N = "node: "
B = "browser: "
ACC = N + "accepted example "
REF = N + "refused example "
FLOWN = B + "a flown battle's debriefing is new, opens in its panel"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    return Plant(f"web-debrief-{pid}", path, old, new, expect)


DEBRIEF_WEB_PLANTS: list[Plant] = [
    plant(
        "extra-field-allowed",
        DEC,
        "  if (!isRecord(value) || !hasExactly(value, Object.keys(spec))) {\n"
        "    return { reason: `${what} has wrong fields` };",
        "  if (!isRecord(value)) {\n    return { reason: `${what} has wrong fields` };",
        N + "an extra field anywhere is refused",
        REF + "push-launch-ended-player-extra-field.json",
    ),
    plant(
        "name-too-long-allowed",
        DEC,
        "const NAME_LENGTH = 12;",
        "const NAME_LENGTH = 13;",
        N + "a number past the schema's bounds is refused",
    ),
    plant(
        "negative-count-allowed",
        DEC,
        '  if (kind === "count") return value >= 0;',
        '  if (kind === "count") return true;',
        N + "a number past the schema's bounds is refused",
        REF + "reply-results-get-negative-kills.json",
    ),
    plant(
        "ending-without-debrief",
        COD,
        '      !hasExactly(data, ["launch_id", "status", "line", "debrief"]) ||',
        '      !hasExactly(data, ["launch_id", "status", "line"]) ||',
        ACC + "push-launch-ended.json",
    ),
    plant(
        "stale-revision",
        COD,
        'if (typeof launcher_version === "string" && schema_revision === 9) {',
        'if (typeof launcher_version === "string" && schema_revision === 8) {',
        ACC + "reply-ok-hello.json",
        REF + "reply-ok-old-revision.json",
    ),
    plant(
        "total-shared-from-assists",
        VIE,
        "        sum(crafts, (r) => r.kills),\n        sum(crafts, (r) => r.shared),",
        "        sum(crafts, (r) => r.kills),\n        sum(crafts, (r) => r.assists),",
        N + "the statistics sum what the 1997 page sums",
    ),
    plant(
        "accuracy-rounded",
        VIE,
        "Math.floor((100 * hits) / shots)",
        "Math.round((100 * hits) / shots)",
        N + "the small words",
    ),
    plant(
        "equal-scores-split",
        VIE,
        "  return scores.map((score) => 1 + scores.filter((s) => s > score).length);",
        "  return scores.map((_, index) => 1 + index);",
        N + "equal team scores share a place",
    ),
    plant(
        "players-under-wrong-team",
        VIE,
        "    for (const p of byScore(d.players.filter((q) => q.team === team.team))) {",
        "    for (const p of byScore(d.players.filter((q) => q.entry === team.team))) {",
        N + "the overview lists teams by score with each team's players under it",
    ),
    plant(
        "list-keeps-21",
        STA,
        "        ].slice(0, RESULTS_KEPT);",
        "        ].slice(0, RESULTS_KEPT + 1);",
        N + "the list keeps the launcher's twenty",
    ),
    plant(
        "ending-not-new",
        STA,
        '  return { results, shown: debrief, tab: "overview", fresh: true };',
        '  return { results, shown: debrief, tab: "overview", fresh: false };',
        N + "a battle's ending shows its debriefing, new",
        FLOWN,
    ),
    plant(
        "arrow-keys-ignored",
        PAN,
        '    if (key === "ArrowRight") return this.tabs[at === last ? 0 : at + 1];',
        '    if (key === "ArrowRight") return undefined;',
        FLOWN,
    ),
    plant(
        "no-list-on-open",
        MAI,
        '    send({ command: "results.list", args: {} });',
        "",
        B + "a page opened after a battle shows the newest kept debriefing",
    ),
    plant(
        "tabpanel-unnamed",
        HTM,
        '                      role="tabpanel"\n',
        "",
        FLOWN,
    ),
]
