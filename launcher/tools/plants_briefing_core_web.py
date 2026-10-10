"""The planted faults of the briefing player: the script, the glide, the grid, markers, labels and icons.

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
    ``from plants_briefing_core_web import BRIEFING_CORE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)


BRIEFING_CORE_PLANTS: list[Plant] = [
    Plant(
        "web-core-earlier-event-applies",
        "page/src/briefing/core.ts",
        "if (event.time === player.t) applyEvent(player, event, atOnce);",
        "applyEvent(player, event, atOnce);",
        ("node: an event timed before its step is skipped and does nothing",),
    ),
    Plant(
        "web-core-stop-flag-never-set",
        "page/src/briefing/core.ts",
        "    player.stopPoint = true;",
        "    player.stopPoint = false;",
        (
            "node: forward plays its last step with sounds and ignores the running time",
            "node: forward: a stop point is a place to stop, then the script runs out and starts over (engine)",
        ),
    ),
    Plant(
        "web-core-slot-block-forgotten",
        "page/src/briefing/core.ts",
        "    player.slotsChanged = true;",
        "    player.slotsChanged = true;\n    player.slots[1].block = 0;",
        (
            "node: type 3 turns both slots off and keeps their blocks; type 4 sets slot 0",
        ),
    ),
    Plant(
        "web-core-center-not-jumped",
        "page/src/briefing/core.ts",
        "      player.cx = player.tcx;",
        "      player.cx = player.cx;",
        (
            "node: a briefing loads playing with step 0 already played",
            "node: a move that wraps past 16 bits is not stopped at the target",
        ),
    ),
    Plant(
        "web-core-zoom-not-jumped",
        "page/src/briefing/core.ts",
        "      player.sx = player.tsx;",
        "      player.sx = player.sx;",
        (
            "node: a briefing loads playing with step 0 already played",
            "node: at zoom 16 only phase 2 lines are minor; under 16 there are none",
        ),
    ),
    Plant(
        "web-core-marker-age-at-once",
        "page/src/briefing/core.ts",
        "    marker.age = age;",
        "    marker.age = 0;",
        (
            "node: a busy frame holds the kinds of record the painter draws, and two clips",
            "node: a marker of age 12 or more is only a box grown by 2, filled and outlined",
        ),
    ),
    Plant(
        "web-core-marker-sounds-at-once",
        "page/src/briefing/core.ts",
        "if (!atOnce) player.sounds.push(soundOf(player, marker.group));",
        "player.sounds.push(soundOf(player, marker.group));",
        ("node: forward plays its last step with sounds and ignores the running time",),
    ),
    Plant(
        "web-core-label-silent-text-sounds",
        "page/src/briefing/core.ts",
        "if (!atOnce && text !== undefined && text.length > 0) {",
        "if (!atOnce && text !== undefined) {",
        (
            "node: sounds: a marker on an IFF 1 group plays target 2, others target 1, a label with text plays text",
        ),
    ),
    Plant(
        "web-core-label-age-at-once",
        "page/src/briefing/core.ts",
        "    label.age = age;",
        "    label.age = 0;",
        (
            "node: a step played at once starts markers and labels at age 80 and moves the view",
            "node: the state text lists markers SLOT:GROUP:AGE and labels SLOT:TEXT:X:Y:ROW:AGE",
        ),
    ),
    Plant(
        "web-core-iff-1-is-target-1",
        "page/src/briefing/core.ts",
        'return iff === 1 ? "sfxTarget2" : "sfxTarget1";',
        'return iff === 2 ? "sfxTarget2" : "sfxTarget1";',
        (
            "node: sounds: a marker on an IFF 1 group plays target 2, others target 1, a label with text plays text",
        ),
    ),
    Plant(
        "web-core-zoom-step-12",
        "page/src/briefing/core.ts",
        "let step = apart >= 12 ? 8 : 2;",
        "let step = apart > 12 ? 8 : 2;",
        (
            "node: the zoom glides 8 at a gap of exactly 12 and 2 at a gap of 10 from a zoom of exactly 10",
        ),
    ),
    Plant(
        "web-core-zoom-step-small",
        "page/src/briefing/core.ts",
        "if (player.sx < 10) step = 1;",
        "if (player.sx <= 10) step = 1;",
        (
            "node: the zoom glides 8 at a gap of exactly 12 and 2 at a gap of 10 from a zoom of exactly 10",
        ),
    ),
    Plant(
        "web-core-center-unit",
        "page/src/briefing/core.ts",
        "Math.trunc(256 / player.sx) + 1;",
        "Math.trunc(256 / player.sx);",
        (
            "node: a target event after time 0 moves only the target; the view glides",
            "node: gaps are cut to 16 bits: a far target on one axis does not hide a near one",
        ),
    ),
    Plant(
        "web-core-center-far-16",
        "page/src/briefing/core.ts",
        "Math.trunc(far / unit) >= 16 ? 4 * unit : 2 * unit",
        "Math.trunc(far / unit) > 16 ? 4 * unit : 2 * unit",
        ("node: the center takes the long step at exactly 16 units of gap per unit",),
    ),
    Plant(
        "web-core-gap-not-cut",
        "page/src/briefing/core.ts",
        "return cut16(Math.abs(a - b));",
        "return Math.abs(a - b);",
        (
            "node: gaps are cut to 16 bits: a far target on one axis does not hide a near one",
        ),
    ),
    Plant(
        "web-core-move-passes-up",
        "page/src/briefing/core.ts",
        "return moved > target ? target : moved;",
        "return Math.min(value + step, target);",
        ("node: a move that wraps past 16 bits is not stopped at the target",),
    ),
    Plant(
        "web-core-move-passes-down",
        "page/src/briefing/core.ts",
        "return moved < target ? target : moved;",
        "return Math.max(value - step, target);",
        (
            "node: moving down past the 16-bit edge wraps and is not stopped at the target",
        ),
    ),
    Plant(
        "web-core-labels-do-not-age",
        "page/src/briefing/core.ts",
        "for (const label of player.labels) if (label.on) label.age += 1;",
        "",
        (
            "node: a label types itself out: older letters are brighter, a block follows",
            "node: a typing label draws its prefixes from base up to base + 6 and no further",
        ),
    ),
    Plant(
        "web-core-markers-age-double",
        "page/src/briefing/core.ts",
        "if (marker.on) marker.age += 1;",
        "if (marker.on) marker.age += 2;",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-core-stopped-updates",
        "page/src/briefing/core.ts",
        "if (!player.playing) return [];",
        "",
        ("node: nothing updates or sounds while stopped, and play resumes",),
    ),
    Plant(
        "web-core-running-time-edge",
        "page/src/briefing/core.ts",
        "if (player.t >= player.script.runningTime) {",
        "if (player.t > player.script.runningTime) {",
        (
            "node: the briefing starts over at the running time and zeroes the page counters",
            "node: the running time can be 0 or 1: every frame starts over",
        ),
    ),
    Plant(
        "web-core-restart-keeps-page",
        "page/src/briefing/core.ts",
        "if (player.t >= player.script.runningTime) {\n    player.page = 0;",
        "if (player.t >= player.script.runningTime) {\n    player.page = 9;",
        (
            "node: the briefing starts over at the running time and zeroes the page counters",
        ),
    ),
    Plant(
        "web-core-zoom-reset",
        "page/src/briefing/core.ts",
        "  player.sx = 32;\n",
        "  player.sx = 31;\n",
        (
            "node: a run begins at state 0 and ends with the unknown glyph count",
            "node: the briefing point of the briefing is the one used",
        ),
    ),
    Plant(
        "web-core-load-stopped",
        "page/src/briefing/core.ts",
        "    playing: true,",
        "    playing: false,",
        (
            "node: a briefing loads playing with step 0 already played",
            "node: a caption block counts a page once, while it is drawn",
        ),
    ),
    Plant(
        "web-core-slot-1-stays-on",
        "page/src/briefing/core.ts",
        "  player.slots[1].on = false;\n  player.markers = emptyMarkers();",
        "  player.markers = emptyMarkers();",
        ("node: forward: a caption that appears later is reached, then held (engine)",),
    ),
    Plant(
        "web-core-labels-stay",
        "page/src/briefing/core.ts",
        "  player.labels = emptyLabels();\n  player.t = 0;",
        "  player.t = 0;",
        ("node: starting over turns the labels off",),
    ),
    Plant(
        "web-core-forward-before-origin",
        "page/src/briefing/core.ts",
        "&& origin <= player.t) break;",
        ") break;",
        (
            "node: forward plays its last step with sounds and ignores the running time",
            "node: forward: a caption that appears later is reached, then held (engine)",
        ),
    ),
    Plant(
        "web-core-forward-stop-needs-text",
        "page/src/briefing/core.ts",
        "  if (player.stopPoint || textFrames === 1) {",
        "  if (textFrames === 1) {",
        (
            "node: forward plays its last step with sounds and ignores the running time",
            "node: forward: a stop point is a place to stop, then the script runs out and starts over (engine)",
        ),
    ),
    Plant(
        "web-core-forward-slot-changed",
        "page/src/briefing/core.ts",
        "    if (player.slotsChanged) {",
        "    if (false) {",
        (
            "node: forward counts a text on slot 0 as text, and stops there (engine)",
            "node: forward walks caption changes and starts over at the end (engine)",
        ),
    ),
    Plant(
        "web-core-forward-counted-block",
        "page/src/briefing/core.ts",
        "  if (player.slots[1].block === player.last) {",
        "  if (player.slots[1].block !== player.last) {",
        (
            "node: a forward onto a caption already counted starts over",
            "node: forward plays its last step with sounds and ignores the running time",
        ),
    ),
    Plant(
        "web-core-forward-while-stopped",
        "page/src/briefing/core.ts",
        "    if (!player.playing) return null;\n    forward(player);",
        "    forward(player);",
        ("node: nothing updates or sounds while stopped, and play resumes",),
    ),
    Plant(
        "web-core-stop-while-stopped",
        "page/src/briefing/core.ts",
        "    if (!player.playing) return null;\n    player.playing = false;",
        "    player.playing = false;",
        (
            "node: a press prints its line, then the state; a press not offered says ignored",
            "node: nothing updates or sounds while stopped, and play resumes",
        ),
    ),
    Plant(
        "web-core-play-while-playing",
        "page/src/briefing/core.ts",
        "    if (player.playing) return null;\n    player.playing = true;",
        "    player.playing = true;",
        (
            "node: a press prints its line, then the state; a press not offered says ignored",
            "node: nothing updates or sounds while stopped, and play resumes",
        ),
    ),
    Plant(
        "web-core-play-offered-playing",
        "page/src/briefing/core.ts",
        'if (button === "play") return !player.playing;',
        'if (button === "play") return player.playing;',
        (
            "node: every button name is one of the four",
            "node: nothing updates or sounds while stopped, and play resumes",
        ),
    ),
    Plant(
        "web-core-forward-silent",
        "page/src/briefing/core.ts",
        "while (target >= player.t) playStep(player, false);",
        "while (target >= player.t) playStep(player, true);",
        ("node: forward plays its last step with sounds and ignores the running time",),
    ),
    Plant(
        "web-core-default-running-time",
        "page/src/briefing/core.ts",
        "DEFAULT_RUNNING_TIME = 200",
        "DEFAULT_RUNNING_TIME = 201",
        (
            "node: the briefing line names the briefing and its running time; the default is briefing 0, 200 frames",
            "node: the default briefing is 200 frames long, empty, and for the first point",
        ),
    ),
    Plant(
        "web-core-default-point",
        "page/src/briefing/core.ts",
        "    index: 0,\n    runningTime: DEFAULT_RUNNING_TIME,",
        "    index: -1,\n    runningTime: DEFAULT_RUNNING_TIME,",
        (
            "node: the briefing line names the briefing and its running time; the default is briefing 0, 200 frames",
            "node: the default briefing is 200 frames long, empty, and for the first point",
        ),
    ),
    Plant(
        "web-core-first-briefing-for-all",
        "page/src/briefing/core.ts",
        "bundle.briefings.find((b) => b.index === choice)",
        "bundle.briefings.find(() => true)",
        (
            "node: the briefing line names the briefing and its running time; the default is briefing 0, 200 frames",
        ),
    ),
    Plant(
        "web-core-cut16-unsigned",
        "page/src/briefing/core.ts",
        "return (value << 16) >> 16;",
        "return value & 0xffff;",
        (
            "node: a move that wraps past 16 bits is not stopped at the target",
            "node: cut16 wraps to 16 bits like the game",
        ),
    ),
    Plant(
        "web-core-age-at-once-79",
        "page/src/briefing/core.ts",
        "AGE_AT_ONCE = 80",
        "AGE_AT_ONCE = 79",
        (
            "node: a step played at once starts markers and labels at age 80 and moves the view",
            "node: the state text lists markers SLOT:GROUP:AGE and labels SLOT:TEXT:X:Y:ROW:AGE",
        ),
    ),
    Plant(
        "web-draw-division-floors",
        "page/src/briefing/draw.ts",
        "Math.trunc((player.sx * (mx - player.cx)) / GRID)",
        "Math.floor((player.sx * (mx - player.cx)) / GRID)",
        ("node: division drops the fraction toward zero",),
    ),
    Plant(
        "web-draw-center-y",
        "page/src/briefing/draw.ts",
        "CENTER_Y = 104",
        "CENTER_Y = 103",
        (
            "node: a flight group is an icon from its box, centered on its point",
            "node: a label that has just started shows only its block",
        ),
    ),
    Plant(
        "web-draw-grid-index-plus-one",
        "page/src/briefing/draw.ts",
        "if (center > 0 && (center & 255) !== 0) phase += 1;",
        "if (center > 0 && (center & 255) !== 0) phase += 0;",
        (
            "node: with the center off the origin the grid's first line follows the center",
        ),
    ),
    Plant(
        "web-draw-grid-start-shift",
        "page/src/briefing/draw.ts",
        "(-center & 255)) >> 8);",
        "(-center & 255)) >> 7);",
        (
            "node: with the center off the origin the grid's first line follows the center",
        ),
    ),
    Plant(
        "web-draw-grid-start-at-zero",
        "page/src/briefing/draw.ts",
        "while (start > 0 && zoom > 0) {",
        "while (start >= 0 && zoom > 0) {",
        ("node: a line that falls exactly on x 0 is the first line",),
    ),
    Plant(
        "web-draw-minor-from-32",
        "page/src/briefing/draw.ts",
        "return player.sx >= 32 ? (phase & 3) !== 0 : (phase & 3) === 2;",
        "return player.sx > 32 ? (phase & 3) !== 0 : (phase & 3) === 2;",
        (
            "node: the grid at zoom 32 and center 0 has minor lines on phases 2 and major on 0",
        ),
    ),
    Plant(
        "web-draw-minor-from-16",
        "page/src/briefing/draw.ts",
        "for (const major of player.sx >= 16 ? [false, true] : [true]) {",
        "for (const major of player.sx > 16 ? [false, true] : [true]) {",
        ("node: at zoom 16 only phase 2 lines are minor; under 16 there are none",),
    ),
    Plant(
        "web-draw-grid-colors-swapped",
        "page/src/briefing/draw.ts",
        'const color = major ? "major" : "minor";',
        'const color = major ? "minor" : "major";',
        (
            "node: at zoom 16 only phase 2 lines are minor; under 16 there are none",
            "node: at zoom 32 or more every phase but 0 is minor; the zoom of x decides for both axes",
        ),
    ),
    Plant(
        "web-draw-iff-4-sheet",
        "page/src/briefing/draw.ts",
        "  4: 1,\n  5: 4,\n};",
        "  4: 4,\n  5: 4,\n};",
        (
            "node: the sheet follows the IFF: 0 to 3, 4 shares 1, 5 is sheet 4, others sheet 0",
        ),
    ),
    Plant(
        "web-draw-iff-2-shade",
        "page/src/briefing/draw.ts",
        "  2: 24,",
        "  2: 16,",
        ("node: the shade base follows the IFF",),
    ),
    Plant(
        "web-draw-icon-half-width",
        "page/src/briefing/draw.ts",
        "left: at.x - (width >> 1),",
        "left: at.x - (width >> 2),",
        (
            "node: a flight group is an icon from its box, centered on its point",
            "node: a marker of age 12 or more is only a box grown by 2, filled and outlined",
        ),
    ),
    Plant(
        "web-draw-box-width",
        "page/src/briefing/draw.ts",
        "const width = shape.right - shape.left + 1;",
        "const width = shape.right - shape.left;",
        (
            "node: a flight group is an icon from its box, centered on its point",
            "node: a marker of age 12 or more is only a box grown by 2, filled and outlined",
        ),
    ),
    Plant(
        "web-draw-age-12-outline",
        "page/src/briefing/draw.ts",
        'rect("outline", 2, base + 6)',
        'rect("outline", 2, base + 5)',
        (
            "node: a marker of age 12 or more is only a box grown by 2, filled and outlined",
        ),
    ),
    Plant(
        "web-draw-young-shade",
        "page/src/briefing/draw.ts",
        "level = base + 7 - 2 * age;",
        "level = base + 7 - age;",
        (
            "node: a young marker draws age + 1 rings, each two pixels nearer and two shades up",
        ),
    ),
    Plant(
        "web-draw-young-offset",
        "page/src/briefing/draw.ts",
        "    offset = 16;",
        "    offset = 15;",
        (
            "node: a new marker draws one ring of four tinted copies, 16 away, in base + 7",
            "node: a young marker draws age + 1 rings, each two pixels nearer and two shades up",
        ),
    ),
    Plant(
        "web-draw-young-count",
        "page/src/briefing/draw.ts",
        "    count = age + 1;",
        "    count = age;",
        (
            "node: a new marker draws one ring of four tinted copies, 16 away, in base + 7",
            "node: a young marker draws age + 1 rings, each two pixels nearer and two shades up",
        ),
    ),
    Plant(
        "web-draw-mid-offset",
        "page/src/briefing/draw.ts",
        "offset = 2 * (11 - age);",
        "offset = 11 - age;",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-draw-mid-count",
        "page/src/briefing/draw.ts",
        "count = age < 8 ? 4 : 12 - age;",
        "count = age < 8 ? 3 : 12 - age;",
        ("node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",),
    ),
    Plant(
        "web-draw-shrunk-fill",
        "page/src/briefing/draw.ts",
        'records.push(rect("fill", -(11 - age), base + 2));',
        'records.push(rect("fill", -(11 - age), base + 1));',
        (
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-draw-shrunk-outline",
        "page/src/briefing/draw.ts",
        'rect("outline", -(11 - age), base + age - 6)',
        'rect("outline", -(11 - age), base + age - 5)',
        (
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-draw-ring-shade-step",
        "page/src/briefing/draw.ts",
        "    level += 2;",
        "    level += 1;",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a young marker draws age + 1 rings, each two pixels nearer and two shades up",
        ),
    ),
    Plant(
        "web-draw-ring-offset-step",
        "page/src/briefing/draw.ts",
        "    offset -= 2;",
        "    offset -= 1;",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a young marker draws age + 1 rings, each two pixels nearer and two shades up",
        ),
    ),
    Plant(
        "web-draw-ring-order",
        "page/src/briefing/draw.ts",
        "      [-1, -1],\n      [1, -1],",
        "      [1, -1],\n      [-1, -1],",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-draw-label-n",
        "page/src/briefing/draw.ts",
        "const n = 2 * label.age;",
        "const n = label.age;",
        (
            "node: a label types itself out: older letters are brighter, a block follows",
            "node: a typing label draws its prefixes from base up to base + 6 and no further",
        ),
    ),
    Plant(
        "web-draw-label-start-2",
        "page/src/briefing/draw.ts",
        "(n === 1 ? 4 : n === 2 ? 2 : 0)",
        "(n === 1 ? 4 : n === 2 ? 3 : 0)",
        (
            "node: a label types itself out: older letters are brighter, a block follows",
            "node: a typing label's first prefix is base + 2 at age 1 and base from age 2",
        ),
    ),
    Plant(
        "web-draw-label-top-shade",
        "page/src/briefing/draw.ts",
        "level <= base + 6",
        "level < base + 6",
        (
            "node: a typing label draws its prefixes from base up to base + 6 and no further",
        ),
    ),
    Plant(
        "web-draw-label-block-x",
        "page/src/briefing/draw.ts",
        "left: at.x + width + 2,",
        "left: at.x + width + 1,",
        (
            "node: a label that has just started shows only its block",
            "node: a label types itself out: older letters are brighter, a block follows",
        ),
    ),
    Plant(
        "web-draw-label-block-height",
        "page/src/briefing/draw.ts",
        "bottom: at.y + 6,",
        "bottom: at.y + 5,",
        (
            "node: a label that has just started shows only its block",
            "node: a label types itself out: older letters are brighter, a block follows",
        ),
    ),
    Plant(
        "web-draw-label-block-shown",
        "page/src/briefing/draw.ts",
        "if (length > n) {",
        "if (length >= n) {",
        ("node: a label as long as its typed count has no block after it",),
    ),
    Plant(
        "web-draw-label-whole-last",
        "page/src/briefing/draw.ts",
        ": 4;\n    out.push(...drawBytes(font, bytes",
        ": 3;\n    out.push(...drawBytes(font, bytes",
        (
            "node: a whole label is base + 7, 6, 5 for its last three steps, then base + 4",
            "node: the settled map shows the whole label and a grown box",
        ),
    ),
    Plant(
        "web-draw-label-brackets",
        "page/src/briefing/draw.ts",
        "(b === 0x5b ? 2 : b === 0x5d ? 1 : b)",
        "(b === 0x5b ? 1 : b === 0x5d ? 2 : b)",
        (
            "node: brackets in a label switch to color code 1 and back to the label's shade",
        ),
    ),
    Plant(
        "web-draw-player-number-team",
        "page/src/briefing/draw.ts",
        "const mine = group.player_number !== 0 && group.team === player.team;",
        "const mine = group.player_number !== 0;",
        ("node: a player number is counted over drawn groups of the viewer's team",),
    ),
    Plant(
        "web-draw-disabled-points-drawn",
        "page/src/briefing/draw.ts",
        "if (place?.enabled !== true) continue;",
        "if (place === null) continue;",
        (
            "node: a player number is counted over drawn groups of the viewer's team",
            "node: the briefing point of the briefing is the one used",
        ),
    ),
    Plant(
        "web-draw-number-x",
        "page/src/briefing/draw.ts",
        "const x = place.left + place.width;",
        "const x = place.left;",
        ("node: a player number is counted over drawn groups of the viewer's team",),
    ),
    Plant(
        "web-draw-number-y",
        "page/src/briefing/draw.ts",
        "const y = place.top + place.height;",
        "const y = place.top;",
        ("node: a player number is counted over drawn groups of the viewer's team",),
    ),
    Plant(
        "web-draw-shade-table",
        "page/src/briefing/draw.ts",
        "n >= 0 && n < 40 ?",
        "n >= 0 && n < 56 ?",
        ("node: a shade past the 40 is drawn black (x0)",),
    ),
    Plant(
        "web-draw-no-group-ignored",
        "page/src/briefing/draw.ts",
        "player.bundle.groups[marker.group] ?? NO_GROUP",
        "player.bundle.groups[marker.group]",
        ("node: a marker on a group that does not exist draws the all-zero group",),
    ),
    Plant(
        "web-draw-page-counted-twice",
        "page/src/briefing/draw.ts",
        "      player.page += 1;",
        "      player.page += 2;",
        (
            "node: a caption block counts a page once, while it is drawn",
            "node: a forward onto a caption already counted starts over",
        ),
    ),
    Plant(
        "web-draw-page-recount",
        "page/src/briefing/draw.ts",
        "if (slot.block !== player.last) {",
        "if (slot.block === player.last) {",
        (
            "node: a caption block counts a page once, while it is drawn",
            "node: a first caption that is block 0 is not counted",
        ),
    ),
    Plant(
        "web-draw-clip-width",
        "page/src/briefing/draw.ts",
        "right: MAP_WIDTH - 1,",
        "right: MAP_WIDTH - 2,",
        ("node: a frame begins with the map's two clips and ends with the page line",),
    ),
    Plant(
        "web-draw-page-line-space",
        "page/src/briefing/draw.ts",
        "`${bundle.front_string} ${String(page)}`",
        "`${bundle.front_string}${String(page)}`",
        (
            "node: a frame begins with the map's two clips and ends with the page line",
            "node: the caption is drawn first, the page is counted, and the page line shows it",
        ),
    ),
    Plant(
        "web-draw-page-x",
        "page/src/briefing/draw.ts",
        "const PAGE_X = 300;",
        "const PAGE_X = 301;",
        (
            "node: a frame begins with the map's two clips and ends with the page line",
            "node: the caption is drawn first, the page is counted, and the page line shows it",
        ),
    ),
    Plant(
        "web-draw-craft-default-box",
        "page/src/briefing/draw.ts",
        "player.bundle.craft_boxes[group.craft_type];",
        "player.bundle.craft_boxes[group.craft_type] ?? 0;",
        ("node: a craft without a box draws no icon",),
    ),
    Plant(
        "web-core-frame-ms",
        "page/src/briefing/core.ts",
        "FRAME_MS = 41",
        "FRAME_MS = 40",
        ("node: a frame is 41 ms",),
    ),
    Plant(
        "web-core-type-2-is-3",
        "page/src/briefing/core.ts",
        "} else if (type === 3) {",
        "} else if (type === 3 || type === 2) {",
        ("node: unknown and no-effect types change nothing",),
    ),
]
