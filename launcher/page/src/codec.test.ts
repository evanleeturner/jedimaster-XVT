import assert from "node:assert/strict";
import { test } from "node:test";

import type { Request } from "./codec.ts";
import { decode, decodeValue, encodeRequest, MAX_ID } from "./codec.ts";

function accepted(value: unknown): unknown {
  const decoded = decodeValue(value);
  assert.ok(decoded.ok, JSON.stringify(decoded));
  return decoded.message;
}

function refused(value: unknown): string {
  const decoded = decodeValue(value);
  assert.ok(!decoded.ok, JSON.stringify(value));
  return decoded.reason;
}

const SETTINGS = { art_scaling: "engine_fit" };

test("every request is read back as written", () => {
  const requests: unknown[] = [
    { id: 1, command: "hello", args: { page_version: "0.1.0" } },
    { id: 2, command: "install.status", args: {} },
    { id: 3, command: "missions.list", args: {} },
    { id: 4, command: "settings.get", args: {} },
    {
      id: MAX_ID,
      command: "settings.set",
      args: { name: "art_scaling", value: "sharp_bilinear" },
    },
    {
      id: 6,
      command: "page.show_mission",
      args: { mission_type: "campaign", id: 0 },
    },
    {
      id: 7,
      command: "page.show_mission",
      args: { mission_type: "training", id: MAX_ID },
    },
  ];
  for (const request of requests) assert.deepEqual(accepted(request), request);
});

test("every kind of ok reply is read back as written", () => {
  const results: unknown[] = [
    { launcher_version: "0.1.0", schema_revision: 2 },
    { found: true, path: "~/Games/XvT", balance_of_power: false },
    { found: false, path: null, balance_of_power: false },
    {
      menus: [
        {
          mission_type: "training",
          game_path: "train\\mission.lst",
          resolved: true,
          entries: [
            {
              section: "A",
              id: 1,
              available: false,
              file: "a.tie",
              title: "T",
            },
          ],
        },
        { mission_type: "melee", game_path: "m", resolved: false, entries: [] },
      ],
    },
    { settings: SETTINGS },
    { shown: true },
    { shown: false },
  ];
  for (const result of results) {
    const reply = { id: 9, ok: true, result };
    assert.deepEqual(accepted(reply), reply);
  }
});

test("an error reply is read back as written, with id 0 allowed", () => {
  for (const code of ["bad_message", "unknown_command", "bad_arguments"]) {
    const reply = { id: 0, ok: false, error: { code, message: "words" } };
    assert.deepEqual(accepted(reply), reply);
  }
});

test("both pushes are read back as written", () => {
  const pushes: unknown[] = [
    {
      event: "status",
      data: { launcher_version: "0.1.0", install_found: true },
    },
    { event: "settings.changed", data: { settings: SETTINGS } },
  ];
  for (const push of pushes) assert.deepEqual(accepted(push), push);
});

test("a request refuses a bad id", () => {
  for (const id of [0, -1, 1.5, "1", true, null, MAX_ID + 1, undefined]) {
    refused({ id, command: "settings.get", args: {} });
  }
});

test("a request refuses missing, extra and mistyped fields", () => {
  refused({ command: "settings.get", args: {} });
  refused({ id: 1, command: "settings.get" });
  refused({ id: 1, command: "settings.get", args: {}, extra: 1 });
  refused({ id: 1, command: "settings.get", args: [] });
  refused({ id: 1, command: "settings.get", args: null });
  refused({ id: 1, command: 7, args: {} });
});

test("a request refuses an off-list command", () => {
  assert.equal(
    refused({ id: 1, command: "shutdown", args: {} }),
    "no such command",
  );
  refused({ id: 1, command: "Hello", args: { page_version: "1" } });
});

test("a request refuses arguments its command does not take", () => {
  refused({ id: 1, command: "hello", args: {} });
  refused({ id: 1, command: "hello", args: { page_version: "" } });
  refused({ id: 1, command: "hello", args: { page_version: "x".repeat(65) } });
  refused({ id: 1, command: "hello", args: { page_version: 1 } });
  refused({ id: 1, command: "hello", args: { page_version: "1", more: 1 } });
  refused({ id: 1, command: "install.status", args: { x: 1 } });
  refused({ id: 1, command: "missions.list", args: { x: 1 } });
  refused({ id: 1, command: "settings.get", args: { x: 1 } });
});

test("settings.set refuses a name or value off the list", () => {
  const set = (args: unknown): unknown => ({
    id: 1,
    command: "settings.set",
    args,
  });
  refused(set({}));
  refused(set({ name: "art_scaling" }));
  refused(set({ name: "volume", value: "high" }));
  refused(set({ name: "art_scaling", value: "stretched" }));
  refused(set({ name: "art_scaling", value: 2 }));
  refused(set({ name: "volume", value: "engine_fit" }));
  refused(set({ name: 3, value: "engine_fit" }));
  refused(set({ name: "art_scaling", value: "engine_fit", force: true }));
});

test("a reply refuses a bad id, a bad flag and a mixed shape", () => {
  const result = { settings: SETTINGS };
  refused({ id: -1, ok: true, result });
  refused({ id: MAX_ID + 1, ok: true, result });
  refused({ id: 1, ok: "true", result });
  refused({ id: 1, ok: true });
  refused({ id: 1, ok: true, result, error: {} });
  refused({ id: 1, ok: false, result });
  refused({ id: 1, ok: false, error: { code: "x", message: "m" } });
  refused({ id: 1, ok: false, error: { code: "bad_message" } });
  refused({ id: 1, ok: false, error: { code: "bad_message", message: 3 } });
  refused({ id: 1, ok: false, error: "words" });
});

test("a reply refuses a result that matches no command", () => {
  const reply = (result: unknown): unknown => ({ id: 1, ok: true, result });
  refused(reply("done"));
  refused(reply(null));
  refused(reply({}));
  refused(reply({ launcher_version: "0.1.0" }));
  refused(reply({ launcher_version: "0.1.0", schema_revision: 1 }));
  refused(reply({ shown: "yes" }));
  refused(reply({ shown: true, more: 1 }));
  refused(reply({ launcher_version: 1, schema_revision: 2 }));
  refused(reply({ found: "yes", path: null, balance_of_power: false }));
  refused(reply({ found: true, path: 3, balance_of_power: false }));
  refused(reply({ settings: { art_scaling: "stretched" } }));
  refused(reply({ settings: { art_scaling: "engine_fit", more: 1 } }));
  refused(reply({ settings: [] }));
  refused(reply({ menus: "none" }));
  refused(reply({ menus: {} }));
  refused(reply({ menus: [3] }));
});

test("a menu refuses a wrong field or a wrong entry", () => {
  const menu = {
    mission_type: "t",
    game_path: "p",
    resolved: true,
    entries: [] as unknown[],
  };
  const reply = (m: unknown): unknown => ({
    id: 1,
    ok: true,
    result: { menus: [m] },
  });
  accepted(reply(menu));
  refused(reply({ ...menu, resolved: "yes" }));
  refused(reply({ ...menu, mission_type: 1 }));
  refused(reply({ ...menu, extra: 1 }));
  refused(reply({ ...menu, entries: {} }));
  const entry = { section: "", id: 1, available: true, file: "f", title: "t" };
  accepted(reply({ ...menu, entries: [entry] }));
  for (const bad of [
    { ...entry, id: 1.5 },
    { ...entry, id: "1" },
    { ...entry, available: 1 },
    { ...entry, file: null },
    { ...entry, title: 3 },
    { ...entry, section: 3 },
    { ...entry, line: 3 },
    "entry",
  ]) {
    refused(reply({ ...menu, entries: [bad] }));
  }
});

test("a push refuses an unknown event and a wrong payload", () => {
  refused({ event: "shutdown", data: {} });
  refused({ event: "status" });
  refused({ event: "status", data: [] });
  refused({ event: "status", data: { launcher_version: "1" } });
  refused({
    event: "status",
    data: { launcher_version: "1", install_found: "y" },
  });
  refused({
    event: "status",
    data: { launcher_version: 1, install_found: true },
  });
  refused({
    event: "status",
    data: { launcher_version: "1", install_found: true, x: 1 },
  });
  refused({ event: "settings.changed", data: {} });
  refused({
    event: "settings.changed",
    data: { settings: { art_scaling: "x" } },
  });
  refused({ event: "settings.changed", data: { settings: SETTINGS, x: 1 } });
  refused({ event: "status", data: {}, extra: 1 });
});

test("a push to show a mission is read back as written", () => {
  for (const id of [0, 2, MAX_ID]) {
    const push = {
      event: "page.show_mission",
      data: { mission_type: "melee", id, title: "Open Field" },
    };
    assert.deepEqual(accepted(push), push);
  }
});

test("a show-mission push refuses a wrong payload", () => {
  const data = { mission_type: "melee", id: 2, title: "t" };
  const push = (changed: unknown): unknown => ({
    event: "page.show_mission",
    data: changed,
  });
  refused(push({}));
  refused(push({ ...data, mission_type: "skirmish" }));
  refused(push({ ...data, mission_type: "Melee" }));
  refused(push({ ...data, id: -1 }));
  refused(push({ ...data, id: MAX_ID + 1 }));
  refused(push({ ...data, id: 1.5 }));
  refused(push({ ...data, id: "2" }));
  refused(push({ ...data, title: 3 }));
  refused(push({ ...data, more: 1 }));
  refused(push({ mission_type: "melee", id: 2 }));
});

test("a show-mission request refuses a wrong type, id or shape", () => {
  const args = { mission_type: "melee", id: 2 };
  const ask = (changed: unknown): unknown => ({
    id: 1,
    command: "page.show_mission",
    args: changed,
  });
  refused(ask({}));
  refused(ask({ mission_type: "melee" }));
  refused(ask({ id: 2 }));
  refused(ask({ ...args, mission_type: "skirmish" }));
  refused(ask({ ...args, mission_type: 4 }));
  refused(ask({ ...args, id: -1 }));
  refused(ask({ ...args, id: MAX_ID + 1 }));
  refused(ask({ ...args, id: 0.5 }));
  refused(ask({ ...args, id: "2" }));
  refused(ask({ ...args, more: 1 }));
});

test("anything that is not an object is refused", () => {
  for (const value of [null, 1, "text", true, [], [1, 2], undefined]) {
    assert.equal(refused(value), "a message must be an object");
  }
  refused({});
  refused({ other: 1 });
});

test("decode refuses text that is not JSON and reads text that is", () => {
  assert.deepEqual(decode("not json"), {
    ok: false,
    reason: "the message is not JSON",
  });
  assert.deepEqual(decode(""), {
    ok: false,
    reason: "the message is not JSON",
  });
  const text = '{"id":1,"command":"settings.get","args":{}}';
  assert.deepEqual(decode(text), {
    ok: true,
    message: { id: 1, command: "settings.get", args: {} },
  });
});

test("encodeRequest writes JSON that decode reads back", () => {
  const request: Request = {
    id: 5,
    command: "settings.set",
    args: { name: "art_scaling", value: "whole_pixels" },
  };
  const text = encodeRequest(request);
  assert.deepEqual(JSON.parse(text), request);
  assert.deepEqual(decode(text), { ok: true, message: request });
});
