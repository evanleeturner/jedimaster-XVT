"""The planted faults of the briefing player: the sheet writer, the CRC, the colors, the layout and the clock.

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
    ``from plants_briefing_output_web import BRIEFING_OUTPUT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)


BRIEFING_OUTPUT_PLANTS: list[Plant] = [
    Plant(
        "web-sheet-escape-7f",
        "page/src/briefing/sheet.ts",
        "code > 0x7e",
        "code > 0x7f",
        (
            "node: text is escaped: quote and backslash with a backslash, other bytes as \\\\xHH",
        ),
    ),
    Plant(
        "web-sheet-escape-backslash",
        "page/src/briefing/sheet.ts",
        'if (ch === \'"\' || ch === "\\\\")',
        "if (ch === '\"')",
        (
            "node: text is escaped: quote and backslash with a backslash, other bytes as \\\\xHH",
        ),
    ),
    Plant(
        "web-sheet-join-x",
        "page/src/briefing/sheet.ts",
        "run.end === record.x",
        "run.end <= record.x",
        ("node: a gap, another row or another color starts a new line",),
    ),
    Plant(
        "web-sheet-join-row",
        "page/src/briefing/sheet.ts",
        "run.y === record.y",
        "true",
        ("node: a gap, another row or another color starts a new line",),
    ),
    Plant(
        "web-sheet-join-color",
        "page/src/briefing/sheet.ts",
        "run.color === record.color",
        "true",
        (
            "node: a gap, another row or another color starts a new line",
            "node: brackets in a label switch to color code 1 and back to the label's shade",
        ),
    ),
    Plant(
        "web-sheet-join-spacing",
        "page/src/briefing/sheet.ts",
        "const width = (font.widths[record.code] ?? 0) + font.spacing;",
        "const width = font.widths[record.code] ?? 0;",
        ("node: a glyph joins at the x where the last one ended, spacing included",),
    ),
    Plant(
        "web-sheet-list-every",
        "page/src/briefing/sheet.ts",
        "LIST_EVERY = 24",
        "LIST_EVERY = 25",
        (
            "node: frame 1 and every 24th frame are listed; the others only when they change",
        ),
    ),
    Plant(
        "web-sheet-list-first",
        "page/src/briefing/sheet.ts",
        "this.frame === 1 ||",
        "",
        ("node: frame 1 is listed even when nothing changed",),
    ),
    Plant(
        "web-sheet-list-pressed",
        "page/src/briefing/sheet.ts",
        "this.#pressed ||",
        "",
        ("node: the first frame after a press is listed",),
    ),
    Plant(
        "web-sheet-compare-ages",
        "page/src/briefing/sheet.ts",
        '`${n(i)}:${n(m.group)}` : "-"',
        '`${n(i)}:${n(m.group)}:${n(m.age)}` : "-"',
        (
            "node: frame 1 and every 24th frame are listed; the others only when they change",
            "node: the compare text leaves out the center, the zoom and the ages",
        ),
    ),
    Plant(
        "web-sheet-state-play",
        "page/src/briefing/sheet.ts",
        "`play=${n(player.playing ? 1 : 0)}`",
        "`play=1`",
        (
            "node: a press prints its line, then the state; a press not offered says ignored",
        ),
    ),
    Plant(
        "web-sheet-state-markers",
        "page/src/briefing/sheet.ts",
        'markers.join(";")',
        'markers.join(",")',
        (
            "node: the state text lists markers SLOT:GROUP:AGE and labels SLOT:TEXT:X:Y:ROW:AGE",
        ),
    ),
    Plant(
        "web-sheet-press-ignored",
        "page/src/briefing/sheet.ts",
        '${sounds === null ? " ignored" : ""}',
        '${""}',
        (
            "node: a press prints its line, then the state; a press not offered says ignored",
        ),
    ),
    Plant(
        "web-sheet-glyph-count",
        "page/src/briefing/sheet.ts",
        'run.lines.push("unknown_glyphs 0");',
        'run.lines.push("unknown_glyphs 1");',
        ("node: a run begins at state 0 and ends with the unknown glyph count",),
    ),
    Plant(
        "web-sheet-run-count",
        "page/src/briefing/sheet.ts",
        "|| count < 0)",
        "|| count < -9)",
        ("node: a recipe with a word it does not know, or a bad count, is refused",),
    ),
    Plant(
        "web-sheet-briefing-line",
        "page/src/briefing/sheet.ts",
        "`briefing ${n(index)} frames ${n(runningTime)}`",
        "`briefing ${n(runningTime)} frames ${n(index)}`",
        (
            "node: the briefing line names the briefing and its running time; the default is briefing 0, 200 frames",
        ),
    ),
    Plant(
        "web-crc-polynomial",
        "page/src/briefing/crc.ts",
        "0xedb88320 ^",
        "0xedb88321 ^",
        (
            "node: the CRC-32 agrees with zlib on bytes with the high bit set",
            "node: the CRC-32 of the standard check string",
        ),
    ),
    Plant(
        "web-crc-bit-test",
        "page/src/briefing/crc.ts",
        "(value & 1) === 1 ?",
        "(value & 1) === 0 ?",
        (
            "node: the CRC-32 agrees with zlib on bytes with the high bit set",
            "node: the CRC-32 of the standard check string",
        ),
    ),
    Plant(
        "web-crc-final-xor",
        "page/src/briefing/crc.ts",
        "return (value ^ 0xffffffff) >>> 0;",
        "return value >>> 0;",
        (
            "node: the CRC-32 agrees with zlib on bytes with the high bit set",
            "node: the CRC-32 of nothing is 0",
        ),
    ),
    Plant(
        "web-crc-start",
        "page/src/briefing/crc.ts",
        "let value = 0xffffffff;",
        "let value = 0;",
        (
            "node: the CRC-32 agrees with zlib on bytes with the high bit set",
            "node: the CRC-32 of nothing is 0",
        ),
    ),
    Plant(
        "web-crc-hex-width",
        "page/src/briefing/crc.ts",
        "padStart(8,",
        "padStart(7,",
        ("node: hex8 is 8 lowercase digits",),
    ),
    Plant(
        "web-col-565-green",
        "page/src/briefing/colors.ts",
        "    (green << 2) | (green >> 4),\n    (blue << 3) | (blue >> 2),\n  ];\n}\n\n/** Return the exact",
        "    (green << 2),\n    (blue << 3) | (blue >> 2),\n  ];\n}\n\n/** Return the exact",
        ("node: a text color code is its 565 value widened",),
    ),
    Plant(
        "web-col-step",
        "page/src/briefing/colors.ts",
        "0x48, 0x60,",
        "0x48, 0x61,",
        ("node: the 40 shades are five rows of eight steps",),
    ),
    Plant(
        "web-col-purple",
        "page/src/briefing/colors.ts",
        "[true, false, true],",
        "[true, true, true],",
        ("node: the 40 shades are five rows of eight steps",),
    ),
    Plant(
        "web-col-intensity-scale",
        "page/src/briefing/colors.ts",
        "FULL_INTENSITY = 31",
        "FULL_INTENSITY = 32",
        ("node: a tint is scaled by the pixel's intensity over 31",),
    ),
    Plant(
        "web-col-intensity-bits",
        "page/src/briefing/colors.ts",
        "return blue8 >> 3;",
        "return blue8 >> 2;",
        ("node: the intensity is the blue field of the pixel in 565",),
    ),
    Plant(
        "web-col-fixed-through-565",
        "page/src/briefing/colors.ts",
        ": through565(fixed);",
        ": fixed;",
        ("node: the grid reds are the two reds through 565",),
    ),
    Plant(
        "web-col-green-bits",
        "page/src/briefing/colors.ts",
        "const green = (value >> 5) & 63;",
        "const green = (value >> 5) & 31;",
        ("node: a text color code is its 565 value widened",),
    ),
    Plant(
        "web-lay-round",
        "page/src/briefing/layout.ts",
        "Math.floor(fit)",
        "Math.round(fit)",
        (
            "node: sharp bilinear: whole-step drawing, fitted size, smoothed a little",
            "node: whole pixels: the largest whole multiple that fits, sharp",
        ),
    ),
    Plant(
        "web-lay-whole-smooth",
        "page/src/briefing/layout.ts",
        "      smooth: false,",
        "      smooth: true,",
        (
            "node: whole pixels: never smaller than 1 when the room is smaller",
            "node: whole pixels: the largest whole multiple that fits, sharp",
        ),
    ),
    Plant(
        "web-lay-engine-factor",
        "page/src/briefing/layout.ts",
        'if (setting === "engine_fit") return { factor: 1,',
        'if (setting === "engine_fit") return { factor: whole,',
        ("node: engine fit: the largest size of the same shape that fits, smoothed",),
    ),
    Plant(
        "web-lay-fit-min",
        "page/src/briefing/layout.ts",
        "Math.min(width / PANEL_W, height / PANEL_H)",
        "Math.max(width / PANEL_W, height / PANEL_H)",
        (
            "node: engine fit: the largest size of the same shape that fits, smoothed",
            "node: the shape is always 3 to 2",
        ),
    ),
    Plant(
        "web-clk-ceil",
        "page/src/briefing/clock.ts",
        "Math.floor(this.#owed / FRAME_MS)",
        "Math.ceil(this.#owed / FRAME_MS)",
        (
            "node: a slow tab runs at most four frames at once and drops the rest",
            "node: time under a frame is kept for the next call",
        ),
    ),
    Plant(
        "web-clk-cap",
        "page/src/briefing/clock.ts",
        "MAX_CATCH_UP = 4",
        "MAX_CATCH_UP = 5",
        ("node: a slow tab runs at most four frames at once and drops the rest",),
    ),
    Plant(
        "web-clk-keeps-owed",
        "page/src/briefing/clock.ts",
        "this.#owed = 0;\n      return MAX_CATCH_UP;",
        "return MAX_CATCH_UP;",
        ("node: a slow tab runs at most four frames at once and drops the rest",),
    ),
    Plant(
        "web-clk-reset",
        "page/src/briefing/clock.ts",
        "    this.#last = null;\n    this.#owed = 0;",
        "    this.#owed = 0;",
        ("node: reset forgets the time: a hidden tab does not catch up",),
    ),
    Plant(
        "web-clk-backwards",
        "page/src/briefing/clock.ts",
        "Math.max(0, now - this.#last)",
        "now - this.#last",
        ("node: a clock that runs backwards owes nothing",),
    ),
    Plant(
        "web-col-565-red",
        "page/src/briefing/colors.ts",
        "const red = rgb[0] >> 3;",
        "const red = rgb[0] >> 2;",
        (
            "node: 565 keeps 5, 6 and 5 bits and widens by repeating the top bits",
            "node: a tint is scaled by the pixel's intensity over 31",
        ),
    ),
    Plant(
        "web-col-black",
        "page/src/briefing/colors.ts",
        "const BLACK: Rgb = [0, 0, 0];",
        "const BLACK: Rgb = [1, 0, 0];",
        ("node: a name that is not a color is black",),
    ),
    Plant(
        "web-sheet-frame-zero",
        "page/src/briefing/sheet.ts",
        "  frame = 0;",
        "  frame = 1;",
        (
            "node: a press before any frame is press 0",
            "node: a press prints its line, then the state; a press not offered says ignored",
        ),
    ),
    Plant(
        "web-sheet-run-text",
        "page/src/briefing/sheet.ts",
        "run.text += String.fromCharCode(record.code);",
        'run.text += "?";',
        (
            "node: a frame begins with the map's two clips and ends with the page line",
            "node: a glyph joins at the x where the last one ended, spacing included",
        ),
    ),
    Plant(
        "web-sheet-vline-words",
        "page/src/briefing/sheet.ts",
        "`vline ${n(record.x)} ${n(record.top)} ${n(record.bottom)} ${record.color}`",
        "`vline ${n(record.top)} ${n(record.x)} ${n(record.bottom)} ${record.color}`",
        (
            "node: a line that falls exactly on x 0 is the first line",
            "node: at zoom 16 only phase 2 lines are minor; under 16 there are none",
        ),
    ),
    Plant(
        "web-sheet-tint-words",
        "page/src/briefing/sheet.ts",
        "`tint ${record.sheet} ${n(record.index)} ${n(record.x)} ${n(record.y)} ${record.color}`",
        "`tint ${record.sheet} ${n(record.x)} ${n(record.index)} ${n(record.y)} ${record.color}`",
        (
            "node: a marker of age 4 to 7 draws four rings from offset 2 * (11 - age)",
            "node: a marker of age 8 to 11 first fills and outlines a shrunk box, then draws its rings",
        ),
    ),
    Plant(
        "web-sheet-list-all",
        "page/src/briefing/sheet.ts",
        "this.listAll ||",
        "",
        ("node: listall lists every later frame",),
    ),
    Plant(
        "web-clk-first-call",
        "page/src/briefing/clock.ts",
        "this.#last = now;\n      return 0;",
        "this.#last = now;\n      return 1;",
        (
            "node: reset forgets the time: a hidden tab does not catch up",
            "node: the first call returns 0 and starts the clock",
        ),
    ),
]
