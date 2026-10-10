import assert from "node:assert/strict";
import { test } from "node:test";

import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import { loadBriefing, updateFrame } from "./core.ts";
import { drawFrame } from "./draw.ts";
import { settled } from "./reduced.ts";
import { recordLines } from "./sheet.ts";

function player() {
  const bundle = makeBundle({
    briefings: [
      briefing({
        events: [
          [1, 6, 300, 0],
          [1, 7, 64, 64],
          [1, 9, 0],
          [1, 18, 0, 5, 5, 1],
        ],
        labels: ["abcdef"],
        captions: ["", "x"],
      }),
    ],
    groups: [group()],
  });
  const made = loadBriefing(bundle, 0);
  updateFrame(made);
  return made;
}

test("a settled player is at its targets, labels whole, markers as their final box", () => {
  const made = player();
  assert.deepEqual([made.cx, made.sx], [0, 32]);
  const still = settled(made);
  assert.deepEqual([still.cx, still.cy, still.sx, still.sy], [300, 0, 64, 64]);
  assert.equal(still.markers[0]?.age, 1000);
  assert.equal(still.labels[0]?.age, 1000);
});

test("settling changes nothing in the real player", () => {
  const made = player();
  const before = JSON.stringify(made);
  settled(made);
  assert.equal(JSON.stringify(made), before);
});

test("the settled map shows the whole label and a grown box", () => {
  const made = player();
  const lines = recordLines(drawFrame(settled(made)), made.bundle.font);
  assert.equal(
    lines.some((l) => l.includes('shade 12 "abcdef"')),
    true,
  );
  assert.equal(
    lines.some((l) => l.startsWith("outline")),
    true,
  );
  assert.equal(
    lines.some((l) => l.startsWith("tint")),
    false,
  );
});
