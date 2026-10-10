import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { drawBytes, measure, toBytes } from "./text.ts";

const FONT = makeBundle().font;
const SPACED = { ...FONT, spacing: 2 };

test("toBytes takes one byte per character", () => {
  assert.deepEqual(toBytes("aBé"), [97, 66, 233]);
  assert.deepEqual(toBytes(""), []);
});

test("measure sums glyph widths, ignoring the color bytes 1 to 6", () => {
  const loud = { ...FONT, widths: FONT.widths.map((w, i) => (i <= 6 ? 9 : w)) };
  assert.equal(measure(FONT, toBytes("ab")), 12);
  assert.equal(measure(loud, [97, 2, 98, 1, 99, 6]), 18);
  assert.equal(measure(FONT, toBytes("a b")), 16);
  assert.equal(measure(FONT, []), 0);
});

test("measure adds the spacing between glyphs, not after the last", () => {
  assert.equal(measure(SPACED, toBytes("ab")), 14);
  assert.equal(measure(SPACED, toBytes("a")), 6);
  assert.equal(measure(SPACED, []), -2);
});

test("a string ends at its first byte 0", () => {
  assert.equal(measure(FONT, [97, 0, 98]), 6);
  assert.deepEqual(
    drawBytes(FONT, [97, 0, 98], "white", 0, 0).glyphs.map((g) => g.code),
    [97],
  );
});

test("glyphs are drawn one after another from the pen", () => {
  const drawn = drawBytes(FONT, toBytes("a b"), "white", 10, 20);
  assert.deepEqual(
    drawn.glyphs.map((g) => [g.code, g.x, g.y]),
    [
      [97, 10, 20],
      [32, 16, 20],
      [98, 20, 20],
    ],
  );
  assert.equal(drawn.pen, 26);
});

test("byte 1 returns to the color the draw began with, bytes 2 to 6 switch", () => {
  const colors = drawBytes(
    FONT,
    [97, 2, 98, 1, 99, 5, 100, 6, 101],
    "yellow",
    0,
    0,
  ).glyphs.map((g) => g.color);
  assert.deepEqual(colors, ["yellow", "code1", "yellow", "code4", "code5"]);
});

test("the spacing moves the pen after each glyph", () => {
  const xs = drawBytes(SPACED, toBytes("abc"), "white", 0, 0).glyphs.map(
    (g) => g.x,
  );
  assert.deepEqual(xs, [0, 8, 16]);
});

test("a glyph of width 0 draws nothing but the spacing still moves the pen", () => {
  const thin = {
    ...SPACED,
    widths: SPACED.widths.map((w, i) => (i === 120 ? 0 : w)),
  };
  const drawn = drawBytes(thin, [97, 120, 98], "white", 0, 0);
  assert.deepEqual(
    drawn.glyphs.map((g) => g.x),
    [0, 10],
  );
});

test("a string stops when the pen stands at x 640 or more before a byte", () => {
  const drawn = drawBytes(FONT, toBytes("abcd"), "white", 636, 0);
  assert.deepEqual(
    drawn.glyphs.map((g) => g.x),
    [636],
  );
  assert.equal(drawBytes(FONT, toBytes("a"), "white", 640, 0).glyphs.length, 0);
});
