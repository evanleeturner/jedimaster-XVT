import assert from "node:assert/strict";
import { test } from "node:test";

import { frameLayout } from "./layout.ts";

test("whole pixels: the largest whole multiple, sharp", () => {
  assert.deepEqual(frameLayout("whole_pixels", 640, 480), {
    factor: 1,
    width: 640,
    height: 480,
    smooth: false,
    scale: 1,
  });
  assert.deepEqual(frameLayout("whole_pixels", 1280, 960), {
    factor: 2,
    width: 1280,
    height: 960,
    smooth: false,
    scale: 2,
  });
  assert.equal(frameLayout("whole_pixels", 1919, 1440)?.factor, 2);
  assert.equal(frameLayout("whole_pixels", 1920, 1440)?.factor, 3);
  assert.equal(frameLayout("whole_pixels", 5000, 1000)?.factor, 2);
});

test("engine fit: the largest size that fits, smoothed", () => {
  assert.deepEqual(frameLayout("engine_fit", 640, 480), {
    factor: 1,
    width: 640,
    height: 480,
    smooth: true,
    scale: 1,
  });
  assert.deepEqual(frameLayout("engine_fit", 1280, 960), {
    factor: 1,
    width: 1280,
    height: 960,
    smooth: true,
    scale: 2,
  });
  const wide = frameLayout("engine_fit", 1920, 1440);
  assert.ok(wide);
  assert.equal(wide.width, 1920);
  assert.equal(wide.height, 1440);
  assert.equal(wide.scale, 3);
  const odd = frameLayout("engine_fit", 1000, 1000);
  assert.ok(odd);
  assert.equal(odd.width, 1000);
  assert.equal(odd.height, 750);
});

test("sharp bilinear: whole multiple drawn, fitted size shown, smoothed", () => {
  assert.deepEqual(frameLayout("sharp_bilinear", 640, 480), {
    factor: 1,
    width: 640,
    height: 480,
    smooth: true,
    scale: 1,
  });
  assert.deepEqual(frameLayout("sharp_bilinear", 1280, 960), {
    factor: 2,
    width: 1280,
    height: 960,
    smooth: true,
    scale: 2,
  });
  const odd = frameLayout("sharp_bilinear", 1000, 1000);
  assert.ok(odd);
  assert.equal(odd.factor, 1);
  assert.equal(odd.width, 1000);
  assert.equal(odd.height, 750);
});

test("no layout below 640 x 480", () => {
  for (const setting of [
    "whole_pixels",
    "engine_fit",
    "sharp_bilinear",
  ] as const) {
    assert.equal(frameLayout(setting, 639, 480), null);
    assert.equal(frameLayout(setting, 640, 479), null);
    assert.equal(frameLayout(setting, 320, 700), null);
  }
});
