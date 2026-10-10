import assert from "node:assert/strict";
import { test } from "node:test";

import {
  captionText,
  currentCaption,
  highlightedNames,
  labelTexts,
} from "./announce.ts";
import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import { loadBriefing, updateFrame } from "./core.ts";
import { toBytes } from "./text.ts";

function playerWith(
  events: readonly (readonly [number, number, ...number[]])[],
  captions: string[] = [],
  labels: string[] = [],
) {
  const bundle = makeBundle({
    briefings: [briefing({ events, captions, labels })],
    groups: [
      group({ name: "Red One" }),
      group({ name: "Red One" }),
      group({ name: "Blue" }),
      group({ name: "" }),
    ],
  });
  return loadBriefing(bundle, 0);
}

test("a caption is spoken without dollar signs and brackets", () => {
  assert.equal(
    captionText(toBytes("One [two] three$Four")),
    "One two three Four",
  );
});

test("a heading is spoken without its >", () => {
  assert.equal(captionText(toBytes(">Title$Body text")), "Title Body text");
});

test("empty lines and edge spaces are dropped", () => {
  assert.equal(captionText(toBytes("  a  $$ b ")), "a b");
  assert.equal(captionText([]), "");
});

test("the caption now shown is slot 1's block; slot 0 is not spoken", () => {
  const player = playerWith(
    [
      [0, 4, 1],
      [0, 5, 2],
    ],
    ["", "zero slot", "one slot"],
  );
  assert.equal(currentCaption(player), "one slot");
  const only0 = playerWith([[0, 4, 1]], ["", "zero slot"]);
  assert.equal(currentCaption(only0), "");
});

test("labels are the whole texts of the labels that are on, in slot order, none empty", () => {
  const player = playerWith(
    [
      [0, 18, 1, 0, 0, 0],
      [0, 20, 0, 0, 0, 0],
      [0, 21, 2, 0, 0, 0],
    ],
    [],
    ["Alpha [one]", "Beta", ""],
  );
  assert.deepEqual(labelTexts(player), ["Beta", "Alpha one"]);
});

test("a label is spoken whole even when it has only begun to type", () => {
  const player = playerWith([[1, 18, 0, 0, 0, 0]], [], ["Whole words"]);
  assert.deepEqual(labelTexts(player), []);
  updateFrame(player);
  assert.deepEqual(labelTexts(player), ["Whole words"]);
});

test("highlighted groups are named once each, in marker order, without empty names", () => {
  const player = playerWith([
    [0, 9, 0],
    [0, 10, 1],
    [0, 11, 2],
    [0, 12, 3],
    [0, 13, 40],
  ]);
  assert.deepEqual(highlightedNames(player), ["Red One", "Blue"]);
});
