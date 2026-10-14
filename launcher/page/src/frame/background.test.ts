import assert from "node:assert/strict";
import { test } from "node:test";

import { frameArt, frameBackground, teamKind } from "./background.ts";

const TABLE: readonly (readonly [string | null, string, string, string])[] = [
  // kind, rebel, imperial, neutral
  ["training", "gametr", "gameti", "gametr"],
  ["combat", "gamecr", "gameci", "gamecn"],
  ["battle", "gamebr", "gamebi", "gamebn"],
  ["campaign", "gamecar", "gamecai", "gamecar"],
  ["melee", "gamemn", "gamemn", "gamemn"],
  ["tournament", "gametrn", "gametrn", "gametrn"],
  ["other", "gametrn", "gametrn", "gametrn"],
  [null, "gametrn", "gametrn", "gametrn"],
];

for (const [kind, rebel, imperial, neutral] of TABLE) {
  test(`background of ${String(kind)}`, () => {
    assert.equal(frameBackground(kind, "rebel"), rebel);
    assert.equal(frameBackground(kind, "imperial"), imperial);
    assert.equal(frameBackground(kind, "neutral"), neutral);
  });
}

test("the side comes from the under-map art name, else neutral", () => {
  assert.equal(teamKind("under-rebel.png"), "rebel");
  assert.equal(teamKind("under-imperial.png"), "imperial");
  assert.equal(teamKind("under-neutral.png"), "neutral");
  assert.equal(teamKind("under-green.png"), "neutral");
  assert.equal(teamKind("nonsense"), "neutral");
  assert.equal(teamKind(null), "neutral");
});

test("the frame art name", () => {
  assert.equal(frameArt("gametr"), "frame-gametr.png");
});
