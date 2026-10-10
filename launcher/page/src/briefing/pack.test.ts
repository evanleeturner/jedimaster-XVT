import assert from "node:assert/strict";
import { test } from "node:test";

import { pack565, packPanel } from "./pack.ts";

test("a color packs to red 5 bits, green 6 bits, blue 5 bits", () => {
  assert.equal(pack565(255, 255, 255), 0xffff);
  assert.equal(pack565(0, 0, 0), 0);
  assert.equal(pack565(255, 0, 0), 0xf800);
  assert.equal(pack565(0, 255, 0), 0x07e0);
  assert.equal(pack565(0, 0, 255), 0x001f);
  assert.equal(pack565(8, 4, 8), (1 << 11) | (1 << 5) | 1);
  assert.equal(pack565(7, 3, 7), 0);
});

test("a panel packs two bytes a pixel, low byte first, ignoring alpha", () => {
  const packed = packPanel([255, 0, 0, 7, 0, 255, 0, 255, 1, 2, 3, 0]);
  assert.deepEqual([...packed], [0x00, 0xf8, 0xe0, 0x07, 0x00, 0x00]);
});

test("an image of no pixels packs to nothing", () => {
  assert.equal(packPanel([]).length, 0);
});
