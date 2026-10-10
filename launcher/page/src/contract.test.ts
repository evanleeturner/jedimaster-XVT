import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";

import { decode } from "./codec.ts";

const FOLDER = join(
  import.meta.dirname,
  "..",
  "..",
  "tests",
  "control-examples",
);

function examples(kind: "accept" | "refuse"): { name: string; text: string }[] {
  const folder = join(FOLDER, kind);
  return readdirSync(folder)
    .filter((name) => name.endsWith(".json"))
    .sort()
    .map((name) => ({ name, text: readFileSync(join(folder, name), "utf8") }));
}

test("the example folders are not empty", () => {
  assert.ok(examples("accept").length >= 15);
  assert.ok(examples("refuse").length >= 20);
});

for (const { name, text } of examples("accept")) {
  test(`accepted example ${name}`, () => {
    const decoded = decode(text);
    assert.ok(decoded.ok, JSON.stringify(decoded));
    assert.deepEqual(decoded.message, JSON.parse(text));
  });
}

for (const { name, text } of examples("refuse")) {
  test(`refused example ${name}`, () => {
    assert.equal(decode(text).ok, false);
  });
}
