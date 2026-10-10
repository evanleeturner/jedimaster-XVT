import assert from "node:assert/strict";
import { test } from "node:test";

import {
  colorRgb,
  exactColor,
  fromColor565,
  intensityOf,
  through565,
  tintRgb,
} from "./colors.ts";

const CODES = [2016, 63488, 65504, 12703, 33823];

test("white stays white and black stays black through 565", () => {
  assert.deepEqual(through565([255, 255, 255]), [255, 255, 255]);
  assert.deepEqual(through565([0, 0, 0]), [0, 0, 0]);
});

test("565 keeps 5, 6 and 5 bits and widens by repeating the top bits", () => {
  assert.deepEqual(through565([0x96, 0, 0]), [148, 0, 0]);
  assert.deepEqual(through565([0, 0x48, 0]), [0, 73, 0]);
  assert.deepEqual(through565([0, 0, 0x48]), [0, 0, 74]);
});

test("the grid reds are the two reds through 565", () => {
  assert.deepEqual(colorRgb("major", CODES), [148, 0, 0]);
  assert.deepEqual(colorRgb("minor", CODES), [82, 0, 0]);
});

test("the 40 shades are five rows of eight steps", () => {
  assert.deepEqual(exactColor("shade 0", CODES), [0, 0x48, 0]);
  assert.deepEqual(exactColor("shade 15", CODES), [0xfc, 0, 0]);
  assert.deepEqual(exactColor("shade 1", CODES), [0, 0x60, 0]);
  assert.deepEqual(exactColor("shade 25", CODES), [0, 0, 0x60]);
  assert.deepEqual(exactColor("shade 16", CODES), [0x48, 0x48, 0]);
  assert.deepEqual(exactColor("shade 31", CODES), [0, 0, 0xfc]);
  assert.deepEqual(exactColor("shade 39", CODES), [0xfc, 0, 0xfc]);
  assert.deepEqual(exactColor("shade 32", CODES), [0x48, 0, 0x48]);
});

test("a name that is not a color is black", () => {
  for (const name of ["x0", "shade 40", "shade -1", "", "code9", "grey"]) {
    assert.deepEqual(colorRgb(name, CODES), [0, 0, 0]);
  }
});

test("a text color code is its 565 value widened", () => {
  assert.deepEqual(fromColor565(0x07e0), [0, 255, 0]);
  assert.deepEqual(colorRgb("code1", CODES), [0, 255, 0]);
  assert.deepEqual(colorRgb("code2", CODES), [255, 0, 0]);
  assert.deepEqual(colorRgb("code3", CODES), [255, 255, 0]);
});

test("yellow and white are the page's own colors", () => {
  assert.deepEqual(colorRgb("yellow", CODES), [255, 255, 0]);
  assert.deepEqual(colorRgb("white", CODES), [255, 255, 255]);
});

test("a tint is scaled by the pixel's intensity over 31", () => {
  assert.deepEqual(tintRgb([0, 252, 0], 31), [0, 255, 0]);
  assert.deepEqual(tintRgb([0, 252, 0], 0), [0, 0, 0]);
  assert.deepEqual(tintRgb([0, 252, 0], 15), [0, 121, 0]);
  assert.deepEqual(tintRgb([248, 0, 248], 31), [255, 0, 255]);
});

test("the intensity is the blue field of the pixel in 565", () => {
  assert.equal(intensityOf(255), 31);
  assert.equal(intensityOf(7), 0);
  assert.equal(intensityOf(8), 1);
});
