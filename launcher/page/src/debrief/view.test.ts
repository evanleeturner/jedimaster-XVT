import assert from "node:assert/strict";
import { test } from "node:test";

import type {
  Debrief,
  DebriefDetail,
  DebriefPlayer,
  DebriefTeam,
} from "../generated/control.ts";
import type { Table } from "./view.ts";
import {
  accuracy,
  clockText,
  headingLine,
  ordinal,
  overviewTables,
  resultLabel,
  standingsTable,
  statisticsTables,
} from "./view.ts";

function player(
  entry: number,
  name: string,
  team: number,
  score: number,
  more: Partial<DebriefPlayer> = {},
): DebriefPlayer {
  return {
    entry,
    name,
    rating: 2,
    rank: "Trainee",
    team,
    flight_group: entry,
    flight_group_name: `Group ${String(entry)}`,
    score,
    kills: 0,
    shared: 0,
    assists: 0,
    losses: 0,
    shots: 0,
    hits: 0,
    warheads: 0,
    warhead_hits: 0,
    has_left: false,
    ...more,
  };
}

function team(number: number, name: string, score: number): DebriefTeam {
  return {
    team: number,
    name,
    completed: false,
    score,
    kills: number + 1,
    shared: 1,
    losses: 2,
    seconds: 0,
  };
}

function detail(more: Partial<DebriefDetail> = {}): DebriefDetail {
  return {
    shots: 212,
    hits: 61,
    warheads: 4,
    warhead_hits: 3,
    losses: 3,
    losses_to_collisions: 1,
    losses_to_starships: 1,
    losses_to_mines: 0,
    friendlies_killed: 0,
    special_inspected: 2,
    by_craft: [
      { craft_type: 5, name: "TIE Fighter", kills: 3, shared: 1, assists: 1 },
      { craft_type: 9, name: "TIE Bomber", kills: 2, shared: 0, assists: 2 },
    ],
    on_pilot_ranks: [{ rank: 6, kills: 2, shared: 0, assists: 0 }],
    on_ai_ranks: [
      { rank: 1, kills: 2, shared: 1, assists: 2 },
      { rank: 3, kills: 1, shared: 0, assists: 1 },
    ],
    killed_by_pilot_ranks: [{ rank: 6, times: 1 }],
    killed_by_ai_ranks: [
      { rank: 2, times: 1 },
      { rank: 4, times: 1 },
    ],
    on_players: [{ entry: 1, name: "Vader", kills: 2, shared: 0 }],
    from_players: [{ entry: 1, name: "Vader", kills: 1, shared: 0 }],
    on_flight_groups: [{ flight_group: 2, name: "Gold", kills: 3, shared: 1 }],
    from_flight_groups: [
      { flight_group: 3, name: "Blue", kills: 1, shared: 0 },
    ],
    ...more,
  };
}

function debrief(more: Partial<Debrief> = {}): Debrief {
  return {
    result_id: 3,
    ended_at: "2026-10-16T21:30:05Z",
    mission_type: "melee",
    side: "none",
    mission: "melee01.tie",
    title: "Two Teams Meet",
    pilot: "Wedge",
    outcome: "completed",
    score: 1450,
    kills: 5,
    losses: 1,
    goals_met_seconds: 412,
    local_entry: 0,
    players: [
      player(0, "Wedge", 0, 1450, { kills: 5, shared: 1, losses: 1 }),
      player(1, "Vader", 1, 980, { has_left: true, rank: "Of 2nd" }),
      player(2, "Luke", 0, 1500),
    ],
    teams: [team(0, "Red", 1450), team(1, "Blue", 980)],
    detail: detail(),
    ...more,
  };
}

function rowsOf(tables: readonly Table[], caption: string): string[][] {
  const table = tables.find((t) => t.caption === caption);
  assert.ok(table, `no table ${caption}`);
  return table.rows.map((row) => [...row]);
}

test("the small words: clock, accuracy, ordinal, the picker's line, the heading", () => {
  assert.equal(clockText(412), "6:52");
  assert.equal(clockText(5), "0:05");
  assert.equal(accuracy(61, 212), "28%", "rounded down, as 1997 divides");
  assert.equal(accuracy(3, 0), "0%", "no shots: 0%");
  const places = [1, 2, 3, 4, 11, 12, 13, 21, 22, 23, 111, 112].map(ordinal);
  assert.deepEqual(places, [
    "1st",
    "2nd",
    "3rd",
    "4th",
    "11th",
    "12th",
    "13th",
    "21st",
    "22nd",
    "23rd",
    "111th",
    "112th",
  ]);
  const line = resultLabel({ ...debrief(), outcome: "not_completed" });
  assert.equal(
    line,
    "#3 Two Teams Meet: not completed, score 1450, 2026-10-16 21:30 UTC",
  );
  assert.equal(
    headingLine(debrief()),
    "Two Teams Meet, flown by Wedge, ended 2026-10-16 21:30 UTC: completed",
  );
});

test("the overview lists teams by score with each team's players under it", () => {
  assert.deepEqual(standingsTable(debrief()).rows, [
    ["1st", "Red", "", "1450", "1 (1)", "2"],
    ["", "Luke", "Trainee", "1500", "0 (0)", "0"],
    ["", "Wedge (you)", "Trainee", "1450", "5 (1)", "1"],
    ["2nd", "Blue", "", "980", "2 (1)", "2"],
    ["", "Vader (left)", "Of 2nd", "980", "0 (0)", "0"],
  ]);
});

test("equal team scores share a place; a player of no listed team comes last", () => {
  const d = debrief({
    teams: [team(0, "Red", 900), team(1, "Blue", 900), team(2, "Gold", 100)],
    players: [player(0, "Wedge", 0, 900), player(5, "Lost", -1, 50)],
  });
  const rows = standingsTable(d).rows;
  assert.deepEqual(
    rows.map((r) => r[0]),
    ["1st", "", "1st", "3rd", ""],
  );
  assert.equal(rows[4]?.[1], "Lost");
});

test("an old result's overview is the pilot's own line", () => {
  const d = debrief({ players: [], teams: [], local_entry: -1, detail: null });
  assert.deepEqual(standingsTable(d).rows, [
    ["", "Wedge", "", "1450", "5", "1"],
  ]);
  assert.equal(overviewTables(d).length, 1, "no Killed tables without detail");
  const stats = statisticsTables(d);
  assert.deepEqual(
    stats.map((t) => t.caption),
    ["Result"],
  );
});

test("Killed lists players first, then flight groups; Killed By the same", () => {
  const tables = overviewTables(debrief());
  assert.deepEqual(rowsOf(tables, "Killed"), [
    ["Vader", "2 (0)"],
    ["Gold", "3 (1)"],
  ]);
  assert.deepEqual(rowsOf(tables, "Killed By"), [
    ["Vader", "1 (0)"],
    ["Blue", "1 (0)"],
  ]);
});

test("the statistics sum what the 1997 page sums", () => {
  const tables = statisticsTables(debrief());
  assert.deepEqual(rowsOf(tables, "Summary Of Kills"), [
    ["Player Kills", "2 (0)"],
    ["Non-Player Kills", "3 (1)"],
    ["Assists", "3"],
    ["Hidden Cargo Found", "2"],
    ["Total Kills", "5 (1)"],
  ]);
  assert.deepEqual(rowsOf(tables, "Accuracy"), [
    ["Laser Accuracy", "28%"],
    ["Warhead Accuracy", "75%"],
  ]);
  assert.deepEqual(rowsOf(tables, "Total Losses"), [
    ["Total Craft Losses", "3"],
    ["To Player Pilots", "1"],
    ["To Non-Player Pilots", "2"],
    ["To Starships", "1"],
    ["To Mines", "0"],
    ["From Collisions", "1"],
  ]);
  assert.deepEqual(rowsOf(tables, "Craft Kills By Type"), [
    ["TIE Fighter", "3 (1)"],
    ["TIE Bomber", "2 (0)"],
  ]);
  assert.deepEqual(rowsOf(tables, "Player Kills By Rank"), [
    ["Of 2nd Vader", "2 (0)"],
  ]);
  assert.deepEqual(rowsOf(tables, "Losses From Players"), [
    ["Of 2nd Vader", "1 (0)"],
  ]);
});

test("a network melee names the place; other kinds name the result", () => {
  const melee = rowsOf(statisticsTables(debrief()), "Result");
  assert.deepEqual(melee, [
    ["Place", "1st of 2 teams"],
    ["Mission Score", "1450"],
  ]);
  const training = {
    mission_type: "training" as const,
    side: "rebel" as const,
  };
  const won = rowsOf(statisticsTables(debrief(training)), "Result");
  assert.deepEqual(won[0], ["Result", "Completed mission in 6:52"]);
  const failed = debrief({ ...training, outcome: "not_completed" });
  assert.deepEqual(rowsOf(statisticsTables(failed), "Result")[0], [
    "Result",
    "Failed mission.",
  ]);
  const tournament = rowsOf(
    statisticsTables(debrief({ mission_type: "tournament" })),
    "Result",
  );
  assert.deepEqual(tournament, [["Mission Score", "1450"]]);
});

test("a solo battle shows no player tables, as in 1997", () => {
  const solo = debrief({ players: [player(0, "Wedge", 0, 1450)] });
  const captions = statisticsTables(solo).map((t) => t.caption);
  assert.ok(!captions.includes("Player Kills By Rank"));
  assert.ok(!captions.includes("Losses From Players"));
  assert.ok(
    !rowsOf(statisticsTables(solo), "Result").some((r) => r[0] === "Place"),
  );
});
