import assert from "node:assert/strict";
import { test } from "node:test";

import type { MissionType } from "../codec.ts";
import type { MenuData } from "../generated/control.ts";
import type { MissionKey, SideLists, SideModel, SlotState } from "./side.ts";
import {
  afterShown,
  buttonSprite,
  describeSide,
  firstAvailable,
  keyOfShown,
  lightNames,
  listsKnown,
  missionArgs,
  missionChoices,
  press,
  sameKey,
  SIDE_BUTTONS,
  slotStates,
  START_MODEL,
  stepMission,
} from "./side.ts";

// The 48 sprite names the sprite route serves.
const SERVED = new Set<string>([
  ...[1, 2, 3, 4, 5, 8].flatMap((n) => [
    `game${String(n)}u`,
    `game${String(n)}d`,
  ]),
  ...[6, 7].flatMap((n) => [`reg${String(n)}u`, `reg${String(n)}d`]),
  ...[1, 2, 3, 4, 5, 6, 7, 8].map((n) => `active${String(n)}`),
  ..."1litb 2lita 2litb 3lita 3litb 4lita 4litb 5lita 5litb 6lita 7litb 8lita"
    .split(" ")
    .flatMap((piece) => [`${piece}0`, `${piece}1`]),
]);

function menu(
  type: MissionType,
  entries: readonly (readonly [number, string, boolean])[],
  resolved = true,
): MenuData {
  return {
    mission_type: type,
    game_path: `${type}\\mission.lst`,
    resolved,
    entries: entries.map(([id, file, available]) => ({
      section: "",
      id,
      available,
      file,
      title: file,
    })),
  };
}

const LISTS: SideLists = {
  menus: [
    menu("training", [
      [1, "ALPHA.TIE", true],
      [2, "BRAVO.TIE", false],
      [3, "CHARLIE.TIE", true],
      [4, "ECHO.TIE", true],
    ]),
    menu("melee", [
      [7, "DELTA.TIE", true],
      [8, "GONE.TIE", false],
      [9, "FOX.TIE", true],
    ]),
    menu("tournament", [], false),
    menu("combat", [
      [1, "SKIRM.TIE", true],
      [2, "RAID.TIE", true],
    ]),
    menu("battle", [[5, "BIG.BTL", false]]),
    menu("campaign", [[1, "CAMP.CMP", true]]),
  ],
  solo: {
    "training:rebel": ["alpha.tie", "bravo.tie", "charlie.tie", "only.tie"],
    "training:imperial": ["echo.tie", "charlie.tie"],
    "combat:rebel": ["skirm.tie"],
    "combat:imperial": ["raid.tie"],
    "campaign:rebel": ["camp.cmp"],
    "campaign:imperial": [],
  },
};

function byId(id: number): MissionKey {
  return { by: "id", id };
}

function byFile(side: "rebel" | "imperial", file: string): MissionKey {
  return { by: "file", side, file };
}

const training: SideModel = { ...START_MODEL, kind: "training" };

test("a selected type and the chosen side are the selected slots", () => {
  assert.deepEqual(slotStates(training), [2, 1, 1, 1, 1, 2, 1, 1]);
  assert.deepEqual(
    slotStates({ ...START_MODEL, kind: "campaign", side: "imperial" }),
    [1, 1, 1, 1, 1, 1, 2, 2],
  );
  assert.deepEqual(slotStates(START_MODEL), [1, 1, 1, 1, 1, 2, 1, 1]);
});

test("lights: active slots first, then the selected slots' pieces", () => {
  assert.deepEqual(lightNames([2, 1, 1, 1, 1, 2, 1, 1]), [
    "active2",
    "active3",
    "active4",
    "active5",
    "active7",
    "active8",
    "1litb1",
    "7litb1",
  ]);
});

test("lights: the first and seventh positions have no lita, the sixth and eighth no litb", () => {
  const only = (slot: number): SlotState[] =>
    [1, 2, 3, 4, 5, 6, 7, 8].map((n) => (n === slot ? 2 : 1));
  // Slots 1 to 5 are positions 1 to 5; slot 8 is position 6; slot 6, 7; slot 7, 8.
  const pieces = (slot: number): string[] =>
    lightNames(only(slot)).filter((name) => !name.startsWith("active"));
  assert.deepEqual(pieces(1), ["1litb1"]);
  assert.deepEqual(pieces(2), ["2lita1", "2litb1"]);
  assert.deepEqual(pieces(3), ["3lita1", "3litb1"]);
  assert.deepEqual(pieces(4), ["4lita1", "4litb1"]);
  assert.deepEqual(pieces(5), ["5lita1", "5litb1"]);
  assert.deepEqual(pieces(8), ["6lita1"]);
  assert.deepEqual(pieces(6), ["7litb1"]);
  assert.deepEqual(pieces(7), ["8lita1"]);
});

test("lights: the neighbours' states are the digits, in position order", () => {
  // Slot 5 is followed by slot 8 in position order, slot 8 by slot 6.
  assert.deepEqual(lightNames([0, 0, 0, 0, 2, 0, 0, 0]), ["5lita0", "5litb0"]);
  assert.deepEqual(lightNames([1, 1, 1, 1, 2, 1, 1, 1]).slice(-2), [
    "5lita1",
    "5litb1",
  ]);
  assert.deepEqual(lightNames([0, 0, 0, 0, 0, 2, 0, 0]), ["7litb0"]);
  assert.deepEqual(lightNames([0, 0, 0, 1, 2, 0, 0, 0]), [
    "active4",
    "5lita1",
    "5litb0",
  ]);
  // Slot 8 is position 6: its before-neighbour is slot 5, and it has no litb.
  assert.deepEqual(lightNames([0, 0, 0, 0, 1, 0, 0, 2]).slice(-1), ["6lita1"]);
});

test("every light and button sprite a solo page asks for is served", () => {
  const kinds: (MissionType | null)[] = [
    null,
    "training",
    "melee",
    "tournament",
    "combat",
    "battle",
    "campaign",
  ];
  for (const kind of kinds) {
    for (const side of ["rebel", "imperial"] as const) {
      const view = describeSide({ ...START_MODEL, kind, side });
      for (const name of [
        ...view.lights,
        ...view.buttons.map((b) => b.sprite),
      ]) {
        assert.ok(SERVED.has(name), `${name} is not served`);
      }
    }
  }
});

test("a button draws d when selected and u when not", () => {
  assert.equal(buttonSprite("game1", 2), "game1d");
  assert.equal(buttonSprite("game1", 1), "game1u");
  assert.equal(buttonSprite("reg7", 0), "reg7u");
  const view = describeSide({ ...START_MODEL, kind: "campaign" });
  const sprites = Object.fromEntries(view.buttons.map((b) => [b.id, b.sprite]));
  assert.deepEqual(sprites, {
    training: "game1u",
    melee: "game2u",
    tournament: "game3u",
    combat: "game4u",
    battle: "game5u",
    campaign: "game8d",
    rebel: "reg6d",
    imperial: "reg7u",
  });
});

test("only the selected type steps", () => {
  const view = describeSide(training);
  assert.deepEqual(
    view.buttons.filter((b) => b.stepping).map((b) => b.id),
    ["training"],
  );
  assert.deepEqual(
    view.buttons.filter((b) => b.selected).map((b) => b.id),
    ["training", "rebel"],
  );
  assert.equal(SIDE_BUTTONS.length, 8);
});

test("a step skips what is not available and wraps", () => {
  const choices = missionChoices(LISTS, "melee", "rebel");
  assert.deepEqual(
    choices.map((c) => c.key),
    [byId(7), byId(8), byId(9)],
  );
  assert.deepEqual(stepMission(choices, byId(7), false), byId(9));
  assert.deepEqual(stepMission(choices, byId(9), false), byId(7));
  assert.deepEqual(stepMission(choices, byId(9), true), byId(7));
  assert.deepEqual(stepMission(choices, byId(7), true), byId(9));
});

test("a step from nothing gives the first (forward) or last (back) one", () => {
  const choices = missionChoices(LISTS, "melee", "rebel");
  assert.deepEqual(stepMission(choices, null, false), byId(7));
  assert.deepEqual(stepMission(choices, null, true), byId(9));
  assert.deepEqual(stepMission(choices, byId(99), false), byId(7));
});

test("a step ends after one lap when none is available", () => {
  const none = missionChoices(LISTS, "battle", "rebel");
  assert.equal(stepMission(none, null, false), null);
  assert.equal(stepMission(none, byId(5), true), null);
  assert.equal(stepMission([], byId(1), false), null);
  const alone = [
    { key: byId(1), available: false },
    { key: byId(2), available: true },
  ];
  assert.deepEqual(stepMission(alone, byId(2), false), byId(2));
  assert.deepEqual(firstAvailable(alone), byId(2));
  assert.equal(firstAvailable(none), null);
});

test("a sided list is that side's solo list itself, every entry offered", () => {
  assert.deepEqual(missionChoices(LISTS, "training", "rebel"), [
    { key: byFile("rebel", "alpha.tie"), available: true },
    { key: byFile("rebel", "bravo.tie"), available: true },
    { key: byFile("rebel", "charlie.tie"), available: true },
    { key: byFile("rebel", "only.tie"), available: true },
  ]);
  assert.deepEqual(
    missionChoices(LISTS, "training", "imperial").map((c) => c.key),
    [byFile("imperial", "echo.tie"), byFile("imperial", "charlie.tie")],
  );
  assert.deepEqual(missionChoices(LISTS, "tournament", "rebel"), []);
  assert.deepEqual(
    missionChoices({ ...LISTS, solo: {} }, "training", "rebel"),
    [],
  );
  assert.deepEqual(
    missionChoices({ menus: null, solo: {} }, "melee", "rebel"),
    [],
  );
});

test("a sided list needs no network menu: a solo-only file is reached", () => {
  const solo: SideLists = { menus: null, solo: LISTS.solo };
  const keys = missionChoices(solo, "training", "rebel").map((c) => c.key);
  assert.ok(keys.some((k) => sameKey(k, byFile("rebel", "only.tie"))));
  const result = press(
    { ...START_MODEL, kind: "training" },
    solo,
    "training",
    true,
  );
  assert.deepEqual(result.show?.key, byFile("rebel", "only.tie"));
});

test("a file the network menu marks not available is reached from its solo list", () => {
  const walk = [
    byFile("rebel", "alpha.tie"),
    byFile("rebel", "bravo.tie"),
    byFile("rebel", "charlie.tie"),
    byFile("rebel", "only.tie"),
    byFile("rebel", "alpha.tie"),
  ];
  let model = press(START_MODEL, LISTS, "training", false).model;
  assert.deepEqual(model.remembered["training:rebel"], walk[0]);
  for (const expected of walk.slice(1)) {
    const result = press(model, LISTS, "training", false);
    assert.deepEqual(result.show?.key, expected);
    model = result.model;
  }
});

test("an unsided kind still skips what its network menu marks not available", () => {
  const choices = missionChoices(LISTS, "melee", "rebel");
  assert.deepEqual(
    choices.map((c) => c.available),
    [true, false, true],
  );
});

test("an unselected type is selected and shows its first mission", () => {
  const result = press(START_MODEL, LISTS, "training", false);
  assert.deepEqual(result.show, {
    missionType: "training",
    key: byFile("rebel", "alpha.tie"),
  });
  assert.equal(result.model.kind, "training");
  assert.equal(result.note, null);
  assert.deepEqual(
    result.model.remembered["training:rebel"],
    byFile("rebel", "alpha.tie"),
  );
});

test("an unselected type shows its remembered mission if still offered", () => {
  const model: SideModel = {
    ...START_MODEL,
    kind: "melee",
    remembered: {
      "training:rebel": byFile("rebel", "charlie.tie"),
      melee: byId(9),
    },
  };
  assert.deepEqual(press(model, LISTS, "training", false).show, {
    missionType: "training",
    key: byFile("rebel", "charlie.tie"),
  });
  const gone: SideModel = {
    ...model,
    remembered: { "training:rebel": byFile("rebel", "vanished.tie") },
  };
  assert.deepEqual(
    press(gone, LISTS, "training", false).show?.key,
    byFile("rebel", "alpha.tie"),
  );
  const locked: SideModel = {
    ...model,
    kind: "training",
    remembered: { melee: byId(8) },
  };
  assert.deepEqual(press(locked, LISTS, "melee", false).show?.key, byId(7));
});

test("a type with nothing available is selected, shows nothing, and says so", () => {
  const result = press(training, LISTS, "battle", false);
  assert.equal(result.show, null);
  assert.equal(result.model.kind, "battle");
  assert.equal(result.note, "No Battle mission is available to show.");
  const again = press(result.model, LISTS, "tournament", false);
  assert.equal(again.model.kind, "tournament");
  assert.equal(again.show, null);
});

test("the selected type steps forward, and back with Shift or a right click", () => {
  let model = press(START_MODEL, LISTS, "training", false).model;
  const files: (string | undefined)[] = [];
  for (const backward of [false, false, false, true, true]) {
    const result = press(model, LISTS, "training", backward);
    const key = result.show?.key;
    files.push(key?.by === "file" ? key.file : undefined);
    model = result.model;
  }
  // alpha -> bravo -> charlie -> only -> charlie -> bravo.
  assert.deepEqual(files, [
    "bravo.tie",
    "charlie.tie",
    "only.tie",
    "charlie.tie",
    "bravo.tie",
  ]);
});

test("a step with nothing available keeps the type and says so", () => {
  const model = press(training, LISTS, "battle", false).model;
  const result = press(model, LISTS, "battle", false);
  assert.equal(result.show, null);
  assert.equal(result.note, "No Battle mission is available to show.");
  assert.equal(result.model.kind, "battle");
});

test("the other side is chosen with the type kept and that side's list shown", () => {
  const shown = press(START_MODEL, LISTS, "training", false).model;
  const result = press(shown, LISTS, "imperial", false);
  assert.equal(result.model.side, "imperial");
  assert.equal(result.model.kind, "training");
  assert.deepEqual(result.show, {
    missionType: "training",
    key: byFile("imperial", "echo.tie"),
  });
});

test("a side switch returns to the mission remembered for that side", () => {
  let model = press(START_MODEL, LISTS, "training", false).model;
  model = press(model, LISTS, "training", false).model; // rebel: bravo
  model = press(model, LISTS, "imperial", false).model; // imperial: echo
  model = press(model, LISTS, "training", false).model; // imperial: charlie
  assert.deepEqual(
    model.remembered["training:imperial"],
    byFile("imperial", "charlie.tie"),
  );
  const back = press(model, LISTS, "rebel", false);
  assert.deepEqual(back.show, {
    missionType: "training",
    key: byFile("rebel", "bravo.tie"),
  });
  const again = press(back.model, LISTS, "imperial", false);
  assert.deepEqual(again.show?.key, byFile("imperial", "charlie.tie"));
});

test("a side switch on a type with one list shows nothing", () => {
  const melee = press(START_MODEL, LISTS, "melee", false).model;
  const result = press(melee, LISTS, "imperial", false);
  assert.equal(result.show, null);
  assert.equal(result.model.side, "imperial");
  assert.equal(result.model.kind, "melee");
  const none = press(START_MODEL, LISTS, "rebel", false);
  assert.equal(none.show, null);
});

test("a side with no mission says so", () => {
  const campaign = press(START_MODEL, LISTS, "campaign", false).model;
  const result = press(campaign, LISTS, "imperial", false);
  assert.equal(result.show, null);
  assert.equal(result.model.side, "imperial");
  assert.equal(result.note, "No Campaign mission is available to show.");
});

test("the chosen side's own button does nothing", () => {
  const result = press(training, LISTS, "rebel", false);
  assert.deepEqual(result, { model: training, show: null, note: null });
});

test("a network entry shown elsewhere selects its type and the side holding its file", () => {
  // Entry 4 is ECHO.TIE, only in the imperial list.
  const model = afterShown(START_MODEL, LISTS, "training", byId(4));
  assert.equal(model.kind, "training");
  assert.equal(model.side, "imperial");
  assert.deepEqual(
    model.remembered["training:imperial"],
    byFile("imperial", "echo.tie"),
  );
  // An entry in both lists keeps the chosen side.
  assert.equal(afterShown(model, LISTS, "training", byId(3)).side, "imperial");
  assert.equal(
    afterShown(START_MODEL, LISTS, "training", byId(3)).side,
    "rebel",
  );
  // An entry in neither list keeps the side and remembers nothing.
  const lost = afterShown(model, LISTS, "training", byId(77));
  assert.equal(lost.side, "imperial");
  assert.deepEqual(lost.remembered, model.remembered);
  // A type with one list leaves the side alone.
  const melee = afterShown(model, LISTS, "melee", byId(7));
  assert.equal(melee.side, "imperial");
  assert.deepEqual(melee.remembered.melee, byId(7));
});

test("a solo mission shown elsewhere sets its own side and is remembered", () => {
  const model = afterShown(
    START_MODEL,
    LISTS,
    "training",
    byFile("imperial", "charlie.tie"),
  );
  assert.equal(model.kind, "training");
  assert.equal(model.side, "imperial");
  assert.deepEqual(
    model.remembered["training:imperial"],
    byFile("imperial", "charlie.tie"),
  );
});

test("the keys: same file in any letter case, never an id and a file", () => {
  assert.ok(
    sameKey(byFile("rebel", "ALPHA.tie"), byFile("rebel", "alpha.tie")),
  );
  assert.ok(
    !sameKey(byFile("rebel", "alpha.tie"), byFile("imperial", "alpha.tie")),
  );
  assert.ok(!sameKey(byId(1), byFile("rebel", "alpha.tie")));
  assert.ok(sameKey(byId(1), byId(1)));
  assert.ok(!sameKey(null, byId(1)));
});

test("the arguments name a mission by id or by side and file", () => {
  assert.deepEqual(missionArgs("melee", byId(7)), {
    mission_type: "melee",
    id: 7,
  });
  assert.deepEqual(missionArgs("training", byFile("rebel", "alpha.tie")), {
    mission_type: "training",
    side: "rebel",
    file: "alpha.tie",
  });
  assert.deepEqual(keyOfShown({ id: 3 }), byId(3));
  assert.deepEqual(
    keyOfShown({ side: "imperial", file: "echo.tie" }),
    byFile("imperial", "echo.tie"),
  );
});

test("a list is known once its menu and, if sided, both solo lists came", () => {
  assert.equal(listsKnown({ menus: null, solo: {} }, "melee"), false);
  assert.equal(listsKnown({ menus: [], solo: {} }, "melee"), true);
  assert.equal(listsKnown({ menus: [], solo: {} }, "training"), false);
  assert.equal(listsKnown(LISTS, "training"), true);
});
