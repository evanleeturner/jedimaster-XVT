import assert from "node:assert/strict";
import { test } from "node:test";

import { makeBundle } from "./briefing/bundle.fixture.ts";
import type { Message } from "./codec.ts";
import type { MenuData, ShownMission } from "./generated/control.ts";
import type { Action, State } from "./state.ts";
import { describe, initialState, optionWords, reduce } from "./state.ts";

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
        data: { launcher_version: "0.1.0", install_found: true, pilot: null },
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
        result: { launcher_version: "2.0", schema_revision: 7 },
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
    {
      text: "Basic: First (a.tie), available",
      current: false,
      pick: { missionType: "training", id: 1, label: "Show First" },
    },
    { text: "Second (b.tie), not available", current: false, pick: null },
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

function menusReply(...menus: MenuData[]): Action {
  return message({ id: 3, ok: true, result: { menus } });
}

function showPush(
  mission_type: ShownMission["mission_type"],
  id: number,
): Action {
  return message({
    event: "page.show_mission",
    data: { mission_type, id, title: `Mission ${String(id)}` },
  });
}

function marked(state: State): string[] {
  return describe(state).menus.flatMap((menu) =>
    menu.entries.filter((e) => e.current).map((e) => e.text),
  );
}

test("before any push, nothing is marked and nothing is announced", () => {
  const state = play(menusReply(TRAINING, MELEE));
  assert.deepEqual(marked(state), []);
  assert.equal(describe(state).shownText, "");
  assert.equal(describe(state).shownCount, 0);
});

test("a show-mission push marks that entry and announces its title", () => {
  const state = play(menusReply(TRAINING, MELEE), showPush("training", 2));
  assert.deepEqual(marked(state), ["Second (b.tie), not available"]);
  assert.equal(describe(state).shownText, "Showing Mission 2");
  assert.equal(describe(state).shownCount, 1);
});

test("a later push moves the mark and counts again", () => {
  const state = play(
    menusReply(TRAINING),
    showPush("training", 2),
    showPush("training", 1),
  );
  assert.deepEqual(marked(state), ["Basic: First (a.tie), available"]);
  assert.equal(describe(state).shownCount, 2);
});

test("the mark needs the same type as well as the same id", () => {
  const other: MenuData = {
    ...MELEE,
    resolved: true,
    entries: [
      { section: "", id: 2, available: true, file: "m.tie", title: "Melee" },
    ],
  };
  const state = play(menusReply(TRAINING, other), showPush("melee", 2));
  assert.deepEqual(marked(state), ["Melee (m.tie), available"]);
});

test("when an id repeats inside a menu, the first entry is marked", () => {
  const twice: MenuData = {
    ...TRAINING,
    entries: [
      { section: "", id: 5, available: true, file: "x.tie", title: "One" },
      { section: "", id: 5, available: true, file: "y.tie", title: "Two" },
    ],
  };
  const state = play(menusReply(twice), showPush("training", 5));
  assert.deepEqual(marked(state), ["One (x.tie), available"]);
});

test("a push for a mission the page does not list marks nothing", () => {
  const state = play(menusReply(TRAINING), showPush("training", 99));
  assert.deepEqual(marked(state), []);
  assert.equal(describe(state).shownText, "Showing Mission 99");
});

test("the mark appears once the lists arrive after the push", () => {
  const state = play(showPush("training", 1), menusReply(TRAINING));
  assert.deepEqual(marked(state), ["Basic: First (a.tie), available"]);
});

test("a show-mission result changes nothing", () => {
  const before = play(menusReply(TRAINING), showPush("training", 1));
  const after = reduce(
    before,
    message({ id: 4, ok: true, result: { shown: true } }),
  );
  assert.deepEqual(after, before);
});

test("a mission pushed to the page makes the briefing wait for its bundle", () => {
  const pushed = play(
    message({
      event: "page.show_mission",
      data: { mission_type: "training", id: 1, title: "First" },
    }),
  );
  const view = describe(pushed);
  assert.equal(view.briefing.status, "loading");
  assert.equal(view.briefingNote, "Loading the briefing.");
});

test("a found briefing is kept for the player", () => {
  const bundle = makeBundle();
  const view = describe(
    play(message({ id: 5, ok: true, result: { found: true, bundle } })),
  );
  const kept = view.briefing;
  assert.ok(kept.status === "ready");
  assert.equal(view.briefingNote, null);
  assert.equal(kept.bundle, bundle);
});

test("a briefing that was not found says so", () => {
  const view = describe(
    play(message({ id: 5, ok: true, result: { found: false, bundle: null } })),
  );
  assert.equal(view.briefing.status, "missing");
  assert.match(view.briefingNote ?? "", /could not be shown/);
});

test("a new push after a briefing waits for the next one", () => {
  const view = describe(
    play(
      message({
        id: 5,
        ok: true,
        result: { found: true, bundle: makeBundle() },
      }),
      message({
        event: "page.show_mission",
        data: { mission_type: "melee", id: 2, title: "Open" },
      }),
    ),
  );
  assert.equal(view.briefing.status, "loading");
});

test("before anything is picked the briefing asks for a pick", () => {
  assert.equal(
    describe(initialState()).briefingNote,
    "Pick a mission to see its briefing.",
  );
});

test("an available entry has a button that asks for that mission; a locked one has none", () => {
  const view = describe(
    play(message({ id: 3, ok: true, result: { menus: [TRAINING, MELEE] } })),
  );
  const [first, second] = view.menus[0]?.entries ?? [];
  assert.deepEqual(first?.pick, {
    missionType: "training",
    id: 1,
    label: "Show First",
  });
  assert.equal(second?.pick, null);
});

const STARTED: Message = {
  event: "launch.started",
  data: {
    launch_id: 4,
    mission_type: "training",
    side: "rebel",
    mission: "a.tie",
    title: "First Flight",
    pilot: "Ace",
  },
};
const ENDED: Message = {
  event: "launch.ended",
  data: { launch_id: 4, status: "flown", line: "flown: completed, score 10" },
};

test("with no battle the page says battles start in Discord and shows no Cancel", () => {
  const view = describe(play({ kind: "opened" }));
  assert.equal(
    view.battleNote,
    "No battle is flying. Battles start in Discord.",
  );
  assert.equal(view.battleLine, null);
  assert.equal(view.battleEnding, null);
  assert.equal(view.cancelShown, false);
  assert.equal(view.cancelEnabled, false);
});

test("a started battle is shown with Cancel until it ends", () => {
  const view = describe(play({ kind: "opened" }, message(STARTED)));
  assert.equal(view.battleNote, "A battle is flying.");
  assert.equal(view.battleLine, "First Flight, flown by Ace.");
  assert.equal(view.cancelShown, true);
  assert.equal(view.cancelEnabled, true);
  assert.equal(view.battleEnding, null);
});

test("Cancel can be pressed once; the page then says it is cancelling", () => {
  const view = describe(
    play(
      { kind: "opened" },
      message(STARTED),
      { kind: "cancelling" },
      { kind: "cancelling" },
    ),
  );
  assert.equal(view.battleNote, "Cancelling the battle.");
  assert.equal(view.cancelShown, true);
  assert.equal(view.cancelEnabled, false);
});

test("a dropped connection stops Cancel being offered as enabled", () => {
  const view = describe(
    play({ kind: "opened" }, message(STARTED), { kind: "closed" }),
  );
  assert.equal(view.cancelEnabled, false);
});

test("the ending line replaces Cancel", () => {
  const view = describe(
    play({ kind: "opened" }, message(STARTED), message(ENDED)),
  );
  assert.equal(view.battleNote, "The battle is over.");
  assert.equal(view.battleEnding, "flown: completed, score 10");
  assert.equal(view.battleLine, "First Flight, flown by Ace.");
  assert.equal(view.cancelShown, false);
});

test("an ending for another battle is ignored, and so is one with none flying", () => {
  const other: Message = {
    event: "launch.ended",
    data: { launch_id: 9, status: "flown", line: "elsewhere" },
  };
  const during = describe(play(message(STARTED), message(other)));
  assert.equal(during.battleEnding, null);
  assert.equal(during.cancelShown, true);
  assert.equal(describe(play(message(other))).battleEnding, null);
});

test("pressing Cancel with nothing flying changes nothing", () => {
  assert.deepEqual(play({ kind: "cancelling" }), initialState());
  const ended = play(message(STARTED), message(ENDED));
  assert.deepEqual(reduce(ended, { kind: "cancelling" }), ended);
});

test("a new battle clears the last ending", () => {
  const view = describe(
    play(message(STARTED), message(ENDED), message(STARTED)),
  );
  assert.equal(view.battleEnding, null);
  assert.equal(view.cancelShown, true);
});

test("a refused cancel is shown in the battle note", () => {
  const view = describe(
    play(
      message(STARTED),
      message({
        id: 5,
        ok: true,
        result: {
          cancelled: false,
          message: "No battle with that number is flying.",
        },
      }),
    ),
  );
  assert.equal(view.battleNote, "No battle with that number is flying.");
});

const DEFAULTS = {
  difficulty: "medium",
  collisions: "on",
  craft_jumping: "on",
  random_setup: "off",
  battle_length: "3",
  mission_time_limit: "default",
  continue_sequence: "continue",
} as const;

test("the options are not shown until they arrive", () => {
  assert.equal(describe(initialState()).optionsRows, null);
});

test("the options reply fills the rows in words", () => {
  const options = {
    ...DEFAULTS,
    difficulty: "easy_cheat",
    collisions: "off",
    battle_length: "3",
    mission_time_limit: "1",
    continue_sequence: "restart",
  } as const;
  const view = describe(
    play(message({ id: 1, ok: true, result: { player: "7", options } })),
  );
  assert.deepEqual(
    view.optionsRows?.map((r) => [r.label, r.value]),
    [
      ["Difficulty", "Easy with cheats (campaigns)"],
      ["Collisions", "Off"],
      ["Craft jumping", "On"],
      ["Random setup", "Off"],
      ["Battle length", "3 wins"],
      [
        "Mission time limit (multiplayer only: no effect in solo play)",
        "1 minute",
      ],
      ["Battle or campaign", "Start again"],
    ],
  );
});

test("options.changed redraws the rows", () => {
  const first = play(
    message({
      event: "options.changed",
      data: { player: "7", options: DEFAULTS },
    }),
  );
  const next = reduce(
    first,
    message({
      event: "options.changed",
      data: {
        player: "7",
        options: {
          ...DEFAULTS,
          mission_time_limit: "none",
          random_setup: "on",
        },
      },
    }),
  );
  const rows = describe(next).optionsRows ?? [];
  assert.equal(
    rows.find((r) => r.name === "mission_time_limit")?.value,
    "No limit",
  );
  assert.equal(rows.find((r) => r.name === "random_setup")?.value, "On");
  assert.equal(describe(first).optionsRows?.[3]?.value, "Off");
});

test("a time limit of 20 is in minutes and the mission's own limit is in words", () => {
  const rows = describe(
    play(
      message({
        event: "options.changed",
        data: {
          player: null,
          options: { ...DEFAULTS, mission_time_limit: "20" },
        },
      }),
    ),
  ).optionsRows;
  assert.equal(rows?.[5]?.value, "20 minutes");
});

test("optionWords names every choice of every option", () => {
  const words: [string, string, string][] = [
    ["difficulty", "easy", "Easy"],
    ["difficulty", "medium", "Medium"],
    ["difficulty", "hard", "Hard"],
    ["difficulty", "easy_cheat", "Easy with cheats (campaigns)"],
    ["collisions", "on", "On"],
    ["collisions", "off", "Off"],
    ["craft_jumping", "on", "On"],
    ["random_setup", "off", "Off"],
    ["battle_length", "2", "2 wins"],
    ["battle_length", "3", "3 wins"],
    ["battle_length", "4", "4 wins"],
    ["mission_time_limit", "none", "No limit"],
    ["mission_time_limit", "default", "The mission's own limit"],
    ["mission_time_limit", "1", "1 minute"],
    ["mission_time_limit", "20", "20 minutes"],
    ["continue_sequence", "restart", "Start again"],
    ["continue_sequence", "continue", "Continue"],
  ];
  for (const [name, value, expected] of words) {
    assert.equal(optionWords(name, value), expected);
    assert.doesNotMatch(optionWords(name, value), /game has it/i);
  }
});

test("the pilot is not known until the status push says", () => {
  const view = describe(initialState());
  assert.equal(view.pilotLine, "The pilot is not known yet.");
  assert.equal(view.pilotBar, "Pilot: not known yet");
});

test("the status push gives the pilot and a null pilot gives the none words", () => {
  const named = describe(
    play(
      message({
        event: "status",
        data: {
          launcher_version: "0.1.0",
          install_found: true,
          pilot: "Wedge",
        },
      }),
    ),
  );
  assert.equal(named.pilotLine, "Your pilot is Wedge.");
  assert.equal(named.pilotBar, "Pilot: Wedge");
  const none = describe(
    play(
      message({
        event: "status",
        data: { launcher_version: "0.1.0", install_found: true, pilot: null },
      }),
    ),
  );
  assert.equal(
    none.pilotLine,
    "No pilot yet. Link Discord with /link and the launcher makes one.",
  );
  assert.equal(none.pilotBar, "Pilot: none yet");
});

test("pilot.changed follows a rename and a pilot that goes away", () => {
  const first = play(
    message({
      event: "status",
      data: { launcher_version: "0.1.0", install_found: true, pilot: "Wedge" },
    }),
  );
  const renamed = reduce(
    first,
    message({ event: "pilot.changed", data: { pilot: "Biggs" } }),
  );
  assert.equal(describe(renamed).pilotLine, "Your pilot is Biggs.");
  assert.equal(describe(renamed).pilotBar, "Pilot: Biggs");
  const gone = reduce(
    renamed,
    message({ event: "pilot.changed", data: { pilot: null } }),
  );
  assert.equal(describe(gone).pilotBar, "Pilot: none yet");
});

const SHOW_FIRST: Message = {
  event: "page.show_mission",
  data: { mission_type: "training", id: 1, title: "First" },
};

test("the kinds list each type with its groups in the game's order and its count", () => {
  const view = describe(
    play(message({ id: 3, ok: true, result: { menus: [TRAINING, MELEE] } })),
  );
  assert.deepEqual(
    view.kinds.map((k) => [k.id, k.label, k.count]),
    [
      ["training", "Training", 2],
      ["melee", "Melee", 0],
    ],
  );
  assert.deepEqual(
    view.kinds[0]?.groups.map((g) => [g.name, g.missions.length]),
    [
      ["Basic", 1],
      ["All missions", 1],
    ],
  );
  assert.deepEqual(
    view.kinds.map((k) => k.note),
    [
      null,
      "The game's list for this kind of mission was not found (melee\\mission.lst).",
    ],
  );
  assert.deepEqual(view.kinds[1]?.groups, []);
});

test("a mission row has a Show request only when available, and the shown one is current", () => {
  const view = describe(
    play(
      message({ id: 3, ok: true, result: { menus: [TRAINING] } }),
      message(SHOW_FIRST),
    ),
  );
  const [basic, rest] = view.kinds[0]?.groups ?? [];
  assert.deepEqual(basic?.missions[0], {
    title: "First",
    current: true,
    pick: { missionType: "training", id: 1, label: "Show First" },
  });
  assert.deepEqual(rest?.missions[0], {
    title: "Second",
    current: false,
    pick: null,
  });
  const other: MenuData = {
    ...MELEE,
    resolved: true,
    entries: [
      { section: "", id: 1, available: true, file: "m.tie", title: "Melee" },
    ],
  };
  const both = describe(
    play(menusReply(TRAINING, other), showPush("training", 1)),
  );
  assert.equal(both.kinds[1]?.groups[0]?.missions[0]?.current, false);
});

test("the shown heading names the kind, the group and the title; it is null before a pick", () => {
  assert.equal(describe(initialState()).shownWhere, null);
  const view = describe(
    play(
      message({ id: 3, ok: true, result: { menus: [TRAINING] } }),
      message(SHOW_FIRST),
    ),
  );
  assert.deepEqual(view.shownWhere, {
    kind: "Training",
    group: "Basic",
    title: "First",
  });
});
