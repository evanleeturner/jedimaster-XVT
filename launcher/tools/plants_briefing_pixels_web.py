"""The planted faults of the briefing map's pixels: background, heading, packing, lists, painter.

Purpose:
    Break one rule of the page's drawing of the setup screen per plant: the
    bundle's new fields, the heading (its bytes, color, place and font), the
    16-bit packing, the reading of a list of records, the painter's order
    (background, records, heading) and its drawing rules (rectangles with no
    area, lines the wrong way round, clips turned inside out, tints of any
    sheet), the tint's rounding, and the pictures the panel loads.

Flow:
    ``plants_briefing_web`` joins this table; ``plant_page_web`` applies each
    plant alone: write ``new`` over ``old`` in ``path``, run ``npm test`` (and
    build and ``npm run test:browser`` when the plant names a browser test),
    restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across the page tables, all starting ``web-px-``.
    - Each ``old`` text occurs exactly once in its file.
    - ``expect`` entries are ``node: <title>`` or ``browser: <title>``.

Call:
    ``from plants_briefing_pixels_web import BRIEFING_PIXELS_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

H = "page/src/briefing/heading.ts"
K = "page/src/briefing/pack.ts"
L = "page/src/briefing/lines.ts"
P = "page/src/briefing/paint.ts"
S = "page/src/briefing/sprites.ts"
B = "page/src/briefing/bundle.ts"
C = "page/src/briefing/colors.ts"
N = "node: "
W = "browser: the panel shows the team's background, the caption band and the heading"
HEAD = N + "the heading is white and the name yellow, from panel (2, -4)"
BYTES = N + "the heading is the string, a space, the yellow color byte and the name"
ORDER = N + "a frame is drawn background first, then the records, then the heading"
LIST = N + "a list is black and its records, with no background and no heading"
AREA = N + "a fill or an outline with no area draws nothing"
LINES = N + "a line with its ends the wrong way round is drawn between them"
CLIPS = N + "a clip turned inside out is ignored and the one before stays"


BRIEFING_PIXELS_PLANTS: list[Plant] = [
    Plant(
        "web-px-bundle-background-unchecked",
        B,
        '    typeof background !== "string" ||\n',
        "",
        (N + "a team's background must be text",),
    ),
    Plant(
        "web-px-bundle-heading-unchecked",
        B,
        "    !isByteText(heading) ||\n",
        "",
        (N + "the heading must be byte text and the heading font a font or null",),
    ),
    Plant(
        "web-px-bundle-heading-font-required",
        B,
        "value.heading_font === null ? { value: null } : fontOf(value.heading_font)",
        "fontOf(value.heading_font)",
        (N + "the heading must be byte text and the heading font a font or null",),
    ),
    Plant(
        "web-px-heading-left",
        H,
        "export const HEADING_X = 2;",
        "export const HEADING_X = 3;",
        (HEAD, W),
    ),
    Plant(
        "web-px-heading-low",
        H,
        "export const HEADING_Y = -4;",
        "export const HEADING_Y = -3;",
        (HEAD, W),
    ),
    Plant(
        "web-px-heading-yellow",
        H,
        'export const HEADING_COLOR = "white";',
        'export const HEADING_COLOR = "yellow";',
        (HEAD, W),
    ),
    Plant(
        "web-px-heading-color-byte",
        H,
        "const TEAM_COLOR_BYTE = 4;",
        "const TEAM_COLOR_BYTE = 3;",
        (BYTES, HEAD),
    ),
    Plant(
        "web-px-heading-separator",
        H,
        "const SPACE = 32;",
        "const SPACE = 33;",
        (BYTES,),
    ),
    Plant(
        "web-px-heading-no-name",
        H,
        "SPACE, TEAM_COLOR_BYTE, ...toBytes(name)]",
        "SPACE, TEAM_COLOR_BYTE]",
        (BYTES, HEAD),
    ),
    Plant(
        "web-px-heading-no-string",
        H,
        "return [...toBytes(bundle.heading), SPACE,",
        "return [SPACE,",
        (BYTES, HEAD),
    ),
    Plant(
        "web-px-heading-unlisted-team-named",
        H,
        '?.name ?? "";',
        '?.name ?? "x";',
        (N + "a team the bundle does not list has an empty name",),
    ),
    Plant(
        "web-px-heading-always",
        H,
        "  if (font === null) return [];",
        "  if (font === undefined) return [];",
        (N + "a bundle with no heading font has no heading",),
    ),
    Plant(
        "web-px-pack-red-bits",
        K,
        "((red >> 3) << 11)",
        "((red >> 2) << 11)",
        (N + "a color packs to red 5 bits, green 6 bits, blue 5 bits",),
    ),
    Plant(
        "web-px-pack-green-bits",
        K,
        "((green >> 2) << 5)",
        "((green >> 3) << 5)",
        (N + "a color packs to red 5 bits, green 6 bits, blue 5 bits",),
    ),
    Plant(
        "web-px-pack-blue-bits",
        K,
        "| (blue >> 3)",
        "| (blue >> 2)",
        (N + "a color packs to red 5 bits, green 6 bits, blue 5 bits",),
    ),
    Plant(
        "web-px-pack-high-byte-first",
        K,
        "    out[pixel * 2] = value & 0xff;\n    out[pixel * 2 + 1] = value >> 8;",
        "    out[pixel * 2] = value >> 8;\n    out[pixel * 2 + 1] = value & 0xff;",
        (N + "a panel packs two bytes a pixel, low byte first, ignoring alpha",),
    ),
    Plant(
        "web-px-pack-alpha-read",
        K,
        "      rgba[pixel * 4 + 2] ?? 0,\n",
        "      rgba[pixel * 4 + 3] ?? 0,\n",
        (N + "a panel packs two bytes a pixel, low byte first, ignoring alpha",),
    ),
    Plant(
        "web-px-lines-edge-kept",
        L,
        'kind: m[1] === "fill" ? "fill" : "outline",',
        'kind: m[1] === undefined ? "fill" : m[1] === "edge" ? "fill" : m[1] === "fill" ? "fill" : "outline",',
        (N + "an edge is read as an outline",),
    ),
    Plant(
        "web-px-lines-hex-short",
        L,
        "      at += 3;",
        "      at += 2;",
        (N + "a quoted string holds escaped quotes, backslashes and hex bytes",),
    ),
    Plant(
        "web-px-lines-escape-kept",
        L,
        "      bytes.push(quoted.charCodeAt(at + 1) & 0xff);\n      at += 1;",
        "      bytes.push(92);\n      at += 1;",
        (N + "a quoted string holds escaped quotes, backslashes and hex bytes",),
    ),
    Plant(
        "web-px-lines-blank-read",
        L,
        '    if (trimmed !== "") records.push',
        "    if (true) records.push",
        (N + "blank lines are skipped and a line of no known kind is an error",),
    ),
    Plant(
        "web-px-lines-unknown-skipped",
        L,
        "  throw new Error(`not a record line: ${line}`);",
        "  return [];",
        (N + "blank lines are skipped and a line of no known kind is an error",),
    ),
    Plant(
        "web-px-lines-vline-ends-swapped",
        L,
        '        top: num(m[2]),\n        bottom: num(m[3]),\n        color: m[4] ?? "",\n      },\n    ];\n  }\n  if ((m = HLINE',
        '        top: num(m[3]),\n        bottom: num(m[2]),\n        color: m[4] ?? "",\n      },\n    ];\n  }\n  if ((m = HLINE',
        (N + "every plain kind is read with its numbers and its color",),
    ),
    Plant(
        "web-px-lines-color-first-word",
        L,
        'const TEXT = new RegExp(`^text ${WHOLE} ${WHOLE} (.+?) "(.*)"$`);',
        'const TEXT = new RegExp(`^text ${WHOLE} ${WHOLE} (\\\\S+?) "(.*)"$`);',
        (N + "a text line is drawn glyph by glyph with the font's widths",),
    ),
    Plant(
        "web-px-paint-no-background",
        P,
        "    if (team !== null) this.#underlay(context, team);\n",
        "",
        (ORDER, N + "a team's own background is the one drawn", W),
    ),
    Plant(
        "web-px-paint-background-for-lists",
        P,
        "    if (team !== null) this.#underlay(context, team);\n",
        "    this.#underlay(context, team ?? 0);\n",
        (LIST,),
    ),
    Plant(
        "web-px-paint-no-heading",
        P,
        "    if (team !== null) this.#heading(context, team);\n",
        "",
        (ORDER, W),
    ),
    Plant(
        "web-px-paint-heading-for-lists",
        P,
        "    if (team !== null) this.#heading(context, team);\n",
        "    this.#heading(context, team ?? 0);\n",
        (LIST,),
    ),
    Plant(
        "web-px-paint-heading-first",
        P,
        "    for (const record of records) this.#draw(context, record);\n"
        "    if (team !== null) this.#heading(context, team);\n",
        "    if (team !== null) this.#heading(context, team);\n"
        "    for (const record of records) this.#draw(context, record);\n",
        (ORDER,),
    ),
    Plant(
        "web-px-paint-background-last",
        P,
        "    if (team !== null) this.#underlay(context, team);\n"
        "    context.beginPath();\n    context.rect(0, 0, PANEL_W, PANEL_H);\n    context.clip();\n"
        "    for (const record of records) this.#draw(context, record);\n",
        "    context.beginPath();\n    context.rect(0, 0, PANEL_W, PANEL_H);\n    context.clip();\n"
        "    for (const record of records) this.#draw(context, record);\n"
        "    if (team !== null) this.#underlay(context, team);\n",
        (ORDER, W),
    ),
    Plant(
        "web-px-paint-heading-under-last-clip",
        P,
        "    if (font === null) return;\n    context.restore();\n    context.save();\n"
        "    context.beginPath();\n    context.rect(0, 0, PANEL_W, PANEL_H);\n    context.clip();\n",
        "    if (font === null) return;\n",
        (N + "the heading is drawn under the whole panel's clip, not the last clip",),
    ),
    Plant(
        "web-px-paint-heading-wrong-atlas",
        P,
        "const atlas = heading ? this.#sprites.headingAtlas : this.#sprites.atlas;",
        "const atlas = this.#sprites.atlas;",
        (
            N
            + "each glyph of the heading comes from font 12's atlas at the heading's place",
            W,
        ),
    ),
    Plant(
        "web-px-paint-heading-font-10-cells",
        P,
        "const { columns, cell_width: cellWidth, cell_height: cellHeight } = font;",
        "const { columns, cell_width: cellWidth, cell_height: cellHeight } =\n      this.#bundle.font;",
        (
            N
            + "each glyph of the heading comes from font 12's atlas at the heading's place",
        ),
    ),
    Plant(
        "web-px-paint-team-0-background",
        P,
        "this.#bundle.teams.find((t) => t.team === team)?.background",
        "this.#bundle.teams.find((t) => t.team === 0)?.background",
        (N + "a team's own background is the one drawn",),
    ),
    Plant(
        "web-px-paint-one-pixel-boxes",
        P,
        "return box.right > box.left && box.bottom > box.top;",
        "return box.right >= box.left && box.bottom >= box.top;",
        (
            N
            + "a rectangle draws only when its right is past its left and its bottom past its top",
            AREA,
        ),
    ),
    Plant(
        "web-px-paint-inside-out-boxes",
        P,
        "return box.right > box.left && box.bottom > box.top;",
        "return box.right !== box.left && box.bottom !== box.top;",
        (
            N
            + "a rectangle draws only when its right is past its left and its bottom past its top",
            AREA,
        ),
    ),
    Plant(
        "web-px-paint-fill-without-area",
        P,
        '      case "fill":\n        if (!hasArea(record)) return;\n',
        '      case "fill":\n',
        (AREA,),
    ),
    Plant(
        "web-px-paint-outline-without-area",
        P,
        "    if (!hasArea(record)) return;\n    context.fillStyle = css(this.#rgb(record.color));\n    const { left, top, right, bottom } = record;",
        "    context.fillStyle = css(this.#rgb(record.color));\n    const { left, top, right, bottom } = record;",
        (AREA,),
    ),
    Plant(
        "web-px-paint-vline-ends",
        P,
        "const top = Math.min(record.top, record.bottom);",
        "const top = record.top;",
        (LINES,),
    ),
    Plant(
        "web-px-paint-hline-ends",
        P,
        "const right = Math.max(record.left, record.right);",
        "const right = record.right;",
        (LINES,),
    ),
    Plant(
        "web-px-paint-inside-out-clip-obeyed",
        P,
        "        if (record.right < record.left || record.bottom < record.top) return;\n",
        "",
        (CLIPS,),
    ),
    Plant(
        "web-px-paint-clip-width-only",
        P,
        "        if (record.right < record.left || record.bottom < record.top) return;\n",
        "        if (record.right < record.left) return;\n",
        (CLIPS,),
    ),
    Plant(
        "web-px-paint-outside-clip-open",
        P,
        "          context.rect(0, 0, 0, 0);",
        "          context.rect(0, 0, PANEL_W, PANEL_H);",
        (N + "a clip past the panel is cut by it and one outside it holds nothing",),
    ),
    Plant(
        "web-px-paint-clip-not-cut",
        P,
        "const right = Math.min(PANEL_W - 1, record.right);",
        "const right = record.right;",
        (N + "a clip past the panel is cut by it and one outside it holds nothing",),
    ),
    Plant(
        "web-px-paint-tint-always-grey",
        P,
        "const grey = this.#sprites.planes.get(sheet);",
        'const grey = this.#sprites.planes.get("greyicon");',
        (N + "a tint reads the plane of the sheet it names",),
    ),
    Plant(
        "web-px-paint-tint-cache-ignores-sheet",
        P,
        "const key = `${sheet} ${String(index)} ${color}`;",
        "const key = `${String(index)} ${color}`;",
        (N + "a tint reads the plane of the sheet it names",),
    ),
    Plant(
        "web-px-colors-tint-red-field",
        C,
        "(scale(tint[0] >> 3) << 11) |",
        "(scale(tint[0] >> 2) << 11) |",
        (N + "a tint is cut to its 565 fields before it is scaled",),
    ),
    Plant(
        "web-px-colors-tint-green-field",
        C,
        "(scale(tint[1] >> 2) << 5) |",
        "(scale(tint[1] >> 3) << 5) |",
        (N + "a tint is cut to its 565 fields before it is scaled",),
    ),
    Plant(
        "web-px-colors-tint-scaled-late",
        C,
        "Math.trunc((field * intensity) / FULL_INTENSITY);",
        "Math.round((field * intensity) / FULL_INTENSITY);",
        (N + "a tint is cut to its 565 fields before it is scaled",),
    ),
    Plant(
        "web-px-sprites-no-backgrounds",
        S,
        "const underNames = [...new Set(bundle.teams.map((t) => t.background))];",
        "const underNames: string[] = [];",
        (W,),
    ),
    Plant(
        "web-px-sprites-no-heading-atlas",
        S,
        "    bundle.heading_font === null\n      ? null\n      : loadImage(bundle.heading_font.picture),",
        "    null,",
        (W,),
    ),
    Plant(
        "web-px-panel-paints-no-team",
        "page/src/briefing/panel.ts",
        "      player.team,\n    );",
        "      null,\n    );",
        (W,),
    ),
    Plant(
        "web-px-pack-extra-pixel",
        K,
        "const count = Math.floor(rgba.length / 4);",
        "const count = Math.floor(rgba.length / 4) + 1;",
        (N + "an image of no pixels packs to nothing",),
    ),
    Plant(
        "web-px-paint-black-background-drawn-anyway",
        P,
        "    if (picture !== undefined) context.drawImage(picture, 0, 0);",
        "    context.drawImage(picture as never, 0, 0);",
        (N + "a team whose background did not load is black under its records",),
    ),
    Plant(
        "web-px-paint-heading-reset-without-font",
        P,
        "    if (font === null) return;\n    context.restore();",
        "    context.restore();",
        (N + "a frame of a bundle with no heading font draws no heading",),
    ),
    Plant(
        "web-px-paint-copy-is-small",
        P,
        "out.drawImage(this.#panel, 0, 0, this.#target.width, this.#target.height);",
        "out.drawImage(this.#panel, 0, 0);",
        (N + "the panel is copied to the page's canvas whole",),
    ),
    Plant(
        "web-px-paint-copy-is-smoothed",
        P,
        "    out.imageSmoothingEnabled = false;",
        "    out.imageSmoothingEnabled = true;",
        (N + "the panel is copied to the page's canvas whole",),
    ),
    Plant(
        "web-px-paint-icon-sheet-ignored",
        P,
        "    const image = this.#sprites.sheets.get(sheet);",
        '    const image = this.#sprites.sheets.get("mapicon0");',
        (N + "a plain icon of the grey sheet is drawn when the sheet is there",),
    ),
]
