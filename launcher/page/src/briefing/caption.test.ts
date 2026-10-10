import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { bracketBytes, drawCaption, wrapCaption } from "./caption.ts";
import { toBytes } from "./text.ts";

const FONT = makeBundle().font;
const WORD = "aaaaaaaaa";

function lines(text: string): string[] {
  return wrapCaption(FONT, toBytes(text)).map((l) =>
    String.fromCharCode(...l.bytes),
  );
}

test("a short caption is one line", () => {
  assert.deepEqual(lines("one two"), ["one two"]);
});

test("a line that reaches 360 pixels ends at the last space, space included", () => {
  const text = Array.from({ length: 7 }, () => WORD).join(" ");
  assert.deepEqual(lines(text), [`${WORD} `.repeat(6), WORD]);
});

test("a line of 354 pixels stays whole and one of 360 wraps at the last space", () => {
  assert.deepEqual(lines(`${"x ".repeat(33)}xxxx`), [`${"x ".repeat(33)}xxxx`]);
  assert.deepEqual(lines(`${"x ".repeat(33)}xxxxx y`), [
    "x ".repeat(33),
    "xxxxx y",
  ]);
});

test("a dollar sign ends a line and is not kept", () => {
  assert.deepEqual(lines("ab$cd"), ["ab", "cd"]);
  assert.deepEqual(lines("ab$"), ["ab"]);
  assert.deepEqual(lines("ab$$cd"), ["ab", "", "cd"]);
  assert.deepEqual(lines("$a"), ["", "a"]);
  assert.deepEqual(lines("$"), [""]);
  assert.deepEqual(lines(""), []);
});

test("a word wider than the line ends the line after it", () => {
  assert.deepEqual(lines(`${"W".repeat(70)} x`), ["W".repeat(70), " x"]);
});

test("lines are 14 pixels apart from y 208 and drawn white from x 0", () => {
  const text = Array.from({ length: 7 }, () => WORD).join(" ");
  const glyphs = drawCaption(FONT, toBytes(text));
  assert.equal(glyphs.length, 6 * 10 + 9);
  assert.deepEqual(
    [glyphs[0]?.x, glyphs[0]?.y, glyphs[0]?.color],
    [0, 208, "white"],
  );
  const second = glyphs.find((g) => g.y === 222);
  assert.deepEqual([second?.x, second?.color], [0, "white"]);
});

test("a line starting with > is a yellow heading, centered one pixel lower, without the >", () => {
  const glyphs = drawCaption(FONT, toBytes(">Title"));
  assert.deepEqual(
    glyphs.map((g) => [g.code, g.x, g.y, g.color]),
    "Title"
      .split("")
      .map((c, i) => [c.charCodeAt(0), 165 + 6 * i, 209, "yellow"]),
  );
});

test("the next line after a heading is plain", () => {
  const glyphs = drawCaption(FONT, toBytes(">Hi$there"));
  assert.deepEqual(
    [glyphs[2]?.x, glyphs[2]?.y, glyphs[2]?.color],
    [0, 222, "white"],
  );
});

test("a heading's width includes its trailing spaces", () => {
  const glyphs = drawCaption(FONT, toBytes(">ab  $"));
  assert.equal(glyphs[0]?.x, 170);
});

test("brackets switch to color code 1 and back; they take no room", () => {
  assert.deepEqual(bracketBytes(toBytes("a[b]c")), [97, 2, 98, 1, 99]);
  const glyphs = drawCaption(FONT, toBytes("a[b]c"));
  assert.deepEqual(
    glyphs.map((g) => [g.x, g.color]),
    [
      [0, "white"],
      [6, "code1"],
      [12, "white"],
    ],
  );
});

test("a line that begins inside brackets begins in color code 1", () => {
  const text = `[${Array.from({ length: 7 }, () => WORD).join(" ")}]`;
  const glyphs = drawCaption(FONT, toBytes(text));
  const first = glyphs.find((g) => g.y === 222);
  assert.equal(first?.color, "code1");
  assert.equal(glyphs.at(-1)?.color, "code1");
});

test("a bracket closed on an earlier line does not color the next one", () => {
  const text = `[a] ${Array.from({ length: 7 }, () => WORD).join(" ")}`;
  const glyphs = drawCaption(FONT, toBytes(text));
  const first = glyphs.find((g) => g.y === 222);
  assert.equal(first?.color, "white");
});

test("a heading inside brackets keeps the bracket color after the >", () => {
  const glyphs = drawCaption(FONT, toBytes("[a$>b]"));
  assert.deepEqual(
    glyphs.map((g) => g.color),
    ["code1", "code1"],
  );
});

test("a tab is never the place a line ends: with no space the line ends after the word", () => {
  const text = `${"x".repeat(30)}\t${"y".repeat(30)}`;
  assert.deepEqual(lines(text), [text]);
});
