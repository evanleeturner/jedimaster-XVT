/**
 * The debriefing's two pages as plain tables: what the page draws, worked out
 * from one debriefing without the DOM.
 *
 * The mission overview lists the teams by score, each team's players under
 * it, then whom the pilot killed and who killed the pilot; the player
 * statistics list the 1997 page's rows in its order, with the totals it
 * summed. Every number is the debriefing's own or a sum of its rows.
 */
import type {
  Debrief,
  DebriefDetail,
  DebriefPlayer,
  DebriefTeam,
} from "../generated/control.ts";

export type DebriefTab = "overview" | "statistics";
export const DEBRIEF_TABS: readonly {
  readonly id: DebriefTab;
  readonly label: string;
}[] = [
  { id: "overview", label: "Mission overview" },
  { id: "statistics", label: "Player statistics" },
];

/** One table: its caption, its column names and its rows of cell text. */
export interface Table {
  readonly caption: string;
  readonly columns: readonly string[];
  readonly rows: readonly (readonly string[])[];
}

const OUTCOME_WORDS: Record<Debrief["outcome"], string> = {
  completed: "completed",
  not_completed: "not completed",
  none: "no goals",
};

/** Return ``kills (shared)``, the way the 1997 pages print a kill count. */
export function killsText(kills: number, shared: number): string {
  return `${String(kills)} (${String(shared)})`;
}

/** Return ``seconds`` as minutes and seconds, ``6:52``. */
export function clockText(seconds: number): string {
  const minutes = Math.floor(seconds / 60);
  return `${String(minutes)}:${String(seconds % 60).padStart(2, "0")}`;
}

/** Return the percentage the 1997 page prints: hits times 100 over shots, 0 with no shots. */
export function accuracy(hits: number, shots: number): string {
  return `${String(shots === 0 ? 0 : Math.floor((100 * hits) / shots))}%`;
}

/** Return the English ordinal of a place counted from 1: 1st, 2nd, 3rd, 4th. */
export function ordinal(place: number): string {
  const tens = place % 100;
  if (tens >= 11 && tens <= 13) return `${String(place)}th`;
  const suffix = ["th", "st", "nd", "rd"][place % 10] ?? "th";
  return `${String(place)}${suffix}`;
}

/** Return the one line a kept result's picker shows. */
export function resultLabel(summary: {
  result_id: number;
  ended_at: string;
  title: string;
  outcome: Debrief["outcome"];
  score: number;
}): string {
  const when = summary.ended_at.replace("T", " ").slice(0, 16);
  const words = OUTCOME_WORDS[summary.outcome];
  return `#${String(summary.result_id)} ${summary.title}: ${words}, score ${String(summary.score)}, ${when} UTC`;
}

/** Return the debriefing's heading line: what was flown, by whom and when. */
export function headingLine(d: Debrief): string {
  const when = d.ended_at.replace("T", " ").slice(0, 16);
  return `${d.title}, flown by ${d.pilot}, ended ${when} UTC: ${OUTCOME_WORDS[d.outcome]}`;
}

/** Return the places of ``scores``: equal scores share a place, as 1997 drew them. */
function places(scores: readonly number[]): number[] {
  return scores.map((score) => 1 + scores.filter((s) => s > score).length);
}

function byScore<T extends { score: number }>(items: readonly T[]): T[] {
  return [...items].sort((a, b) => b.score - a.score);
}

/** One line of the overview's standings: a team, or a player under it. */
export interface StandingRow {
  readonly kind: "team" | "player";
  /** The team's place counted from 1, 0 for a player. */
  readonly place: number;
  readonly name: string;
  readonly rank: string;
  readonly score: number;
  readonly kills: number;
  readonly shared: number;
  readonly deaths: number;
  readonly local: boolean;
  readonly left: boolean;
}

function playerName(row: StandingRow): string {
  const marks = [row.local ? "you" : "", row.left ? "left" : ""];
  const said = marks.filter((m) => m !== "").join(", ");
  return said === "" ? row.name : `${row.name} (${said})`;
}

function playerRow(p: DebriefPlayer, local: number): StandingRow {
  return {
    kind: "player",
    place: 0,
    name: p.name,
    rank: p.rank,
    score: p.score,
    kills: p.kills,
    shared: p.shared,
    deaths: p.losses,
    local: p.entry === local,
    left: p.has_left,
  };
}

/** Return the overview's standings: teams by score, each team's players under it. */
export function standingRows(d: Debrief): StandingRow[] {
  if (d.players.length === 0 && d.teams.length === 0) {
    return [
      {
        kind: "player",
        place: 0,
        name: d.pilot,
        rank: "",
        score: d.score,
        kills: d.kills,
        shared: 0,
        deaths: d.losses,
        local: false,
        left: false,
      },
    ];
  }
  const teams: DebriefTeam[] = byScore(d.teams);
  const placed = places(teams.map((t) => t.score));
  const rows: StandingRow[] = [];
  teams.forEach((team, index) => {
    rows.push({
      kind: "team",
      place: placed[index] ?? 0,
      name: team.name,
      rank: "",
      score: team.score,
      kills: team.kills,
      shared: team.shared,
      deaths: team.losses,
      local: false,
      left: false,
    });
    for (const p of byScore(d.players.filter((q) => q.team === team.team))) {
      rows.push(playerRow(p, d.local_entry));
    }
  });
  const listed = new Set(teams.map((t) => t.team));
  for (const p of byScore(d.players.filter((q) => !listed.has(q.team)))) {
    rows.push(playerRow(p, d.local_entry));
  }
  return rows;
}

/** Return the overview's table of teams by score, each team's players under it. */
export function standingsTable(d: Debrief): Table {
  const columns = [
    "Place",
    "Team or player",
    "Rank",
    "Score",
    "Kills",
    "Deaths",
  ];
  const rows = standingRows(d).map((row) => [
    row.kind === "team" ? ordinal(row.place) : "",
    row.kind === "team" ? row.name : playerName(row),
    row.rank,
    String(row.score),
    // The pilot's own line without players has no shared count to print.
    d.players.length === 0 && d.teams.length === 0
      ? String(row.kills)
      : killsText(row.kills, row.shared),
    String(row.deaths),
  ]);
  return { caption: "Mission overview", columns, rows };
}

function killsTable(
  caption: string,
  players: readonly { name: string; kills: number; shared: number }[],
  groups: readonly { name: string; kills: number; shared: number }[],
): Table {
  const rows = [...players, ...groups].map((r) => [
    r.name,
    killsText(r.kills, r.shared),
  ]);
  return { caption, columns: ["Pilot or flight group", "Kills"], rows };
}

/** Return the overview's tables: the standings, then Killed and Killed By. */
export function overviewTables(d: Debrief): Table[] {
  const tables = [standingsTable(d)];
  if (d.detail !== null) {
    const { on_players, on_flight_groups, from_players, from_flight_groups } =
      d.detail;
    tables.push(killsTable("Killed", on_players, on_flight_groups));
    tables.push(killsTable("Killed By", from_players, from_flight_groups));
  }
  return tables;
}

function sum<T>(rows: readonly T[], value: (row: T) => number): number {
  return rows.reduce((total, row) => total + value(row), 0);
}

function rankedName(d: Debrief, entry: number, name: string): string {
  const player = d.players.find((p) => p.entry === entry);
  return player === undefined ? name : `${player.rank} ${name}`;
}

function summaryRows(detail: DebriefDetail): string[][] {
  const crafts = detail.by_craft;
  return [
    [
      "Player Kills",
      killsText(
        sum(detail.on_pilot_ranks, (r) => r.kills),
        sum(detail.on_pilot_ranks, (r) => r.shared),
      ),
    ],
    [
      "Non-Player Kills",
      killsText(
        sum(detail.on_ai_ranks, (r) => r.kills),
        sum(detail.on_ai_ranks, (r) => r.shared),
      ),
    ],
    ["Assists", String(sum(crafts, (r) => r.assists))],
    ["Hidden Cargo Found", String(detail.special_inspected)],
    [
      "Total Kills",
      killsText(
        sum(crafts, (r) => r.kills),
        sum(crafts, (r) => r.shared),
      ),
    ],
  ];
}

function lossRows(detail: DebriefDetail): string[][] {
  return [
    ["Total Craft Losses", String(detail.losses)],
    [
      "To Player Pilots",
      String(sum(detail.killed_by_pilot_ranks, (r) => r.times)),
    ],
    [
      "To Non-Player Pilots",
      String(sum(detail.killed_by_ai_ranks, (r) => r.times)),
    ],
    ["To Starships", String(detail.losses_to_starships)],
    ["To Mines", String(detail.losses_to_mines)],
    ["From Collisions", String(detail.losses_to_collisions)],
  ];
}

/** Return the first rows: the place in a network melee, the result elsewhere, and the score. */
function resultRows(d: Debrief): string[][] {
  const rows: string[][] = [];
  if (d.mission_type === "melee" && d.players.length > 1) {
    const local = d.players.find((p) => p.entry === d.local_entry);
    const teams = byScore(d.teams);
    const index = teams.findIndex((t) => t.team === local?.team);
    if (index >= 0) {
      const place = places(teams.map((t) => t.score))[index] ?? 0;
      rows.push([
        "Place",
        `${ordinal(place)} of ${String(teams.length)} teams`,
      ]);
    }
  }
  if (d.mission_type !== "melee" && d.mission_type !== "tournament") {
    if (d.outcome === "completed") {
      rows.push([
        "Result",
        `Completed mission in ${clockText(d.goals_met_seconds)}`,
      ]);
    } else if (d.outcome === "not_completed") {
      rows.push(["Result", "Failed mission."]);
    }
  }
  rows.push(["Mission Score", String(d.score)]);
  return rows;
}

/** Return the player statistics page's tables, in the 1997 page's order. */
export function statisticsTables(d: Debrief): Table[] {
  const columns = ["Count", "Value"];
  const tables: Table[] = [{ caption: "Result", columns, rows: resultRows(d) }];
  const detail = d.detail;
  if (detail === null) return tables;
  tables.push({
    caption: "Summary Of Kills",
    columns,
    rows: summaryRows(detail),
  });
  tables.push({
    caption: "Accuracy",
    columns,
    rows: [
      ["Laser Accuracy", accuracy(detail.hits, detail.shots)],
      ["Warhead Accuracy", accuracy(detail.warhead_hits, detail.warheads)],
    ],
  });
  // A network battle's pages only, as in 1997: a solo battle lists one player.
  const network = d.players.length > 1;
  if (network && detail.on_players.length > 0) {
    tables.push({
      caption: "Player Kills By Rank",
      columns: ["Player", "Kills"],
      rows: detail.on_players.map((r) => [
        rankedName(d, r.entry, r.name),
        killsText(r.kills, r.shared),
      ]),
    });
  }
  if (detail.by_craft.length > 0) {
    tables.push({
      caption: "Craft Kills By Type",
      columns: ["Craft", "Kills"],
      rows: detail.by_craft.map((r) => [r.name, killsText(r.kills, r.shared)]),
    });
  }
  tables.push({ caption: "Total Losses", columns, rows: lossRows(detail) });
  if (network && detail.from_players.length > 0) {
    tables.push({
      caption: "Losses From Players",
      columns: ["Player", "Kills"],
      rows: detail.from_players.map((r) => [
        rankedName(d, r.entry, r.name),
        killsText(r.kills, r.shared),
      ]),
    });
  }
  return tables;
}

/** Return the tables of ``tab``. */
export function tablesOf(d: Debrief, tab: DebriefTab): Table[] {
  return tab === "overview" ? overviewTables(d) : statisticsTables(d);
}
