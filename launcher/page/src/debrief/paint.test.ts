import assert from "node:assert/strict";
import { test } from "node:test";

import type { FontData } from "../generated/briefing.ts";
import type { DrawnLine } from "./lines.ts";
import type { Fonts } from "./paint.ts";
import {
  INKS,
  parseFont,
  parseFonts,
  placeLine,
  PLATE_RGB,
  readsBare,
  WORD_ALPHA,
  wordPlates,
  ZONE_ALPHA,
} from "./paint.ts";

const TEXT_COLORS = [0x07e0, 0xf800, 0xffe0, 0x321f, 0x841f];

function font(more: Partial<FontData> = {}): FontData {
  return {
    spacing: 1,
    height: 10,
    widths: Array.from({ length: 256 }, (_, code) => (code < 33 ? 0 : 6)),
    text_colors: TEXT_COLORS,
    columns: 16,
    cell_width: 8,
    cell_height: 12,
    picture: "times12.png",
    ...more,
  };
}

const FONTS: Fonts = {
  font12: font(),
  font15: font({ height: 14, picture: "times15.png" }),
};

function line(more: Partial<DrawnLine> = {}): DrawnLine {
  return { font: 12, x: 88, y: 111, ink: "white", text: "AB", ...more };
}

test("the fonts json is read when both fonts have the shape, else it is nothing", () => {
  const json = { font12: font(), font15: font({ picture: "times15.png" }) };
  assert.deepEqual(parseFonts(json), {
    font12: font(),
    font15: font({ picture: "times15.png" }),
  });
  assert.equal(parseFonts({ font12: font() }), null);
  assert.equal(parseFonts(null), null);
  assert.equal(parseFonts([]), null);
  assert.equal(parseFont({ ...font(), widths: ["x"] }), null);
  assert.equal(parseFont({ ...font(), picture: 3 }), null);
  assert.equal(parseFont({ ...font(), height: undefined }), null);
});

test("a line is placed glyph by glyph: width plus spacing apart, from its x and y", () => {
  const placed = placeLine(FONTS, line());
  assert.deepEqual(
    placed.glyphs.map((g) => [g.code, g.x, g.y]),
    [
      [65, 88, 111],
      [66, 95, 111],
    ],
  );
  assert.equal(placed.clipRight, null);
});

test("a colour byte switches the colour and byte 1 returns to the line's own", () => {
  const placed = placeLine(
    FONTS,
    line({ ink: "yellow", text: "\u0002A\u0001B" }),
  );
  assert.deepEqual(
    placed.glyphs.map((g) => g.color),
    ["code1", "yellow"],
  );
});

test("a centred line starts half its width left of the rectangle's middle", () => {
  const centred = { left: 84, top: 90, right: 434, bottom: 106 };
  const placed = placeLine(FONTS, line({ font: 15, text: "AB", centred }));
  // width of "AB" is 6 + 1 + 6 + 1 - 1 = 13 (the game's measure drops one spacing)
  assert.deepEqual(
    placed.glyphs.slice(0, 1).map((g) => [g.x, g.y]),
    [[84 + 175 - 6, 90 + 8 - 7]],
  );
});

test("a clipped line keeps its cut for the painter", () => {
  assert.equal(placeLine(FONTS, line({ clipRight: 225 })).clipRight, 225);
});

function luminance([r, g, b]: readonly [number, number, number]): number {
  const channel = (value: number): number => {
    const v = value / 255;
    return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

function contrast(
  a: readonly [number, number, number],
  b: readonly [number, number, number],
): number {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return ((hi ?? 0) + 0.05) / ((lo ?? 0) + 0.05);
}

/** The plate's colour laid at `alpha` over a white picture: the worst art. */
function overWhite(alpha: number): readonly [number, number, number] {
  return PLATE_RGB.map(
    (c) => alpha * c + (1 - alpha) * 255,
  ) as unknown as readonly [number, number, number];
}

/** The colour bytes the page uses: 2 (green), 4 (yellow), 6 (light blue). */
const BYTE_RGB = [TEXT_COLORS[0], TEXT_COLORS[2], TEXT_COLORS[4]].map(
  (code) => {
    const value = code ?? 0;
    return [
      ((value >> 11) & 31) * 8 + 4,
      ((value >> 5) & 63) * 4 + 2,
      (value & 31) * 8 + 4,
    ] as const;
  },
);

test("white and yellow keep 4.5:1 on the zone plate over a white picture", () => {
  const worst = overWhite(ZONE_ALPHA);
  for (const rgb of [INKS.white, INKS.yellow, BYTE_RGB[1]]) {
    if (rgb === undefined) continue;
    assert.ok(contrast(rgb, worst) >= 4.5, String(contrast(rgb, worst)));
    assert.ok(readsBare(rgb), String(rgb));
  }
});

test("every other ink and colour byte needs its word plate, and keeps 4.5:1 on it", () => {
  const zone = overWhite(ZONE_ALPHA);
  const word = overWhite(WORD_ALPHA);
  const others = [INKS.gray, INKS.red, BYTE_RGB[0], BYTE_RGB[2]];
  for (const rgb of others) {
    if (rgb === undefined) continue;
    assert.ok(contrast(rgb, zone) < 4.5, `${String(rgb)} reads bare`);
    assert.equal(readsBare(rgb), false, String(rgb));
    assert.ok(contrast(rgb, word) >= 4.5, `${String(rgb)} on its plate`);
  }
});

test("a word plate covers each run in an ink that needs one, never white or yellow", () => {
  const font12 = FONTS.font12;
  // Light blue (byte 6) for the rank, then the line's own yellow for the name.
  const ranked = placeLine(
    FONTS,
    line({ ink: "yellow", x: 100, text: "\u0006AB\u0001CD" }),
  );
  assert.deepEqual(wordPlates(ranked, font12), [
    { left: 99, top: 110, right: 100 + 7 + 8 + 1, bottom: 111 + 12 + 1 },
  ]);
  const plain = placeLine(FONTS, line({ ink: "white", text: "AB" }));
  assert.deepEqual(wordPlates(plain, font12), []);
  // Two runs apart give two plates; a cut line cuts its plate.
  const two = placeLine(
    FONTS,
    line({ ink: "gray", x: 0, text: "A\u0004B\u0001C", clipRight: 17 }),
  );
  assert.deepEqual(wordPlates(two, font12), [
    { left: -1, top: 110, right: 9, bottom: 124 },
    { left: 13, top: 110, right: 17, bottom: 124 },
  ]);
});
