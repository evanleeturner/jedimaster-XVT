import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";

import { decodeDebrief, decodeSummaries } from "./decode.ts";

type Fields = Record<string, unknown>;

/** The flown battle's debriefing from the shared accepted example. */
function example(): Fields {
  const path = join(
    import.meta.dirname,
    "..",
    "..",
    "..",
    "tests",
    "control-examples",
    "accept",
    "push-launch-ended.json",
  );
  const push = JSON.parse(readFileSync(path, "utf8")) as {
    data: { debrief: Fields };
  };
  return push.data.debrief;
}

function isFields(value: unknown): value is Fields {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

/** Return the object at ``key`` of ``value``. */
function part(value: unknown, key: string): Fields {
  assert.ok(isFields(value));
  const found = value[key];
  assert.ok(isFields(found), `no object at ${key}`);
  return found;
}

/** Return the first row of the list at ``key`` of ``value``. */
function first(value: unknown, key: string): Fields {
  assert.ok(isFields(value));
  const list: unknown = value[key];
  assert.ok(Array.isArray(list), `no list at ${key}`);
  const row: unknown = list[0];
  assert.ok(isFields(row), `no first row at ${key}`);
  return row;
}

function refused(value: unknown, why: string): void {
  const read = decodeDebrief(value);
  assert.ok("reason" in read, `${why}: was accepted`);
}

/** Every object of the debriefing that has fields: the whole, a player, a team, the detail, a row of each table. */
function parts(d: Fields): [string, (copy: Fields) => Fields][] {
  const detail = part(d, "detail");
  const out: [string, (copy: Fields) => Fields][] = [
    ["debriefing", (c) => c],
    ["player", (c) => first(c, "players")],
    ["team", (c) => first(c, "teams")],
    ["detail", (c) => part(c, "detail")],
  ];
  for (const [name, rows] of Object.entries(detail)) {
    if (Array.isArray(rows) && rows.length > 0) {
      out.push([`detail ${name}`, (c) => first(part(c, "detail"), name)]);
    }
  }
  return out;
}

test("the example debriefing reads back as written", () => {
  const read = decodeDebrief(example());
  assert.ok("value" in read, JSON.stringify(read));
  assert.deepEqual(read.value, example());
});

test("an old result's debriefing, with no tables, reads back", () => {
  const old = { ...example(), local_entry: -1, players: [], teams: [] };
  const read = decodeDebrief({ ...old, detail: null });
  assert.ok("value" in read);
  assert.equal(read.value.detail, null);
});

test("a missing field anywhere is refused", () => {
  for (const [what, pick] of parts(example())) {
    const keys = Object.keys(pick(example()));
    for (const key of keys) {
      const copy = example();
      Reflect.deleteProperty(pick(copy), key);
      refused(copy, `${what} without ${key}`);
    }
  }
});

test("an extra field anywhere is refused", () => {
  for (const [what, pick] of parts(example())) {
    const copy = example();
    pick(copy).extra = 1;
    refused(copy, `${what} with an extra field`);
  }
});

test("a value of the wrong type anywhere is refused", () => {
  for (const [what, pick] of parts(example())) {
    for (const key of Object.keys(pick(example()))) {
      const copy = example();
      const part = pick(copy);
      part[key] = typeof part[key] === "string" ? 7 : "seven";
      refused(copy, `${what}'s ${key} of the wrong type`);
    }
  }
});

test("a number past the schema's bounds is refused", () => {
  const cases: [string, (c: Fields) => void][] = [
    ["result id 0", (c) => (c.result_id = 0)],
    ["local entry 8", (c) => (c.local_entry = 8)],
    ["local entry -2", (c) => (c.local_entry = -2)],
    ["negative kills", (c) => (c.kills = -1)],
    ["a fraction", (c) => (c.score = 1.5)],
    ["an unknown outcome", (c) => (c.outcome = "won")],
    ["an unknown kind", (c) => (c.mission_type = "skirmish")],
    [
      "a 13-letter player name",
      (c) => (first(c, "players").name = "Thirteen Char"),
    ],
    ["player entry 8", (c) => (first(c, "players").entry = 8)],
    ["team -2", (c) => (first(c, "players").team = -2)],
    [
      "craft type 100",
      (c) => (first(part(c, "detail"), "by_craft").craft_type = 100),
    ],
    [
      "flight group 48",
      (c) => (first(part(c, "detail"), "on_flight_groups").flight_group = 48),
    ],
    ["rank 25", (c) => (first(part(c, "detail"), "on_ai_ranks").rank = 25)],
    ["players not a list", (c) => (c.players = {})],
  ];
  for (const [why, change] of cases) {
    const copy = example();
    change(copy);
    refused(copy, why);
  }
  refused(null, "null");
  refused([], "a list");
});

test("the kept results list reads back, and a bad line is refused", () => {
  const line = {
    result_id: 3,
    ended_at: "2026-10-16T21:30:00Z",
    title: "Two Teams Meet",
    outcome: "completed",
    score: 1450,
  };
  const read = decodeSummaries([line, { ...line, result_id: 2 }]);
  assert.ok("value" in read);
  assert.equal(read.value.length, 2);
  assert.ok("reason" in decodeSummaries([{ ...line, result_id: 0 }]));
  assert.ok("reason" in decodeSummaries([{ ...line, more: 1 }]));
  assert.ok("reason" in decodeSummaries({}));
});
