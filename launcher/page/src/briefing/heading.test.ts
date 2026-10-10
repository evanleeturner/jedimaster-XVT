import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./bundle.fixture.ts";
import { headingBytes, headingGlyphs } from "./heading.ts";

test("the heading is the string, a space, the yellow color byte and the name", () => {
  const bundle = makeBundle({ heading: "Ab" });
  assert.deepEqual(headingBytes(bundle, 0), [
    65,
    98,
    32,
    4,
    ...Array.from("Team 0", (c) => c.charCodeAt(0)),
  ]);
});

test("a team the bundle does not list has an empty name", () => {
  const bundle = makeBundle({ heading: "A" });
  assert.deepEqual(headingBytes(bundle, 12), [65, 32, 4]);
});

test("text characters are taken as bytes", () => {
  assert.deepEqual(
    headingBytes(makeBundle({ heading: "é" }), 12),
    [233, 32, 4],
  );
});

test("the heading is white and the name yellow, from panel (2, -4)", () => {
  const bundle = makeBundle({ heading: "AB" });
  const glyphs = headingGlyphs(bundle, 1);
  const widths = bundle.heading_font?.widths ?? [];
  assert.deepEqual(
    glyphs.slice(0, 2).map((g) => [g.code, g.x, g.y, g.color]),
    [
      [65, 2, -4, "white"],
      [66, 2 + (widths[65] ?? 0) + 1, -4, "white"],
    ],
  );
  const name = glyphs.slice(3);
  assert.equal(name.map((g) => String.fromCharCode(g.code)).join(""), "Team 1");
  assert.ok(name.every((g) => g.color === "code3" && g.y === -4));
});

test("the color byte draws nothing and the name starts after the space", () => {
  const bundle = makeBundle({ heading: "A" });
  const glyphs = headingGlyphs(bundle, 0);
  const font = bundle.heading_font;
  const [first, space, name] = glyphs;
  assert.ok(font !== null && first !== undefined && space !== undefined);
  assert.equal(space.code, 32);
  assert.equal(space.color, "white");
  assert.equal(name?.x, space.x + (font.widths[32] ?? 0) + 1);
  assert.equal(glyphs.length, 2 + "Team 0".length);
});

test("a bundle with no heading font has no heading", () => {
  assert.deepEqual(headingGlyphs(makeBundle({ headingFont: false }), 0), []);
});
