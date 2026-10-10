import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { readRecords, unquote } from "./lines.ts";

const FONT = makeBundle().font;

test("every plain kind is read with its numbers and its color", () => {
  const records = readRecords(
    [
      "clip 1 2 3 4",
      "vline 5 6 7 white",
      "hline 8 9 10 shade 3",
      "icon mapicon0 11 12 -13",
      "tint greyicon 14 15 16 shade 9",
      "fill -1 -2 3 4 code2",
      "outline 5 6 7 8 yellow",
    ].join("\n"),
    FONT,
  );
  assert.deepEqual(records, [
    { kind: "clip", left: 1, top: 2, right: 3, bottom: 4 },
    { kind: "vline", x: 5, top: 6, bottom: 7, color: "white" },
    { kind: "hline", y: 8, left: 9, right: 10, color: "shade 3" },
    { kind: "icon", sheet: "mapicon0", index: 11, x: 12, y: -13 },
    {
      kind: "tint",
      sheet: "greyicon",
      index: 14,
      x: 15,
      y: 16,
      color: "shade 9",
    },
    { kind: "fill", left: -1, top: -2, right: 3, bottom: 4, color: "code2" },
    { kind: "outline", left: 5, top: 6, right: 7, bottom: 8, color: "yellow" },
  ]);
});

test("an edge is read as an outline", () => {
  assert.deepEqual(readRecords("edge 1 2 3 4 code1", FONT), [
    { kind: "outline", left: 1, top: 2, right: 3, bottom: 4, color: "code1" },
  ]);
});

test("a text line is drawn glyph by glyph with the font's widths", () => {
  const records = readRecords('text 10 20 shade 33 "ab"', FONT);
  assert.deepEqual(records, [
    { kind: "glyph", code: 97, x: 10, y: 20, color: "shade 33" },
    { kind: "glyph", code: 98, x: 16, y: 20, color: "shade 33" },
  ]);
});

test("a quoted string holds escaped quotes, backslashes and hex bytes", () => {
  assert.deepEqual(
    unquote('a\\"b\\\\c\\x07\\xff'),
    [97, 34, 98, 92, 99, 7, 255],
  );
  assert.deepEqual(unquote(""), []);
});

test("a glyph of width 0 draws nothing but the pen still moves", () => {
  const thin = {
    ...FONT,
    spacing: 2,
    widths: FONT.widths.map((w, i) => (i === 7 ? 0 : w)),
  };
  const records = readRecords('text 0 0 white "\\x07a"', thin);
  assert.deepEqual(records, [
    { kind: "glyph", code: 97, x: 2, y: 0, color: "white" },
  ]);
});

test("blank lines are skipped and a line of no known kind is an error", () => {
  assert.deepEqual(readRecords("\n  \nclip 0 0 1 1\n\n", FONT).length, 1);
  assert.throws(() => readRecords("circle 1 2 3", FONT), /not a record line/);
  assert.throws(() => readRecords("fill 1 2 3", FONT), /not a record line/);
});
