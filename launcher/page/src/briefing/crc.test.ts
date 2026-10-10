import assert from "node:assert/strict";
import { test } from "node:test";
import { crc32 as zlibCrc32 } from "node:zlib";

import { crc32, crc32OfText, hex8 } from "./crc.ts";

test("the CRC-32 of the standard check string", () => {
  assert.equal(crc32OfText("123456789"), 0xcbf43926);
});

test("the CRC-32 of nothing is 0", () => {
  assert.equal(crc32(new Uint8Array(0)), 0);
});

test("the CRC-32 agrees with zlib on bytes with the high bit set", () => {
  const bytes = Uint8Array.from({ length: 300 }, (_, i) => (i * 37 + 11) % 256);
  assert.equal(crc32(bytes), zlibCrc32(bytes));
});

test("hex8 is 8 lowercase digits", () => {
  assert.equal(hex8(0xab), "000000ab");
  assert.equal(hex8(0xffffffff), "ffffffff");
});
