import assert from "node:assert/strict";
import { test } from "node:test";

import { FrameClock, MAX_CATCH_UP } from "./clock.ts";
import { FRAME_MS } from "./core.ts";

test("a frame is 41 ms", () => {
  assert.equal(FRAME_MS, 41);
});

test("the first call returns 0 and starts the clock", () => {
  const clock = new FrameClock();
  assert.equal(clock.advance(1000), 0);
  assert.equal(clock.advance(1041), 1);
});

test("time under a frame is kept for the next call", () => {
  const clock = new FrameClock();
  clock.advance(0);
  assert.equal(clock.advance(30), 0);
  assert.equal(clock.advance(60), 1);
  assert.equal(clock.advance(82), 1);
  assert.equal(clock.advance(100), 0);
});

test("a slow tab runs at most four frames at once and drops the rest", () => {
  const clock = new FrameClock();
  clock.advance(0);
  assert.equal(MAX_CATCH_UP, 4);
  assert.equal(clock.advance(41 * 4), 4);
  assert.equal(clock.advance(41 * 4 + 41 * 50 + 20), 4);
  assert.equal(clock.advance(41 * 4 + 41 * 50 + 50), 0);
});

test("reset forgets the time: a hidden tab does not catch up", () => {
  const clock = new FrameClock();
  clock.advance(0);
  clock.advance(100);
  clock.reset();
  assert.equal(clock.advance(60_000), 0);
  assert.equal(clock.advance(60_041), 1);
});

test("a clock that runs backwards owes nothing", () => {
  const clock = new FrameClock();
  clock.advance(1000);
  assert.equal(clock.advance(900), 0);
});
