/**
 * The 1997 debriefing screen's rules: which background, which tabs, which
 * badge, and where the screen's pieces sit.
 *
 * The background follows `mission_debrief_update`: a file name reads
 * `deb` + kind + result + side. The page flies one mission at a time, but
 * 1997 drew a battle's (`debb`) and a campaign's (`debca`) pictures after
 * every mission of the sequence, so those kinds keep their own. A
 * tournament's pictures (`debtw`, `debtb`) came only after its last
 * mission, so a tournament mission shows the melee ones, as 1997 did
 * before the last. The award pictures (`b`) and the tournament ones wait
 * for the awards, which the result does not carry. Everything is decided
 * from the debriefing alone (and the pilot's chosen side, for a kind that
 * names no side).
 */
import type { Debrief, DebriefTeam } from "../generated/control.ts";
import type { DebriefTab } from "./view.ts";

export const DEBRIEF_PATH = "/art/debrief/";
export type PilotSide = "rebel" | "imperial";
type Result = "win" | "loss" | "tie";

/** The 1997 team number of the Imperial side: the other teams are Rebel. */
const IMPERIAL_TEAM = 0;
const PILOT_LINE_FRAMES = 16;

/** Return the local player's team number, or null when the result names none. */
export function localTeam(d: Debrief): number | null {
  const local = d.players.find((p) => p.entry === d.local_entry);
  return local === undefined ? null : local.team;
}

function completed(teams: readonly DebriefTeam[], number: number): boolean {
  return teams.find((t) => t.team === number)?.completed ?? false;
}

function letter(side: PilotSide): "i" | "r" {
  return side === "imperial" ? "i" : "r";
}

/** Return the side of the pilot: the battle's own, else the chosen one. */
export function pilotSide(d: Debrief, chosen: PilotSide): PilotSide {
  return d.side === "none" ? chosen : d.side;
}

function combatResult(d: Debrief, team: number): Result {
  const first = completed(d.teams, IMPERIAL_TEAM);
  const second = completed(d.teams, 1);
  const network = d.players.length > 1;
  if (first !== second) {
    return first === (team === IMPERIAL_TEAM) ? "win" : "loss";
  }
  if (network || first || second) return "tie";
  // Alone with neither side's goals met, 1997 shows a tie after a battle's
  // mission and a loss after a combat engagement.
  return d.mission_type === "battle" ? "tie" : "loss";
}

function outcomeResult(d: Debrief): Result {
  return d.outcome === "completed" ? "win" : "loss";
}

function meleeResult(d: Debrief): Result {
  const local = d.players.find((p) => p.entry === d.local_entry);
  if (local === undefined) return outcomeResult(d);
  const best = Math.max(...d.teams.map((t) => t.score));
  const mine = d.teams.find((t) => t.team === local.team);
  return mine?.score === best ? "win" : "loss";
}

/**
 * Return the background file's stem (`debcwi`...) of a debriefing.
 *
 * Training, a campaign, a melee and a tournament name a result and a side
 * of the pilot; a combat engagement and a battle compare the two teams'
 * goals and name the local team's side. With no teams in the result the
 * outcome gives a win or a loss and the battle's side the letter. A side of
 * `none` is the pilot's chosen side.
 */
export function backgroundStem(d: Debrief, chosen: PilotSide): string {
  const side = pilotSide(d, chosen);
  if (d.mission_type === "training" || d.mission_type === "campaign") {
    const kind = d.mission_type === "training" ? "tr" : "ca";
    return `deb${kind}${outcomeResult(d) === "win" ? "w" : "l"}${letter(side)}`;
  }
  if (d.mission_type === "melee" || d.mission_type === "tournament") {
    return `debm${meleeResult(d) === "win" ? "w" : "l"}${letter(side)}`;
  }
  const kind = d.mission_type === "battle" ? "b" : "c";
  const team = localTeam(d);
  if (team === null || d.teams.length === 0) {
    return `deb${kind}${outcomeResult(d) === "win" ? "w" : "l"}${letter(side)}`;
  }
  const result = combatResult(d, team);
  const mark = { win: "w", loss: "l", tie: "t" }[result];
  return `deb${kind}${mark}${team === IMPERIAL_TEAM ? "i" : "r"}`;
}

/** Return the tabs 1997 shows for this debriefing, in the tab bar's order. */
export function screenTabs(d: Debrief): DebriefTab[] {
  const overview =
    d.players.length > 1 ||
    d.mission_type === "melee" ||
    d.mission_type === "tournament";
  return overview ? ["overview", "statistics"] : ["statistics"];
}

/** Return the tab the screen shows: ``tab`` when 1997 offers it, else the first. */
export function screenTab(d: Debrief, tab: DebriefTab): DebriefTab {
  const tabs = screenTabs(d);
  return tabs.includes(tab) ? tab : (tabs[0] ?? "statistics");
}

/** Return the name of the side badge's picture, `imptiny3` or `rebtiny3`. */
export function badgeName(side: PilotSide, frame: number): string {
  const n =
    ((frame % PILOT_LINE_FRAMES) + PILOT_LINE_FRAMES) % PILOT_LINE_FRAMES;
  return `${side === "imperial" ? "imp" : "reb"}tiny${String(n)}`;
}

export const BADGE_FRAMES = PILOT_LINE_FRAMES;

export interface Rect {
  readonly left: number;
  readonly top: number;
  readonly right: number;
  readonly bottom: number;
}

/** The 1997 rectangles, in the 640 x 480 screen's pixels. */
export const PLACES = {
  tabs: {
    overview: { left: 22, top: 114, right: 42, bottom: 138 },
    statistics: { left: 22, top: 142, right: 42, bottom: 166 },
  },
  leave: { left: 85, top: 447, right: 176, bottom: 471 },
  mission: { left: 158, top: 52, right: 491, bottom: 68 },
  title: { left: 84, top: 90, right: 434, bottom: 106 },
  pilot: { left: 200, top: 452, right: 436, bottom: 464 },
  badgeLeft: { x: 204, y: 453 },
  badgeRight: { x: 420, y: 453 },
} as const;

export const SPRITES = {
  overview: ["debrief1u", "debrief1d"],
  statistics: ["debrief2u", "debrief2d"],
  leave: ["leaveup", "leavedown"],
} as const;

/** Return the URL of the debriefing route's file `name`. */
export function artUrl(name: string): string {
  return `${DEBRIEF_PATH}${name}.png`;
}
