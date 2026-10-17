/**
 * The debriefing's decoder: a flown battle's numbers, checked field by field.
 *
 * Each object of the debriefing is checked against a table of its fields,
 * the control schema's own bounds, and rebuilt from the checked values only;
 * a table that names a field the generated type lacks, or misses one, does
 * not compile.
 */
import type {
  CraftKills,
  Debrief,
  DebriefDetail,
  DebriefPlayer,
  DebriefTeam,
  GroupKills,
  PlayerKills,
  RankKills,
  RankTimes,
  ResultSummary,
} from "../generated/control.ts";

type Fields = Record<string, unknown>;
export type Parsed<T> = { readonly value: T } | { readonly reason: string };

/** A field's kind: any whole number, a count (0 up), text, a flag, a range or a choice. */
type Kind =
  | "int"
  | "count"
  | "text"
  | "name"
  | "flag"
  | readonly [number, number]
  | readonly string[];
type Spec<T> = Readonly<Record<keyof T, Kind>>;

const MAX_ID = 2 ** 31 - 1;
/** The longest player name the pilot record holds. */
const NAME_LENGTH = 12;
const OUTCOMES = ["none", "completed", "not_completed"] as const;
const MISSION_TYPES = [
  "training",
  "melee",
  "tournament",
  "combat",
  "battle",
  "campaign",
] as const;
const SIDES = ["rebel", "imperial", "none"] as const;
const ENTRY = [0, 7] as const;

const PLAYER = {
  entry: ENTRY,
  name: "name",
  rating: "int",
  rank: "text",
  team: [-1, MAX_ID],
  flight_group: "count",
  flight_group_name: "text",
  score: "int",
  kills: "count",
  shared: "count",
  assists: "count",
  losses: "count",
  shots: "int",
  hits: "int",
  warheads: "count",
  warhead_hits: "count",
  has_left: "flag",
} as const satisfies Spec<DebriefPlayer>;
const TEAM = {
  team: ENTRY,
  name: "text",
  completed: "flag",
  score: "count",
  kills: "count",
  shared: "count",
  losses: "count",
  seconds: "int",
} as const satisfies Spec<DebriefTeam>;
const CRAFT = {
  craft_type: [0, 99],
  name: "text",
  kills: "count",
  shared: "count",
  assists: "count",
} as const satisfies Spec<CraftKills>;
const RANK_KILLS = {
  rank: [0, 24],
  kills: "count",
  shared: "count",
  assists: "count",
} as const satisfies Spec<RankKills>;
const RANK_TIMES = {
  rank: [0, 24],
  times: "count",
} as const satisfies Spec<RankTimes>;
const PLAYER_KILLS = {
  entry: ENTRY,
  name: "text",
  kills: "count",
  shared: "count",
} as const satisfies Spec<PlayerKills>;
const GROUP_KILLS = {
  flight_group: [0, 47],
  name: "text",
  kills: "count",
  shared: "count",
} as const satisfies Spec<GroupKills>;
const DETAIL_COUNTS = {
  shots: "count",
  hits: "count",
  warheads: "count",
  warhead_hits: "count",
  losses: "count",
  losses_to_collisions: "count",
  losses_to_starships: "count",
  losses_to_mines: "count",
  friendlies_killed: "count",
  special_inspected: "count",
} as const;
const DETAIL_TABLES = {
  by_craft: CRAFT,
  on_pilot_ranks: RANK_KILLS,
  on_ai_ranks: RANK_KILLS,
  killed_by_pilot_ranks: RANK_TIMES,
  killed_by_ai_ranks: RANK_TIMES,
  on_players: PLAYER_KILLS,
  from_players: PLAYER_KILLS,
  on_flight_groups: GROUP_KILLS,
  from_flight_groups: GROUP_KILLS,
} as const;
const DEBRIEF = {
  result_id: [1, MAX_ID],
  ended_at: "text",
  mission_type: MISSION_TYPES,
  side: SIDES,
  mission: "text",
  title: "text",
  pilot: "text",
  outcome: OUTCOMES,
  score: "int",
  kills: "count",
  losses: "count",
  goals_met_seconds: "count",
  local_entry: [-1, 7],
} as const;
const SUMMARY = {
  result_id: [1, MAX_ID],
  ended_at: "text",
  title: "text",
  outcome: OUTCOMES,
  score: "int",
} as const satisfies Spec<ResultSummary>;

// The detail's and the debriefing's tables are checked whole against their types here.
const DETAIL_FIELDS = { ...DETAIL_COUNTS, ...DETAIL_TABLES } satisfies Record<
  keyof DebriefDetail,
  unknown
>;
const DEBRIEF_FIELDS = {
  ...DEBRIEF,
  players: PLAYER,
  teams: TEAM,
  detail: DETAIL_FIELDS,
} satisfies Record<keyof Debrief, unknown>;

/** True when the two key lists are the same, both ways; a spread hides an extra key from satisfies. */
type SameKeys<A, B> = [A] extends [B]
  ? [B] extends [A]
    ? true
    : false
  : false;
export const FIELDS_MATCH: [
  SameKeys<keyof typeof DETAIL_FIELDS, keyof DebriefDetail>,
  SameKeys<keyof typeof DEBRIEF_FIELDS, keyof Debrief>,
] = [true, true];

function isRecord(value: unknown): value is Fields {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasExactly(value: Fields, names: readonly string[]): boolean {
  const keys = Object.keys(value);
  return keys.length === names.length && names.every((n) => n in value);
}

function fits(value: unknown, kind: Kind): boolean {
  if (kind === "flag") return typeof value === "boolean";
  if (kind === "text") return typeof value === "string";
  if (kind === "name") {
    return typeof value === "string" && value.length <= NAME_LENGTH;
  }
  if (typeof value === "string") {
    return typeof kind !== "string" && kind.some((choice) => choice === value);
  }
  if (typeof value !== "number" || !Number.isInteger(value)) return false;
  if (kind === "int") return true;
  if (kind === "count") return value >= 0;
  const [low, high] = kind;
  return (
    typeof low === "number" &&
    typeof high === "number" &&
    value >= low &&
    value <= high
  );
}

/** Return the fields ``spec`` names, checked, from ``value``; extra or missing fields fail. */
function checked(
  value: unknown,
  spec: Readonly<Record<string, Kind>>,
  what: string,
): Parsed<Fields> {
  if (!isRecord(value) || !hasExactly(value, Object.keys(spec))) {
    return { reason: `${what} has wrong fields` };
  }
  const out: Fields = {};
  for (const [name, kind] of Object.entries(spec)) {
    if (!fits(value[name], kind)) {
      return { reason: `${what}: ${name} has a wrong value` };
    }
    out[name] = value[name];
  }
  return { value: out };
}

function rows(
  value: unknown,
  spec: Readonly<Record<string, Kind>>,
  what: string,
): Parsed<Fields[]> {
  if (!Array.isArray(value)) return { reason: `${what} must be a list` };
  const out: Fields[] = [];
  for (const item of value) {
    const row = checked(item, spec, what);
    if ("reason" in row) return row;
    out.push(row.value);
  }
  return { value: out };
}

function detailOf(value: unknown): Parsed<DebriefDetail | null> {
  if (value === null) return { value: null };
  if (!isRecord(value) || !hasExactly(value, Object.keys(DETAIL_FIELDS))) {
    return { reason: "the debriefing's detail has wrong fields" };
  }
  const counts = checked(
    Object.fromEntries(Object.keys(DETAIL_COUNTS).map((n) => [n, value[n]])),
    DETAIL_COUNTS,
    "the debriefing's detail",
  );
  if ("reason" in counts) return counts;
  const out: Fields = { ...counts.value };
  for (const [name, spec] of Object.entries(DETAIL_TABLES)) {
    const table = rows(value[name], spec, `the detail's ${name}`);
    if ("reason" in table) return table;
    out[name] = table.value;
  }
  return { value: out as unknown as DebriefDetail };
}

/** Return the debriefing ``value`` holds, rebuilt from checked fields, or why it is wrong. */
export function decodeDebrief(value: unknown): Parsed<Debrief> {
  if (!isRecord(value) || !hasExactly(value, Object.keys(DEBRIEF_FIELDS))) {
    return { reason: "a debriefing has wrong fields" };
  }
  const head = checked(
    Object.fromEntries(Object.keys(DEBRIEF).map((n) => [n, value[n]])),
    DEBRIEF,
    "a debriefing",
  );
  if ("reason" in head) return head;
  const players = rows(value.players, PLAYER, "a debriefing's player");
  if ("reason" in players) return players;
  const teams = rows(value.teams, TEAM, "a debriefing's team");
  if ("reason" in teams) return teams;
  const detail = detailOf(value.detail);
  if ("reason" in detail) return detail;
  const out = {
    ...head.value,
    players: players.value,
    teams: teams.value,
    detail: detail.value,
  };
  return { value: out as unknown as Debrief };
}

/** Return the list of kept results ``value`` holds, or why it is wrong. */
export function decodeSummaries(value: unknown): Parsed<ResultSummary[]> {
  const list = rows(value, SUMMARY, "a kept result");
  if ("reason" in list) return list;
  return { value: list.value as unknown as ResultSummary[] };
}
