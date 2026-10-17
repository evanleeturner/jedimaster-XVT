import assert from "node:assert/strict";
import { test } from "node:test";

import { debrief, detail, player, solo, team } from "./debrief.fixture.ts";
import type { DrawnLine } from "./lines.ts";
import {
  missionTitle,
  overviewLines,
  pilotLine,
  plain,
  ROW,
  STATISTICS,
  statisticsLines,
  statisticsRowCount,
  statisticsRows,
} from "./lines.ts";
import type { Table } from "./view.ts";
import { overviewTables, statisticsTables } from "./view.ts";

function at(lines: readonly DrawnLine[], text: string): DrawnLine {
  const found = lines.find((l) => plain(l.text) === text);
  assert.ok(found, `no line ${text}`);
  return found;
}

function cell(
  tables: readonly Table[],
  line: DrawnLine,
  column: number,
): string {
  const source = line.source;
  assert.ok(source);
  return tables[source.table]?.rows[source.row]?.[column] ?? "";
}

test("the overview: headings at 1997's places, rows 15 apart from y 134", () => {
  const lines = overviewLines(debrief());
  assert.deepEqual(
    ["Score", "Kills", "Deaths"].map((w) => [at(lines, w).x, at(lines, w).y]),
    [
      [228, 114],
      [298, 114],
      [363, 114],
    ],
  );
  assert.equal(
    at(lines, "Trainee Luke").y,
    134 + ROW,
    "the team row is first, its best player under it",
  );
  assert.equal(at(lines, "1. Red").y, 134);
  assert.equal(at(lines, "1. Red").x, 88);
  assert.equal(at(lines, "Trainee Luke").x, 103);
});

test("the overview: places 1 to 3 are green, a left pilot is gray in brackets", () => {
  const lines = overviewLines(debrief());
  assert.ok(at(lines, "1. Red").text.startsWith("\u0002"));
  const left = at(lines, "[Of 2nd Vader]");
  assert.equal(left.ink, "gray");
  assert.equal(left.clipRight, 225);
  assert.equal(at(lines, "Trainee Wedge").ink, "yellow");
});

test("the overview: the sixth team is not green", () => {
  const teams = Array.from({ length: 4 }, (_, n) =>
    team(n, `T${String(n)}`, 100 - n * 10),
  );
  const d = debrief({ teams, players: [player(0, "Wedge", 0, 5)] });
  const lines = overviewLines(d);
  assert.ok(at(lines, "4. T3").text.startsWith("\u0001"));
  assert.ok(at(lines, "3. T2").text.startsWith("\u0002"));
});

test("every drawn overview number is a cell of the tables", () => {
  const d = debrief();
  const tables = overviewTables(d);
  const lines = overviewLines(d).filter((l) => l.source !== undefined);
  assert.ok(lines.length > 20);
  for (const line of lines) {
    const column = line.source?.column ?? -1;
    if (column >= 3 || line.source?.table !== 0) {
      assert.equal(plain(line.text), cell(tables, line, column));
    }
  }
  const columns = new Set(
    lines.filter((l) => l.source?.table === 0).map((l) => l.x),
  );
  assert.deepEqual(
    [...columns].sort((a, b) => a - b),
    [88, 103, 228, 298, 363],
  );
});

test("the killed lists: names at 88 and 259, counts at 208 and 379, under the standings", () => {
  const lines = overviewLines(debrief());
  const killed = at(lines, "Killed");
  const killedBy = at(lines, "Killed By");
  assert.equal(killed.x, 88);
  assert.equal(killedBy.x, 259);
  assert.equal(killed.y, killedBy.y);
  assert.equal(
    killed.y,
    134 + ROW * 5 + ROW,
    "five standing rows, then a gap row",
  );
  assert.equal(at(lines, "Vader").x, 88);
  assert.equal(lines.filter((l) => l.x === 208).length, 2);
  assert.equal(lines.filter((l) => l.x === 379).length, 2);
});

test("kill rows that would fall below the page are not drawn", () => {
  const many = Array.from({ length: 30 }, (_, n) => ({
    entry: n % 8,
    name: `P${String(n)}`,
    kills: 1,
    shared: 0,
  }));
  const d = debrief({ detail: detail({ on_players: many }) });
  const lines = overviewLines(d);
  assert.ok(Math.max(...lines.map((l) => l.y)) <= 436);
});

test("the statistics page: label yellow at 88, value white at 268, from y 111", () => {
  const lines = statisticsLines(debrief(), 0);
  const score = at(lines, "Mission Score 1450");
  assert.deepEqual([score.x, score.y], [88, 111 + ROW]);
  const total = at(lines, "Total Kills");
  assert.deepEqual([total.x, total.ink], [88, "yellow"]);
  const value = lines.find((l) => l.y === total.y && l.x === 268);
  assert.ok(value);
  assert.equal(value.text, "5 (1)");
  assert.equal(value.ink, "white");
});

test("the statistics page: Total Kills first under its heading, craft names red", () => {
  const rows = statisticsRows(debrief());
  const heading = rows.findIndex(
    (r) => r.kind === "heading" && r.text === "Summary Of Kills",
  );
  const first = rows[heading + 1];
  assert.ok(first?.kind === "pair");
  assert.equal(first.label, "Total Kills");
  const craft = statisticsLines(debrief(), 0).filter((l) => l.ink === "red");
  assert.deepEqual(
    craft.map((l) => l.text),
    ["TIE Fighter", "TIE Bomber"],
  );
});

test("the statistics page: a blank row and a heading before each block but the accuracy", () => {
  const rows = statisticsRows(debrief());
  const kinds = rows.map((r) =>
    r.kind === "heading" ? `H:${r.text}` : r.kind,
  );
  const heads = kinds.filter((k) => k.startsWith("H:"));
  assert.deepEqual(heads, [
    "H:Summary Of Kills",
    "H:Player Kills By Rank",
    "H:Craft Kills By Type",
    "H:Total Losses",
    "H:Losses From Players",
  ]);
  for (const [at2, kind] of kinds.entries()) {
    if (kind.startsWith("H:")) assert.equal(kinds[at2 - 1], "blank");
  }
  assert.ok(!heads.includes("H:Accuracy"));
});

test("the statistics page: 21 rows are shown and the scroll moves the window", () => {
  const d = debrief();
  const total = statisticsRowCount(d);
  assert.ok(total > STATISTICS.rows, "a network battle needs the scroll");
  const top = statisticsLines(d, 0).filter((l) => l.x === 88);
  const rest = statisticsLines(d, total - STATISTICS.rows).filter(
    (l) => l.x === 88,
  );
  assert.ok(Math.max(...top.map((l) => l.y)) <= 111 + ROW * 20);
  assert.notDeepEqual(
    top.map((l) => l.text),
    rest.map((l) => l.text),
  );
  assert.equal(
    statisticsLines(d, total).length,
    1,
    "past the end only the title is left",
  );
});

test("every drawn statistics number is a cell of the tables", () => {
  const d = debrief();
  const tables = statisticsTables(d);
  const all = [
    ...statisticsLines(d, 0),
    ...statisticsLines(d, 8),
    ...statisticsLines(d, 17),
  ].filter((l) => l.source !== undefined);
  assert.ok(all.length > 30);
  for (const line of all) {
    const source = line.source;
    assert.ok(source);
    const row = tables[source.table]?.rows[source.row] ?? [];
    if (source.column === -1) {
      assert.equal(plain(line.text), `${row[0] ?? ""} ${row[1] ?? ""}`);
    } else {
      assert.equal(plain(line.text), row[source.column]);
    }
  }
});

test("a solo flight with no detail draws the result lines only", () => {
  const d = solo({ detail: null });
  const lines = statisticsLines(d, 0);
  assert.ok(
    lines.some((l) => plain(l.text) === "Result Completed mission in 6:52"),
  );
  assert.equal(lines.filter((l) => l.x === 268).length, 0);
});

test("the title lines: mission up to its last bracket, pilot line with the rank", () => {
  assert.equal(missionTitle("Skirmish (Combat)"), "Skirmish");
  assert.equal(missionTitle("No bracket"), "No bracket");
  assert.equal(missionTitle("(odd)"), "(odd)");
  const line = pilotLine(debrief());
  assert.equal(plain(line.text), "Trainee Wedge");
  assert.equal(plain(pilotLine(debrief({ local_entry: -1 })).text), "Wedge");
});
