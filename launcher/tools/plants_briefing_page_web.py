"""The planted faults of the briefing player: the bundle check, the screen-reader words, the settled view, the panel, the codec, the state and the page.

Purpose:
    Break one rule of ``page/src/briefing/``, the codec's and the state's
    briefing parts, the panel, the page's markup and its painter per plant:
    the script and buttons, the glide, the grid, markers, labels and icons,
    the caption wrap, the sheet writer and CRC, colors and layout, the clock,
    the bundle check, the words for a screen reader, and the panel's wiring.


Flow:
    ``plants_page_web`` joins this table; ``plant_page_web`` applies each plant
    alone: write ``new`` over ``old`` in ``path``, run ``npm test`` (and build
    and ``npm run test:browser`` when the plant names a browser test or plants
    the page itself), restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the page tables, all starting ``web-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant may stop a test run from ending: a run that is cut off counts
      as failing every test.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>``.

Call:
    ``from plants_briefing_page_web import BRIEFING_PAGE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)


BRIEFING_PAGE_PLANTS: list[Plant] = [
    Plant(
        "web-bun-length-shrinks",
        "page/src/briefing/bundle.ts",
        "(count !== null && value.length !== count)",
        "(count !== null && value.length > count)",
        (
            "node: a briefing.get reply refuses a bundle the player could not use",
            "node: the fixed lengths are held: 10 teams, 32 texts, 8 points, 70 boxes, 106 crafts, 256 IFFs and widths, 5 colors",
        ),
    ),
    Plant(
        "web-bun-wholes-length",
        "page/src/briefing/bundle.ts",
        "(count === null || value.length === count)",
        "(count === null || value.length <= count)",
        (
            "node: the fixed lengths are held: 10 teams, 32 texts, 8 points, 70 boxes, 106 crafts, 256 IFFs and widths, 5 colors",
        ),
    ),
    Plant(
        "web-bun-byte-text",
        "page/src/briefing/bundle.ts",
        "> 0xff",
        "> 0x100",
        ("node: a text character above 255 is refused; one at 255 is kept",),
    ),
    Plant(
        "web-bun-version",
        "page/src/briefing/bundle.ts",
        'value.format !== "jedimaster.briefing" || value.format_version !== 1',
        'value.format !== "jedimaster.briefing"',
        ("node: other formats and versions are refused",),
    ),
    Plant(
        "web-bun-team-range",
        "page/src/briefing/bundle.ts",
        "team >= TEAMS",
        "team > TEAMS",
        ("node: a team number past 9 and a briefing index past 7 are refused",),
    ),
    Plant(
        "web-bun-briefing-range",
        "page/src/briefing/bundle.ts",
        "index >= BRIEFINGS",
        "index > BRIEFINGS",
        ("node: a team number past 9 and a briefing index past 7 are refused",),
    ),
    Plant(
        "web-bun-flag-type",
        "page/src/briefing/bundle.ts",
        'typeof enabled !== "boolean"',
        'typeof enabled === "undefined"',
        (
            "node: refused example reply-briefing-get-point-flag-number.json",
            "node: values of the wrong type are refused",
        ),
    ),
    Plant(
        "web-bun-grey-null",
        "page/src/briefing/bundle.ts",
        "value.grey_sheet === null ? { value: null } : sheetOf(value.grey_sheet)",
        "sheetOf(value.grey_sheet)",
        ("node: no grey sheet is allowed",),
    ),
    Plant(
        "web-bun-extra-fields",
        "page/src/briefing/bundle.ts",
        "keys.length === names.length && names.every",
        "names.every",
        (
            "node: a missing or extra field is refused",
            "node: refused example reply-briefing-get-extra-field.json",
        ),
    ),
    Plant(
        "web-ann-closing-bracket",
        "page/src/briefing/announce.ts",
        "else if (byte !== OPEN && byte !== CLOSE)",
        "else if (byte !== OPEN)",
        (
            "node: a caption is spoken without dollar signs and brackets",
            "node: labels are the whole texts of the labels that are on, in slot order, none empty",
        ),
    ),
    Plant(
        "web-ann-heading",
        "page/src/briefing/announce.ts",
        "line[0] === HEADING ? line.slice(1) : line",
        "line",
        ("node: a heading is spoken without its >",),
    ),
    Plant(
        "web-ann-empty-lines",
        "page/src/briefing/announce.ts",
        '.filter((line) => line !== "")',
        "",
        ("node: empty lines and edge spaces are dropped",),
    ),
    Plant(
        "web-ann-slot-0",
        "page/src/briefing/announce.ts",
        "const slot = player.slots[1];",
        "const slot = player.slots[0];",
        ("node: the caption now shown is slot 1's block; slot 0 is not spoken",),
    ),
    Plant(
        "web-ann-names-once",
        "page/src/briefing/announce.ts",
        "!names.includes(name)",
        "true",
        (
            "node: highlighted groups are named once each, in marker order, without empty names",
        ),
    ),
    Plant(
        "web-ann-empty-labels",
        "page/src/briefing/announce.ts",
        'if (text !== "") texts.push(text);',
        "texts.push(text);",
        (
            "node: labels are the whole texts of the labels that are on, in slot order, none empty",
        ),
    ),
    Plant(
        "web-ann-labels-off",
        "page/src/briefing/announce.ts",
        "if (!label.on) continue;",
        "",
        (
            "node: a label is spoken whole even when it has only begun to type",
            "node: labels are the whole texts of the labels that are on, in slot order, none empty",
        ),
    ),
    Plant(
        "web-red-center",
        "page/src/briefing/reduced.ts",
        "cx: player.tcx,",
        "cx: player.cx,",
        (
            "node: a settled player is at its targets, labels whole, markers as their final box",
        ),
    ),
    Plant(
        "web-red-marker-age",
        "page/src/briefing/reduced.ts",
        "age: m.on ? SETTLED_AGE : m.age",
        "age: m.age",
        (
            "node: a settled player is at its targets, labels whole, markers as their final box",
            "node: the settled map shows the whole label and a grown box",
        ),
    ),
    Plant(
        "web-red-label-age",
        "page/src/briefing/reduced.ts",
        "age: l.on ? SETTLED_AGE : l.age",
        "age: l.age",
        (
            "node: a settled player is at its targets, labels whole, markers as their final box",
            "node: the settled map shows the whole label and a grown box",
        ),
    ),
    Plant(
        "web-red-zoom",
        "page/src/briefing/reduced.ts",
        "sy: player.tsy,",
        "sy: player.sy,",
        (
            "node: a settled player is at its targets, labels whole, markers as their final box",
        ),
    ),
    Plant(
        "web-pan-sort",
        "page/src/briefing/panel.ts",
        "teams.sort((a, b) => a - b)",
        "teams.sort((a, b) => b - a)",
        ("node: the picker lists the teams that have a briefing, lowest first",),
    ),
    Plant(
        "web-pan-default-team",
        "page/src/briefing/panel.ts",
        ": [0];",
        ": [];",
        ("node: a bundle where no team has a briefing offers team 0 alone",),
    ),
    Plant(
        "web-bun-teams-lost",
        "page/src/briefing/bundle.ts",
        "teams: teams.value,",
        "teams: [],",
        (
            "node: a briefing.get reply is read back with its bundle, or with none",
            "node: a bundle as the launcher writes it is accepted and kept whole",
        ),
    ),
    Plant(
        "web-sta-ready-missing",
        "page/src/state.ts",
        'briefing: { status: "ready", bundle: result.bundle }',
        'briefing: { status: "missing" }',
        ("node: a found briefing is kept for the player",),
    ),
    Plant(
        "web-cod-id-unchecked",
        "page/src/codec.ts",
        "!isWhole(mission, 0) ||",
        "",
        (
            "node: a show-mission request refuses a wrong type, id or shape",
            "node: briefing.get refuses what show_mission refuses",
        ),
    ),
    Plant(
        "web-cod-type-unchecked",
        "page/src/codec.ts",
        "!isMissionType(mission_type) ||\n        !isWhole(mission, 0)",
        "!isWhole(mission, 0)",
        (
            "node: a show-mission request refuses a wrong type, id or shape",
            "node: briefing.get refuses what show_mission refuses",
        ),
    ),
    Plant(
        "web-cod-extra-args",
        "page/src/codec.ts",
        '!hasExactly(args, ["mission_type", "id"]) ||',
        "",
        (
            "node: a show-mission request refuses a wrong type, id or shape",
            "node: briefing.get refuses what show_mission refuses",
        ),
    ),
    Plant(
        "web-cod-found-type",
        "page/src/codec.ts",
        'if (typeof found !== "boolean") return fail("found must be true or false");',
        "",
        (
            "node: a briefing.get reply refuses a bundle the player could not use",
            "node: refused example reply-briefing-get-found-text.json",
        ),
    ),
    Plant(
        "web-cod-bundle-lost",
        "page/src/codec.ts",
        "return { value: { found, bundle: checked.value } };",
        "return { value: { found, bundle: null } };",
        (
            "node: a briefing.get reply is read back with its bundle, or with none",
            "node: accepted example reply-ok-briefing-get-found.json",
        ),
    ),
    Plant(
        "web-red-markers-mutated",
        "page/src/briefing/reduced.ts",
        "markers: player.markers.map((m) => ({\n      ...m,\n      age: m.on ? SETTLED_AGE : m.age,\n    })),",
        "markers: player.markers.map((m) => {\n      m.age = m.on ? SETTLED_AGE : m.age;\n      return m;\n    }),",
        ("node: settling changes nothing in the real player",),
    ),
    Plant(
        "web-bun-record-check",
        "page/src/briefing/bundle.ts",
        'if (!isRecord(value) || !exactly(value, names)) {\n    return fail("a bundle has the wrong fields");',
        'if (!exactly(value as never, names)) {\n    return fail("a bundle has the wrong fields");',
        ("node: things that are not objects are refused",),
    ),
    Plant(
        "web-cod-revision",
        "page/src/codec.ts",
        "schema_revision === 3",
        "schema_revision === 2",
        (
            "node: a hello reply of another revision is refused",
            "node: accepted example reply-ok-hello.json",
        ),
    ),
    Plant(
        "web-cod-briefing-get",
        "page/src/codec.ts",
        '    case "briefing.get":\n    case "page.show_mission": {',
        '    case "page.show_mission": {',
        (
            "node: accepted example request-briefing-get-id-zero.json",
            "node: accepted example request-briefing-get.json",
        ),
    ),
    Plant(
        "web-cod-null-bundle",
        "page/src/codec.ts",
        "if (bundle === null) return { value: { found, bundle: null } };",
        "",
        (
            "node: a briefing.get reply is read back with its bundle, or with none",
            "node: accepted example reply-ok-briefing-get-missing.json",
        ),
    ),
    Plant(
        "web-cod-bundle-unchecked",
        "page/src/codec.ts",
        'if ("reason" in checked) return fail(checked.reason);',
        "",
        (
            "node: a briefing.get reply refuses a bundle the player could not use",
            "node: refused example reply-briefing-get-extra-field.json",
        ),
    ),
    Plant(
        "web-sta-push-loading",
        "page/src/state.ts",
        'briefing: { status: "loading" },\n',
        'briefing: { status: "none" },\n',
        (
            "node: a mission pushed to the page makes the briefing wait for its bundle",
            "node: a new push after a briefing waits for the next one",
        ),
    ),
    Plant(
        "web-sta-missing",
        "page/src/state.ts",
        'return { ...state, briefing: { status: "missing" } };',
        'return { ...state, briefing: { status: "none" } };',
        ("node: a briefing that was not found says so",),
    ),
    Plant(
        "web-sta-note-pick",
        "page/src/state.ts",
        'none: "Pick a mission to see its briefing.",',
        'none: "Pick.",',
        ("node: before anything is picked the briefing asks for a pick",),
    ),
    Plant(
        "web-sta-note-missing",
        "page/src/state.ts",
        "could not be shown.",
        "failed.",
        ("node: a briefing that was not found says so",),
    ),
    Plant(
        "web-sta-locked-button",
        "page/src/state.ts",
        "entry.available && isMissionType(menu.mission_type)",
        "isMissionType(menu.mission_type)",
        (
            "node: an available entry has a button that asks for that mission; a locked one has none",
            "node: missions list one heading per menu, availability in words",
        ),
    ),
    Plant(
        "web-sta-pick-label",
        "page/src/state.ts",
        "label: `Show ${entry.title}`",
        "label: `Show ${entry.file}`",
        (
            "node: an available entry has a button that asks for that mission; a locked one has none",
            "node: missions list one heading per menu, availability in words",
        ),
    ),
    Plant(
        "web-pan-opens-playing",
        "page/src/briefing/panel.ts",
        '    press(player, "stop");\n    this.#player = player;',
        "    this.#player = player;",
        (
            "browser: a picked mission opens its briefing stopped, with its words for a screen reader",
            "browser: axe finds nothing with the briefing open, in both schemes",
        ),
    ),
    Plant(
        "web-pan-aria-disabled",
        "page/src/briefing/panel.ts",
        'offered(player, button) ? "false" : "true"',
        '"false"',
        (
            "browser: Play, Stop, Rewind and Forward work from the keyboard",
            "browser: a control that is not offered stays reachable and does nothing",
        ),
    ),
    Plant(
        "web-pan-highlight-words",
        "page/src/briefing/panel.ts",
        '`Highlighted: ${names.join(", ")}.`',
        '`Lit: ${names.join(", ")}.`',
        (
            "browser: Play, Stop, Rewind and Forward work from the keyboard",
            "browser: axe finds nothing with the briefing open, in both schemes",
        ),
    ),
    Plant(
        "web-pan-reduced-ignored",
        "page/src/briefing/panel.ts",
        "this.#motion.matches ? drawFrame(settled(player)) : this.#records",
        "this.#records",
        ("browser: with reduced motion the map shows the marker's final box at once",),
    ),
    Plant(
        "web-pan-height-share",
        "page/src/briefing/panel.ts",
        "window.innerHeight * 0.7",
        "window.innerHeight * 0.5",
        ("browser: whole pixels draws the largest whole multiple, sharp",),
    ),
    Plant(
        "web-pan-labels-list",
        "page/src/briefing/panel.ts",
        "item.textContent = text;",
        'item.textContent = "";',
        ("browser: Play, Stop, Rewind and Forward work from the keyboard",),
    ),
    Plant(
        "web-main-scaling-unused",
        "page/src/main.ts",
        "if (view.artScaling !== null) briefingPanel.setScaling(view.artScaling);",
        "",
        ("browser: whole pixels draws the largest whole multiple, sharp",),
    ),
    Plant(
        "web-main-note-unused",
        "page/src/main.ts",
        'briefingPanel.hide(view.briefingNote ?? "");',
        'briefingPanel.hide("");',
        (
            "browser: a mission without a bundle says so",
            "browser: a picked mission opens its briefing stopped, with its words for a screen reader",
        ),
    ),
    Plant(
        "web-htm-canvas-role",
        "page/public/index.html",
        '              role="img"\n',
        "",
        (
            "browser: a picked mission opens its briefing stopped, with its words for a screen reader",
        ),
    ),
    Plant(
        "web-htm-sound-switch",
        "page/public/index.html",
        '                role="switch"\n',
        "",
        (
            "browser: a picked mission opens its briefing stopped, with its words for a screen reader",
        ),
    ),
    Plant(
        "web-pnt-rendering",
        "page/src/briefing/paint.ts",
        'const rendering = layout.smooth ? "auto" : "pixelated";',
        'const rendering = "auto";',
        ("browser: whole pixels draws the largest whole multiple, sharp",),
    ),
    Plant(
        "web-main-wrong-mission-asked",
        "page/src/main.ts",
        "id: message.data.id },",
        "id: message.data.id + 1 },",
        (
            "browser: Play, Stop, Rewind and Forward work from the keyboard",
            "browser: a control that is not offered stays reachable and does nothing",
        ),
    ),
    Plant(
        "web-main-wrong-pick",
        "page/src/main.ts",
        "const id = Number(target.dataset.missionId);",
        "const id = Number(target.dataset.missionId) + 1;",
        (
            "browser: Play, Stop, Rewind and Forward work from the keyboard",
            "browser: a control that is not offered stays reachable and does nothing",
        ),
    ),
    Plant(
        "web-htm-live-off",
        "page/public/index.html",
        '            aria-live="polite"\n            aria-atomic="true"',
        '            aria-live="off"\n            aria-atomic="true"',
        (
            "browser: a picked mission opens its briefing stopped, with its words for a screen reader",
        ),
    ),
    Plant(
        "web-pan-team-box",
        "page/src/briefing/panel.ts",
        "this.#teamBox.hidden = teams.length < 2;",
        "this.#teamBox.hidden = teams.length < 3;",
        ("browser: the team picker opens that team's briefing, stopped",),
    ),
    Plant(
        "web-pnt-channel-order",
        "page/src/briefing/paint.ts",
        "return `rgb(${String(rgb[0])} ${String(rgb[1])} ${String(rgb[2])})`;",
        "return `rgb(${String(rgb[2])} ${String(rgb[1])} ${String(rgb[0])})`;",
        (
            "browser: the map is drawn: the grid's red and an icon's colors are on the canvas",
        ),
    ),
]
