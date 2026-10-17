import assert from "node:assert/strict";
import { test } from "node:test";

import type { Debrief } from "../generated/control.ts";
import { debrief, player, solo, team } from "./debrief.fixture.ts";
import {
  artUrl,
  backgroundStem,
  badgeName,
  localTeam,
  pilotSide,
  screenTab,
  screenTabs,
} from "./look.ts";

/** The 32 backgrounds the game code names (the route's whole set). */
const GAME_FILES = new Set(
  (
    "debcawi debcawr debcali debcalr debtrwi debtrwr debtrbi debtrbr debtrli " +
    "debtrlr debtwi debtwr debtbi debtbr debmwi debmwr debmbi debmbr debmli " +
    "debmlr debbwr debcwr debbli debcli debbwi debcwi debblr debclr debbti " +
    "debcti debbtr debctr"
  ).split(" "),
);

/** Imperial team 0 and Rebel team 1, with the local pilot on `mine`. */
function battle(
  mine: number,
  first: boolean,
  second: boolean,
  more: Partial<Debrief> = {},
): Debrief {
  return debrief({
    local_entry: 0,
    players: [player(0, "Wedge", mine, 100), player(1, "Vader", 1 - mine, 50)],
    teams: [team(0, "Red", 100, first), team(1, "Blue", 50, second)],
    ...more,
  });
}

test("the combat-like rule: the teams' goals and the local team name the file", () => {
  assert.equal(backgroundStem(battle(1, false, true), "rebel"), "debcwr");
  assert.equal(backgroundStem(battle(0, false, true), "rebel"), "debcli");
  assert.equal(backgroundStem(battle(0, true, false), "rebel"), "debcwi");
  assert.equal(backgroundStem(battle(1, true, false), "imperial"), "debclr");
});

test("equal goals: a tie on the network or when both completed, else a loss", () => {
  assert.equal(backgroundStem(battle(0, true, true), "rebel"), "debcti");
  assert.equal(backgroundStem(battle(1, false, false), "rebel"), "debctr");
  const alone = (mine: number, both: boolean): Debrief =>
    battle(mine, both, both, {
      players: [player(0, "Wedge", mine, 100)],
    });
  assert.equal(backgroundStem(alone(0, true), "rebel"), "debcti");
  assert.equal(backgroundStem(alone(1, true), "rebel"), "debctr");
  assert.equal(backgroundStem(alone(0, false), "rebel"), "debcli");
  assert.equal(backgroundStem(alone(1, false), "rebel"), "debclr");
});

test("the side letter is the local team's, never the pilot's chosen side", () => {
  const d = battle(1, false, true, { side: "imperial" });
  assert.equal(backgroundStem(d, "imperial"), "debcwr");
});

test("training: the outcome names win or loss and the battle's side the letter", () => {
  const train = (outcome: Debrief["outcome"], side: Debrief["side"]): Debrief =>
    solo({ mission_type: "training", outcome, side });
  assert.equal(
    backgroundStem(train("completed", "rebel"), "imperial"),
    "debtrwr",
  );
  assert.equal(
    backgroundStem(train("completed", "imperial"), "rebel"),
    "debtrwi",
  );
  assert.equal(
    backgroundStem(train("not_completed", "rebel"), "rebel"),
    "debtrlr",
  );
  assert.equal(backgroundStem(train("none", "imperial"), "rebel"), "debtrli");
});

test("a kind that names no side takes the pilot's chosen side", () => {
  const d = solo({ mission_type: "training", side: "none" });
  assert.equal(pilotSide(d, "imperial"), "imperial");
  assert.equal(backgroundStem(d, "imperial"), "debtrwi");
  assert.equal(backgroundStem(d, "rebel"), "debtrwr");
  assert.equal(pilotSide(solo({ side: "rebel" }), "imperial"), "rebel");
});

test("melee: first place by score wins, else loses; the chosen side gives the letter", () => {
  const melee = (mine: number): Debrief =>
    debrief({
      mission_type: "melee",
      side: "none",
      players: [player(0, "Wedge", mine, 5), player(1, "Vader", 1 - mine, 5)],
      teams: [
        team(0, "Red", mine === 0 ? 9 : 3),
        team(1, "Blue", mine === 1 ? 9 : 3),
      ],
    });
  assert.equal(backgroundStem(melee(0), "rebel"), "debmwr");
  assert.equal(backgroundStem(melee(0), "imperial"), "debmwi");
  const lost = melee(0);
  const worse = { ...lost, teams: [team(0, "Red", 1), team(1, "Blue", 9)] };
  assert.equal(backgroundStem(worse, "imperial"), "debmli");
});

test("a battle's mission keeps the battle pictures, by the combat-like rule", () => {
  const b = (mine: number, first: boolean, second: boolean): Debrief =>
    battle(mine, first, second, { mission_type: "battle" });
  assert.equal(backgroundStem(b(1, false, true), "imperial"), "debbwr");
  assert.equal(backgroundStem(b(0, false, true), "rebel"), "debbli");
  assert.equal(backgroundStem(b(0, true, false), "rebel"), "debbwi");
  assert.equal(backgroundStem(b(1, true, false), "imperial"), "debblr");
  assert.equal(backgroundStem(b(0, true, true), "rebel"), "debbti");
});

test("alone with neither side's goals met: a battle ties, a combat engagement loses", () => {
  const alone = (kind: "battle" | "combat", mine: number): Debrief =>
    battle(mine, false, false, {
      mission_type: kind,
      players: [player(0, "Wedge", mine, 100)],
    });
  assert.equal(backgroundStem(alone("battle", 0), "rebel"), "debbti");
  assert.equal(backgroundStem(alone("battle", 1), "rebel"), "debbtr");
  assert.equal(backgroundStem(alone("combat", 0), "rebel"), "debcli");
  assert.equal(backgroundStem(alone("combat", 1), "rebel"), "debclr");
});

test("a campaign's mission: the outcome names win or loss, the battle's side the letter", () => {
  const camp = (outcome: Debrief["outcome"], side: Debrief["side"]): Debrief =>
    battle(1, true, false, { mission_type: "campaign", outcome, side });
  assert.equal(
    backgroundStem(camp("completed", "imperial"), "rebel"),
    "debcawi",
  );
  assert.equal(
    backgroundStem(camp("not_completed", "rebel"), "imperial"),
    "debcalr",
  );
});

test("a tournament's mission shows the melee pictures", () => {
  const d = battle(0, false, false, {
    mission_type: "tournament",
    side: "none",
  });
  assert.equal(backgroundStem(d, "imperial"), "debmwi");
  const behind = { ...d, teams: [team(0, "Red", 1), team(1, "Blue", 9)] };
  assert.equal(backgroundStem(behind, "rebel"), "debmlr");
});

test("a result with no teams or players: the outcome and the battle's side", () => {
  const bare = (outcome: Debrief["outcome"], side: Debrief["side"]): Debrief =>
    debrief({
      players: [],
      teams: [],
      local_entry: -1,
      detail: null,
      outcome,
      side,
    });
  assert.equal(
    backgroundStem(bare("completed", "imperial"), "rebel"),
    "debcwi",
  );
  assert.equal(
    backgroundStem(bare("not_completed", "rebel"), "imperial"),
    "debclr",
  );
  assert.equal(backgroundStem(bare("none", "none"), "rebel"), "debclr");
});

test("every file the page can pick is one the game code names", () => {
  const seen = new Set<string>();
  const kinds = [
    "training",
    "melee",
    "tournament",
    "combat",
    "battle",
    "campaign",
  ] as const;
  for (const kind of kinds) {
    for (const side of ["rebel", "imperial", "none"] as const) {
      for (const outcome of ["none", "completed", "not_completed"] as const) {
        for (const mine of [0, 1]) {
          for (const [a, b] of [
            [true, false],
            [false, true],
            [true, true],
            [false, false],
          ]) {
            const d = battle(mine, a ?? false, b ?? false, {
              mission_type: kind,
              side,
              outcome,
            });
            for (const chosen of ["rebel", "imperial"] as const) {
              seen.add(backgroundStem(d, chosen));
              seen.add(
                backgroundStem({ ...d, players: [], teams: [] }, chosen),
              );
            }
          }
        }
      }
    }
  }
  for (const stem of seen) assert.ok(GAME_FILES.has(stem), stem);
  assert.equal(seen.size, 24, "every name but the award and tournament ones");
});

test("the tab set: the overview only for the network, melee and tournament", () => {
  assert.deepEqual(screenTabs(debrief()), ["overview", "statistics"]);
  assert.deepEqual(screenTabs(solo()), ["statistics"]);
  assert.deepEqual(screenTabs(solo({ mission_type: "melee" })), [
    "overview",
    "statistics",
  ]);
  assert.deepEqual(screenTabs(solo({ mission_type: "tournament" })), [
    "overview",
    "statistics",
  ]);
  assert.deepEqual(screenTabs(solo({ mission_type: "campaign" })), [
    "statistics",
  ]);
});

test("a tab that is not offered falls back to the first one", () => {
  assert.equal(screenTab(solo(), "overview"), "statistics");
  assert.equal(screenTab(solo(), "statistics"), "statistics");
  assert.equal(screenTab(debrief(), "overview"), "overview");
});

test("the local team is the local player's, null when none is named", () => {
  assert.equal(localTeam(debrief()), 0);
  assert.equal(localTeam(debrief({ local_entry: 2 })), 0);
  assert.equal(localTeam(debrief({ local_entry: -1 })), null);
});

test("the badge picture names: imperial and rebel, 16 frames", () => {
  assert.equal(badgeName("imperial", 0), "imptiny0");
  assert.equal(badgeName("rebel", 15), "rebtiny15");
  assert.equal(badgeName("rebel", 16), "rebtiny0");
  assert.equal(artUrl("imptiny3"), "/art/debrief/imptiny3.png");
});
