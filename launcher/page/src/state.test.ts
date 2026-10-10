import assert from "node:assert/strict";
import { test } from "node:test";

import type { Message } from "./codec.ts";
import type { MenuData } from "./generated/control.ts";
import type { Action, State } from "./state.ts";
import { describe, initialState, reduce } from "./state.ts";

function play(...actions: Action[]): State {
  return actions.reduce(reduce, initialState());
}

function message(value: Message): Action {
  return { kind: "message", message: value };
}

const TRAINING: MenuData = {
  mission_type: "training",
  game_path: "train\\mission.lst",
  resolved: true,
  entries: [
    { section: "Basic", id: 1, available: true, file: "a.tie", title: "First" },
    { section: "", id: 2, available: false, file: "b.tie", title: "Second" },
  ],
};
const MELEE: MenuData = {
  mission_type: "melee",
  game_path: "melee\\mission.lst",
  resolved: false,
  entries: [],
};

test("a new page is connecting and knows nothing", () => {
  const view = describe(initialState());
  assert.equal(view.connectionText, "Connecting to the launcher.");
  assert.equal(view.showReconnect, false);
  assert.equal(view.settingsEnabled, false);
  assert.equal(view.versionText, "Launcher version: not known yet.");
  assert.equal(view.installText, "Not known yet.");
  assert.equal(view.balanceText, "Not known yet.");
  assert.equal(view.missionsNote, "The mission lists have not arrived yet.");
  assert.deepEqual(view.menus, []);
  assert.equal(view.artScaling, null);
  assert.equal(view.problem, null);
});

test("an open socket says connected and lets settings change", () => {
  const view = describe(play({ kind: "opened" }));
  assert.equal(view.connectionText, "Connected to the launcher.");
  assert.equal(view.settingsEnabled, true);
  assert.equal(view.showReconnect, false);
});

test("a dropped socket says so, offers Reconnect and locks settings", () => {
  const view = describe(play({ kind: "opened" }, { kind: "closed" }));
  assert.equal(
    view.connectionText,
    "The connection to the launcher has dropped.",
  );
  assert.equal(view.showReconnect, true);
  assert.equal(view.settingsEnabled, false);
});

test("reconnecting clears the offer and a problem, and keeps what was known", () => {
  const refusal = message({
    id: 5,
    ok: false,
    error: { code: "bad_arguments", message: "words" },
  });
  const known = play(
    { kind: "opened" },
    message({
      event: "settings.changed",
      data: { settings: { art_scaling: "engine_fit" } },
    }),
    refusal,
    { kind: "closed" },
  );
  assert.notEqual(describe(known).problem, null);
  const view = describe(reduce(known, { kind: "connecting" }));
  assert.equal(view.connectionText, "Connecting to the launcher.");
  assert.equal(view.showReconnect, false);
  assert.equal(view.artScaling, "engine_fit");
  assert.equal(view.problem, null);
});

test("the status push gives the launcher's version", () => {
  const view = describe(
    play(
      message({
        event: "status",
        data: { launcher_version: "0.1.0", install_found: true },
      }),
    ),
  );
  assert.equal(view.versionText, "Launcher version: 0.1.0");
});

test("a hello reply gives the launcher's version", () => {
  const view = describe(
    play(
      message({
        id: 1,
        ok: true,
        result: { launcher_version: "2.0", schema_revision: 1 },
      }),
    ),
  );
  assert.equal(view.versionText, "Launcher version: 2.0");
});

test("an install that was found shows its path and Balance of Power", () => {
  const found = { found: true, path: "~/Games/XvT", balance_of_power: true };
  const view = describe(play(message({ id: 2, ok: true, result: found })));
  assert.equal(view.installText, "Game install: ~/Games/XvT");
  assert.equal(view.balanceText, "Balance of Power: found.");
  const plain = { ...found, balance_of_power: false };
  const without = describe(play(message({ id: 2, ok: true, result: plain })));
  assert.equal(without.balanceText, "Balance of Power: not found.");
});

test("an install that was not found says so", () => {
  const missing = { found: false, path: null, balance_of_power: false };
  const view = describe(play(message({ id: 2, ok: true, result: missing })));
  assert.match(view.installText, /^No game install was found/);
  assert.equal(view.balanceText, "Not known yet.");
});

test("missions list one heading per menu, availability in words", () => {
  const view = describe(
    play(message({ id: 3, ok: true, result: { menus: [TRAINING, MELEE] } })),
  );
  assert.equal(view.missionsNote, null);
  assert.equal(view.menus.length, 2);
  const training = view.menus[0];
  const melee = view.menus[1];
  assert.ok(training && melee);
  assert.equal(training.heading, "Training missions");
  assert.equal(training.note, null);
  assert.deepEqual(training.entries, [
    { text: "Basic: First (a.tie), available" },
    { text: "Second (b.tie), not available" },
  ]);
  assert.equal(melee.heading, "Melee missions");
  assert.deepEqual(melee.entries, []);
  assert.match(melee.note ?? "", /was not found \(melee\\mission\.lst\)/);
});

test("a menu that resolved but is empty says so", () => {
  const empty = { ...TRAINING, entries: [] };
  const view = describe(
    play(message({ id: 3, ok: true, result: { menus: [empty] } })),
  );
  assert.equal(view.menus[0]?.note, "This list has no missions.");
});

test("a settings reply and a settings.changed push both set the choice", () => {
  const reply = message({
    id: 4,
    ok: true,
    result: { settings: { art_scaling: "sharp_bilinear" } },
  });
  assert.equal(describe(play(reply)).artScaling, "sharp_bilinear");
  const push = message({
    event: "settings.changed",
    data: { settings: { art_scaling: "engine_fit" } },
  });
  assert.equal(describe(play(reply, push)).artScaling, "engine_fit");
});

test("a refusal shows a problem, and the next good message clears it", () => {
  const refusal = message({
    id: 5,
    ok: false,
    error: { code: "bad_arguments", message: "no such setting" },
  });
  const view = describe(play(refusal));
  assert.equal(
    view.problem,
    "The launcher refused a request: no such setting.",
  );
  const cleared = play(
    refusal,
    message({
      id: 6,
      ok: true,
      result: { settings: { art_scaling: "engine_fit" } },
    }),
  );
  assert.equal(describe(cleared).problem, null);
});

test("a request message changes nothing", () => {
  const before = initialState();
  const after = reduce(
    before,
    message({ id: 1, command: "settings.get", args: {} }),
  );
  assert.deepEqual(after, before);
});

test("reduce returns a new state and leaves the old one alone", () => {
  const before = initialState();
  const after = reduce(before, { kind: "opened" });
  assert.notEqual(after, before);
  assert.equal(before.connection, "connecting");
});

test("an open socket clears a problem", () => {
  const refusal = message({
    id: 5,
    ok: false,
    error: { code: "bad_message", message: "words" },
  });
  const state = play(refusal, { kind: "opened" });
  assert.equal(describe(state).problem, null);
});
