import assert from "node:assert/strict";
import { test } from "node:test";
import { crc32 } from "node:zlib";

import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import { loadBriefing } from "./core.ts";
import { hex8 } from "./crc.ts";
import type { GlyphRecord } from "./records.ts";
import {
  briefingLine,
  compareText,
  recordLines,
  runSteps,
  stateText,
} from "./sheet.ts";

const FONT = makeBundle().font;
const SPACED = { ...FONT, spacing: 2 };

function glyph(code: number, x: number, y = 5, color = "white"): GlyphRecord {
  return { kind: "glyph", code, x, y, color };
}

test("glyphs drawn one after another join one text line", () => {
  assert.deepEqual(
    recordLines([glyph(97, 0), glyph(98, 6), glyph(99, 12)], FONT),
    ['text 0 5 white "abc"'],
  );
});

test("a glyph joins at the x where the last one ended, spacing included", () => {
  assert.deepEqual(recordLines([glyph(97, 0), glyph(98, 8)], SPACED), [
    'text 0 5 white "ab"',
  ]);
  assert.deepEqual(recordLines([glyph(97, 0), glyph(98, 6)], SPACED), [
    'text 0 5 white "a"',
    'text 6 5 white "b"',
  ]);
});

test("a gap, another row or another color starts a new line", () => {
  assert.deepEqual(recordLines([glyph(97, 0), glyph(98, 7)], FONT), [
    'text 0 5 white "a"',
    'text 7 5 white "b"',
  ]);
  assert.equal(recordLines([glyph(97, 0), glyph(98, 6, 6)], FONT).length, 2);
  assert.equal(
    recordLines([glyph(97, 0), glyph(98, 6, 5, "code1")], FONT).length,
    2,
  );
});

test("text is escaped: quote and backslash with a backslash, other bytes as \\xHH", () => {
  const records = [34, 92, 126, 127, 233, 31].map((code, i) =>
    glyph(code, 6 * i),
  );
  assert.deepEqual(recordLines(records, FONT), [
    'text 0 5 white "\\"\\\\~\\x7f\\xe9\\x1f"',
  ]);
});

test("other records are written in the engine's words", () => {
  const lines = recordLines(
    [
      { kind: "clip", left: 0, top: 1, right: 2, bottom: 3 },
      { kind: "vline", x: -4, top: 0, bottom: 208, color: "minor" },
      { kind: "hline", y: 7, left: 0, right: 360, color: "major" },
      { kind: "icon", sheet: "mapicon1", index: 12, x: 3, y: 4 },
      {
        kind: "tint",
        sheet: "greyicon",
        index: 2,
        x: -1,
        y: 9,
        color: "shade 7",
      },
      { kind: "fill", left: 1, top: 2, right: 3, bottom: 4, color: "shade 2" },
      { kind: "outline", left: 1, top: 2, right: 3, bottom: 4, color: "x0" },
    ],
    FONT,
  );
  assert.deepEqual(lines, [
    "clip 0 1 2 3",
    "vline -4 0 208 minor",
    "hline 7 0 360 major",
    "icon mapicon1 12 3 4",
    "tint greyicon 2 -1 9 shade 7",
    "fill 1 2 3 4 shade 2",
    "outline 1 2 3 4 x0",
  ]);
});

const BUNDLE = makeBundle({
  briefings: [
    briefing({
      events: [
        [0, 5, 1],
        [3, 9, 0],
      ],
      captions: ["", "Hello"],
    }),
  ],
});

function frameLines(lines: string[], frame: number): string[] {
  const start = lines.findIndex((l) => l.startsWith(`draw ${String(frame)} `));
  const rest = lines.slice(start + 1);
  const end = rest.findIndex((l) => !l.startsWith("  "));
  return rest.slice(0, end < 0 ? rest.length : end);
}

test("a run begins at state 0 and ends with the unknown glyph count", () => {
  const lines = runSteps(BUNDLE, 0, "run 2");
  assert.match(
    lines[0] ?? "",
    /^state 0 play=1 t=1 c=0,0 tc=0,0 s=32,32 ts=32,32 page=0 last=0 slots=0:0,1:1 markers=- labels=-$/,
  );
  assert.equal(lines.at(-1), "unknown_glyphs 0");
});

test("the draw line carries the CRC-32 of the records, each with a newline, and their count", () => {
  const lines = runSteps(BUNDLE, 0, "run 1");
  const records = frameLines(lines, 1).map((l) => l.slice(2));
  const body = records.map((l) => `${l}\n`).join("");
  const draw = lines.find((l) => l.startsWith("draw 1 "));
  assert.equal(
    draw,
    `draw 1 crc=${hex8(crc32(Buffer.from(body, "latin1")))} records=${String(records.length)}`,
  );
});

test("sounds print before their frame's state; a frame is listed when its state changes", () => {
  const bundle = makeBundle({
    briefings: [briefing({ events: [[2, 9, 0]] })],
    groups: [group()],
  });
  const lines = runSteps(bundle, 0, "run 3");
  const at = (word: string): number =>
    lines.findIndex((l) => l.startsWith(word));
  assert.equal(at("sound 2 sfxTarget1") + 1, at("state 2 "));
});

test("frame 1 and every 24th frame are listed; the others only when they change", () => {
  const lines = runSteps(BUNDLE, 0, "run 49");
  const listed = (n: number): boolean => frameLines(lines, n).length > 0;
  assert.deepEqual([1, 2, 3, 4, 23, 24, 25, 47, 48, 49].map(listed), [
    true,
    false,
    true,
    false,
    false,
    true,
    false,
    false,
    true,
    false,
  ]);
});

test("frame 1 is listed even when nothing changed", () => {
  const lines = runSteps(makeBundle(), 0, "run 3");
  assert.deepEqual(
    [1, 2, 3].map((n) => frameLines(lines, n).length > 0),
    [true, false, false],
  );
});

test("listall lists every later frame", () => {
  const lines = runSteps(BUNDLE, 0, "run 1 listall run 3");
  assert.deepEqual(
    [2, 3, 4].map((n) => frameLines(lines, n).length > 0),
    [true, true, true],
  );
});

test("the first frame after a press is listed", () => {
  const lines = runSteps(BUNDLE, 0, "run 5 play run 2");
  assert.equal(frameLines(lines, 6).length > 0, true);
  assert.equal(frameLines(lines, 7).length > 0, false);
});

test("a press prints its line, then the state; a press not offered says ignored", () => {
  const lines = runSteps(BUNDLE, 0, "run 2 forward play stop stop");
  const presses = lines.filter((l) => l.startsWith("press"));
  assert.deepEqual(presses, [
    "press 2 forward",
    "press 2 play ignored",
    "press 2 stop",
    "press 2 stop ignored",
  ]);
  const at = lines.findIndex((l) => l === "press 2 stop");
  assert.match(lines[at + 1] ?? "", /^state 2 play=0 /);
});

test("a press before any frame is press 0", () => {
  assert.equal(runSteps(BUNDLE, 0, "rewind").includes("press 0 rewind"), true);
});

test("a recipe with a word it does not know, or a bad count, is refused", () => {
  assert.throws(() => runSteps(BUNDLE, 0, "run 1 jump"), /unknown step/);
  assert.throws(() => runSteps(BUNDLE, 0, "run x"), /count/);
  assert.throws(() => runSteps(BUNDLE, 0, "run -2"), /count/);
});

test("the state text lists markers SLOT:GROUP:AGE and labels SLOT:TEXT:X:Y:ROW:AGE", () => {
  const bundle = makeBundle({
    briefings: [
      briefing({
        events: [
          [0, 10, 2],
          [0, 12, 3],
          [0, 19, 4, -5, 6, 1],
        ],
      }),
    ],
  });
  const text = stateText(loadBriefing(bundle, 0));
  assert.match(text, /markers=1:2:80;3:3:80 labels=1:4:-5:6:1:80$/);
});

test("the compare text leaves out the center, the zoom and the ages", () => {
  const bundle = makeBundle({
    briefings: [briefing({ events: [[0, 10, 2]] })],
  });
  const player = loadBriefing(bundle, 0);
  const before = compareText(player);
  player.cx = 99;
  player.sx = 7;
  player.markers[1] = { on: true, group: 2, age: 5 };
  assert.equal(compareText(player), before);
  player.tcx = 1;
  assert.notEqual(compareText(player), before);
});

test("the briefing line names the briefing and its running time; the default is briefing 0, 200 frames", () => {
  const bundle = makeBundle({
    briefings: [briefing({ index: 3, runningTime: 321 })],
    teamBriefings: { 2: 3 },
  });
  assert.equal(briefingLine(bundle, 2), "briefing 3 frames 321");
  assert.equal(briefingLine(bundle, 5), "briefing 0 frames 200");
});
