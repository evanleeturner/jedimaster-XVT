import assert from "node:assert/strict";
import { test } from "node:test";

import { layoutFor } from "./layout.ts";

test("whole pixels: the largest whole multiple that fits, sharp", () => {
  assert.deepEqual(layoutFor("whole_pixels", 900, 600), {
    factor: 2,
    width: 720,
    height: 480,
    smooth: false,
  });
  assert.equal(layoutFor("whole_pixels", 1080, 720).factor, 3);
  assert.equal(layoutFor("whole_pixels", 1079, 720).factor, 2);
  assert.equal(layoutFor("whole_pixels", 2000, 479).factor, 1);
});

test("whole pixels: never smaller than 1 when the room is smaller", () => {
  assert.deepEqual(layoutFor("whole_pixels", 200, 100), {
    factor: 1,
    width: 360,
    height: 240,
    smooth: false,
  });
});

test("engine fit: the largest size of the same shape that fits, smoothed", () => {
  assert.deepEqual(layoutFor("engine_fit", 900, 600), {
    factor: 1,
    width: 900,
    height: 600,
    smooth: true,
  });
  assert.deepEqual(layoutFor("engine_fit", 900, 300), {
    factor: 1,
    width: 450,
    height: 300,
    smooth: true,
  });
});

test("sharp bilinear: whole-step drawing, fitted size, smoothed a little", () => {
  assert.deepEqual(layoutFor("sharp_bilinear", 900, 600), {
    factor: 2,
    width: 900,
    height: 600,
    smooth: true,
  });
  assert.equal(layoutFor("sharp_bilinear", 300, 200).factor, 1);
});

test("the shape is always 3 to 2", () => {
  for (const setting of [
    "whole_pixels",
    "engine_fit",
    "sharp_bilinear",
  ] as const) {
    const made = layoutFor(setting, 777, 555);
    assert.equal(made.width * 2, made.height * 3);
  }
});
