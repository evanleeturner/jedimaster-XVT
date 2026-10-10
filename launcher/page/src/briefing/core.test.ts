import assert from "node:assert/strict";
import { test } from "node:test";

import type { Ev } from "./bundle.fixture.ts";
import { briefing, group, makeBundle } from "./bundle.fixture.ts";
import type { Button, Player } from "./core.ts";
import {
  cut16,
  defaultScript,
  loadBriefing,
  offered,
  press,
  scriptFor,
  updateFrame,
} from "./core.ts";
import { drawFrame } from "./draw.ts";

function playerOf(
  list: readonly Ev[],
  more: Record<string, unknown> = {},
): Player {
  const options = { events: list, ...more };
  return loadBriefing(
    makeBundle({
      briefings: [briefing(options)],
      groups: [group(), group({ iff: 1 })],
    }),
    0,
  );
}

function frame(player: Player): string[] {
  const sounds = updateFrame(player);
  drawFrame(player);
  return sounds;
}

function frames(player: Player, count: number): void {
  for (let i = 0; i < count; i += 1) frame(player);
}

test("cut16 wraps to 16 bits like the game", () => {
  assert.equal(cut16(32768), -32768);
  assert.equal(cut16(-32769), 32767);
  assert.equal(cut16(70000), 4464);
  assert.equal(cut16(-5), -5);
});

test("a briefing loads playing with step 0 already played", () => {
  const player = playerOf([
    [0, 6, 40, -50],
    [0, 7, 64, 48],
    [0, 5, 3],
  ]);
  assert.equal(player.playing, true);
  assert.equal(player.t, 1);
  assert.deepEqual(
    [player.cx, player.cy, player.tcx, player.tcy],
    [40, -50, 40, -50],
  );
  assert.deepEqual(
    [player.sx, player.sy, player.tsx, player.tsy],
    [64, 48, 64, 48],
  );
  assert.deepEqual(player.slots[1], { on: true, block: 3 });
  assert.equal(player.page, 0);
});

test("with no events the view starts at the origin and zoom 32", () => {
  const player = playerOf([]);
  assert.deepEqual(
    [player.cx, player.cy, player.sx, player.sy],
    [0, 0, 32, 32],
  );
  assert.deepEqual(player.slots, [
    { on: false, block: 0 },
    { on: false, block: 0 },
  ]);
});

test("each frame plays one step: events at that time apply", () => {
  const player = playerOf([
    [2, 9, 1],
    [4, 18, 0, 5, 6, 2],
  ]);
  frames(player, 1);
  assert.equal(player.markers[0]?.on, false);
  frames(player, 1);
  assert.deepEqual(player.markers[0], { on: true, group: 1, age: 0 });
  frames(player, 2);
  assert.deepEqual(player.markers[0], { on: true, group: 1, age: 2 });
  assert.deepEqual(player.labels[0], {
    on: true,
    text: 0,
    x: 5,
    y: 6,
    row: 2,
    age: 0,
  });
  assert.equal(player.t, 5);
});

test("an event timed before its step is skipped and does nothing", () => {
  const player = playerOf([
    [5, 9, 0],
    [2, 10, 1],
  ]);
  frames(player, 8);
  assert.equal(player.markers[0]?.on, true);
  assert.equal(player.markers[1]?.on, false);
});

test("sounds: a marker on an IFF 1 group plays target 2, others target 1, a label with text plays text", () => {
  const player = playerOf(
    [
      [1, 9, 0],
      [2, 10, 1],
      [3, 18, 0, 0, 0, 0],
      [4, 19, 1, 0, 0, 0],
    ],
    {
      labels: ["Hi", ""],
    },
  );
  assert.deepEqual(frame(player), ["sfxTarget1"]);
  assert.deepEqual(frame(player), ["sfxTarget2"]);
  assert.deepEqual(frame(player), ["sfxText"]);
  assert.deepEqual(frame(player), []);
});

test("sounds: a marker on a group that does not exist plays target 1", () => {
  const player = playerOf([[1, 9, 40]]);
  assert.deepEqual(frame(player), ["sfxTarget1"]);
});

test("nothing updates or sounds while stopped, and play resumes", () => {
  const player = playerOf([[2, 9, 0]]);
  assert.deepEqual(press(player, "stop"), []);
  assert.equal(offered(player, "play"), true);
  assert.equal(offered(player, "stop"), false);
  assert.equal(offered(player, "forward"), false);
  frames(player, 5);
  assert.equal(player.t, 1);
  assert.equal(press(player, "stop"), null);
  assert.equal(press(player, "forward"), null);
  assert.deepEqual(press(player, "play"), []);
  assert.equal(press(player, "play"), null);
  frames(player, 2);
  assert.equal(player.t, 3);
});

test("rewind is always offered, keeps playing or stopped, and zeroes the page", () => {
  const player = playerOf([[0, 5, 2]], { captions: ["", "", "A caption"] });
  frames(player, 6);
  assert.equal(player.page, 1);
  press(player, "stop");
  assert.deepEqual(press(player, "rewind"), []);
  assert.deepEqual(
    [player.t, player.page, player.last, player.playing],
    [1, 0, 0, false],
  );
  assert.equal(offered(player, "rewind"), true);
  press(player, "play");
  assert.deepEqual(press(player, "rewind"), []);
  assert.equal(player.playing, true);
});

test("the briefing starts over at the running time and zeroes the page counters", () => {
  const player = playerOf(
    [
      [0, 5, 2],
      [1, 9, 0],
    ],
    { runningTime: 4, captions: ["", "", "Words"] },
  );
  frames(player, 3);
  assert.equal(player.t, 4);
  assert.equal(player.markers[0]?.on, true);
  assert.equal(player.page, 1);
  updateFrame(player);
  assert.equal(player.t, 1);
  assert.equal(
    player.markers.some((m) => m.on),
    false,
  );
  assert.equal(player.page, 0);
  assert.equal(player.last, 0);
  drawFrame(player);
  assert.deepEqual([player.page, player.last], [1, 2]);
});

test("the running time can be 0 or 1: every frame starts over", () => {
  for (const runningTime of [0, 1]) {
    const player = playerOf([[0, 9, 0]], { runningTime });
    frames(player, 3);
    assert.equal(player.t, 1);
  }
});

test("a step played at once starts markers and labels at age 80 and moves the view", () => {
  const player = playerOf([
    [0, 9, 0],
    [0, 18, 0, 1, 2, 3],
    [0, 6, 7, 8],
  ]);
  assert.equal(player.markers[0]?.age, 80);
  assert.equal(player.labels[0]?.age, 80);
  assert.deepEqual([player.cx, player.cy], [7, 8]);
});

test("a target event after time 0 moves only the target; the view glides", () => {
  const player = playerOf([
    [2, 6, 100, 0],
    [2, 7, 40, 32],
  ]);
  frames(player, 2);
  assert.deepEqual([player.tcx, player.tsx], [100, 40]);
  assert.deepEqual([player.cx, player.sx], [0, 32]);
  frame(player);
  assert.equal(player.sx, 34);
  assert.equal(player.cx, 16);
});

test("the zoom glides 8 when 12 or more apart, 2 nearer, 1 when under 10", () => {
  const far = playerOf([[2, 7, 100, 100]]);
  frames(far, 3);
  assert.equal(far.sx, 40);
  const near = playerOf([[2, 7, 40, 40]]);
  frames(near, 3);
  assert.equal(near.sx, 34);
  const small = playerOf([
    [0, 7, 5, 5],
    [2, 7, 9, 5],
  ]);
  frames(small, 3);
  assert.deepEqual([small.sx, small.sy], [6, 5]);
});

test("the center glides 2 steps of 256 / zoom + 1 near the target and 4 far", () => {
  const near = playerOf([[2, 6, 100, 0]]);
  frames(near, 3);
  assert.equal(near.cx, 18);
  const far = playerOf([[2, 6, 1000, 0]]);
  frames(far, 3);
  assert.equal(far.cx, 36);
  const nearer = playerOf([
    [0, 7, 64, 64],
    [2, 6, 20, 0],
  ]);
  frames(nearer, 3);
  assert.equal(nearer.cx, 10);
});

test("the center stops at its target and never passes it", () => {
  const player = playerOf([[2, 6, 30, -3]]);
  frames(player, 30);
  assert.deepEqual([player.cx, player.cy], [30, -3]);
});

test("gaps are cut to 16 bits: a far target on one axis does not hide a near one", () => {
  const player = playerOf([
    [0, 6, 32000, 32000],
    [1, 6, 32767, -32768],
  ]);
  frames(player, 20);
  const before = player.cx;
  frame(player);
  assert.equal(player.cx - before, 18);
});

test("a move that wraps past 16 bits is not stopped at the target", () => {
  const player = playerOf([
    [0, 6, 32760, 0],
    [1, 6, 32767, 0],
  ]);
  frames(player, 12);
  assert.equal(player.cx < 0, true);
});

test("a caption block counts a page once, while it is drawn", () => {
  const player = playerOf(
    [
      [0, 5, 1],
      [3, 5, 2],
      [6, 5, 1],
    ],
    { captions: ["", "one", "two"] },
  );
  const seen: number[] = [];
  for (let i = 0; i < 9; i += 1) {
    frame(player);
    seen.push(player.page);
  }
  assert.deepEqual(seen, [1, 1, 2, 2, 2, 3, 3, 3, 3]);
});

test("a first caption that is block 0 is not counted", () => {
  const player = playerOf([[0, 5, 0]], { captions: ["zero"] });
  frames(player, 3);
  assert.equal(player.page, 0);
});

test("type 3 turns both slots off and keeps their blocks; type 4 sets slot 0", () => {
  const player = playerOf([
    [0, 4, 7],
    [0, 5, 9],
    [2, 3],
  ]);
  assert.deepEqual(player.slots[0], { on: true, block: 7 });
  frames(player, 2);
  assert.deepEqual(player.slots, [
    { on: false, block: 7 },
    { on: false, block: 9 },
  ]);
});

test("types 8 and 17 turn all markers and all labels off", () => {
  const player = playerOf([
    [0, 9, 0],
    [0, 12, 1],
    [0, 18, 0, 0, 0, 0],
    [2, 8],
    [3, 17],
  ]);
  frames(player, 2);
  assert.equal(
    player.markers.some((m) => m.on),
    false,
  );
  assert.equal(player.labels[0]?.on, true);
  frame(player);
  assert.equal(
    player.labels.some((l) => l.on),
    false,
  );
});

test("unknown and no-effect types change nothing", () => {
  const player = playerOf([
    [0, 5, 1],
    [1, 0],
    [1, 2, 5],
    [1, 26],
    [1, 30],
  ]);
  const before = JSON.stringify([player.slots, player.markers, player.labels]);
  frames(player, 3);
  assert.equal(
    JSON.stringify([player.slots, player.markers, player.labels]),
    before,
  );
});

test("the default briefing is 200 frames long, empty, and for the first point", () => {
  const script = defaultScript();
  assert.equal(script.runningTime, 200);
  assert.deepEqual(
    script.events.map((e) => e.type),
    [34],
  );
  assert.equal(
    script.captions.every((c) => c.length === 0),
    true,
  );
  const bundle = makeBundle({ teamBriefings: { 1: 0 } });
  assert.equal(scriptFor(bundle, 4).runningTime, 200);
  assert.equal(scriptFor(bundle, 1).index, 0);
  assert.equal(scriptFor(bundle, 4).index, 0);
});

function forwards(list: readonly Ev[], steps: number): number[] {
  const player = playerOf(list, { captions: ["", "x"] });
  frame(player);
  press(player, "rewind");
  const out: number[] = [];
  for (let i = 0; i < steps; i += 1) {
    press(player, "forward");
    out.push(player.t);
  }
  return out;
}

test("forward: one caption and nothing else steps one step at a time, then holds (engine)", () => {
  assert.deepEqual(forwards([[0, 5, 1]], 4), [2, 3, 3, 3]);
});

test("forward: a caption that appears later is reached, then held (engine)", () => {
  assert.deepEqual(forwards([[2, 5, 1]], 4), [4, 5, 5, 5]);
});

test("forward: no caption on a slot-0 text starts over every time (engine)", () => {
  assert.deepEqual(forwards([[0, 4, 0]], 4), [1, 1, 1, 1]);
});

test("forward: a stop point is a place to stop, then the script runs out and starts over (engine)", () => {
  assert.deepEqual(
    forwards(
      [
        [0, 5, 1],
        [3, 1],
      ],
      4,
    ),
    [2, 5, 1, 2],
  );
});

test("forward plays its last step with sounds and ignores the running time", () => {
  const player = playerOf(
    [
      [0, 5, 1],
      [70, 9, 0],
      [80, 1],
      [81, 9, 0],
    ],
    { runningTime: 60, captions: ["", "x"] },
  );
  frame(player);
  press(player, "rewind");
  press(player, "forward");
  assert.equal(player.t, 2);
  const made = press(player, "forward");
  assert.deepEqual(made, ["sfxTarget1"]);
  assert.equal(player.t, 82);
});

test("a forward onto a caption already counted starts over", () => {
  const player = playerOf([[0, 5, 1]], { captions: ["", "x"] });
  frame(player);
  assert.equal(player.page, 1);
  press(player, "forward");
  assert.deepEqual([player.t, player.page, player.last], [1, 0, 0]);
});

test("every button name is one of the four", () => {
  const names: Button[] = ["play", "stop", "rewind", "forward"];
  const player = playerOf([]);
  assert.deepEqual(
    names.map((b) => offered(player, b)),
    [false, true, true, true],
  );
});

test("the zoom glides 8 at a gap of exactly 12 and 2 at a gap of 10 from a zoom of exactly 10", () => {
  const twelve = playerOf([[2, 7, 44, 44]]);
  frames(twelve, 3);
  assert.equal(twelve.sx, 40);
  const ten = playerOf([
    [0, 7, 10, 10],
    [2, 7, 20, 20],
  ]);
  frames(ten, 3);
  assert.equal(ten.sx, 12);
});

test("the center takes the long step at exactly 16 units of gap per unit", () => {
  const player = playerOf([[2, 6, 144, 0]]);
  frames(player, 3);
  assert.equal(player.cx, 36);
});

test("moving down past the 16-bit edge wraps and is not stopped at the target", () => {
  const player = playerOf([
    [0, 6, 0, -32760],
    [1, 6, 0, -32768],
  ]);
  frames(player, 2);
  assert.equal(player.cy, 32758);
});

test("starting over turns the labels off", () => {
  const player = playerOf([[3, 18, 0, 1, 1, 0]], { labels: ["x"] });
  frames(player, 4);
  assert.equal(player.labels[0]?.on, true);
  press(player, "rewind");
  assert.equal(
    player.labels.some((l) => l.on),
    false,
  );
});

function seen(list: readonly Ev[], steps: number): number[] {
  const player = playerOf(list, { captions: ["", "one", "two"] });
  frame(player);
  const out: number[] = [];
  for (let i = 0; i < steps; i += 1) {
    press(player, "forward");
    out.push(player.t);
  }
  return out;
}

test("forward walks caption changes and starts over at the end (engine)", () => {
  const list: Ev[] = [
    [0, 5, 1],
    [4, 3],
    [6, 5, 2],
    [9, 1],
  ];
  assert.deepEqual(seen(list, 7), [8, 11, 1, 2, 8, 11, 1]);
});

test("forward counts a text on slot 0 as text, and stops there (engine)", () => {
  const list: Ev[] = [
    [0, 4, 1],
    [3, 3],
    [5, 5, 2],
    [8, 3],
    [10, 5, 1],
  ];
  assert.deepEqual(seen(list, 7), [7, 12, 13, 13, 13, 13, 13]);
});
