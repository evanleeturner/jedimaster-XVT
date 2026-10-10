"""The planted faults of the briefing player: the caption wrap and the text drawing.

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
    ``from plants_briefing_text_web import BRIEFING_TEXT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)


BRIEFING_TEXT_PLANTS: list[Plant] = [
    Plant(
        "web-cap-top",
        "page/src/briefing/caption.ts",
        "CAPTION_TOP = 208",
        "CAPTION_TOP = 209",
        (
            "node: a bracket closed on an earlier line does not color the next one",
            "node: a line starting with > is a yellow heading, centered one pixel lower, without the >",
        ),
    ),
    Plant(
        "web-cap-wrap-at-360",
        "page/src/briefing/caption.ts",
        "if (measure(font, line) >= PANEL_WIDTH) {",
        "if (measure(font, line) > PANEL_WIDTH) {",
        (
            "node: a line of 354 pixels stays whole and one of 360 wraps at the last space",
        ),
    ),
    Plant(
        "web-cap-no-last-space",
        "page/src/briefing/caption.ts",
        "if (lastSpace >= 0) {",
        "if (false) {",
        (
            "node: a bracket closed on an earlier line does not color the next one",
            "node: a line of 354 pixels stays whole and one of 360 wraps at the last space",
        ),
    ),
    Plant(
        "web-cap-tab-is-space",
        "page/src/briefing/caption.ts",
        "if (byte === SPACE) lastSpace = line.length - 1;",
        "lastSpace = line.length - 1;",
        (
            "node: a tab is never the place a line ends: with no space the line ends after the word",
        ),
    ),
    Plant(
        "web-cap-heading-center",
        "page/src/briefing/caption.ts",
        "(measure(font, shown) >> 1)",
        "(measure(font, shown) >> 2)",
        (
            "node: a heading's width includes its trailing spaces",
            "node: a line starting with > is a yellow heading, centered one pixel lower, without the >",
        ),
    ),
    Plant(
        "web-cap-heading-row",
        "page/src/briefing/caption.ts",
        '"yellow", x, y + 1)',
        '"yellow", x, y)',
        (
            "node: a line starting with > is a yellow heading, centered one pixel lower, without the >",
        ),
    ),
    Plant(
        "web-cap-inside-color",
        "page/src/briefing/caption.ts",
        "const bytes = line.inside ? [2, ...shown] : shown;",
        "const bytes = shown;",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-cap-open-bracket",
        "page/src/briefing/caption.ts",
        "if (byte === 2) depth += 1;",
        "if (byte === 2) depth += 0;",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-cap-no-heading",
        "page/src/briefing/caption.ts",
        "const heading = line.bytes[0] === HEADING;",
        "const heading = false;",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a heading's width includes its trailing spaces",
        ),
    ),
    Plant(
        "web-cap-line-height",
        "page/src/briefing/caption.ts",
        "row * font.height",
        "row * 13",
        (
            "node: a bracket closed on an earlier line does not color the next one",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-cap-bracket-bytes",
        "page/src/briefing/caption.ts",
        "(b === OPEN ? 2 : b === CLOSE ? 1 : b)",
        "(b === OPEN ? 1 : b === CLOSE ? 2 : b)",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-cap-inside-lost",
        "page/src/briefing/caption.ts",
        "inside: isInside(bytes.slice(0, start))",
        "inside: false",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-text-stop-x",
        "page/src/briefing/text.ts",
        "const STOP_X = 640;",
        "const STOP_X = 641;",
        ("node: a string stops when the pen stands at x 640 or more before a byte",),
    ),
    Plant(
        "web-text-pen-stop",
        "page/src/briefing/text.ts",
        "if (pen >= STOP_X) break;",
        "if (pen > STOP_X) break;",
        ("node: a string stops when the pen stands at x 640 or more before a byte",),
    ),
    Plant(
        "web-text-measure-spacing",
        "page/src/briefing/text.ts",
        "total += widthOf(font, byte) + font.spacing;",
        "total += widthOf(font, byte);",
        ("node: measure adds the spacing between glyphs, not after the last",),
    ),
    Plant(
        "web-text-measure-last-spacing",
        "page/src/briefing/text.ts",
        "return total - font.spacing;",
        "return total;",
        ("node: measure adds the spacing between glyphs, not after the last",),
    ),
    Plant(
        "web-text-code-color",
        "page/src/briefing/text.ts",
        "`code${String(byte - 1)}`",
        "`code${String(byte)}`",
        (
            "node: a heading inside brackets keeps the bracket color after the >",
            "node: a line that begins inside brackets begins in color code 1",
        ),
    ),
    Plant(
        "web-text-width-0-glyph",
        "page/src/briefing/text.ts",
        "      if (width > 0)\n",
        "      if (width >= 0)\n",
        ("node: a glyph of width 0 draws nothing but the spacing still moves the pen",),
    ),
    Plant(
        "web-text-zero-ends",
        "page/src/briefing/text.ts",
        "return end < 0 ? bytes : bytes.slice(0, end);",
        "return bytes;",
        ("node: a string ends at its first byte 0",),
    ),
    Plant(
        "web-text-reset-byte",
        "page/src/briefing/text.ts",
        "RESET_BYTE = 1;",
        "RESET_BYTE = 0;",
        (
            "node: brackets in a label switch to color code 1 and back to the label's shade",
            "node: brackets switch to color code 1 and back; they take no room",
        ),
    ),
    Plant(
        "web-cap-dollar-skips",
        "page/src/briefing/caption.ts",
        "if (byte === DOLLAR) {\n        pos += 1;",
        "if (byte === DOLLAR) {\n        pos += 2;",
        ("node: a dollar sign ends a line and is not kept",),
    ),
    Plant(
        "web-cap-wrap-every-word",
        "page/src/briefing/caption.ts",
        "if (measure(font, line) >= PANEL_WIDTH) {",
        "if (measure(font, line) >= 1) {",
        (
            "node: a dollar sign ends a line and is not kept",
            "node: a heading's width includes its trailing spaces",
        ),
    ),
    Plant(
        "web-cap-no-space-goes-on",
        "page/src/briefing/caption.ts",
        "        ended = true;\n      }\n    }",
        "        ended = lastSpace >= 0;\n      }\n    }",
        ("node: a word wider than the line ends the line after it",),
    ),
    Plant(
        "web-text-color-bytes-widths",
        "page/src/briefing/text.ts",
        "if (byte > LAST_COLOR_BYTE) total",
        "if (byte > 0) total",
        ("node: measure sums glyph widths, ignoring the color bytes 1 to 6",),
    ),
    Plant(
        "web-text-pen-step",
        "page/src/briefing/text.ts",
        "pen += width + font.spacing;",
        "pen += width + 1 + font.spacing;",
        (
            "node: a frame begins with the map's two clips and ends with the page line",
            "node: a glyph of width 0 draws nothing but the spacing still moves the pen",
        ),
    ),
    Plant(
        "web-text-bytes-offset",
        "page/src/briefing/text.ts",
        "text.charCodeAt(i) & 0xff",
        "(text.charCodeAt(i) & 0xff) + 1",
        (
            "node: a bracket closed on an earlier line does not color the next one",
            "node: a caption is spoken without dollar signs and brackets",
        ),
    ),
]
